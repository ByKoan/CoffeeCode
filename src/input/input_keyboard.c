#include "input_internal.h"

void move_cursor(Editor *e, int line, int col) {
    int total = buf_line_count(e->buf);
    if (line < 0) line = 0;
    if (line >= total) line = total - 1;
    if (col < 0) col = 0;
    size_t pos = editor_pos_from_line_col(e, line, col);
    buf_move_to(e->buf, pos);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

/* -- move_cursor con selección Shift -------------------------------- */

void move_cursor_select(Editor *e, int line, int col, int selecting) {
    if (selecting) {
        if (!e->sel_active) {
            /* ancla en posición actual */
            e->sel_active = 1;
            e->sel_anchor_line = e->cursor_line;
            e->sel_anchor_col = e->cursor_col;
        }
    } else {
        editor_sel_clear(e);
    }
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
        if (!e->sel_active) {
            e->sel_active = 1;
            e->sel_anchor_line = e->cursor_line;
            e->sel_anchor_col = e->cursor_col;
        }
    } else {
        /* si había selección, saltar al inicio de la misma */
        if (e->sel_active) {
            size_t from, to;
            if (editor_sel_range(e, &from, &to)) {
                editor_sel_clear(e);
                buf_move_to(e->buf, from);
                editor_sync_cursor(e);
                editor_ensure_visible(e);
                e->needs_redraw = 1;
                return;
            }
            editor_sel_clear(e);
        }
    }
    if (e->cursor_col > 0) {
        move_cursor(e, e->cursor_line, e->cursor_col - 1);
    } else if (e->cursor_line > 0) {
        int prev = e->cursor_line - 1;
        size_t end = buf_line_end(e->buf, editor_pos_from_line_col(e, prev, 0));
        int lc = (int)(end - editor_pos_from_line_col(e, prev, 0));
        move_cursor(e, prev, lc);
    }
}

void move_col_right(Editor *e, int sel) {
    if (sel) {
        if (!e->sel_active) {
            e->sel_active = 1;
            e->sel_anchor_line = e->cursor_line;
            e->sel_anchor_col = e->cursor_col;
        }
    } else {
        /* si había selección, saltar al final */
        if (e->sel_active) {
            size_t from, to;
            if (editor_sel_range(e, &from, &to)) {
                editor_sel_clear(e);
                buf_move_to(e->buf, to);
                editor_sync_cursor(e);
                editor_ensure_visible(e);
                e->needs_redraw = 1;
                return;
            }
            editor_sel_clear(e);
        }
    }
    size_t pos = buf_cursor_pos(e->buf);
    size_t len = buf_length(e->buf);
    if (pos < len) {
        buf_move_right(e->buf);
        editor_sync_cursor(e);
        editor_ensure_visible(e);
        e->needs_redraw = 1;
    }
}

void move_home(Editor *e, int sel) {
    move_cursor_select(e, e->cursor_line, 0, sel);
}

void move_end(Editor *e, int sel) {
    size_t pos = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t end = buf_line_end(e->buf, pos);
    int col = (int)(end - pos);
    move_cursor_select(e, e->cursor_line, col, sel);
}

/* -- salto de palabra (Ctrl+Left / Ctrl+Right) --------------------- */

void move_word_left(Editor *e, int sel) {
    if (sel && !e->sel_active) {
        e->sel_active = 1;
        e->sel_anchor_line = e->cursor_line;
        e->sel_anchor_col = e->cursor_col;
    } else if (!sel) {
        editor_sel_clear(e);
    }
    size_t pos = buf_cursor_pos(e->buf);
    if (pos == 0) return;
    pos--;
    /* salta espacios/no-word */
    while (pos > 0 && !isalnum((unsigned char)buf_char_at(e->buf, pos)) &&
           buf_char_at(e->buf, pos) != '_')
        pos--;
    /* salta la palabra */
    while (pos > 0 && (isalnum((unsigned char)buf_char_at(e->buf, pos - 1)) ||
                       buf_char_at(e->buf, pos - 1) == '_'))
        pos--;
    buf_move_to(e->buf, pos);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

void move_word_right(Editor *e, int sel) {
    if (sel && !e->sel_active) {
        e->sel_active = 1;
        e->sel_anchor_line = e->cursor_line;
        e->sel_anchor_col = e->cursor_col;
    } else if (!sel) {
        editor_sel_clear(e);
    }
    size_t pos = buf_cursor_pos(e->buf);
    size_t len = buf_length(e->buf);
    /* salta la palabra actual */
    while (pos < len &&
           (isalnum((unsigned char)buf_char_at(e->buf, pos)) || buf_char_at(e->buf, pos) == '_'))
        pos++;
    /* salta espacios/no-word */
    while (pos < len && !isalnum((unsigned char)buf_char_at(e->buf, pos)) &&
           buf_char_at(e->buf, pos) != '_')
        pos++;
    buf_move_to(e->buf, pos);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

/* -- operaciones de edición ----------------------------------------------- */

/* Borra la selección activa y la registra en undo. Devuelve 1 si borró algo */

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
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
    return 1;
}

void insert_newline(Editor *e) {
    delete_selection(e);
    size_t pos = buf_cursor_pos(e->buf);
    editor_undo_push_insert(e, pos, "\n", 1);
    buf_insert(e->buf, '\n');
    editor_sync_cursor(e);
    size_t prev_start = editor_pos_from_line_col(e, e->cursor_line - 1, 0);
    size_t len = buf_length(e->buf);
    size_t i = prev_start;
    while (i < len) {
        char c = buf_char_at(e->buf, i);
        if (c == ' ') {
            buf_insert(e->buf, ' ');
            i++;
        } else if (c == '\t') {
            buf_insert(e->buf, '\t');
            i++;
        } else
            break;
    }
    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line - 1);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
}

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
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
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
    editor_update_lexer(e, e->cursor_line < prev_line ? e->cursor_line : prev_line);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
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
    editor_update_lexer(e, e->cursor_line);
    e->modified = 1;
    e->needs_redraw = 1;
}

