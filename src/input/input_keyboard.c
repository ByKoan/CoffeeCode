/**
 * @file input_keyboard.c
 * @brief Movimiento del cursor, selección y operaciones de edición de texto
 *        (las que disparan teclas y atajos).
 */
#include "input_internal.h"

/* ── Helpers comunes ──────────────────────────────────────────────────────── */

/** Refresca el estado tras una edición: re-tokeniza desde @p dirty_line, ajusta
 *  el scroll y marca el buffer como modificado. (No mueve el cursor.) */
static void after_edit(Editor *e, int dirty_line) {
    editor_update_lexer(e, dirty_line);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
}

/** Refresca tras mover el cursor (sin marcar modificado ni tocar el lexer). */
static void after_cursor_move(Editor *e) {
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

/** Si no hay selección activa, fija el ancla en la posición actual del cursor. */
static void begin_selection(Editor *e) {
    if (!e->sel_active) {
        e->sel_active = 1;
        e->sel_anchor_line = e->cursor_line;
        e->sel_anchor_col = e->cursor_col;
    }
}

/** ¿Es @p c un carácter de palabra (alfanumérico o '_')? */
static int is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

/* ── Movimiento del cursor ────────────────────────────────────────────────── */

void move_cursor(Editor *e, int line, int col) {
    int total = buf_line_count(e->buf);
    if (line < 0) line = 0;
    if (line >= total) line = total - 1;
    if (col < 0) col = 0;
    buf_move_to(e->buf, editor_pos_from_line_col(e, line, col));
    after_cursor_move(e);
}

/** Mueve el cursor extendiendo la selección si @p selecting (Shift). */
void move_cursor_select(Editor *e, int line, int col, int selecting) {
    if (selecting)
        begin_selection(e);
    else
        editor_sel_clear(e);
    move_cursor(e, line, col);
}

void move_line_up(Editor *e, int sel) {
    move_cursor_select(e, e->cursor_line - 1, e->cursor_col, sel);
}

void move_line_down(Editor *e, int sel) {
    move_cursor_select(e, e->cursor_line + 1, e->cursor_col, sel);
}

void move_col_left(Editor *e, int sel) {
    if (sel) {
        begin_selection(e);
    } else if (e->sel_active) {
        /* sin Shift y con selección: colapsar al inicio de la misma */
        size_t from, to;
        if (editor_sel_range(e, &from, &to)) {
            editor_sel_clear(e);
            buf_move_to(e->buf, from);
            after_cursor_move(e);
            return;
        }
        editor_sel_clear(e);
    }

    if (e->cursor_col > 0) {
        move_cursor(e, e->cursor_line, e->cursor_col - 1);
    } else if (e->cursor_line > 0) {
        int prev = e->cursor_line - 1;
        size_t prev_start = editor_pos_from_line_col(e, prev, 0);
        int line_len = (int)(buf_line_end(e->buf, prev_start) - prev_start);
        move_cursor(e, prev, line_len);
    }
}

void move_col_right(Editor *e, int sel) {
    if (sel) {
        begin_selection(e);
    } else if (e->sel_active) {
        /* sin Shift y con selección: colapsar al final de la misma */
        size_t from, to;
        if (editor_sel_range(e, &from, &to)) {
            editor_sel_clear(e);
            buf_move_to(e->buf, to);
            after_cursor_move(e);
            return;
        }
        editor_sel_clear(e);
    }

    if (buf_cursor_pos(e->buf) < buf_length(e->buf)) {
        buf_move_right(e->buf);
        after_cursor_move(e);
    }
}

void move_home(Editor *e, int sel) {
    move_cursor_select(e, e->cursor_line, 0, sel);
}

void move_end(Editor *e, int sel) {
    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    int line_len = (int)(buf_line_end(e->buf, line_start) - line_start);
    move_cursor_select(e, e->cursor_line, line_len, sel);
}

/* ── Salto de palabra (Ctrl+Left / Ctrl+Right) ────────────────────────────── */

void move_word_left(Editor *e, int sel) {
    if (sel)
        begin_selection(e);
    else
        editor_sel_clear(e);

    size_t pos = buf_cursor_pos(e->buf);
    if (pos == 0) return;
    pos--;
    while (pos > 0 && !is_word_char(buf_char_at(e->buf, pos)))
        pos--; /* saltar separadores */
    while (pos > 0 && is_word_char(buf_char_at(e->buf, pos - 1)))
        pos--; /* saltar la palabra */
    buf_move_to(e->buf, pos);
    after_cursor_move(e);
}

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

/* ── Operaciones de edición ───────────────────────────────────────────────── */

/** Borra la selección activa y la registra en el undo. @return 1 si borró algo. */
int delete_selection(Editor *e) {
    size_t from, to;
    if (!editor_sel_range(e, &from, &to)) return 0;

    size_t len = to - from;
    char *tmp = malloc(len + 1);
    if (tmp) {
        buf_get_text(e->buf, from, to, tmp);
        editor_undo_push_delete(e, from, tmp, len);
        free(tmp);
    }
    buf_delete_range(e->buf, from, to);
    buf_move_to(e->buf, from);
    editor_sel_clear(e);
    editor_sync_cursor(e);
    after_edit(e, e->cursor_line);
    return 1;
}

/** Inserta un salto de línea con autoindentación (copia la sangría anterior). */
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
        if (c != ' ' && c != '\t') break;
        buf_insert(e->buf, c);
    }
    editor_sync_cursor(e);
    after_edit(e, e->cursor_line - 1);
}

