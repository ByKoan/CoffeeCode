#include "editor.h"
#include "render.h"
#include "input.h"
#include "font_data.h"
#include <string.h>
#include <stdio.h>

/* ── editor_pos_from_line_col ───────────────────────────────────────────── */
size_t editor_pos_from_line_col(Editor *e, int line, int col) {
    size_t len = buf_length(&e->buf);
    int cur_line = 0, cur_col = 0;
    for (size_t i = 0; i <= len; i++) {
        if (cur_line == line && cur_col == col) return i;
        if (i == len) break;
        char c = buf_char_at(&e->buf, i);
        if (c == '\n') {
            if (cur_line == line) return i; /* col fuera de rango → fin de línea */
            cur_line++;
            cur_col = 0;
        } else {
            cur_col++;
        }
    }
    return len;
}

/* ── editor_sync_cursor ─────────────────────────────────────────────────── */
void editor_sync_cursor(Editor *e) {
    size_t pos = buf_cursor_pos(&e->buf);
    int line, col;
    buf_line_col(&e->buf, pos, &line, &col);
    e->cursor_line = line;
    e->cursor_col  = col;
}

/* ── editor_ensure_visible ──────────────────────────────────────────────── */
void editor_ensure_visible(Editor *e) {
    int left_off  = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    int vis_lines = (e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT) / LINE_HEIGHT;
    int vis_cols  = (e->win_w - left_off - GUTTER_WIDTH - PADDING_LEFT) / e->char_w;

    /* scroll vertical */
    if (e->cursor_line < e->scroll_line)
        e->scroll_line = e->cursor_line;
    if (e->cursor_line >= e->scroll_line + vis_lines)
        e->scroll_line = e->cursor_line - vis_lines + 1;

    /* scroll horizontal */
    if (e->cursor_col < e->scroll_col)
        e->scroll_col = e->cursor_col;
    if (e->cursor_col >= e->scroll_col + vis_cols)
        e->scroll_col = e->cursor_col - vis_cols + 1;

    if (e->scroll_line < 0) e->scroll_line = 0;
    if (e->scroll_col  < 0) e->scroll_col  = 0;
}

/* ── editor_update_lexer ────────────────────────────────────────────────── */
void editor_update_lexer(Editor *e, int from_line) {
    int total = buf_line_count(&e->buf);
    lexer_cache_resize(&e->lex, total);
    lexer_cache_dirty(&e->lex, from_line);
}

/* ── editor_init ────────────────────────────────────────────────────────── */
int editor_init(Editor *e, const char *filepath) {
    memset(e, 0, sizeof(*e));
    e->running      = 1;
    e->needs_redraw = 1;
    e->menu_hovered = -1;

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
    e->window = SDL_CreateWindow("CoffeeCode",
                                  e->win_w, e->win_h,
                                  SDL_WINDOW_RESIZABLE);
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

    /* Activa la entrada de texto (SDL3: está desactivada por defecto) */
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
        if (!io) {
            fprintf(stderr, "SDL_IOFromConstMem: %s\\n", SDL_GetError());
            return 0;
        }
        e->font = TTF_OpenFontIO(io, 1, FONT_SIZE);
        if (!e->font) {
            fprintf(stderr, "TTF_OpenFontIO: %s\\n", SDL_GetError());
            return 0;
        }
    }

    /* Calcular ancho de carácter monoespaciado */
    {
        int w = 0, h = 0;
        TTF_GetStringSize(e->font, "M", 1, &w, &h);
        e->char_w = w > 0 ? w : FONT_SIZE / 2;
    }

#ifdef _DEBUG
    fprintf(stderr, "STEP: buf_init\n");
#endif
    /* Buffer */
    if (!buf_init(&e->buf)) return 0;

    /* FileTree (panel lateral) */
    ftree_init(&e->ftree);

    if (filepath && filepath[0]) {
        strncpy(e->filepath, filepath, sizeof(e->filepath) - 1);
        buf_load_file(&e->buf, filepath);
        SDL_SetWindowTitle(e->window, filepath);
    }

#ifdef _DEBUG
    fprintf(stderr, "STEP: lexer_cache_init\n");
#endif
    /* Lexer */
    int total = buf_line_count(&e->buf);
    if (!lexer_cache_init(&e->lex, total > 0 ? total : 1)) return 0;

    editor_sync_cursor(e);
#ifdef _DEBUG
    fprintf(stderr, "STEP: editor_init OK\n");
#endif
    return 1;
}

/* ── editor_free ────────────────────────────────────────────────────────── */
void editor_free(Editor *e) {
    buf_free(&e->buf);
    lexer_cache_free(&e->lex);
    ftree_free(&e->ftree);
    if (e->font)     TTF_CloseFont(e->font);
    if (e->renderer) SDL_StopTextInput(e->window);
    if (e->renderer) SDL_DestroyRenderer(e->renderer);
    if (e->window)   SDL_DestroyWindow(e->window);
    TTF_Quit();
    SDL_Quit();
}

/* ── editor_run ─────────────────────────────────────────────────────────── */
void editor_run(Editor *e) {
    SDL_Event ev;

    while (e->running) {
        /* espera eventos (ahorra CPU) */
        if (SDL_WaitEventTimeout(&ev, 16)) {
            input_handle_event(e, &ev);
            /* vacía la cola de eventos del mismo frame */
            while (SDL_PollEvent(&ev))
                input_handle_event(e, &ev);
        }

        if (e->needs_redraw) {
            render_frame(e);
            e->needs_redraw = 0;
        }
    }
}
