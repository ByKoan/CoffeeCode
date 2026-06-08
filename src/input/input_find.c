#include "input_internal.h"

size_t find_next(Editor *e, size_t start_pos) {
    FindBar *f = &e->find;
    if (f->query_len == 0) return (size_t)-1;
    size_t len = buf_length(e->buf);
    size_t qlen = (size_t)f->query_len;
    for (size_t i = start_pos; i + qlen <= len; i++) {
        int match = 1;
        for (size_t j = 0; j < qlen && match; j++) {
            char bc = buf_char_at(e->buf, i + j);
            char qc = f->query[j];
            /* búsqueda case-insensitive */
            if (tolower((unsigned char)bc) != tolower((unsigned char)qc)) match = 0;
        }
        if (match) return i;
    }
    return (size_t)-1;
}

/* Cuenta todas las coincidencias y actualiza match_count / match_index
   según la posición actual del cursor. */

void count_matches(Editor *e) {
    FindBar *f = &e->find;
    f->match_count = 0;
    f->match_index = 0;
    if (f->query_len == 0) return;
    size_t len = buf_length(e->buf);
    size_t qlen = (size_t)f->query_len;
    size_t cur = buf_cursor_pos(e->buf);
    /* cursor está al FINAL del match actual (hit + qlen), así que
       el inicio del match actual es cur - qlen */
    size_t match_start = (cur >= qlen) ? cur - qlen : 0;
    int idx = 0;
    for (size_t i = 0; i + qlen <= len; i++) {
        int match = 1;
        for (size_t j = 0; j < qlen && match; j++) {
            if (tolower((unsigned char)buf_char_at(e->buf, i + j)) !=
                tolower((unsigned char)f->query[j]))
                match = 0;
        }
        if (match) {
            if (i < match_start)
                idx = f->match_count;
            else if (i == match_start)
                idx = f->match_count;
            f->match_count++;
            i += qlen - 1;
        }
    }
    f->match_index = idx;
}

void find_jump(Editor *e) {
    if (e->find.query_len == 0) {
        /* query vacio: limpiar seleccion y resultado */
        editor_sel_clear(e);
        e->find.result_line = -1;
        e->needs_redraw = 1;
        return;
    }
    size_t from = buf_cursor_pos(e->buf) + 1;
    size_t hit = find_next(e, from);
    if (hit == (size_t)-1) {
        /* wrap around */
        hit = find_next(e, 0);
    }
    if (hit == (size_t)-1) {
        editor_sel_clear(e);
        e->find.result_line = -1;
        e->needs_redraw = 1;
        return;
    }
    /* posicionar cursor en el inicio del match para calcular linea/col del ancla */
    buf_move_to(e->buf, hit);
    editor_sync_cursor(e);
    e->find.result_line = e->cursor_line;
    e->find.result_col = e->cursor_col;
    /* fijar ancla ANTES de mover cursor al final */
    e->sel_active = 1;
    e->sel_anchor_line = e->cursor_line;
    e->sel_anchor_col = e->cursor_col;
    /* ahora mover cursor al final del match */
    buf_move_to(e->buf, hit + (size_t)e->find.query_len);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    count_matches(e);
    e->needs_redraw = 1;
}

/* Igual que find_jump pero siempre empieza desde el principio del documento.
   Se usa al escribir en el campo query para mostrar el primer resultado. */

void find_first(Editor *e) {
    if (e->find.query_len == 0) {
        editor_sel_clear(e);
        e->find.result_line = -1;
        e->needs_redraw = 1;
        return;
    }
    size_t hit = find_next(e, 0);
    if (hit == (size_t)-1) {
        editor_sel_clear(e);
        e->find.result_line = -1;
        e->needs_redraw = 1;
        return;
    }
    buf_move_to(e->buf, hit);
    editor_sync_cursor(e);
    e->find.result_line = e->cursor_line;
    e->find.result_col = e->cursor_col;
    e->sel_active = 1;
    e->sel_anchor_line = e->cursor_line;
    e->sel_anchor_col = e->cursor_col;
    buf_move_to(e->buf, hit + (size_t)e->find.query_len);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    count_matches(e);
    e->needs_redraw = 1;
}

/* Salta a la coincidencia ANTERIOR (hacia atrás). */

