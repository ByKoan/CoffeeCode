/**
 * @file input_find.c
 * @brief Lógica de la barra de búsqueda / reemplazo (Ctrl+F): buscar, contar,
 *        navegar entre coincidencias y reemplazar.
 *
 * @note Modelo de búsqueda. La query (texto a buscar) vive en @c e->find.query
 * y la comparación es INSENSIBLE A MAYÚSCULAS (se compara todo en minúsculas
 * con
 * @c tolower). El buffer se direcciona por offset lógico (0 = primer carácter
 * del archivo), igual que en editor.c. Casi todo se construye sobre ::match_at,
 * que dice si la query encaja exactamente en una posición dada; encima van la
 * búsqueda hacia delante (::find_next), hacia atrás (::find_last_before) y la
 * navegación con "wrap" (al llegar al final se vuelve al principio y
 * viceversa). Una coincidencia se representa seleccionándola: ancla al inicio,
 * cursor al final. El valor centinela @c (size_t)-1 significa "no hay
 * coincidencia".
 */
#include "input_internal.h"

/**
 * @brief ¿Coincide la query (case-insensitive) en la posición @p pos del
 * buffer?
 *
 * Compara carácter a carácter la query contra el buffer a partir de @p pos,
 * pasando ambos lados por @c tolower para ignorar mayúsculas/minúsculas. El
 * cast a
 * @c unsigned char antes de @c tolower evita comportamiento indefinido con
 * bytes cuyo valor sea negativo en @c char con signo.
 *
 * @param e   Editor (aporta la query y el buffer).
 * @param pos Offset lógico del buffer donde probar la coincidencia.
 * @return 1 si la query encaja entera empezando en @p pos; 0 si no (o no cabe).
 */
static int match_at(Editor *e, size_t pos) {
    FindBar *f = &e->find;
    size_t qlen = (size_t)f->query_len;
    if (pos + qlen > buf_length(e->buf))
        return 0; /* la query no cabe desde pos */
    for (size_t j = 0; j < qlen; j++) {
        /* comparar en minúsculas; al primer carácter distinto, no hay match */
        if (tolower((unsigned char)buf_char_at(e->buf, pos + j)) !=
            tolower((unsigned char)f->query[j]))
            return 0;
    }
    return 1;
}

/** Limpia la selección y el resultado de búsqueda actuales (y pide redibujar).
 */
static void clear_find_result(Editor *e) {
    editor_sel_clear(e);
    e->find.result_line = -1; /* -1 = "sin resultado" */
    e->needs_redraw = 1;
}

/**
 * @brief Primera coincidencia en @c [start_pos, fin), o @c (size_t)-1 si no
 * hay.
 *
 * Búsqueda lineal hacia DELANTE desde @p start_pos: prueba ::match_at en cada
 * posición y devuelve la primera donde encaje. La condición @c i+qlen<=len
 * evita mirar más allá del final del buffer.
 *
 * @param e         Editor.
 * @param start_pos Offset desde el que empezar a buscar (inclusive).
 * @return Offset de inicio de la primera coincidencia, o @c (size_t)-1 si
 * ninguna.
 */
size_t find_next(Editor *e, size_t start_pos) {
    if (e->find.query_len == 0)
        return (size_t)-1; /* sin query, nada que buscar */
    size_t len = buf_length(e->buf);
    size_t qlen = (size_t)e->find.query_len;
    for (size_t i = start_pos; i + qlen <= len; i++)
        if (match_at(e, i)) return i;
    return (size_t)-1; /* recorrió hasta el final sin encontrar */
}

/**
 * @brief Última coincidencia que empieza estrictamente antes de @p before, o
 * -1.
 *
 * No existe búsqueda nativa "hacia atrás", así que se recorre TODO el buffer de
 * principio a fin guardando la última coincidencia vista cuyo inicio sea menor
 * que
 * @p before; al terminar, esa es la coincidencia anterior. Tras cada acierto se
 * salta @c qlen-1 posiciones para no contar coincidencias solapadas.
 *
 * @param e      Editor.
 * @param before Límite superior exclusivo: solo cuentan coincidencias antes de
 * él.
 * @return Offset de la última coincidencia previa a @p before, o @c (size_t)-1.
 */
