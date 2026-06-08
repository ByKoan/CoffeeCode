#include "editor.h"
#include "render.h"
#include "input.h"
#include "font_data.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>   /* stat() para mtime */

/* ═══════════════════════════════════════════════════════════════════════════
 * DISEÑO DE TABS
 * ─────────────────────────────────────────────────────────────────────────
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

/* ── Utilidad: mtime del fichero ─────────────────────────────────────────── */
static long file_mtime(const char *path) {
    if (!path || !path[0]) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (long)st.st_mtime;
}

/* ── editor_pos_from_line_col ────────────────────────────────────────────── */
size_t editor_pos_from_line_col(Editor *e, int line, int col) {
    size_t len = buf_length(e->buf);
    int cur_line = 0, cur_col = 0;
    for (size_t i = 0; i <= len; i++) {
        if (cur_line == line && cur_col == col) return i;
        if (i == len) break;
        char c = buf_char_at(e->buf, i);
        if (c == '\n') {
            if (cur_line == line) return i;
            cur_line++; cur_col = 0;
        } else {
            cur_col++;
        }
    }
    return len;
}

/* ── editor_sync_cursor ──────────────────────────────────────────────────── */
void editor_sync_cursor(Editor *e) {
    size_t pos = buf_cursor_pos(e->buf);
    int line, col;
    buf_line_col(e->buf, pos, &line, &col);
    e->cursor_line = line;
    e->cursor_col  = col;
}

/* ── editor_ensure_visible ───────────────────────────────────────────────── */
void editor_ensure_visible(Editor *e) {
    int left_off  = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    int vis_lines = (e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT - SHORTCUT_HEIGHT) / LINE_HEIGHT;
    int vis_cols  = (e->win_w - left_off - GUTTER_WIDTH - PADDING_LEFT) / e->char_w;

    if (e->cursor_line < e->scroll_line)
        e->scroll_line = e->cursor_line;
    if (e->cursor_line >= e->scroll_line + vis_lines)
        e->scroll_line = e->cursor_line - vis_lines + 1;

    if (e->cursor_col < e->scroll_col)
        e->scroll_col = e->cursor_col;
    if (e->cursor_col >= e->scroll_col + vis_cols)
        e->scroll_col = e->cursor_col - vis_cols + 1;

    if (e->scroll_line < 0) e->scroll_line = 0;
    if (e->scroll_col  < 0) e->scroll_col  = 0;
}

/* ── editor_update_lexer ─────────────────────────────────────────────────── */
void editor_update_lexer(Editor *e, int from_line) {
    int total = buf_line_count(e->buf);
    lexer_cache_resize(e->lex, total);
    lexer_cache_dirty(e->lex, from_line);
}

/* ── Selección ───────────────────────────────────────────────────────────── */
int editor_sel_range(Editor *e, size_t *from, size_t *to) {
    if (!e->sel_active) return 0;
    size_t anchor = editor_pos_from_line_col(e, e->sel_anchor_line, e->sel_anchor_col);
    size_t cursor = buf_cursor_pos(e->buf);
    if (anchor <= cursor) { *from = anchor; *to = cursor; }
    else                  { *from = cursor; *to = anchor; }
    return (*from != *to);
}

