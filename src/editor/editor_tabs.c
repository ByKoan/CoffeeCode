/**
 * @file editor_tabs.c
 * @brief Gestión de pestañas (crear, abrir, cerrar, cambiar) y guardado/
 *        restauración del estado de cada pestaña.
 *
 * Diseño: @c e->buf / @c e->lex / @c e->undo / @c e->hl son PUNTEROS directos a
 * @c tabs[active_tab] (no copias por valor), así nunca divergen aunque el buffer
 * haga realloc. @ref editor_tab_load_state redirige esos punteros y restaura los
 * escalares; @ref editor_tab_save_state guarda solo los escalares.
 *
 * Recarga eficiente: al volver a una pestaña NO modificada cuyo fichero cambió
 * en disco (mtime distinto), se re-lee; si hay cambios sin guardar nunca se pisa
 * el trabajo del usuario.
 */
#include "editor_internal.h"

/** mtime del fichero @p path, o 0 si no existe / sin ruta. */
static long file_mtime(const char *path) {
    if (!path || !path[0]) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (long)st.st_mtime;
}

/** Inicializa una pestaña nueva y vacía (buffer + pila de undo). */
static void tab_init(EditorTab *t) {
    memset(t, 0, sizeof(*t));
    buf_init(&t->buf);
    ring_init(&t->undo.entries, sizeof(UndoEntry), UNDO_MAX);
}

/** Guarda en la pestaña activa los escalares del editor (cursor, scroll, sel). */
void editor_tab_save_state(Editor *e) {
    if (e->tab_count == 0) return;
    EditorTab *t = &e->tabs[e->active_tab];
    t->cursor_line = e->cursor_line;
    t->cursor_col = e->cursor_col;
    t->scroll_line = e->scroll_line;
    t->scroll_col = e->scroll_col;
    t->modified = e->modified;
    t->sel_active = e->sel_active;
    t->sel_anchor_line = e->sel_anchor_line;
    t->sel_anchor_col = e->sel_anchor_col;
}

/** Apunta e->buf/lex/undo/hl a la pestaña activa, restaura sus escalares y, si
 *  procede, recarga el fichero desde disco. */
static void editor_tab_load_state(Editor *e) {
    EditorTab *t = &e->tabs[e->active_tab];

    e->buf = &t->buf;
    e->lex = &t->lex;
    e->undo = &t->undo;
    e->hl = t->hl;

    /* recarga eficiente: solo si tiene ruta, no está modificado y cambió el mtime */
    if (t->filepath[0] && !t->modified) {
        long current_mtime = file_mtime(t->filepath);
        if (current_mtime != 0 && current_mtime != t->loaded_mtime) {
            buf_free(&t->buf);
            buf_init(&t->buf);
            buf_load_file(&t->buf, t->filepath);
            t->loaded_mtime = current_mtime;

            lexer_cache_free(&t->lex);
            int total = buf_line_count(&t->buf);
            lexer_cache_init(&t->lex, total > 0 ? total : 1);

            t->cursor_line = t->cursor_col = 0; /* reset tras recarga externa */
            t->scroll_line = t->scroll_col = 0;
        }
    }

    e->cursor_line = t->cursor_line;
    e->cursor_col = t->cursor_col;
    e->scroll_line = t->scroll_line;
    e->scroll_col = t->scroll_col;
    e->modified = t->modified;
    e->sel_active = t->sel_active;
    e->sel_anchor_line = t->sel_anchor_line;
    e->sel_anchor_col = t->sel_anchor_col;
    strncpy(e->filepath, t->filepath, sizeof(e->filepath) - 1);
}

/** Crea una pestaña nueva vacía y la activa. */
void editor_tab_new(Editor *e) {
    if (e->tab_count >= MAX_TABS) return;
    if (e->tab_count > 0) editor_tab_save_state(e);

    int idx = e->tab_count++;
    EditorTab *t = &e->tabs[idx];
    tab_init(t);
    lexer_cache_init(&t->lex, 1);
    t->hl = highlighter_default();

    e->active_tab = idx;
    editor_tab_load_state(e);
    e->needs_redraw = 1;
}

/** Abre @p path: si ya está abierto activa esa pestaña; si no, crea una nueva. */
void editor_tab_open(Editor *e, const char *path) {
    for (int i = 0; i < e->tab_count; i++) {
        if (strcmp(e->tabs[i].filepath, path) == 0) {
            if (i == e->active_tab) return;
            editor_tab_save_state(e);
            e->active_tab = i;
            editor_tab_load_state(e); /* recarga si cambió el mtime */
            editor_update_lexer(e, 0);
            e->needs_redraw = 1;
            return;
        }
    }
    if (e->tab_count >= MAX_TABS) return;
    if (e->tab_count > 0) editor_tab_save_state(e);

    int idx = e->tab_count++;
    EditorTab *t = &e->tabs[idx];
    tab_init(t);
    buf_load_file(&t->buf, path);
    t->loaded_mtime = file_mtime(path);
    strncpy(t->filepath, path, sizeof(t->filepath) - 1);
    int total = buf_line_count(&t->buf);
    lexer_cache_init(&t->lex, total > 0 ? total : 1);
    t->hl = highlighter_for_path(path);

    e->active_tab = idx;
    editor_tab_load_state(e);
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

/** Libera buffer/lexer/undo de una pestaña (sin tocar los punteros de e). */
void tab_free_resources(EditorTab *t) {
    buf_free(&t->buf);
    lexer_cache_free(&t->lex);
    /* la pila de undo posee los `text` de cada entrada: liberarlos antes del ring */
    for (size_t i = 0; i < ring_len(&t->undo.entries); i++)
        free(((UndoEntry *)ring_at(&t->undo.entries, i))->text);
    ring_free(&t->undo.entries);
}

/** Cierra la pestaña activa. */
void editor_tab_close(Editor *e) {
    if (e->tab_count == 0) return;

    tab_free_resources(&e->tabs[e->active_tab]);

    /* desplazar las pestañas restantes y limpiar el hueco final */
    for (int i = e->active_tab; i < e->tab_count - 1; i++)
        e->tabs[i] = e->tabs[i + 1];
    memset(&e->tabs[e->tab_count - 1], 0, sizeof(EditorTab));
    e->tab_count--;

    if (e->tab_count == 0) {
        /* sin pestañas: punteros a NULL (render_frame muestra la bienvenida) */
        e->buf = NULL;
        e->lex = NULL;
        e->undo = NULL;
        e->hl = NULL;
        e->active_tab = 0;
        e->filepath[0] = '\0';
        e->modified = 0;
        e->cursor_line = e->cursor_col = 0;
        e->scroll_line = e->scroll_col = 0;
        editor_sel_clear(e);
    } else {
        if (e->active_tab >= e->tab_count) e->active_tab = e->tab_count - 1;
        editor_tab_load_state(e);
        editor_update_lexer(e, 0);
        editor_sync_cursor(e);
    }
    e->needs_redraw = 1;
}

/** Cambia a la pestaña @p i. */
void editor_tab_switch(Editor *e, int i) {
    if (i < 0 || i >= e->tab_count || i == e->active_tab) return;
    editor_tab_save_state(e);
    e->active_tab = i;
    editor_tab_load_state(e); /* recarga si cambió el mtime y no hay cambios */
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}
