#include "render_internal.h"
#include <stdio.h>
#include <string.h>

void set_color(SDL_Renderer *r, uint8_t R, uint8_t G, uint8_t B, uint8_t A) {
    SDL_SetRenderDrawColor(r, R, G, B, A);
}

int draw_text(Editor *e, const char *text, int x, int y, uint8_t R, uint8_t G, uint8_t B) {
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

/*
 * get_line_text — O(longitud de línea), no O(n).
 * Usa el índice de líneas del Buffer para saltar directamente al inicio
 * de la línea pedida, evitando iterar desde el principio del archivo.
 */
int get_line_text(Editor *e, int line, char *out, int max) {
    const Buffer *b = e->buf;
    if (line < 0 || line >= buf_line_count(b)) {
        out[0] = '\0';
        return 0;
    }

    size_t start = buf_line_offset(b, line);
    size_t total = buf_length(b);
    int col = 0;

    for (size_t i = start; i < total && col < max - 1; i++) {
        char c = buf_char_at(b, i);
        if (c == '\n') break;
        if (c == '\t') {
            int spaces = TAB_SIZE - (col % TAB_SIZE);
            for (int s = 0; s < spaces && col < max - 1; s++)
                out[col++] = ' ';
        } else {
            out[col++] = c;
        }
    }
    out[col] = '\0';
    return col;
}

/* -- Resaltado de selección ----------------------------------------- */
void render_selection(Editor *e, int left_offset, int text_top, int visible_lines) {
    if (!e->sel_active) return;

    size_t from, to;
    if (!editor_sel_range(e, &from, &to)) return;

    SDL_Renderer *r = e->renderer;
    int text_x = left_offset + GUTTER_WIDTH + PADDING_LEFT;
    int total_lines = buf_line_count(e->buf);

    /* O(log n): usar buf_line_col con búsqueda binaria */
    int from_line, from_col, to_line, to_col;
    buf_line_col(e->buf, from, &from_line, &from_col);
    buf_line_col(e->buf, to, &to_line, &to_col);

    set_color(r, COL_SEL_BG);

    /* Si to_col == 0 y to_line > from_line, el cursor está al inicio de
     * to_line: visualmente la selección cubre hasta el \n de (to_line-1),
     * así que pintamos hasta to_line-1 completa y no tocamos to_line. */
    int paint_to_line = to_line;
    int paint_to_col = to_col;
    if (to_col == 0 && to_line > from_line) {
        paint_to_line = to_line - 1;
        /* col_end para esa línea = longitud + 1 (incluye \n visual) */
        size_t ls = buf_line_offset(e->buf, paint_to_line);
        size_t le = buf_line_end(e->buf, ls);
        paint_to_col = (int)(le - ls) + 1;
    }

    for (int li = from_line; li <= paint_to_line && li < total_lines; li++) {
        int vi = li - e->scroll_line;
        if (vi < 0 || vi >= visible_lines) continue;

        int y = text_top + vi * LINE_HEIGHT;

        int col_start = (li == from_line) ? from_col : 0;
        int col_end;
        if (li == paint_to_line) {
            col_end = paint_to_col;
        } else {
            /* toda la línea hasta el final + 1 para incluir el \n visualmente */
            size_t ls = buf_line_offset(e->buf, li);
            size_t le = buf_line_end(e->buf, ls);
            col_end = (int)(le - ls) + 1;
        }

        int x_start = text_x + (col_start - e->scroll_col) * e->char_w;
        int x_end = text_x + (col_end - e->scroll_col) * e->char_w;
        if (x_start < text_x) x_start = text_x;
        if (x_end < x_start) x_end = x_start + e->char_w;

        SDL_FRect sel_rect = {(float)x_start, (float)y, (float)(x_end - x_start),
                              (float)LINE_HEIGHT};
        SDL_RenderFillRect(r, &sel_rect);
    }
}

/* -- render_frame ---------------------------------------------------------- */

void render_frame(Editor *e) {
    SDL_Renderer *r = e->renderer;

    int left_offset;
    if (e->ftree.open)
        left_offset = e->ftree.width;
    else
        left_offset = FTREE_TOGGLE_BTN_W;

    int text_top = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    int text_height = e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT - SHORTCUT_HEIGHT;
    int visible_lines = text_height / LINE_HEIGHT;
    int total_lines = (e->tab_count > 0) ? buf_line_count(e->buf) : 0;

    /* fondo */
    set_color(r, COL_BG);
    SDL_RenderClear(r);

    /* -- Pantalla vacía cuando no hay ningún archivo abierto -- */
    if (e->tab_count == 0) {
        render_navbar(e);
        render_tabbar(e);
        if (e->ftree.open)
            render_filetree(e);
        else
            render_filetree_toggle_closed(e);
        render_menu(e);

        /* mensaje centrado — adaptado al ancho disponible */
        const char *line1 = "No hay ningún archivo abierto";
        const char *hint1 = "Ctrl+O  Abrir archivo";
        const char *hint2 = "Ctrl+N  Nuevo archivo";
        const char *hint3 = "Ctrl+K  Abrir carpeta";
        int w1 = 0, wh = 0, h = 0;
        TTF_GetStringSize(e->font, line1, 0, &w1, &h);
        TTF_GetStringSize(e->font, hint1, 0, &wh, &h);
        int left_off = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
        int area_left = left_off;
        int area_w = e->win_w - area_left;
        int area_top = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
        int area_h = e->win_h - area_top - STATUS_HEIGHT;
        int cx = area_left + area_w / 2;
        int mid_y = area_top + area_h / 2;
        /* título */
        draw_text(e, line1, cx - w1 / 2, mid_y - LINE_HEIGHT * 2, 0x6B, 0x72, 0x88);
        /* atajos en tres líneas separadas */
        draw_text(e, hint1, cx - wh / 2, mid_y, 0x45, 0x4C, 0x5E);
        draw_text(e, hint2, cx - wh / 2, mid_y + LINE_HEIGHT, 0x45, 0x4C, 0x5E);
        draw_text(e, hint3, cx - wh / 2, mid_y + LINE_HEIGHT * 2, 0x45, 0x4C, 0x5E);

        /* barra de estado mínima */
        {
            int sy = e->win_h - STATUS_HEIGHT;
            set_color(r, COL_STATUS_BG);
            SDL_FRect sb = {0, (float)sy, (float)e->win_w, (float)STATUS_HEIGHT};
            SDL_RenderFillRect(r, &sb);
            set_color(r, 0x35, 0x3A, 0x45, 0xFF);
            SDL_FRect sep_s = {0, (float)sy, (float)e->win_w, 1};
            SDL_RenderFillRect(r, &sep_s);
            draw_text(e, "  CoffeeCode", 0, sy + (STATUS_HEIGHT - FONT_SIZE) / 2, 0x98, 0xC3, 0x79);
        }

        SDL_RenderPresent(r);
        return;
    }

    /* actualizar lexer */
    {
        int in_block = 0;
        for (int li = 0; li < lexer_cache_count(e->lex); li++) {
            if (*lexer_cache_dirty_at(e->lex, li)) {
                char line_buf[4096];
                get_line_text(e, li, line_buf, sizeof(line_buf));
                in_block = e->hl->tokenize_line(e->hl, line_buf, (int)strlen(line_buf),
                                                lexer_cache_line(e->lex, li), in_block);
                *lexer_cache_dirty_at(e->lex, li) = 0;
            }
        }
    }

    /* -- Resaltado de línea activa (solo cuando no hay selección) -- */
    if (!e->sel_active) {
        int vi_cursor = e->cursor_line - e->scroll_line;
        if (vi_cursor >= 0 && vi_cursor < visible_lines) {
            int y = text_top + vi_cursor * LINE_HEIGHT;
            set_color(r, COL_CURSOR_LINE);
            SDL_FRect hl = {0, (float)y, (float)e->win_w, (float)LINE_HEIGHT};
            SDL_RenderFillRect(r, &hl);
        }
    }

    /* -- Resaltado de selección (encima del fondo, debajo del texto) -- */
    render_selection(e, left_offset, text_top, visible_lines);

    /* -- líneas de texto -- */
    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi;
        if (li >= total_lines) break;

        int y = text_top + vi * LINE_HEIGHT;
        int text_x = left_offset + GUTTER_WIDTH + PADDING_LEFT;

        char line_buf[4096];
        int line_len = get_line_text(e, li, line_buf, sizeof(line_buf));

        if (li < lexer_cache_count(e->lex) && lexer_cache_line(e->lex, li)->count > 0) {
            LineTokens *lt = lexer_cache_line(e->lex, li);
            int drawn_to = 0;

            for (int ti = 0; ti < lt->count; ti++) {
                Token *tok = &lt->tokens[ti];
                int col_end = tok->col + tok->len - e->scroll_col;
                if (col_end <= 0) {
                    drawn_to = tok->col + tok->len;
                    continue;
                }

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
                        draw_text(e, tmp, text_x + gap_start * e->char_w,
                                  y + (LINE_HEIGHT - FONT_SIZE) / 2, dc.r, dc.g, dc.b);
                    }
                }

                int draw_col = (tok->col > e->scroll_col) ? tok->col - e->scroll_col : 0;
                int actual_start = tok->col < e->scroll_col ? e->scroll_col : tok->col;
                int actual_len = tok->col + tok->len - actual_start;
                if (actual_len <= 0) {
                    drawn_to = tok->col + tok->len;
                    continue;
                }
                if (actual_start + actual_len > line_len) actual_len = line_len - actual_start;
                if (actual_len <= 0) {
                    drawn_to = tok->col + tok->len;
                    continue;
                }

                char tmp[256];
                int cp = actual_len < 255 ? actual_len : 255;
                memcpy(tmp, line_buf + actual_start, (size_t)cp);
                tmp[cp] = '\0';

                Color tc = TOKEN_COLORS[tok->type];
                (void)col_end;
                draw_text(e, tmp, text_x + draw_col * e->char_w, y + (LINE_HEIGHT - FONT_SIZE) / 2,
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
                    draw_text(e, tmp, text_x + start * e->char_w, y + (LINE_HEIGHT - FONT_SIZE) / 2,
                              dc.r, dc.g, dc.b);
                }
            }
        } else {
            Color dc = TOKEN_COLORS[TOK_DEFAULT];
            int start = e->scroll_col < line_len ? e->scroll_col : line_len;
            if (start < line_len) {
                draw_text(e, line_buf + start, text_x, y + (LINE_HEIGHT - FONT_SIZE) / 2, dc.r,
                          dc.g, dc.b);
            }
        }
    }

    /* -- gutter -- */
    set_color(r, COL_GUTTER);
    SDL_FRect gutter = {(float)left_offset, (float)text_top, (float)GUTTER_WIDTH,
                        (float)text_height};
    SDL_RenderFillRect(r, &gutter);

    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi;
        if (li >= total_lines) break;
        char num[16];
        snprintf(num, sizeof(num), "%4d", li + 1);
        int y = text_top + vi * LINE_HEIGHT;
        draw_text(e, num, left_offset + 4, y + (LINE_HEIGHT - FONT_SIZE) / 2, 0x49, 0x50, 0x5E);
    }

    /* -- cursor -- */
    {
        int vis_line = e->cursor_line - e->scroll_line;
        int vis_col = e->cursor_col - e->scroll_col;
        if (vis_line >= 0 && vis_line < visible_lines && vis_col >= 0) {
            int cx = left_offset + GUTTER_WIDTH + PADDING_LEFT + vis_col * e->char_w;
            int cy = text_top + vis_line * LINE_HEIGHT;
            set_color(r, COL_CURSOR);
            SDL_FRect cur = {(float)cx, (float)cy, 2.0f, (float)LINE_HEIGHT};
            SDL_RenderFillRect(r, &cur);
        }
    }

    /* atajos visuales desactivados */

    /* -- barra de estado -- */
    {
        int sy = e->win_h - STATUS_HEIGHT;
        set_color(r, COL_STATUS_BG);
        SDL_FRect sb = {0, (float)sy, (float)e->win_w, (float)STATUS_HEIGHT};
        SDL_RenderFillRect(r, &sb);

        /* línea separadora superior de status */
        set_color(r, 0x35, 0x3A, 0x45, 0xFF);
        SDL_FRect sep_status = {0, (float)sy, (float)e->win_w, 1};
        SDL_RenderFillRect(r, &sep_status);

        char status[128];
        snprintf(status, sizeof(status), " CoffeeCode | Ln %d, Col %d%s |", e->cursor_line + 1,
                 e->cursor_col + 1, e->modified ? "  *" : "");
        draw_text(e, status, 0, sy + (STATUS_HEIGHT - FONT_SIZE) / 2, 0x98, 0xC3, 0x79);
    }

    /* -- scrollbar vertical -- */
    render_scrollbar(e, left_offset);

    /* -- panel lateral -- */
    if (e->ftree.open)
        render_filetree(e);
    else
        render_filetree_toggle_closed(e);

    /* -- navbar -- */
    render_navbar(e);

    /* -- tab bar -- */
    render_tabbar(e);

    /* -- find bar -- */
    render_find_bar(e);

    /* -- menú -- */
    render_menu(e);

    SDL_RenderPresent(r);
}