void editor_sel_clear(Editor *e) {
    e->sel_active = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * GESTIÓN DE TABS
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Guarda los campos escalares del editor en el tab activo.
 * buf/lex/undo NO se copian: el tab es su propietario permanente. */
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

    /* ── punteros directos al almacenamiento del tab ── */
    e->buf  = &t->buf;
    e->lex  = &t->lex;
    e->undo = &t->undo;

    /* ── recarga eficiente ──────────────────────────────────────────────
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

    /* ── escalares ── */
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
    e->active_tab = idx;
    editor_tab_load_state(e);
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

/* Libera los recursos de un EditorTab (sin tocar e->buf/lex/undo) */
static void tab_free_resources(EditorTab *t) {
    buf_free(&t->buf);
    lexer_cache_free(&t->lex);
    /* liberar pila de undo */
    for (int i = 0; i < UNDO_MAX; i++) {
        free(t->undo.entries[i].text);
        t->undo.entries[i].text = NULL;
    }
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

static void undo_entry_free(UndoEntry *ue) {
    free(ue->text);
    ue->text = NULL;
    ue->len  = 0;
}

static void undo_discard_redo(UndoStack *us) {
    if (us->redo_top == 0) return;
    for (int i = 0; i < us->redo_top; i++) {
        int idx = ((us->head - 1 - i) % UNDO_MAX + UNDO_MAX) % UNDO_MAX;
        undo_entry_free(&us->entries[idx]);
    }
    us->count   -= us->redo_top;
    us->head     = ((us->head - us->redo_top) % UNDO_MAX + UNDO_MAX) % UNDO_MAX;
    us->redo_top = 0;
}

static void undo_push(UndoStack *us, UndoType type,
                      size_t pos, const char *text, size_t len,
                      int cl, int cc) {
    undo_discard_redo(us);
    int idx = us->head % UNDO_MAX;
    if (us->count == UNDO_MAX)
        undo_entry_free(&us->entries[idx]);

    us->entries[idx].type              = type;
    us->entries[idx].pos               = pos;
    us->entries[idx].text              = malloc(len + 1);
    if (us->entries[idx].text) {
        memcpy(us->entries[idx].text, text, len);
        us->entries[idx].text[len] = '\0';
    }
    us->entries[idx].len               = len;
    us->entries[idx].cursor_line_after = cl;
    us->entries[idx].cursor_col_after  = cc;

    us->head = (us->head + 1) % UNDO_MAX;
    if (us->count < UNDO_MAX) us->count++;
    us->redo_top = 0;
}

void editor_undo_push_insert(Editor *e, size_t pos, const char *text, size_t len) {
    undo_push(e->undo, UNDO_INSERT, pos, text, len, e->cursor_line, e->cursor_col);
}

void editor_undo_push_delete(Editor *e, size_t pos, const char *text, size_t len) {
    undo_push(e->undo, UNDO_DELETE, pos, text, len, e->cursor_line, e->cursor_col);
}

void editor_undo(Editor *e) {
    UndoStack *us = e->undo;
    if (us->count == 0 || us->count == us->redo_top) return;

    int idx = ((us->head - 1 - us->redo_top) % UNDO_MAX + UNDO_MAX) % UNDO_MAX;
    UndoEntry *ue = &us->entries[idx];

    if (ue->type == UNDO_INSERT) {
        buf_delete_range(e->buf, ue->pos, ue->pos + ue->len);
        buf_move_to(e->buf, ue->pos);
    } else {
        buf_move_to(e->buf, ue->pos);
        buf_insert_str(e->buf, ue->text, ue->len);
        buf_move_to(e->buf, ue->pos);
    }
    us->redo_top++;
    editor_sync_cursor(e);
    editor_update_lexer(e, 0);
    editor_ensure_visible(e);
    e->modified     = 1;
    e->needs_redraw = 1;
}

void editor_redo(Editor *e) {
    UndoStack *us = e->undo;
    if (us->redo_top == 0) return;

    us->redo_top--;
    int idx = ((us->head - 1 - us->redo_top) % UNDO_MAX + UNDO_MAX) % UNDO_MAX;
    UndoEntry *ue = &us->entries[idx];

    if (ue->type == UNDO_INSERT) {
        buf_move_to(e->buf, ue->pos);
        buf_insert_str(e->buf, ue->text, ue->len);
        buf_move_to(e->buf, ue->pos + ue->len);
    } else {
        buf_delete_range(e->buf, ue->pos, ue->pos + ue->len);
        buf_move_to(e->buf, ue->pos);
    }
    editor_sync_cursor(e);
    editor_update_lexer(e, 0);
    editor_ensure_visible(e);
    e->modified     = 1;
    e->needs_redraw = 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * INIT / FREE / RUN
 * ═══════════════════════════════════════════════════════════════════════════ */

int editor_init(Editor *e, const char *filepath) {
    memset(e, 0, sizeof(*e));
    e->running          = 1;
    e->needs_redraw     = 1;
    e->menu_hovered     = -1;
    e->find.result_line = -1;

    /* SDL */
#ifdef _DEBUG
    fprintf(stderr, "STEP: SDL_Init\n");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 0;
    }

    e->win_w = 1200;
    e->win_h = 800;
#ifdef _DEBUG
    fprintf(stderr, "STEP: SDL_CreateWindow\n");
#endif
    e->window = SDL_CreateWindow("CoffeeCode", e->win_w, e->win_h, SDL_WINDOW_RESIZABLE);
    if (!e->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 0;
    }

#ifdef _DEBUG
    fprintf(stderr, "STEP: SDL_CreateRenderer\n");
#endif
    e->renderer = SDL_CreateRenderer(e->window, NULL);
    if (!e->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 0;
    }
    SDL_SetRenderVSync(e->renderer, 1);
    SDL_StartTextInput(e->window);

    /* SDL_ttf */
#ifdef _DEBUG
    fprintf(stderr, "STEP: TTF_Init\n");
#endif
    if (!TTF_Init()) {
        fprintf(stderr, "TTF_Init: %s\n", SDL_GetError());
        return 0;
    }

#ifdef _DEBUG
    fprintf(stderr, "STEP: TTF_OpenFont\n");
#endif
    {
        SDL_IOStream *io = SDL_IOFromConstMem(g_font_data, (Sint64)g_font_size);
        if (!io) { fprintf(stderr, "SDL_IOFromConstMem: %s\n", SDL_GetError()); return 0; }
        e->font = TTF_OpenFontIO(io, 1, FONT_SIZE);
        if (!e->font) { fprintf(stderr, "TTF_OpenFontIO: %s\n", SDL_GetError()); return 0; }
    }

    {
        int w = 0, h = 0;
        TTF_GetStringSize(e->font, "M", 1, &w, &h);
        e->char_w = w > 0 ? w : FONT_SIZE / 2;
    }

    /* FileTree */
    ftree_init(&e->ftree);

    /* ── Tabs ───────────────────────────────────────────────────────────
     * Si se pasó un filepath, crear el tab inicial con ese archivo.
     * Si no, arrancar con tab_count = 0 (pantalla de bienvenida). */
    e->tab_count  = 0;
    e->active_tab = 0;
    e->buf  = NULL;
    e->lex  = NULL;
    e->undo = NULL;

    if (filepath && filepath[0]) {
#ifdef _DEBUG
        fprintf(stderr, "STEP: opening initial file\n");
#endif
        editor_tab_open(e, filepath);
        SDL_SetWindowTitle(e->window, filepath);
    }

#ifdef _DEBUG
    fprintf(stderr, "STEP: editor_init OK\n");
#endif
    return 1;
}

/* Libera todos los tabs y los recursos SDL */
void editor_free(Editor *e) {
    for (int i = 0; i < e->tab_count; i++)
        tab_free_resources(&e->tabs[i]);
    ftree_free(&e->ftree);
    if (e->font)     TTF_CloseFont(e->font);
    if (e->renderer) SDL_StopTextInput(e->window);
    if (e->renderer) SDL_DestroyRenderer(e->renderer);
    if (e->window)   SDL_DestroyWindow(e->window);
    TTF_Quit();
    SDL_Quit();
}

void editor_run(Editor *e) {
    SDL_Event ev;
    while (e->running) {
        if (SDL_WaitEventTimeout(&ev, 16)) {
            input_handle_event(e, &ev);
            while (SDL_PollEvent(&ev))
                input_handle_event(e, &ev);
        }

        /* Autoguardado instantaneo si hay cambios y filepath definido */
        if (e->autosave && e->modified && e->filepath[0]) {
            Uint64 now = SDL_GetTicks();
            if (now - e->autosave_last_ms >= 300) {
                if (buf_save_file(e->buf, e->filepath)) {
                    e->modified = 0;
                    if (e->tab_count > 0) {
                        EditorTab *_t = &e->tabs[e->active_tab];
                        strncpy(_t->filepath, e->filepath, 511);
                        _t->modified = 0;
                        { struct stat _st; _t->loaded_mtime = (stat(e->filepath, &_st) == 0) ? (long)_st.st_mtime : 0; }
                    }
                    e->needs_redraw = 1;
                }
                e->autosave_last_ms = now;
            }
        }

        if (e->needs_redraw) {
            render_frame(e);
            e->needs_redraw = 0;
        }
    }
}
