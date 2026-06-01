#include "render.h"
#include "filetree.h"
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
#define MENU_WIDTH     180
#define MENU_ITEMS      5   /* Nuevo, Abrir archivo, Abrir carpeta, sep, Guardar */

static const char *MENU_LABELS[MENU_ITEMS] = {
    "Nuevo",
    "Abrir archivo...",
    "Abrir carpeta...",
    NULL,           /* separador */
    "Guardar"
};
/* shortcut mostrado a la derecha */
static const char *MENU_HINTS[MENU_ITEMS] = {
    "Ctrl+N", "Ctrl+O", "Ctrl+K", NULL, "Ctrl+S"
};

/* Colores panel lateral */
#define COL_FTREE_BG    0x1E, 0x22, 0x2A, 0xFF
#define COL_FTREE_HOVER 0x2C, 0x31, 0x3C, 0xFF
#define COL_FTREE_SEP   0x3A, 0x3F, 0x4A, 0xFF
#define COL_FTREE_DIR   0xE5, 0xC0, 0x7B, 0xFF
#define COL_FTREE_FILE  0xAB, 0xB2, 0xBF, 0xFF
#define COL_FTREE_ROOT  0x61, 0xAF, 0xEF, 0xFF

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


/* ── Render del panel lateral (explorador de carpetas) ──────────────────── */
static void render_filetree(Editor *e) {
    FileTree *ft = &e->ftree;
    if (!ft->open) return;
    SDL_Renderer *r = e->renderer;

    int panel_x = 0;
    int panel_y = NAVBAR_HEIGHT;
    int panel_w = ft->width;
    int panel_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;

    /* Fondo del panel */
    set_color(r, COL_FTREE_BG);
    SDL_FRect bg = {(float)panel_x, (float)panel_y,
                    (float)panel_w, (float)panel_h};
    SDL_RenderFillRect(r, &bg);

    /* Borde derecho separador */
    set_color(r, COL_FTREE_SEP);
    SDL_FRect sep = {(float)(panel_x + panel_w - 1), (float)panel_y,
                     1.0f, (float)panel_h};
    SDL_RenderFillRect(r, &sep);

    /* Botón toggle (flecha « en el borde derecho) */
    int btn_w = FTREE_TOGGLE_BTN_W;
    int btn_h = 40;
    int btn_y = panel_y + (panel_h - btn_h) / 2;
    int btn_x = panel_x + panel_w - btn_w;
    set_color(r, 0x2C, 0x31, 0x3C, 0xFF);
    SDL_FRect tbtn = {(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h};
    SDL_RenderFillRect(r, &tbtn);
    /* Icono « */
    draw_text(e, "<", btn_x + 2, btn_y + (btn_h - FONT_SIZE) / 2,
              0x61, 0xAF, 0xEF);

    /* Cabecera con nombre de la carpeta raíz */
    int header_h = 26;
    set_color(r, 0x17, 0x1A, 0x21, 0xFF);
    SDL_FRect hdr = {(float)panel_x, (float)panel_y,
                     (float)(panel_w - btn_w), (float)header_h};
    SDL_RenderFillRect(r, &hdr);

    /* Nombre raíz (truncado) */
    char root_label[64];
    const char *rname = ft->root_path;
    const char *s = rname + strlen(rname);
    while (s > rname && *(s-1) != '/' && *(s-1) != '\\') s--;
    snprintf(root_label, sizeof(root_label), " %s", *s ? s : rname);
    draw_text(e, root_label, panel_x + 4,
              panel_y + (header_h - FONT_SIZE) / 2,
              0x61, 0xAF, 0xEF);

    /* Entradas visibles */
    int visible_rows = (panel_h - header_h) / FTREE_ITEM_H;
    int vis_count    = ftree_visible_count(ft);
    int max_scroll   = vis_count - visible_rows;
    if (ft->scroll > max_scroll) ft->scroll = max_scroll;
    if (ft->scroll < 0)         ft->scroll = 0;

    int drawn = 0;
    for (int i = 0; i < ft->count && drawn < visible_rows + ft->scroll; i++) {
        FEntry *en = &ft->entries[i];
        if (!en->visible) continue;
        int vis_idx = drawn++;  /* índice visible */
        if (vis_idx < ft->scroll) continue;  /* encima del scroll */
        int row = vis_idx - ft->scroll;

        int ey = panel_y + header_h + row * FTREE_ITEM_H;
        int ex = panel_x + 4 + en->depth * FTREE_INDENT;

        /* highlight hover */
        if (ft->hovered == i) {
            set_color(r, COL_FTREE_HOVER);
            SDL_FRect hi = {(float)panel_x, (float)ey,
                            (float)(panel_w - btn_w), (float)FTREE_ITEM_H};
            SDL_RenderFillRect(r, &hi);
        }

        /* Icono triangular para directorios */
        if (en->type == FTYPE_DIR) {
            const char *icon = en->expanded ? "v " : "> ";
            draw_text(e, icon, ex, ey + (FTREE_ITEM_H - FONT_SIZE) / 2,
                      0xE5, 0xC0, 0x7B);
            ex += FTREE_ICON_W;
        } else {
            ex += FTREE_ICON_W;
        }

        /* Nombre */
        /* Truncar si el nombre es muy largo */
        char label[128];
        int max_chars = (panel_w - btn_w - ex - 4) / (e->char_w > 0 ? e->char_w : 8);
        if (max_chars < 3) max_chars = 3;
        if ((int)strlen(en->name) > max_chars) {
            strncpy(label, en->name, (size_t)(max_chars - 2));
            label[max_chars - 2] = '.';
            label[max_chars - 1] = '.';
            label[max_chars]     = '\0';
        } else {
            strncpy(label, en->name, sizeof(label) - 1);
            label[sizeof(label) - 1] = '\0';
        }

        if (en->type == FTYPE_DIR) {
            draw_text(e, label, ex, ey + (FTREE_ITEM_H - FONT_SIZE) / 2,
                      0xE5, 0xC0, 0x7B);
        } else {
            draw_text(e, label, ex, ey + (FTREE_ITEM_H - FONT_SIZE) / 2,
                      0xAB, 0xB2, 0xBF);
        }
    }
}

/* ── Render del botón toggle cuando el panel está cerrado ────────────────── */
static void render_filetree_toggle_closed(Editor *e) {
    if (e->ftree.open) return;
    SDL_Renderer *r = e->renderer;
    int btn_w = FTREE_TOGGLE_BTN_W;
    int btn_h = 40;
    int panel_y = NAVBAR_HEIGHT;
    int panel_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;
    int btn_y   = panel_y + (panel_h - btn_h) / 2;

    set_color(r, 0x2C, 0x31, 0x3C, 0xFF);
    SDL_FRect tbtn = {0.0f, (float)btn_y, (float)btn_w, (float)btn_h};
    SDL_RenderFillRect(r, &tbtn);

    /* Borde derecho */
    set_color(r, COL_FTREE_SEP);
    SDL_FRect sep = {(float)(btn_w - 1), (float)panel_y, 1.0f, (float)panel_h};
    SDL_RenderFillRect(r, &sep);

    draw_text(e, ">", 2, btn_y + (btn_h - FONT_SIZE) / 2,
              0x61, 0xAF, 0xEF);
}

/* ── render_frame ────────────────────────────────────────────────────────── */
void render_frame(Editor *e) {
    SDL_Renderer *r = e->renderer;

    /* Calcular offset izquierdo según el panel lateral */
    int left_offset;
    if (e->ftree.open)
        left_offset = e->ftree.width;
    else
        left_offset = FTREE_TOGGLE_BTN_W;

    /* área de texto empieza DEBAJO de la navbar y a la derecha del panel */
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
        int text_x = left_offset + GUTTER_WIDTH + PADDING_LEFT;

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
    SDL_FRect gutter = {(float)left_offset, (float)text_top,
                        (float)GUTTER_WIDTH, (float)text_height};
    SDL_RenderFillRect(r, &gutter);

    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi;
        if (li >= total_lines) break;
        char num[16];
        snprintf(num, sizeof(num), "%4d", li + 1);
        int y = text_top + vi * LINE_HEIGHT;
        draw_text(e, num, left_offset + 4, y + (LINE_HEIGHT - FONT_SIZE) / 2,
                  0x49, 0x50, 0x5E);
    }

    /* ── cursor ── */
    {
        int vis_line = e->cursor_line - e->scroll_line;
        int vis_col  = e->cursor_col  - e->scroll_col;
        if (vis_line >= 0 && vis_line < visible_lines && vis_col >= 0) {
            int cx = left_offset + GUTTER_WIDTH + PADDING_LEFT + vis_col * e->char_w;
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
        snprintf(status, sizeof(status), "  %s%s  |  Ln %d, Col %d  |  CoffeeCode",
                 e->filepath[0] ? e->filepath : "sin título",
                 e->modified ? " *" : "",
                 e->cursor_line + 1, e->cursor_col + 1);
        draw_text(e, status, 0, sy + (STATUS_HEIGHT - FONT_SIZE) / 2,
                  0x98, 0xC3, 0x79);
    }

    /* ── panel lateral (encima del área de texto) ── */
    if (e->ftree.open)
        render_filetree(e);
    else
        render_filetree_toggle_closed(e);

    /* ── navbar (encima de todo) ── */
    render_navbar(e);

    /* ── menú desplegable (última capa) ── */
    render_menu(e);

    SDL_RenderPresent(r);
}
