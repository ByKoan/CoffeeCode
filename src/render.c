#include "render.h"
#include <stdio.h>
#include <string.h>

/* ── Colores del tema ────────────────────────────────────────────────────── */
#define COL_BG          0x28, 0x2C, 0x34, 0xFF
#define COL_GUTTER      0x21, 0x25, 0x2B, 0xFF
#define COL_CURSOR_LINE 0x2C, 0x31, 0x3C, 0xFF
#define COL_CURSOR      0x52, 0x8B, 0xFF, 0xFF
#define COL_STATUS_BG   0x21, 0x25, 0x2B, 0xFF

/* Navbar */
#define COL_NAVBAR_BG   0x1A, 0x1D, 0x23, 0xFF
#define COL_NAVBAR_BTN  0x2C, 0x31, 0x3C, 0xFF  /* hover del botón */
#define COL_MENU_BG     0x1E, 0x22, 0x2A, 0xFF
#define COL_MENU_HOVER  0x3E, 0x44, 0x55, 0xFF
#define COL_MENU_SEP    0x3A, 0x3F, 0x4A, 0xFF
#define COL_MENU_BORDER 0x3A, 0x3F, 0x4A, 0xFF

/* Menú "Archivo": items y separadores (-1 = separador) */
#define MENU_ITEM_H     26
#define MENU_WIDTH     160
#define MENU_ITEMS      4   /* Nuevo, Abrir, sep, Guardar */

static const char *MENU_LABELS[MENU_ITEMS] = {
    "Nuevo",
    "Abrir archivo...",
    NULL,           /* separador */
    "Guardar"
};
/* shortcut mostrado a la derecha */
static const char *MENU_HINTS[MENU_ITEMS] = {
    "Ctrl+N", "Ctrl+O", NULL, "Ctrl+S"
};

static void set_color(SDL_Renderer *r, uint8_t R, uint8_t G, uint8_t B, uint8_t A) {
    SDL_SetRenderDrawColor(r, R, G, B, A);
}

static int draw_text(Editor *e, const char *text, int x, int y,
                     uint8_t R, uint8_t G, uint8_t B)
{
    if (!text || !text[0]) return 0;
    SDL_Color col = {R, G, B, 255};
    SDL_Surface *surf = TTF_RenderText_Blended(e->font, text, 0, col);
    if (!surf) return 0;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(e->renderer, surf);
    SDL_FRect dst = {(float)x, (float)y, (float)surf->w, (float)surf->h};
    SDL_RenderTexture(e->renderer, tex, NULL, &dst);
    int w = surf->w;
    SDL_DestroySurface(surf);
    SDL_DestroyTexture(tex);
    return w;
}

static int get_line_text(Editor *e, int line, char *out, int max) {
    int cur_line = 0, col = 0;
    size_t len = buf_length(&e->buf);
    for (size_t i = 0; i <= len && col < max - 1; i++) {
        if (cur_line == line) {
            if (i == len || buf_char_at(&e->buf, i) == '\n') {
                out[col] = '\0';
                return col;
            }
            char c = buf_char_at(&e->buf, i);
            if (c == '\t') {
                int spaces = TAB_SIZE - (col % TAB_SIZE);
                for (int s = 0; s < spaces && col < max - 1; s++)
                    out[col++] = ' ';
            } else {
                out[col++] = c;
            }
        } else {
            if (i < len && buf_char_at(&e->buf, i) == '\n') cur_line++;
        }
    }
    out[col] = '\0';
    return col;
}

