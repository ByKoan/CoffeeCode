#include "editor_internal.h"
#include "input/input.h"
#include "render/render.h"

/**
 * @brief Abre la fuente del editor desde disco (ya no va embebida en el binario).
 *
 * Orden de búsqueda: variable de entorno @c COFFEECODE_FONT, luego @c font.ttf y
 * @c assets/font.ttf junto al ejecutable (vía @c SDL_GetBasePath), y por último
 * relativas al directorio de trabajo. Devuelve @c NULL si no encuentra ninguna.
 */
static TTF_Font *load_editor_font(float size) {
    const char *env = getenv("COFFEECODE_FONT");
    if (env && env[0]) {
        TTF_Font *f = TTF_OpenFont(env, size);
        if (f) return f;
    }

    const char *base = SDL_GetBasePath();
    if (base) {
        char path[1024];
        snprintf(path, sizeof(path), "%sfont.ttf", base);
        TTF_Font *f = TTF_OpenFont(path, size);
        if (f) return f;
        snprintf(path, sizeof(path), "%sassets/font.ttf", base);
        f = TTF_OpenFont(path, size);
        if (f) return f;
    }

    TTF_Font *f = TTF_OpenFont("assets/font.ttf", size);
    if (f) return f;
    return TTF_OpenFont("font.ttf", size);
}

size_t editor_pos_from_line_col(Editor *e, int line, int col) {
    Buffer *b = e->buf;
    int total = buf_line_count(b);
    if (line < 0) line = 0;
    if (line >= total) line = total - 1;

    /* O(1): inicio de línea directo desde el índice de líneas */
    size_t line_start = buf_line_offset(b, line);

    /* fin de línea sin incluir \n: O(longitud de línea) */
    size_t line_end = buf_line_end(b, line_start);

    /* clamp col al rango real */
    int line_len = (int)(line_end - line_start);
    if (col < 0) col = 0;
    if (col > line_len) col = line_len;

    return line_start + (size_t)col;
}

/* -- editor_sync_cursor ---------------------------------------------------- */

void editor_sync_cursor(Editor *e) {
    size_t pos = buf_cursor_pos(e->buf);
    int line, col;
    buf_line_col(e->buf, pos, &line, &col);
    e->cursor_line = line;
    e->cursor_col = col;
}

/* -- editor_ensure_visible ------------------------------------------------- */

void editor_ensure_visible(Editor *e) {
    int left_off = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    int vis_lines =
        (e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT - SHORTCUT_HEIGHT) / LINE_HEIGHT;
    int vis_cols = (e->win_w - left_off - GUTTER_WIDTH - PADDING_LEFT) / e->char_w;

    if (e->cursor_line < e->scroll_line) e->scroll_line = e->cursor_line;
    if (e->cursor_line >= e->scroll_line + vis_lines)
        e->scroll_line = e->cursor_line - vis_lines + 1;

    if (e->cursor_col < e->scroll_col) e->scroll_col = e->cursor_col;
    if (e->cursor_col >= e->scroll_col + vis_cols) e->scroll_col = e->cursor_col - vis_cols + 1;

    if (e->scroll_line < 0) e->scroll_line = 0;
    if (e->scroll_col < 0) e->scroll_col = 0;
}

/* -- editor_update_lexer --------------------------------------------------- */

void editor_update_lexer(Editor *e, int from_line) {
    int total = buf_line_count(e->buf);
    lexer_cache_resize(e->lex, total);
    lexer_cache_dirty(e->lex, from_line);
}

/* -- Selección ------------------------------------------------------------- */

int editor_sel_range(Editor *e, size_t *from, size_t *to) {
    if (!e->sel_active) return 0;
    size_t anchor = editor_pos_from_line_col(e, e->sel_anchor_line, e->sel_anchor_col);
    size_t cursor = buf_cursor_pos(e->buf);
    if (anchor <= cursor) {
        *from = anchor;
        *to = cursor;
    } else {
        *from = cursor;
        *to = anchor;
    }
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

int editor_init(Editor *e, const char *filepath) {
    memset(e, 0, sizeof(*e));
    e->running = 1;
    e->needs_redraw = 1;
    e->menu_hovered = -1;
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
    fprintf(stderr, "STEP: load font\n");
#endif
    e->font = load_editor_font(FONT_SIZE);
    if (!e->font) {
        fprintf(stderr, "No se pudo cargar la fuente (font.ttf). Coloca font.ttf junto al "
                        "ejecutable o define COFFEECODE_FONT.\n");
        return 0;
    }

    {
        int w = 0, h = 0;
        TTF_GetStringSize(e->font, "M", 1, &w, &h);
        e->char_w = w > 0 ? w : FONT_SIZE / 2;
    }

    /* FileTree */
    ftree_init(&e->ftree);

    /* -- Tabs -----------------------------------------------------------
     * Si se pasó un filepath, crear el tab inicial con ese archivo.
     * Si no, arrancar con tab_count = 0 (pantalla de bienvenida). */
    e->tab_count = 0;
    e->active_tab = 0;
    e->buf = NULL;
    e->lex = NULL;
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
    if (e->font) TTF_CloseFont(e->font);
    if (e->renderer) SDL_StopTextInput(e->window);
    if (e->renderer) SDL_DestroyRenderer(e->renderer);
    if (e->window) SDL_DestroyWindow(e->window);
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
                        {
                            struct stat _st;
                            _t->loaded_mtime =
                                (stat(e->filepath, &_st) == 0) ? (long)_st.st_mtime : 0;
                        }
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
