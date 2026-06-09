/**
 * @file input_keyboard.c
 * @brief Movimiento del cursor, selección y operaciones de edición de texto
 *        (las que disparan teclas y atajos).
 *
 * @note Modelo de cursor y selección. El texto se guarda en un "gap buffer" que
 * se direcciona con un offset lineal (0 = primer carácter). El editor mantiene
 * en paralelo (cursor_line, cursor_col) para la UI; tras cada cambio hay que
 * re-sincronizarlas (::editor_sync_cursor). La *selección* se define con un
 * "ancla" (sel_anchor_line/col, donde empezó) y el cursor (donde está ahora);
 * el rango ordenado se obtiene con ::editor_sel_range. Las teclas de movimiento
 * reciben un flag @p sel/@p selecting: con Shift se amplía la selección desde
 * el ancla; sin Shift se descarta.
 */
#include "input_internal.h"

/* ── Helpers comunes ────────────────────────────────────────────────────────
 */

/**
 * @brief Refresca el estado tras una edición que cambió el texto.
 *
 * Re-tokeniza desde @p dirty_line (un cambio puede afectar líneas siguientes,
 * p. ej. abrir un comentario de bloque), reencuadra el scroll y marca el buffer
 * como modificado y la pantalla como sucia. No mueve el cursor.
 *
 * @param e          Editor.
 * @param dirty_line Primera línea que hay que volver a resaltar.
 */
static void after_edit(Editor *e, int dirty_line) {
    editor_update_lexer(e, dirty_line);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
}

/**
 * @brief Refresca tras mover el cursor (sin marcar modificado ni tocar el
 * lexer).
 *
 * Recalcula (línea, columna) desde la posición del buffer, reencuadra el scroll
 * y pide redibujar. Se usa cuando solo cambió la posición del cursor.
 *
 * @param e Editor.
 */