/* ── Navbar ─────────────────────────────────────────────────────────────── */
static void render_navbar(Editor *e) {
    SDL_Renderer *r = e->renderer;

    /* fondo */
    set_color(r, COL_NAVBAR_BG);
    SDL_FRect nb = {0, 0, (float)e->win_w, (float)NAVBAR_HEIGHT};
    SDL_RenderFillRect(r, &nb);

    /* separador inferior */
    set_color(r, 0x3A, 0x3F, 0x4A, 0xFF);
    SDL_FRect sep = {0, (float)(NAVBAR_HEIGHT - 1), (float)e->win_w, 1};
    SDL_RenderFillRect(r, &sep);

    /* botón "Archivo" */
    int btn_x = 4, btn_y = 2;
    int btn_w = 70, btn_h = NAVBAR_HEIGHT - 4;

    if (e->menu_open) {
        /* resaltado cuando el menú está abierto */
        set_color(r, COL_NAVBAR_BTN);
        SDL_FRect btn = {(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h};
        SDL_RenderFillRect(r, &btn);
    }

    int ty = btn_y + (btn_h - FONT_SIZE) / 2;
    draw_text(e, "  Archivo", btn_x, ty, 0xCC, 0xCC, 0xCC);

    /* título centrado */
    const char *title = e->filepath[0] ? e->filepath : "sin título";
    int tw = 0, th = 0;
    TTF_GetStringSize(e->font, title, 0, &tw, &th);
    int cx = (e->win_w - tw) / 2;
    if (cx < btn_x + btn_w + 8) cx = btn_x + btn_w + 8;
    draw_text(e, title, cx, ty, 0x60, 0x65, 0x70);

    /* punto rojo de modificado */
    if (e->modified) {
        set_color(r, 0xE0, 0x6C, 0x75, 0xFF);
        SDL_FRect dot = {(float)(cx + tw + 6), (float)(ty + FONT_SIZE/2 - 3), 6, 6};
        SDL_RenderFillRect(r, &dot);
    }
}

/* ── Menú desplegable ────────────────────────────────────────────────────── */
static void render_menu(Editor *e) {
    if (!e->menu_open) return;
    SDL_Renderer *r = e->renderer;

    int mx = 4, my = NAVBAR_HEIGHT;
    int total_h = 0;
    for (int i = 0; i < MENU_ITEMS; i++)
        total_h += MENU_LABELS[i] ? MENU_ITEM_H : 8; /* separador = 8px */

    /* sombra */
    set_color(r, 0x00, 0x00, 0x00, 0x60);
    SDL_FRect shadow = {(float)(mx+3), (float)(my+3),
                        (float)MENU_WIDTH, (float)total_h};
    SDL_RenderFillRect(r, &shadow);

    /* fondo */
    set_color(r, COL_MENU_BG);
    SDL_FRect bg = {(float)mx, (float)my, (float)MENU_WIDTH, (float)total_h};
    SDL_RenderFillRect(r, &bg);

    /* borde */
    set_color(r, COL_MENU_BORDER);
    SDL_FRect borders[4] = {
        {(float)mx, (float)my, (float)MENU_WIDTH, 1},
        {(float)mx, (float)(my+total_h-1), (float)MENU_WIDTH, 1},
        {(float)mx, (float)my, 1, (float)total_h},
        {(float)(mx+MENU_WIDTH-1), (float)my, 1, (float)total_h}
    };
    for (int i = 0; i < 4; i++) SDL_RenderFillRect(r, &borders[i]);

    int iy = my;
    for (int i = 0; i < MENU_ITEMS; i++) {
        if (!MENU_LABELS[i]) {
            /* separador */
            set_color(r, COL_MENU_SEP);
            SDL_FRect s = {(float)(mx+8), (float)(iy+4),
                           (float)(MENU_WIDTH-16), 1};
            SDL_RenderFillRect(r, &s);
            iy += 8;
            continue;
        }

        if (e->menu_hovered == i) {
            set_color(r, COL_MENU_HOVER);
            SDL_FRect hi = {(float)(mx+1), (float)iy,
                            (float)(MENU_WIDTH-2), (float)MENU_ITEM_H};
            SDL_RenderFillRect(r, &hi);
        }

        int ty2 = iy + (MENU_ITEM_H - FONT_SIZE) / 2;
        draw_text(e, MENU_LABELS[i], mx + 14, ty2, 0xCC, 0xCC, 0xCC);

        if (MENU_HINTS[i]) {
            int hw = 0, hh = 0;
            TTF_GetStringSize(e->font, MENU_HINTS[i], 0, &hw, &hh);
            draw_text(e, MENU_HINTS[i],
                      mx + MENU_WIDTH - hw - 10, ty2,
                      0x60, 0x65, 0x70);
        }
        iy += MENU_ITEM_H;
    }
}

/* ── render_frame ────────────────────────────────────────────────────────── */
void render_frame(Editor *e) {
    SDL_Renderer *r = e->renderer;

    /* área de texto empieza DEBAJO de la navbar */
    int text_top     = NAVBAR_HEIGHT;
    int text_height  = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;
    int visible_lines = text_height / LINE_HEIGHT;
    int total_lines   = buf_line_count(&e->buf);

    /* fondo */
    set_color(r, COL_BG);
    SDL_RenderClear(r);

    /* actualizar lexer */
    {
        int in_block = 0;
        for (int li = 0; li < e->lex.count; li++) {
            if (e->lex.dirty[li]) {
                char line_buf[4096];
                get_line_text(e, li, line_buf, sizeof(line_buf));
                in_block = lexer_tokenize_line(line_buf,
                               (int)strlen(line_buf),
                               &e->lex.lines[li], in_block);
                e->lex.dirty[li] = 0;
            }
        }
    }

    /* ── líneas de texto ── */
    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi;
        if (li >= total_lines) break;

        int y      = text_top + vi * LINE_HEIGHT;
        int text_x = GUTTER_WIDTH + PADDING_LEFT;

        if (li == e->cursor_line) {
            set_color(r, COL_CURSOR_LINE);
            SDL_FRect hl = {0, (float)y, (float)e->win_w, (float)LINE_HEIGHT};
            SDL_RenderFillRect(r, &hl);
        }

        char line_buf[4096];
        int  line_len = get_line_text(e, li, line_buf, sizeof(line_buf));

        if (li < e->lex.count && e->lex.lines[li].count > 0) {
            LineTokens *lt = &e->lex.lines[li];
            int drawn_to = 0;

            for (int ti = 0; ti < lt->count; ti++) {
                Token *tok = &lt->tokens[ti];
                int col_end = tok->col + tok->len - e->scroll_col;
                if (col_end <= 0) { drawn_to = tok->col + tok->len; continue; }

                if (drawn_to < tok->col) {
                    int gap_start = drawn_to - e->scroll_col;
                    if (gap_start < 0) gap_start = 0;
                    int gap_len = tok->col - e->scroll_col - gap_start;
                    if (gap_len > 0 && gap_start + e->scroll_col < line_len) {
                        char tmp[256];
                        int cp = gap_len < 255 ? gap_len : 255;
                        memcpy(tmp, line_buf + gap_start + e->scroll_col, (size_t)cp);
                        tmp[cp] = '\0';
                        Color dc = TOKEN_COLORS[TOK_DEFAULT];
                        draw_text(e, tmp,
                                  text_x + gap_start * e->char_w,
                                  y + (LINE_HEIGHT - FONT_SIZE) / 2,
                                  dc.r, dc.g, dc.b);
                    }
                }

                int draw_col    = (tok->col > e->scroll_col) ? tok->col - e->scroll_col : 0;
                int actual_start = tok->col < e->scroll_col ? e->scroll_col : tok->col;
                int actual_len   = tok->col + tok->len - actual_start;
                if (actual_len <= 0) { drawn_to = tok->col + tok->len; continue; }
                if (actual_start + actual_len > line_len)
                    actual_len = line_len - actual_start;
                if (actual_len <= 0) { drawn_to = tok->col + tok->len; continue; }

                char tmp[256];
                int cp = actual_len < 255 ? actual_len : 255;
                memcpy(tmp, line_buf + actual_start, (size_t)cp);
                tmp[cp] = '\0';

                Color tc = TOKEN_COLORS[tok->type];
                (void)col_end;
                draw_text(e, tmp,
                          text_x + draw_col * e->char_w,
                          y + (LINE_HEIGHT - FONT_SIZE) / 2,
                          tc.r, tc.g, tc.b);
                drawn_to = tok->col + tok->len;
            }

            if (drawn_to < line_len) {
                int start = drawn_to - e->scroll_col;
                if (start < 0) start = 0;
                if (start < line_len) {
                    char tmp[256];
                    int cp = (line_len - start) < 255 ? (line_len - start) : 255;
                    memcpy(tmp, line_buf + start, (size_t)cp);
                    tmp[cp] = '\0';
                    Color dc = TOKEN_COLORS[TOK_DEFAULT];
                    draw_text(e, tmp,
                              text_x + start * e->char_w,
                              y + (LINE_HEIGHT - FONT_SIZE) / 2,
                              dc.r, dc.g, dc.b);
                }
            }
        } else {
            Color dc = TOKEN_COLORS[TOK_DEFAULT];
            int start = e->scroll_col < line_len ? e->scroll_col : line_len;
            if (start < line_len) {
                draw_text(e, line_buf + start, text_x,
                          y + (LINE_HEIGHT - FONT_SIZE) / 2,
                          dc.r, dc.g, dc.b);
            }
        }
    }

    /* ── gutter ── */
    set_color(r, COL_GUTTER);
    SDL_FRect gutter = {0, (float)text_top, (float)GUTTER_WIDTH, (float)text_height};
    SDL_RenderFillRect(r, &gutter);

    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi;
        if (li >= total_lines) break;
        char num[16];
        snprintf(num, sizeof(num), "%4d", li + 1);
        int y = text_top + vi * LINE_HEIGHT;
        draw_text(e, num, 4, y + (LINE_HEIGHT - FONT_SIZE) / 2,
                  0x49, 0x50, 0x5E);
    }

    /* ── cursor ── */
    {
        int vis_line = e->cursor_line - e->scroll_line;
        int vis_col  = e->cursor_col  - e->scroll_col;
        if (vis_line >= 0 && vis_line < visible_lines && vis_col >= 0) {
            int cx = GUTTER_WIDTH + PADDING_LEFT + vis_col * e->char_w;
            int cy = text_top + vis_line * LINE_HEIGHT;
            set_color(r, COL_CURSOR);
            SDL_FRect cur = {(float)cx, (float)cy, 2.0f, (float)LINE_HEIGHT};
            SDL_RenderFillRect(r, &cur);
        }
    }

    /* ── barra de estado ── */
    {
        int sy = e->win_h - STATUS_HEIGHT;
        set_color(r, COL_STATUS_BG);
        SDL_FRect sb = {0, (float)sy, (float)e->win_w, (float)STATUS_HEIGHT};
        SDL_RenderFillRect(r, &sb);

        char status[256];
        snprintf(status, sizeof(status), "  %s%s  |  Ln %d, Col %d  |  SDL3 IDE",
                 e->filepath[0] ? e->filepath : "sin título",
                 e->modified ? " *" : "",
                 e->cursor_line + 1, e->cursor_col + 1);
        draw_text(e, status, 0, sy + (STATUS_HEIGHT - FONT_SIZE) / 2,
                  0x98, 0xC3, 0x79);
    }

    /* ── navbar (encima de todo) ── */
    render_navbar(e);

    /* ── menú desplegable (última capa) ── */
    render_menu(e);

    SDL_RenderPresent(r);
}