/* -- Nuevo archivo --------------------------------------------------------- */

void select_all(Editor *e) {
    /* ancla en posición lógica 0 → línea 0, col 0 */
    e->sel_anchor_line = 0;
    e->sel_anchor_col = 0;
    e->sel_active = 1;
    /* mover cursor al final del documento */
    size_t end_pos = buf_length(e->buf);
    buf_move_to(e->buf, end_pos);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

/* -- Clipboard ------------------------------------------------------ */

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

    delete_selection(e); /* borra selección si la hay */

    size_t pos = buf_cursor_pos(e->buf);
    editor_undo_push_insert(e, pos, text, len);
    buf_insert_str(e->buf, text, len);
    SDL_free(text);
    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
}

/* -- Ctrl+D — duplicar línea actual --------------------------------- */

void duplicate_line(Editor *e) {
    /* Obtener texto de la línea actual */
    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t line_end = buf_line_end(e->buf, line_start);
    size_t len = line_end - line_start;

    char *tmp = malloc(len + 2);
    if (!tmp) return;
    buf_get_text(e->buf, line_start, line_end, tmp);
    tmp[len] = '\n';
    tmp[len + 1] = '\0';

    /* Insertar al final de la línea */
    buf_move_to(e->buf, line_end);
    editor_undo_push_insert(e, line_end, tmp, len + 1);
    buf_insert_str(e->buf, tmp, len + 1);
    free(tmp);

    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
}

/* -- Ctrl+/ — comentar/descomentar línea --------------------------- */

void toggle_line_comment(Editor *e) {
    /* Detecta el comentario según la extensión del archivo */
    const char *prefix = "// "; /* default C/C++/JS */
    const char *ext = strrchr(e->filepath, '.');
    if (ext) {
        if (strcmp(ext, ".py") == 0 || strcmp(ext, ".sh") == 0 || strcmp(ext, ".rb") == 0 ||
            strcmp(ext, ".yaml") == 0 || strcmp(ext, ".yml") == 0 || strcmp(ext, ".toml") == 0)
            prefix = "# ";
        else if (strcmp(ext, ".lua") == 0)
            prefix = "-- ";
        else if (strcmp(ext, ".sql") == 0)
            prefix = "-- ";
    }
    size_t plen = strlen(prefix);

    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t line_end = buf_line_end(e->buf, line_start);
    size_t line_len = line_end - line_start;

    char *line_text = malloc(line_len + 1);
    if (!line_text) return;
    buf_get_text(e->buf, line_start, line_end, line_text);
    line_text[line_len] = '\0';

    /* Omitir espacios iniciales para el check */
    size_t indent = 0;
    while (indent < line_len && (line_text[indent] == ' ' || line_text[indent] == '\t'))
        indent++;

    if (line_len - indent >= plen && strncmp(line_text + indent, prefix, plen) == 0) {
        /* ya está comentado → quitar prefijo */
        size_t del_pos = line_start + indent;
        editor_undo_push_delete(e, del_pos, prefix, plen);
        buf_delete_range(e->buf, del_pos, del_pos + plen);
        buf_move_to(e->buf, del_pos);
    } else {
        /* sin comentar → añadir prefijo en la posición de sangría */
        size_t ins_pos = line_start + indent;
        editor_undo_push_insert(e, ins_pos, prefix, plen);
        buf_move_to(e->buf, ins_pos);
        buf_insert_str(e->buf, prefix, plen);
        buf_move_to(e->buf, ins_pos + plen);
    }
    free(line_text);

    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
}

/* -- Ctrl+L — seleccionar línea completa ---------------------------- */

void select_line(Editor *e) {
    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t line_end = buf_line_end(e->buf, line_start);
    /* si no es la última línea, incluye el \n */
    if (line_end < buf_length(e->buf)) line_end++;

    e->sel_active = 1;
    e->sel_anchor_line = e->cursor_line;
    e->sel_anchor_col = 0;
    buf_move_to(e->buf, line_end);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

/* -- Ctrl+G — ir a línea -------------------------------------------- */
/* Implementación simple: el número se teclea en la barra find reutilizada.
   Se activa con un flag especial y se interpreta el texto como número de línea. */
#define GOTO_MODE_PREFIX "Ir a línea: "

/* -- Ctrl+F — barra de búsqueda ------------------------------------ */

/* Busca hacia adelante desde `start_pos` (exclusivo).
   Devuelve la posición lógica del match o (size_t)-1 si no encontrado. */