static size_t find_last_before(Editor *e, size_t before) {
    size_t qlen = (size_t)e->find.query_len;
    size_t len = buf_length(e->buf);
    size_t hit = (size_t)-1; /* mejor candidato hasta ahora */
    for (size_t i = 0; i + qlen <= len; i++) {
        if (match_at(e, i)) {
            if (i < before)
                hit = i;   /* candidata válida: queda antes del límite */
            i += qlen - 1; /* no solapar coincidencias */
        }
    }
    return hit;
}

/**
 * @brief Cuenta todas las coincidencias y fija match_index según el cursor.
 *
 * Recorre el buffer contando todas las coincidencias (para el típico "3 de 7")
 * y, de paso, calcula cuál es la coincidencia ACTUAL. Como tras seleccionar una
 * coincidencia el cursor queda al FINAL de ella, su inicio es @c cursor-qlen;
 * el índice actual es el número de coincidencias cuyo inicio es @c <= ese
 * punto.
 *
 * @param e Editor (lee la query y el cursor; escribe match_count/match_index).
 */
void count_matches(Editor *e) {
    FindBar *f = &e->find;
    f->match_count = 0;
    f->match_index = 0;
    if (f->query_len == 0) return;

    size_t len = buf_length(e->buf);
    size_t qlen = (size_t)f->query_len;
    size_t cur = buf_cursor_pos(e->buf);
    /* inicio de la coincidencia actual: el cursor está al final de ella */
    size_t match_start = (cur >= qlen) ? cur - qlen : 0;

    for (size_t i = 0; i + qlen <= len; i++) {
        if (match_at(e, i)) {
            /* las que empiezan en/antes del actual fijan su índice (base 0) */
            if (i <= match_start) f->match_index = f->match_count;
            f->match_count++;
            i += qlen -
                 1; /* saltar el resto de esta coincidencia (no solapar) */
        }
    }
}

/**
 * @brief Selecciona la coincidencia en @p hit (ancla al inicio, cursor al
 * final).
 *
 * Deja la coincidencia resaltada como una selección normal del editor: primero
 * mueve el cursor al INICIO y ahí fija el ancla; luego mueve el cursor al FINAL
 * (inicio + longitud de la query). Así la selección abarca justo la
 * coincidencia. Además guarda su (línea, columna) como resultado, asegura que
 * sea visible y recuenta para actualizar el "N de M".
 *
 * @param e   Editor.
 * @param hit Offset lógico de inicio de la coincidencia a seleccionar.
 */
static void select_match(Editor *e, size_t hit) {
    buf_move_to(e->buf, hit); /* cursor al inicio de la coincidencia */
    editor_sync_cursor(e);
    e->find.result_line = e->cursor_line; /* recordar dónde está el resultado */
    e->find.result_col = e->cursor_col;

    e->sel_active = 1; /* ancla ANTES de mover el cursor al final */
    e->sel_anchor_line = e->cursor_line;
    e->sel_anchor_col = e->cursor_col;

    buf_move_to(e->buf, hit + (size_t)e->find.query_len); /* cursor al final */
    editor_sync_cursor(e);
    editor_ensure_visible(e); /* desplazar la vista para que se vea */
    count_matches(e);         /* actualizar "N de M" */
    e->needs_redraw = 1;
}

/**
 * @brief Salta a la siguiente coincidencia (con wrap al principio).
 *
 * Busca desde justo después del cursor (@c +1 para no quedarse en la
 * coincidencia actual). Si no encuentra nada hasta el final, reintenta desde 0
 * (wrap circular).
 *
 * @param e Editor.
 */
void find_jump(Editor *e) {
    if (e->find.query_len == 0) {
        clear_find_result(e);
        return;
    }
    size_t hit = find_next(e, buf_cursor_pos(e->buf) + 1);
    if (hit == (size_t)-1) hit = find_next(e, 0); /* wrap */
    if (hit == (size_t)-1) {
        clear_find_result(e); /* de verdad no hay ninguna */
        return;
    }
    select_match(e, hit);
}

/**
 * @brief Salta a la primera coincidencia del documento (al teclear en el
 * campo).
 *
 * Se usa al cambiar la query: busca siempre desde el principio (sin wrap,
 * porque ya empieza en 0) para mostrar la primera ocurrencia mientras se
 * escribe.
 *
 * @param e Editor.
 */
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