static void after_cursor_move(Editor *e) {
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

/**
 * @brief Si no hay selección activa, fija el ancla en la posición actual.
 *
 * Es el arranque de una selección con Shift: la primera vez ancla en donde está
 * el cursor; en llamadas posteriores (selección ya activa) no toca el ancla, de
 * modo que el rango crece/encoge moviendo solo el cursor.
 *
 * @param e Editor.
 */
static void begin_selection(Editor *e) {
    if (!e->sel_active) {
        e->sel_active = 1;
        e->sel_anchor_line = e->cursor_line;
        e->sel_anchor_col = e->cursor_col;
    }
}

/**
 * @brief ¿Es @p c un carácter de palabra (alfanumérico o '_')?
 *
 * Define qué cuenta como "palabra" para el salto Ctrl+Left/Right.
 *
 * @param c Carácter a clasificar.
 * @return Distinto de 0 si es letra, dígito o guion bajo; 0 si es separador.
 */
static int is_word_char(char c) {
    /* el cast a unsigned char evita comportamiento indefinido de isalnum con
     * valores negativos (bytes >= 0x80 en char con signo) */
    return isalnum((unsigned char)c) || c == '_';
}

/* ── Movimiento del cursor ──────────────────────────────────────────────────
 */

/**
 * @brief Mueve el cursor a (line, col), recortando a un destino válido.
 *
 * @param e    Editor.
 * @param line Línea destino (se recorta a [0, nº_líneas-1]).
 * @param col  Columna destino (se recorta a >= 0; el fin de línea lo ajusta
 *             ::editor_pos_from_line_col). No toca la selección.
 */
void move_cursor(Editor *e, int line, int col) {
    int total = buf_line_count(e->buf);
    if (line < 0) line = 0;
    if (line >= total) line = total - 1;
    if (col < 0) col = 0;
    buf_move_to(e->buf, editor_pos_from_line_col(e, line, col));
    after_cursor_move(e);
}

/**
 * @brief Mueve el cursor extendiendo la selección si @p selecting (Shift).
 *
 * @param e         Editor.
 * @param line      Línea destino.
 * @param col       Columna destino.
 * @param selecting 1 (Shift) para ampliar la selección desde el ancla;
 *                  0 para descartar cualquier selección antes de mover.
 */
void move_cursor_select(Editor *e, int line, int col, int selecting) {
    if (selecting)
        begin_selection(e); /* ancla la selección si aún no estaba activa */
    else
        editor_sel_clear(e); /* movimiento normal: sin selección */
    move_cursor(e, line, col);
}

/** @brief Flecha arriba: sube una línea (mantiene columna). @param e Editor.
 * @param sel Shift. */
void move_line_up(Editor *e, int sel) {
    move_cursor_select(e, e->cursor_line - 1, e->cursor_col, sel);
}

/** @brief Flecha abajo: baja una línea. @param e Editor. @param sel Shift. */
void move_line_down(Editor *e, int sel) {
    move_cursor_select(e, e->cursor_line + 1, e->cursor_col, sel);
}

/**
 * @brief Flecha izquierda: retrocede una columna (o salta al fin de la previa).
 *
 * Con Shift amplía la selección. Sin Shift y con selección activa, la primera
 * pulsación *colapsa* la selección a su inicio (no mueve más). Si está en la
 * columna 0, salta al final de la línea anterior.
 *
 * @param e   Editor.
 * @param sel 1 si Shift está pulsado.
 */
void move_col_left(Editor *e, int sel) {
    if (sel) {
        begin_selection(e);
    } else if (e->sel_active) {
        /* sin Shift y con selección: colapsar al inicio de la misma */
        size_t from, to;
        if (editor_sel_range(e, &from, &to)) {
            editor_sel_clear(e);
            buf_move_to(e->buf, from); /* cursor al inicio del rango */
            after_cursor_move(e);
            return;
        }
        editor_sel_clear(e);
    }

    if (e->cursor_col > 0) {
        move_cursor(e, e->cursor_line, e->cursor_col - 1);
    } else if (e->cursor_line > 0) {
        /* en la columna 0: ir al final de la línea anterior */
        int prev = e->cursor_line - 1;
        size_t prev_start = editor_pos_from_line_col(e, prev, 0);
        int line_len = (int)(buf_line_end(e->buf, prev_start) - prev_start);
        move_cursor(e, prev, line_len);
    }
}

/**
 * @brief Flecha derecha: avanza una columna.
 *
 * Simétrica de ::move_col_left: con Shift amplía; sin Shift y con selección,
 * colapsa al final de esta; en otro caso avanza una posición del buffer (lo que
 * cruza saltos de línea de forma natural) si no está ya al final del documento.
 *
 * @param e   Editor.
 * @param sel 1 si Shift está pulsado.
 */
void move_col_right(Editor *e, int sel) {
    if (sel) {
        begin_selection(e);
    } else if (e->sel_active) {
        /* sin Shift y con selección: colapsar al final de la misma */
        size_t from, to;
        if (editor_sel_range(e, &from, &to)) {
            editor_sel_clear(e);
            buf_move_to(e->buf, to); /* cursor al fin del rango */
            after_cursor_move(e);
            return;
        }
        editor_sel_clear(e);
    }

    if (buf_cursor_pos(e->buf) < buf_length(e->buf)) {
        buf_move_right(e->buf); /* avanzar una posición lógica */
        after_cursor_move(e);
    }
}

/** @brief Inicio: cursor a la columna 0 de la línea. @param e Editor. @param
 * sel Shift. */
void move_home(Editor *e, int sel) {
    move_cursor_select(e, e->cursor_line, 0, sel);
}

/**
 * @brief Fin: cursor al final (sin '\n') de la línea actual.
 *
 * @param e   Editor.
 * @param sel 1 si Shift está pulsado.
 */
void move_end(Editor *e, int sel) {
    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    int line_len = (int)(buf_line_end(e->buf, line_start) - line_start);
    move_cursor_select(e, e->cursor_line, line_len, sel);
}

/* ── Salto de palabra (Ctrl+Left / Ctrl+Right) ──────────────────────────────
 */

/**
 * @brief Ctrl+Left: mueve el cursor al inicio de la palabra anterior.
 *
 * Desde la posición actual retrocede saltando primero los separadores y luego
 * los caracteres de palabra, dejando el cursor justo en el comienzo de la
 * palabra previa.
 *
 * @param e   Editor.
 * @param sel 1 (Shift) para ampliar la selección; 0 para descartarla.
 */
void move_word_left(Editor *e, int sel) {
    if (sel)
        begin_selection(e);
    else
        editor_sel_clear(e);

    size_t pos = buf_cursor_pos(e->buf);
    if (pos == 0) return; /* ya al principio del documento */
    pos--;
    while (pos > 0 && !is_word_char(buf_char_at(e->buf, pos)))
        pos--; /* saltar separadores */
    while (pos > 0 && is_word_char(buf_char_at(e->buf, pos - 1)))
        pos--; /* saltar la palabra */
    buf_move_to(e->buf, pos);
    after_cursor_move(e);
}

/**
 * @brief Ctrl+Right: mueve el cursor al inicio de la palabra siguiente.
 *
 * Avanza saltando primero los caracteres de la palabra actual y luego los
 * separadores, dejando el cursor al comienzo de la próxima palabra.
 *
 * @param e   Editor.
 * @param sel 1 (Shift) para ampliar la selección; 0 para descartarla.
 */
void move_word_right(Editor *e, int sel) {
    if (sel)
        begin_selection(e);
    else
        editor_sel_clear(e);

    size_t pos = buf_cursor_pos(e->buf);
    size_t len = buf_length(e->buf);
    while (pos < len && is_word_char(buf_char_at(e->buf, pos)))
        pos++; /* saltar la palabra actual */
    while (pos < len && !is_word_char(buf_char_at(e->buf, pos)))
        pos++; /* saltar separadores */
    buf_move_to(e->buf, pos);
    after_cursor_move(e);
}

/* ── Operaciones de edición ─────────────────────────────────────────────────
 */

/**
 * @brief Borra la selección activa y la registra en el undo.
 *
 * Obtiene el rango ordenado, copia su texto a un buffer temporal para poder
 * deshacer el borrado, lo registra en el undo, borra del buffer, mueve el
 * cursor al inicio del hueco y limpia la selección.
 *
 * @param e Editor.
 * @return 1 si había selección y borró algo; 0 si no había nada que borrar.
 */
int delete_selection(Editor *e) {
    size_t from, to;
    if (!editor_sel_range(e, &from, &to)) return 0;

    size_t len = to - from;
    char *tmp = malloc(len + 1); /* copia para el undo (+1 por el '\0') */
    if (tmp) {
        buf_get_text(e->buf, from, to, tmp);
        editor_undo_push_delete(e, from, tmp, len);
        free(tmp);
    }
    buf_delete_range(e->buf, from, to);
    buf_move_to(e->buf,
                from); /* cursor donde estaba el inicio de la selección */
    editor_sel_clear(e);
    editor_sync_cursor(e);
    after_edit(e, e->cursor_line);
    return 1;
}

/**
 * @brief Inserta un salto de línea con autoindentación (copia la sangría
 * previa).
 *
 * Borra primero la selección si la hubiera, inserta el '\n' (registrándolo en
 * el undo) y luego replica al inicio de la nueva línea los espacios/tabs con
 * que empezaba la línea anterior, para mantener la sangría.
 *
 * @param e Editor.
 */
void insert_newline(Editor *e) {
    delete_selection(e);
    size_t pos = buf_cursor_pos(e->buf);
    editor_undo_push_insert(e, pos, "\n", 1);
    buf_insert(e->buf, '\n');
    editor_sync_cursor(e);

    /* copiar la sangría (espacios/tabs) del inicio de la línea anterior */
    size_t prev_start = editor_pos_from_line_col(e, e->cursor_line - 1, 0);
    size_t len = buf_length(e->buf);
    for (size_t i = prev_start; i < len; i++) {
        char c = buf_char_at(e->buf, i);
        if (c != ' ' && c != '\t') break; /* fin de la sangría */
        buf_insert(e->buf, c);
    }
    editor_sync_cursor(e);
    after_edit(e,
               e->cursor_line - 1); /* la línea previa también pudo cambiar */
}

/**
 * @brief Inserta espacios hasta el siguiente tab stop.
 *
 * Calcula cuántos espacios faltan para alinear con la rejilla de TAB_SIZE según
 * la columna actual, los registra en el undo y los inserta. (Indenta con
 * espacios, no con un carácter tab.)
 *
 * @param e Editor.
 */
void insert_tab(Editor *e) {
    delete_selection(e);
    int spaces =
        TAB_SIZE - (e->cursor_col % TAB_SIZE); /* hasta el próximo tab stop */
    size_t pos = buf_cursor_pos(e->buf);

    char tmp[TAB_SIZE + 1];
    for (int i = 0; i < spaces; i++)
        tmp[i] = ' ';
    tmp[spaces] = '\0';

    editor_undo_push_insert(e, pos, tmp, spaces);
    for (int i = 0; i < spaces; i++)
        buf_insert(e->buf, ' ');
    editor_sync_cursor(e);
    after_edit(e, e->cursor_line);
}

/**
 * @brief Retroceso: borra la selección, o el carácter a la izquierda del
 * cursor.
 *
 * Si hay selección, la borra entera. Si no, y no está al inicio del documento,
 * borra el carácter anterior (registrándolo en el undo). El @c dirty_line para
 * re-tokenizar es la menor entre la línea previa y la actual, porque al borrar
 * un '\n' el cursor sube de línea.
 *
 * @param e Editor.
 */
void do_backspace(Editor *e) {
    if (e->sel_active) {
        delete_selection(e);
        return;
    }
    if (buf_cursor_pos(e->buf) == 0) return; /* nada antes del cursor */

    int prev_line = e->cursor_line;
    size_t pos = buf_cursor_pos(e->buf) - 1;
    char c = buf_char_at(e->buf, pos);
    editor_undo_push_delete(e, pos, &c, 1);
    buf_delete_before(e->buf);
    editor_sync_cursor(e);
    /* re-tokenizar desde la línea más arriba afectada (al borrar '\n' subimos)
     */
    after_edit(e, e->cursor_line < prev_line ? e->cursor_line : prev_line);
}

/**
 * @brief Supr: borra la selección, o el carácter a la derecha del cursor.
 *
 * A diferencia del retroceso, Supr no mueve el cursor; por eso refresca a mano
 * (re-tokeniza y marca para redibujar) en vez de llamar a after_edit.
 *
 * @param e Editor.
 */
void do_delete(Editor *e) {
    if (e->sel_active) {
        delete_selection(e);
        return;
    }
    if (buf_cursor_pos(e->buf) >= buf_length(e->buf))
        return; /* nada tras el cursor */

    size_t pos = buf_cursor_pos(e->buf);
    char c = buf_char_at(e->buf, pos);
    editor_undo_push_delete(e, pos, &c, 1);
    buf_delete_after(e->buf);
    /* Supr no mueve el cursor: solo re-tokeniza y repinta */
    editor_update_lexer(e, e->cursor_line);
    e->modified = 1;
    e->needs_redraw = 1;
}

/* ── Selección global ───────────────────────────────────────────────────────
 */

/**
 * @brief Ctrl+A: selecciona todo el documento.
 *
 * Ancla la selección en (0,0) y lleva el cursor al final del buffer.
 *
 * @param e Editor.
 */
void select_all(Editor *e) {
    e->sel_anchor_line = 0; /* ancla en (0,0) */
    e->sel_anchor_col = 0;
    e->sel_active = 1;
    buf_move_to(e->buf, buf_length(e->buf)); /* cursor al final */
    after_cursor_move(e);
}

/* ── Portapapeles ───────────────────────────────────────────────────────────
 */

/**
 * @brief Ctrl+C: copia la selección al portapapeles del sistema.
 *
 * Si no hay selección no hace nada. Copia el texto del rango a un temporal
 * terminado en '\0' y lo entrega a SDL con @c SDL_SetClipboardText.
 *
 * @param e Editor.
 */
void do_copy(Editor *e) {
    size_t from, to;
    if (!editor_sel_range(e, &from, &to)) return;
    size_t len = to - from;
    char *tmp = malloc(len + 1);
    if (!tmp) return;
    buf_get_text(e->buf, from, to, tmp);
    tmp[len] = '\0';
    SDL_SetClipboardText(tmp); /* poner en el portapapeles del SO */
    free(tmp);
}

/** @brief Ctrl+X: copiar la selección y luego borrarla. @param e Editor. */
void do_cut(Editor *e) {
    do_copy(e);
    delete_selection(e);
}

/**
 * @brief Ctrl+V: pega el texto del portapapeles en la posición del cursor.
 *
 * Lee el portapapeles del sistema con SDL (la cadena devuelta hay que liberarla
 * con @c SDL_free). Si hay selección, la reemplaza. Registra la inserción en el
 * undo y la vuelca al buffer.
 *
 * @param e Editor.
 */
void do_paste(Editor *e) {
    if (!SDL_HasClipboardText()) return;
    char *text =
        SDL_GetClipboardText(); /* propiedad de SDL: liberar con SDL_free */
    if (!text) return;
    size_t len = strlen(text);
    if (len == 0) {
        SDL_free(text);
        return;
    }

    delete_selection(e); /* reemplaza la selección si la hay */
    size_t pos = buf_cursor_pos(e->buf);
    editor_undo_push_insert(e, pos, text, len);
    buf_insert_str(e->buf, text, len);
    SDL_free(text);
    editor_sync_cursor(e);
    after_edit(e, e->cursor_line);
}

/* ── Ctrl+D — duplicar la línea actual ──────────────────────────────────────
 */

/**
 * @brief Ctrl+D: duplica la línea actual debajo de sí misma.
 *
 * Copia el texto de la línea (sin el '\n') a un temporal, le añade un '\n',
 * y lo inserta tras el final de la línea, dejando una copia idéntica debajo.
 *
 * @param e Editor.
 */
void duplicate_line(Editor *e) {
    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t line_end = buf_line_end(e->buf, line_start);
    size_t len = line_end - line_start;

    char *tmp = malloc(len + 2); /* +1 para el '\n', +1 para el '\0' */
    if (!tmp) return;
    buf_get_text(e->buf, line_start, line_end, tmp);
    tmp[len] = '\n';
    tmp[len + 1] = '\0';

    buf_move_to(e->buf, line_end);
    editor_undo_push_insert(e, line_end, tmp, len + 1);
    buf_insert_str(e->buf, tmp, len + 1);
    free(tmp);

    editor_sync_cursor(e);
    after_edit(e, e->cursor_line);
}

/* ── Ctrl+/ — comentar / descomentar la línea ───────────────────────────────
 */

/**
 * @brief Prefijo de comentario de línea según la extensión del archivo.
 *
 * @param filepath Ruta del archivo (se mira su extensión).
 * @return "# " para Python/shell/Ruby/YAML/TOML, "-- " para Lua/SQL, y "// "
 *         por defecto (C/C++/JS y cuando no hay extensión).
 */
static const char *line_comment_prefix(const char *filepath) {
    const char *ext = strrchr(filepath, '.'); /* última '.' = extensión */
    if (!ext) return "// ";
    if (!strcmp(ext, ".py") || !strcmp(ext, ".sh") || !strcmp(ext, ".rb") ||
        !strcmp(ext, ".yaml") || !strcmp(ext, ".yml") || !strcmp(ext, ".toml"))
        return "# ";
    if (!strcmp(ext, ".lua") || !strcmp(ext, ".sql")) return "-- ";
    return "// "; /* C/C++/JS por defecto */
}

/**
 * @brief Ctrl+/: alterna el comentario de línea en la línea actual.
 *
 * Lee la línea, salta su sangría inicial y mira si ya empieza por el prefijo de
 * comentario: si es así lo borra (descomenta), si no lo inserta (comenta). Todo
 * queda registrado en el undo. El prefijo depende del tipo de archivo.
 *
 * @param e Editor.
 */
void toggle_line_comment(Editor *e) {
    const char *prefix = line_comment_prefix(e->filepath);
    size_t prefix_len = strlen(prefix);

    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t line_end = buf_line_end(e->buf, line_start);
    size_t line_len = line_end - line_start;

    char *line_text = malloc(line_len + 1);
    if (!line_text) return;
    buf_get_text(e->buf, line_start, line_end, line_text);
    line_text[line_len] = '\0';

    /* omitir la sangría inicial para detectar/insertar el prefijo */
    size_t indent = 0;
    while (indent < line_len &&
           (line_text[indent] == ' ' || line_text[indent] == '\t'))
        indent++;

    /* ¿el texto tras la sangría ya empieza por el prefijo de comentario? */
    int already_commented =
        (line_len - indent >= prefix_len &&
         strncmp(line_text + indent, prefix, prefix_len) == 0);

    if (already_commented) {
        /* descomentar: borrar el prefijo justo tras la sangría */
        size_t del_pos = line_start + indent;
        editor_undo_push_delete(e, del_pos, prefix, prefix_len);
        buf_delete_range(e->buf, del_pos, del_pos + prefix_len);
        buf_move_to(e->buf, del_pos);
    } else {
        /* comentar: insertar el prefijo tras la sangría */
        size_t ins_pos = line_start + indent;
        editor_undo_push_insert(e, ins_pos, prefix, prefix_len);
        buf_move_to(e->buf, ins_pos);
        buf_insert_str(e->buf, prefix, prefix_len);
        buf_move_to(e->buf, ins_pos + prefix_len); /* cursor tras el prefijo */
    }
    free(line_text);

    editor_sync_cursor(e);
    after_edit(e, e->cursor_line);
}

/* ── Ctrl+L — seleccionar la línea completa ─────────────────────────────────
 */

/**
 * @brief Ctrl+L: selecciona la línea actual entera (incluido su '\n').
 *
 * Ancla en la columna 0 de la línea y lleva el cursor al final, incluyendo el
 * salto de línea salvo que sea la última línea del documento.
 *
 * @param e Editor.
 */
void select_line(Editor *e) {
    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t line_end = buf_line_end(e->buf, line_start);
    if (line_end < buf_length(e->buf))
        line_end++; /* incluir el '\n' si no es la última */

    e->sel_active = 1;
    e->sel_anchor_line = e->cursor_line;
    e->sel_anchor_col = 0;
    buf_move_to(e->buf, line_end);
    after_cursor_move(e);
}