/** Inserta espacios hasta el siguiente tab stop. */
void insert_tab(Editor *e) {
    delete_selection(e);
    int spaces = TAB_SIZE - (e->cursor_col % TAB_SIZE);
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

void do_backspace(Editor *e) {
    if (e->sel_active) {
        delete_selection(e);
        return;
    }
    if (buf_cursor_pos(e->buf) == 0) return;

    int prev_line = e->cursor_line;
    size_t pos = buf_cursor_pos(e->buf) - 1;
    char c = buf_char_at(e->buf, pos);
    editor_undo_push_delete(e, pos, &c, 1);
    buf_delete_before(e->buf);
    editor_sync_cursor(e);
    after_edit(e, e->cursor_line < prev_line ? e->cursor_line : prev_line);
}

void do_delete(Editor *e) {
    if (e->sel_active) {
        delete_selection(e);
        return;
    }
    if (buf_cursor_pos(e->buf) >= buf_length(e->buf)) return;

    size_t pos = buf_cursor_pos(e->buf);
    char c = buf_char_at(e->buf, pos);
    editor_undo_push_delete(e, pos, &c, 1);
    buf_delete_after(e->buf);
    /* Supr no mueve el cursor: solo re-tokeniza y repinta */
    editor_update_lexer(e, e->cursor_line);
    e->modified = 1;
    e->needs_redraw = 1;
}

/* ── Selección global ─────────────────────────────────────────────────────── */

void select_all(Editor *e) {
    e->sel_anchor_line = 0; /* ancla en (0,0) */
    e->sel_anchor_col = 0;
    e->sel_active = 1;
    buf_move_to(e->buf, buf_length(e->buf)); /* cursor al final */
    after_cursor_move(e);
}

/* ── Portapapeles ─────────────────────────────────────────────────────────── */

void do_copy(Editor *e) {
    size_t from, to;
    if (!editor_sel_range(e, &from, &to)) return;
    size_t len = to - from;
    char *tmp = malloc(len + 1);
    if (!tmp) return;
    buf_get_text(e->buf, from, to, tmp);
    tmp[len] = '\0';
    SDL_SetClipboardText(tmp);
    free(tmp);
}

void do_cut(Editor *e) {
    do_copy(e);
    delete_selection(e);
}

void do_paste(Editor *e) {
    if (!SDL_HasClipboardText()) return;
    char *text = SDL_GetClipboardText();
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

/* ── Ctrl+D — duplicar la línea actual ────────────────────────────────────── */

void duplicate_line(Editor *e) {
    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t line_end = buf_line_end(e->buf, line_start);
    size_t len = line_end - line_start;

    char *tmp = malloc(len + 2);
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

/* ── Ctrl+/ — comentar / descomentar la línea ─────────────────────────────── */

/** Prefijo de comentario de línea según la extensión del archivo. */
static const char *line_comment_prefix(const char *filepath) {
    const char *ext = strrchr(filepath, '.');
    if (!ext) return "// ";
    if (!strcmp(ext, ".py") || !strcmp(ext, ".sh") || !strcmp(ext, ".rb") ||
        !strcmp(ext, ".yaml") || !strcmp(ext, ".yml") || !strcmp(ext, ".toml"))
        return "# ";
    if (!strcmp(ext, ".lua") || !strcmp(ext, ".sql")) return "-- ";
    return "// "; /* C/C++/JS por defecto */
}

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
    while (indent < line_len && (line_text[indent] == ' ' || line_text[indent] == '\t'))
        indent++;

    int already_commented =
        (line_len - indent >= prefix_len && strncmp(line_text + indent, prefix, prefix_len) == 0);

    if (already_commented) {
        size_t del_pos = line_start + indent;
        editor_undo_push_delete(e, del_pos, prefix, prefix_len);
        buf_delete_range(e->buf, del_pos, del_pos + prefix_len);
        buf_move_to(e->buf, del_pos);
    } else {
        size_t ins_pos = line_start + indent;
        editor_undo_push_insert(e, ins_pos, prefix, prefix_len);
        buf_move_to(e->buf, ins_pos);
        buf_insert_str(e->buf, prefix, prefix_len);
        buf_move_to(e->buf, ins_pos + prefix_len);
    }
    free(line_text);

    editor_sync_cursor(e);
    after_edit(e, e->cursor_line);
}

/* ── Ctrl+L — seleccionar la línea completa ───────────────────────────────── */

void select_line(Editor *e) {
    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t line_end = buf_line_end(e->buf, line_start);
    if (line_end < buf_length(e->buf)) line_end++; /* incluir el '\n' si no es la última */

    e->sel_active = 1;
    e->sel_anchor_line = e->cursor_line;
    e->sel_anchor_col = 0;
    buf_move_to(e->buf, line_end);
    after_cursor_move(e);
}
