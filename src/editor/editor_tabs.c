#include "editor_internal.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * DISEÑO DE TABS
 * -------------------------------------------------------------------------
 * • e->buf / e->lex / e->undo son PUNTEROS que apuntan directamente a
 *   tabs[active_tab].buf/lex/undo.  No hay copia por valor — nunca
 *   pueden divergir aunque buf_insert/delete haga realloc internamente.
 * • editor_tab_load_state()  = redirigir los punteros + restaurar escalares.
 * • editor_tab_save_state()  = guardar solo los escalares (cursor/scroll…).
 * • Recarga eficiente: al volver a un tab NO modificado, se rehace
 *   buf_load_file() solo si el mtime del fichero cambió desde la última
 *   carga.  Si el fichero no fue modificado externamente, el buffer ya
 *   tiene el contenido correcto y no se re-lee.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* -- Utilidad: mtime del fichero ------------------------------------------- */

static long file_mtime(const char *path) {
    if (!path || !path[0]) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (long)st.st_mtime;
}

/* -- editor_pos_from_line_col ---------------------------------------------- */

void editor_tab_save_state(Editor *e) {
    if (e->tab_count == 0) return;
    EditorTab *t = &e->tabs[e->active_tab];
    t->cursor_line     = e->cursor_line;
    t->cursor_col      = e->cursor_col;
    t->scroll_line     = e->scroll_line;
    t->scroll_col      = e->scroll_col;
    t->modified        = e->modified;
    t->sel_active      = e->sel_active;
    t->sel_anchor_line = e->sel_anchor_line;
    t->sel_anchor_col  = e->sel_anchor_col;
}

/* Apunta e->buf/lex/undo al tab activo y restaura los escalares.
 * Si el archivo no fue modificado y su mtime cambió, lo recarga. */

static void editor_tab_load_state(Editor *e) {
    EditorTab *t = &e->tabs[e->active_tab];

    /* -- punteros directos al almacenamiento del tab -- */
    e->buf  = &t->buf;
    e->lex  = &t->lex;
    e->undo = &t->undo;
    e->hl   = t->hl;

    /* -- recarga eficiente ----------------------------------------------
     * Solo si el tab tiene ruta, NO está modificado y el mtime del
     * fichero en disco es distinto al que guardamos al cargar.
     * Si hay cambios sin guardar, nunca pisamos el trabajo del usuario. */
    if (t->filepath[0] && !t->modified) {
        long current_mtime = file_mtime(t->filepath);
        if (current_mtime != 0 && current_mtime != t->loaded_mtime) {
            /* Recargar: liberar buffer/lex actuales y releer */
            buf_free(&t->buf);
            buf_init(&t->buf);
            buf_load_file(&t->buf, t->filepath);
            t->loaded_mtime = current_mtime;

            lexer_cache_free(&t->lex);
            int total = buf_line_count(&t->buf);
            lexer_cache_init(&t->lex, total > 0 ? total : 1);

            /* reset de cursor/scroll al inicio tras recarga externa */
            t->cursor_line = 0; t->cursor_col = 0;
            t->scroll_line = 0; t->scroll_col = 0;
        }
    }

    /* -- escalares -- */
    e->cursor_line     = t->cursor_line;
    e->cursor_col      = t->cursor_col;
    e->scroll_line     = t->scroll_line;
    e->scroll_col      = t->scroll_col;
    e->modified        = t->modified;
    e->sel_active      = t->sel_active;
    e->sel_anchor_line = t->sel_anchor_line;
    e->sel_anchor_col  = t->sel_anchor_col;
    strncpy(e->filepath, t->filepath, sizeof(e->filepath) - 1);
}

/* Crea un nuevo tab vacío y lo activa */