void find_prev(Editor *e) {
    if (e->find.query_len == 0) {
        editor_sel_clear(e);
        e->find.result_line = -1;
        e->needs_redraw = 1;
        return;
    }
    /* La posición de inicio de la selección actual es el ancla.
       Busca el último match que termine ANTES de esa posición. */
    size_t qlen = (size_t)e->find.query_len;
    size_t len = buf_length(e->buf);
    /* cursor actual apunta al final del match; retroceder al inicio */
    size_t cur = buf_cursor_pos(e->buf);
    size_t search_end = (cur >= qlen) ? cur - qlen : 0; /* excluye match actual */

    size_t hit = (size_t)-1;
    /* Buscar hacia atrás: iteramos todos los matches y nos quedamos con el último < search_end */
    for (size_t i = 0; i + qlen <= len; i++) {
        int match = 1;
        for (size_t j = 0; j < qlen && match; j++) {
            if (tolower((unsigned char)buf_char_at(e->buf, i + j)) !=
                tolower((unsigned char)e->find.query[j]))
                match = 0;
        }
        if (match) {
            if (i < search_end) hit = i;
            i += qlen - 1;
        }
    }
    if (hit == (size_t)-1) {
        /* wrap around: último match del documento */
        for (size_t i = 0; i + qlen <= len; i++) {
            int match = 1;
            for (size_t j = 0; j < qlen && match; j++) {
                if (tolower((unsigned char)buf_char_at(e->buf, i + j)) !=
                    tolower((unsigned char)e->find.query[j]))
                    match = 0;
            }
            if (match) {
                hit = i;
                i += qlen - 1;
            }
        }
    }
    if (hit == (size_t)-1) {
        editor_sel_clear(e);
        e->find.result_line = -1;
        e->needs_redraw = 1;
        return;
    }
    buf_move_to(e->buf, hit);
    editor_sync_cursor(e);
    e->find.result_line = e->cursor_line;
    e->find.result_col = e->cursor_col;
    e->sel_active = 1;
    e->sel_anchor_line = e->cursor_line;
    e->sel_anchor_col = e->cursor_col;
    buf_move_to(e->buf, hit + qlen);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    count_matches(e);
    e->needs_redraw = 1;
}

void do_replace(Editor *e) {
    FindBar *f = &e->find;
    if (f->query_len == 0) return;

    /* Si el campo reemplazar está vacío, simplemente enfocar ese campo
       para que el usuario sepa que debe escribir el texto de reemplazo.
       Así evitamos borrar texto accidentalmente. */
    if (f->replace_len == 0 && !f->replace_focused) {
        f->replace_focused = 1;
        f->bar_focused = 1;
        e->needs_redraw = 1;
        return;
    }

    /* Buscar el match actual: si hay seleccion activa que coincide, usarla;
       si no, buscar desde el principio para encontrar el match mas cercano. */
    size_t hit = (size_t)-1;
    size_t qlen = (size_t)f->query_len;

    if (e->sel_active) {
        size_t from, to;
        if (editor_sel_range(e, &from, &to) && (to - from) == qlen) {
            int match = 1;
            for (size_t j = 0; j < qlen && match; j++) {
                if (tolower((unsigned char)buf_char_at(e->buf, from + j)) !=
                    tolower((unsigned char)f->query[j]))
                    match = 0;
            }
            if (match) hit = from;
        }
    }

    /* Si no hay seleccion valida, buscar desde el cursor actual */
    if (hit == (size_t)-1) {
        size_t cur = buf_cursor_pos(e->buf);
        hit = find_next(e, cur);
        if (hit == (size_t)-1) hit = find_next(e, 0);
    }

    if (hit == (size_t)-1) {
        f->result_line = -1;
        e->needs_redraw = 1;
        return;
    }

    /* Borrar el match y escribir el reemplazo */
    buf_delete_range(e->buf, hit, hit + qlen);
    buf_move_to(e->buf, hit);
    if (f->replace_len > 0) buf_insert_str(e->buf, f->replace, (size_t)f->replace_len);
    editor_sync_cursor(e);
    editor_update_lexer(e, 0);
    e->modified = 1;

    /* Saltar al siguiente resultado */
    find_jump(e);
}

void open_find_bar(Editor *e) {
    e->find.visible = 1;
    e->find.query[0] = '\0';
    e->find.query_len = 0;
    e->find.replace[0] = '\0';
    e->find.replace_len = 0;
    e->find.replace_focused = 0;
    e->find.bar_focused = 1;
    e->find.result_line = -1;
    e->find.query_sel_start = -1;
    e->find.query_sel_end = -1;
    e->find.replace_sel_start = -1;
    e->find.replace_sel_end = -1;
    e->find.match_count = 0;
    e->find.match_index = 0;
    e->find.prev_btn_w = 0;
    e->find.next_btn_w = 0;
    e->needs_redraw = 1;
}

void close_find_bar(Editor *e) {
    e->find.visible = 0;
    e->needs_redraw = 1;
}

/* -- Ctrl+B — toggle panel lateral --------------------------------- */