/**
 * @brief Salta a la coincidencia anterior (con wrap al final).
 *
 * Calcula el inicio de la coincidencia actual (@c cursor-qlen) y busca la
 * última que empiece estrictamente antes. Si no hay ninguna previa, hace wrap
 * buscando la última de todo el documento (límite @c len+1 para incluir la
 * última posición).
 *
 * @param e Editor.
 */
void find_prev(Editor *e) {
    if (e->find.query_len == 0) {
        clear_find_result(e);
        return;
    }
    size_t qlen = (size_t)e->find.query_len;
    size_t cur = buf_cursor_pos(e->buf);
    size_t search_end =
        (cur >= qlen) ? cur - qlen : 0; /* excluye el match actual */

    size_t hit = find_last_before(e, search_end);
    if (hit == (size_t)-1)
        hit = find_last_before(e, buf_length(e->buf) + 1); /* wrap: último */
    if (hit == (size_t)-1) {
        clear_find_result(e);
        return;
    }
    select_match(e, hit);
}

/**
 * @brief Reemplaza la coincidencia actual por el texto de reemplazo y salta a
 * la siguiente.
 *
 * Decide qué coincidencia reemplazar: si la selección actual ES exactamente una
 * coincidencia, esa; si no, la siguiente desde el cursor (con wrap a 0). Luego
 * borra ese rango, inserta el texto de reemplazo (si lo hay), marca el archivo
 * como modificado, re-tokeniza y salta a la siguiente coincidencia. Como
 * salvaguarda, si el campo de reemplazo está vacío y aún sin foco, solo le da
 * foco (para que el usuario no borre texto sin querer al pulsar "Reemplazar").
 *
 * @param e Editor.
 */
void do_replace(Editor *e) {
    FindBar *f = &e->find;
    if (f->query_len == 0) return; /* sin query no hay nada que reemplazar */

    /* Si el campo reemplazar está vacío y sin foco, solo lo enfocamos
       (evita borrar texto accidentalmente). */
    if (f->replace_len == 0 && !f->replace_focused) {
        f->replace_focused = 1;
        f->bar_focused = 1;
        e->needs_redraw = 1;
        return;
    }

    size_t qlen = (size_t)f->query_len;
    size_t hit =
        (size_t)-1; /* coincidencia a reemplazar (centinela: ninguna) */

    /* Si la selección actual ES la coincidencia, reemplazarla; si no, buscar.
     */
    if (e->sel_active) {
        size_t from, to;
        /* la selección debe medir justo qlen y encajar con la query en su
         * inicio */
        if (editor_sel_range(e, &from, &to) && (to - from) == qlen &&
            match_at(e, from))
            hit = from;
    }
    if (hit == (size_t)-1) { /* no había selección útil: buscar la siguiente */
        hit = find_next(e, buf_cursor_pos(e->buf));
        if (hit == (size_t)-1) hit = find_next(e, 0); /* wrap */
    }
    if (hit == (size_t)-1) { /* de verdad no hay nada que reemplazar */
        f->result_line = -1;
        e->needs_redraw = 1;
        return;
    }

    buf_delete_range(e->buf, hit, hit + qlen); /* quitar la coincidencia */
    buf_move_to(e->buf, hit);
    /* insertar el reemplazo (si está vacío, equivale a borrar la coincidencia)
     */
    if (f->replace_len > 0)
        buf_insert_str(e->buf, f->replace, (size_t)f->replace_len);
    editor_sync_cursor(e);
    editor_update_lexer(e, 0); /* re-tokenizar desde el principio */
    e->modified = 1;

    find_jump(e); /* saltar a la siguiente coincidencia */
}

/**
 * @brief Abre la barra de búsqueda y resetea su estado.
 *
 * Deja la barra visible y con foco, y limpia query, reemplazo, selecciones de
 * los campos, contadores y geometría de botones, para empezar de cero cada vez.
 *
 * @param e Editor.
 */
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
    f->prev_btn_w = 0; /* anchos a 0: render aún no ha colocado los botones */
    f->next_btn_w = 0;
    e->needs_redraw = 1;
}

/** Cierra la barra de búsqueda (solo la oculta; no toca el texto). */
void close_find_bar(Editor *e) {
    e->find.visible = 0;
    e->needs_redraw = 1;
}
