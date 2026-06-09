/**
 * @file input_find.c
 * @brief Lógica de la barra de búsqueda / reemplazo (Ctrl+F): buscar, contar,
 *        navegar entre coincidencias y reemplazar.
 */
#include "input_internal.h"

/** ¿Coincide la query (case-insensitive) en la posición @p pos del buffer? */
static int match_at(Editor *e, size_t pos) {
    FindBar *f = &e->find;
    size_t qlen = (size_t)f->query_len;
    if (pos + qlen > buf_length(e->buf)) return 0;
    for (size_t j = 0; j < qlen; j++) {
        if (tolower((unsigned char)buf_char_at(e->buf, pos + j)) !=
            tolower((unsigned char)f->query[j]))
            return 0;
    }
    return 1;
}

/** Limpia la selección y el resultado de búsqueda actuales. */
static void clear_find_result(Editor *e) {
    editor_sel_clear(e);
    e->find.result_line = -1;
    e->needs_redraw = 1;
}

/** Primera coincidencia en @c [start_pos, fin), o @c (size_t)-1 si no hay. */
size_t find_next(Editor *e, size_t start_pos) {
    if (e->find.query_len == 0) return (size_t)-1;
    size_t len = buf_length(e->buf);
    size_t qlen = (size_t)e->find.query_len;
    for (size_t i = start_pos; i + qlen <= len; i++)
        if (match_at(e, i)) return i;
    return (size_t)-1;
}

/** Última coincidencia que empieza estrictamente antes de @p before, o -1. */
static size_t find_last_before(Editor *e, size_t before) {
    size_t qlen = (size_t)e->find.query_len;
    size_t len = buf_length(e->buf);
    size_t hit = (size_t)-1;
    for (size_t i = 0; i + qlen <= len; i++) {
        if (match_at(e, i)) {
            if (i < before) hit = i;
            i += qlen - 1; /* no solapar coincidencias */
        }
    }
    return hit;
}

/**
 * @brief Cuenta todas las coincidencias y fija match_index según el cursor.
 *
 * El cursor está al FINAL de la coincidencia actual, así que su inicio es
 * @c cursor-qlen.
 */
void count_matches(Editor *e) {
    FindBar *f = &e->find;
    f->match_count = 0;
    f->match_index = 0;
    if (f->query_len == 0) return;

    size_t len = buf_length(e->buf);
    size_t qlen = (size_t)f->query_len;
    size_t cur = buf_cursor_pos(e->buf);
    size_t match_start = (cur >= qlen) ? cur - qlen : 0;

    for (size_t i = 0; i + qlen <= len; i++) {
        if (match_at(e, i)) {
            if (i <= match_start) f->match_index = f->match_count;
            f->match_count++;
            i += qlen - 1;
        }
    }
}

/** Selecciona la coincidencia en @p hit (ancla al inicio, cursor al final). */
static void select_match(Editor *e, size_t hit) {
    buf_move_to(e->buf, hit);
    editor_sync_cursor(e);
    e->find.result_line = e->cursor_line;
    e->find.result_col = e->cursor_col;

    e->sel_active = 1; /* ancla ANTES de mover el cursor al final */
    e->sel_anchor_line = e->cursor_line;
    e->sel_anchor_col = e->cursor_col;

    buf_move_to(e->buf, hit + (size_t)e->find.query_len);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    count_matches(e);
    e->needs_redraw = 1;
}

/** Salta a la siguiente coincidencia (con wrap al principio). */
void find_jump(Editor *e) {
    if (e->find.query_len == 0) {
        clear_find_result(e);
        return;
    }
    size_t hit = find_next(e, buf_cursor_pos(e->buf) + 1);
    if (hit == (size_t)-1) hit = find_next(e, 0); /* wrap */
    if (hit == (size_t)-1) {
        clear_find_result(e);
        return;
    }
    select_match(e, hit);
}

/** Salta a la primera coincidencia del documento (al teclear en el campo). */
void find_first(Editor *e) {
    if (e->find.query_len == 0) {
        clear_find_result(e);
        return;
    }
    size_t hit = find_next(e, 0);
    if (hit == (size_t)-1) {
        clear_find_result(e);
        return;
    }
    select_match(e, hit);
}

/** Salta a la coincidencia anterior (con wrap al final). */
void find_prev(Editor *e) {
    if (e->find.query_len == 0) {
        clear_find_result(e);
        return;
    }
    size_t qlen = (size_t)e->find.query_len;
    size_t cur = buf_cursor_pos(e->buf);
    size_t search_end = (cur >= qlen) ? cur - qlen : 0; /* excluye el match actual */

    size_t hit = find_last_before(e, search_end);
    if (hit == (size_t)-1) hit = find_last_before(e, buf_length(e->buf) + 1); /* wrap: último */
    if (hit == (size_t)-1) {
        clear_find_result(e);
        return;
    }
    select_match(e, hit);
}

/** Reemplaza la coincidencia actual por el texto de reemplazo y salta a la siguiente. */
void do_replace(Editor *e) {
    FindBar *f = &e->find;
    if (f->query_len == 0) return;

    /* Si el campo reemplazar está vacío y sin foco, solo lo enfocamos
       (evita borrar texto accidentalmente). */
    if (f->replace_len == 0 && !f->replace_focused) {
        f->replace_focused = 1;
        f->bar_focused = 1;
        e->needs_redraw = 1;
        return;
    }

    size_t qlen = (size_t)f->query_len;
    size_t hit = (size_t)-1;

    /* Si la selección actual ES la coincidencia, reemplazarla; si no, buscar. */
    if (e->sel_active) {
        size_t from, to;
        if (editor_sel_range(e, &from, &to) && (to - from) == qlen && match_at(e, from)) hit = from;
    }
    if (hit == (size_t)-1) {
        hit = find_next(e, buf_cursor_pos(e->buf));
        if (hit == (size_t)-1) hit = find_next(e, 0);
    }
    if (hit == (size_t)-1) {
        f->result_line = -1;
        e->needs_redraw = 1;
        return;
    }

    buf_delete_range(e->buf, hit, hit + qlen);
    buf_move_to(e->buf, hit);
    if (f->replace_len > 0) buf_insert_str(e->buf, f->replace, (size_t)f->replace_len);
    editor_sync_cursor(e);
    editor_update_lexer(e, 0);
    e->modified = 1;

    find_jump(e); /* saltar a la siguiente coincidencia */
}

/** Abre la barra de búsqueda y resetea su estado. */
void open_find_bar(Editor *e) {
    FindBar *f = &e->find;
    f->visible = 1;
    f->query[0] = '\0';
    f->query_len = 0;
    f->replace[0] = '\0';
    f->replace_len = 0;
    f->replace_focused = 0;
    f->bar_focused = 1;
    f->result_line = -1;
    f->query_sel_start = f->query_sel_end = -1;
    f->replace_sel_start = f->replace_sel_end = -1;
    f->match_count = 0;
    f->match_index = 0;
    f->prev_btn_w = 0;
    f->next_btn_w = 0;
    e->needs_redraw = 1;
}

/** Cierra la barra de búsqueda. */
void close_find_bar(Editor *e) {
    e->find.visible = 0;
    e->needs_redraw = 1;
}