void editor_tab_new(Editor *e) {
    if (e->tab_count >= MAX_TABS) return;
    if (e->tab_count > 0) editor_tab_save_state(e);
    int idx = e->tab_count++;
    EditorTab *t = &e->tabs[idx];
    memset(t, 0, sizeof(*t));
    buf_init(&t->buf);
    lexer_cache_init(&t->lex, 1);
    ring_init(&t->undo.entries, sizeof(UndoEntry), UNDO_MAX);
    t->hl = highlighter_default();
    t->filepath[0] = '\0';
    e->active_tab = idx;
    editor_tab_load_state(e);
    e->needs_redraw = 1;
}

/* Abre un archivo.  Si ya está en un tab, activa ese tab (sin recargar).
 * Si es nuevo, crea un tab y carga el archivo. */

void editor_tab_open(Editor *e, const char *path) {
    /* ¿ya está abierto? */
    for (int i = 0; i < e->tab_count; i++) {
        if (strcmp(e->tabs[i].filepath, path) == 0) {
            if (i == e->active_tab) return;
            editor_tab_save_state(e);
            e->active_tab = i;
            editor_tab_load_state(e);   /* hace recarga si mtime cambió */
            editor_update_lexer(e, 0);
            e->needs_redraw = 1;
            return;
        }
    }
    if (e->tab_count >= MAX_TABS) return;
    if (e->tab_count > 0) editor_tab_save_state(e);

    int idx = e->tab_count++;
    EditorTab *t = &e->tabs[idx];
    memset(t, 0, sizeof(*t));
    buf_init(&t->buf);
    buf_load_file(&t->buf, path);
    t->loaded_mtime = file_mtime(path);
    strncpy(t->filepath, path, sizeof(t->filepath) - 1);
    int total = buf_line_count(&t->buf);
    lexer_cache_init(&t->lex, total > 0 ? total : 1);
    ring_init(&t->undo.entries, sizeof(UndoEntry), UNDO_MAX);
    t->hl = highlighter_for_path(path);
    e->active_tab = idx;
    editor_tab_load_state(e);
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

/* Libera los recursos de un EditorTab (sin tocar e->buf/lex/undo) */

void tab_free_resources(EditorTab *t) {
    buf_free(&t->buf);
    lexer_cache_free(&t->lex);
    /* liberar pila de undo: primero el text de cada entrada, luego el ring */
    for (size_t i = 0; i < ring_len(&t->undo.entries); i++)
        free(((UndoEntry *)ring_at(&t->undo.entries, i))->text);
    ring_free(&t->undo.entries);
}

/* Cierra el tab activo */

void editor_tab_close(Editor *e) {
    if (e->tab_count == 0) return;

    tab_free_resources(&e->tabs[e->active_tab]);

    int closing = e->active_tab;
    /* desplazar tabs restantes */
    for (int i = closing; i < e->tab_count - 1; i++)
        e->tabs[i] = e->tabs[i + 1];
    /* limpiar el hueco que quedó al final */
    memset(&e->tabs[e->tab_count - 1], 0, sizeof(EditorTab));
    e->tab_count--;

    if (e->tab_count == 0) {
        /* sin tabs: apuntar a NULL — render_frame lo gestiona */
        e->buf  = NULL;
        e->lex  = NULL;
        e->undo = NULL;
        e->hl   = NULL;
        e->active_tab      = 0;
        e->filepath[0]     = '\0';
        e->modified        = 0;
        e->cursor_line     = 0; e->cursor_col  = 0;
        e->scroll_line     = 0; e->scroll_col  = 0;
        editor_sel_clear(e);
    } else {
        if (e->active_tab >= e->tab_count)
            e->active_tab = e->tab_count - 1;
        editor_tab_load_state(e);
        editor_update_lexer(e, 0);
        editor_sync_cursor(e);
    }
    e->needs_redraw = 1;
}

/* Cambia al tab i */

void editor_tab_switch(Editor *e, int i) {
    if (i < 0 || i >= e->tab_count || i == e->active_tab) return;
    editor_tab_save_state(e);
    e->active_tab = i;
    editor_tab_load_state(e);   /* recarga si mtime cambió y no hay cambios */
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * UNDO / REDO
 * ═══════════════════════════════════════════════════════════════════════════ */

