/**
 * @file render.c
 * @brief Orquestación del frame y utilidades de dibujo compartidas del módulo
 *        render. Las secciones concretas (navbar, pestañas, panel, etc.) están
 *        en render_ui.c / render_filetree.c / render_find.c.
 */
#include "render_internal.h"
#include <stdio.h>
#include <string.h>

/* ── Constantes ───────────────────────────────────────────────────────────── */
#define CURSOR_W       2     /* ancho del cursor (px)                       */
#define GUTTER_NUM_PAD 4     /* sangría del número de línea en el gutter     */
#define LINE_BUF_SZ    4096  /* buffer temporal por línea visible           */
#define TOKEN_CHUNK    255   /* máx. caracteres por fragmento de texto       */

#define COL_STATUS_SEP    0x35, 0x3A, 0x45, 0xFF
#define TXT_GUTTER_NUM    0x49, 0x50, 0x5E
#define TXT_STATUS        0x98, 0xC3, 0x79
#define TXT_WELCOME_TITLE 0x6B, 0x72, 0x88
#define TXT_WELCOME_HINT  0x45, 0x4C, 0x5E

/* ── Utilidades de dibujo compartidas ─────────────────────────────────────── */

void set_color(SDL_Renderer *r, uint8_t R, uint8_t G, uint8_t B, uint8_t A) {
    SDL_SetRenderDrawColor(r, R, G, B, A);
}

/* Rellena un rectángulo con el color actual del renderer. */
void fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_FRect rect = {(float)x, (float)y, (float)w, (float)h};
    SDL_RenderFillRect(r, &rect);
}

/* Dibuja el contorno (1 px) de un rectángulo con el color actual. */
void stroke_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_FRect sides[4] = {{(float)x, (float)y, (float)w, 1},
                          {(float)x, (float)(y + h - 1), (float)w, 1},
                          {(float)x, (float)y, 1, (float)h},
                          {(float)(x + w - 1), (float)y, 1, (float)h}};
    SDL_RenderFillRects(r, sides, 4);
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

/** Ancho visual de una línea (caracteres) incluyendo el '\n' final. */
static int line_visual_width(Editor *e, int line) {
    size_t ls = buf_line_offset(e->buf, line);
    size_t le = buf_line_end(e->buf, ls);
    return (int)(le - ls) + 1;
}

/* ── Resaltado de selección ───────────────────────────────────────────────── */
void render_selection(Editor *e, int left_offset, int text_top, int visible_lines) {
    if (!e->sel_active) return;

    size_t from, to;
    if (!editor_sel_range(e, &from, &to)) return;

    SDL_Renderer *r = e->renderer;
    int text_x = left_offset + GUTTER_WIDTH + PADDING_LEFT;
    int total_lines = buf_line_count(e->buf);

    int from_line, from_col, to_line, to_col;
    buf_line_col(e->buf, from, &from_line, &from_col);
    buf_line_col(e->buf, to, &to_line, &to_col);

    set_color(r, COL_SEL_BG);

    /* Si to_col == 0 y to_line > from_line, el cursor está al inicio de to_line:
     * la selección cubre hasta el '\n' de (to_line-1), así que pintamos esa línea
     * completa y no tocamos to_line. */
    int paint_to_line = to_line;
    int paint_to_col = to_col;
    if (to_col == 0 && to_line > from_line) {
        paint_to_line = to_line - 1;
        paint_to_col = line_visual_width(e, paint_to_line);
    }

    for (int li = from_line; li <= paint_to_line && li < total_lines; li++) {
        int vi = li - e->scroll_line;
        if (vi < 0 || vi >= visible_lines) continue;

        int y = text_top + vi * LINE_HEIGHT;
        int col_start = (li == from_line) ? from_col : 0;
        int col_end = (li == paint_to_line) ? paint_to_col : line_visual_width(e, li);

        int x_start = text_x + (col_start - e->scroll_col) * e->char_w;
        int x_end = text_x + (col_end - e->scroll_col) * e->char_w;
        if (x_start < text_x) x_start = text_x;
        if (x_end < x_start) x_end = x_start + e->char_w;

        fill_rect(r, x_start, y, x_end - x_start, LINE_HEIGHT);
    }
}

/* ── render_frame y sus ayudantes ─────────────────────────────────────────── */

/** Dibuja la barra de estado inferior con el texto @p text. */
static void draw_status_bar(Editor *e, const char *text) {
    SDL_Renderer *r = e->renderer;
    int y = e->win_h - STATUS_HEIGHT;
    set_color(r, COL_STATUS_BG);
    fill_rect(r, 0, y, e->win_w, STATUS_HEIGHT);
    set_color(r, COL_STATUS_SEP);
    fill_rect(r, 0, y, e->win_w, 1);
    draw_text(e, text, 0, y + (STATUS_HEIGHT - FONT_SIZE) / 2, TXT_STATUS);
}

/** Pantalla de bienvenida cuando no hay ningún archivo abierto. */
static void render_empty_screen(Editor *e) {
    render_navbar(e);
    render_tabbar(e);
    if (e->ftree.open)
        render_filetree(e);
    else
        render_filetree_toggle_closed(e);
    render_menu(e);

    const char *title = "No hay ningún archivo abierto";
    const char *hints[] = {"Ctrl+O  Abrir archivo", "Ctrl+N  Nuevo archivo",
                           "Ctrl+K  Abrir carpeta"};
    int title_w = 0, hint_w = 0, h = 0;
    TTF_GetStringSize(e->font, title, 0, &title_w, &h);
    TTF_GetStringSize(e->font, hints[0], 0, &hint_w, &h);

    int area_left = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    int area_top = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    int center_x = area_left + (e->win_w - area_left) / 2;
    int mid_y = area_top + (e->win_h - area_top - STATUS_HEIGHT) / 2;

    draw_text(e, title, center_x - title_w / 2, mid_y - LINE_HEIGHT * 2, TXT_WELCOME_TITLE);
    for (int i = 0; i < 3; i++)
        draw_text(e, hints[i], center_x - hint_w / 2, mid_y + LINE_HEIGHT * i, TXT_WELCOME_HINT);

    draw_status_bar(e, "  CoffeeCode");
    SDL_RenderPresent(e->renderer);
}

/** Re-tokeniza las líneas marcadas como "sucias" en la cache del lexer. */
static void update_lexer_cache(Editor *e) {
    int in_block = 0;
    for (int li = 0; li < lexer_cache_count(e->lex); li++) {
        if (*lexer_cache_dirty_at(e->lex, li)) {
            char line_buf[LINE_BUF_SZ];
            get_line_text(e, li, line_buf, sizeof(line_buf));
            in_block = e->hl->tokenize_line(e->hl, line_buf, (int)strlen(line_buf),
                                            lexer_cache_line(e->lex, li), in_block);
            *lexer_cache_dirty_at(e->lex, li) = 0;
        }
    }
}

/** Dibuja `line[src, src+len)` en píxel @p px con color @p c (recorta a TOKEN_CHUNK). */
static void draw_substr(Editor *e, const char *line, int src, int len, int px, int y, Color c) {
    if (len <= 0) return;
    if (len > TOKEN_CHUNK) len = TOKEN_CHUNK;
    char tmp[TOKEN_CHUNK + 1];
    memcpy(tmp, line + src, (size_t)len);
    tmp[len] = '\0';
    draw_text(e, tmp, px, y, c.r, c.g, c.b);
}

/** Dibuja una línea de texto con resaltado de sintaxis (o plano si no hay tokens). */
static void render_text_line(Editor *e, int li, int y, int text_x) {
    char line_buf[LINE_BUF_SZ];
    int line_len = get_line_text(e, li, line_buf, sizeof(line_buf));
    int text_y = y + (LINE_HEIGHT - FONT_SIZE) / 2;

    if (!(li < lexer_cache_count(e->lex) && lexer_cache_line(e->lex, li)->count > 0)) {
        /* sin tokens: dibujar el resto de la línea en color por defecto */
        int start = e->scroll_col < line_len ? e->scroll_col : line_len;
        if (start < line_len) {
            Color dc = TOKEN_COLORS[TOK_DEFAULT];
            draw_text(e, line_buf + start, text_x, text_y, dc.r, dc.g, dc.b);
        }
        return;
    }

    LineTokens *lt = lexer_cache_line(e->lex, li);
    int drawn_to = 0;
    for (int ti = 0; ti < lt->count; ti++) {
        Token *tok = &lt->tokens[ti];
        int col_end = tok->col + tok->len - e->scroll_col;
        if (col_end <= 0) {
            drawn_to = tok->col + tok->len;
            continue;
        }

        /* hueco (texto sin token) antes de este token */
        if (drawn_to < tok->col) {
            int gap_start = drawn_to - e->scroll_col;
            if (gap_start < 0) gap_start = 0;
            int gap_len = tok->col - e->scroll_col - gap_start;
            if (gap_len > 0 && gap_start + e->scroll_col < line_len)
                draw_substr(e, line_buf, gap_start + e->scroll_col, gap_len,
                            text_x + gap_start * e->char_w, text_y, TOKEN_COLORS[TOK_DEFAULT]);
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

        draw_substr(e, line_buf, actual_start, actual_len, text_x + draw_col * e->char_w, text_y,
                    TOKEN_COLORS[tok->type]);
        drawn_to = tok->col + tok->len;
    }

    /* texto restante tras el último token */
    if (drawn_to < line_len) {
        int start = drawn_to - e->scroll_col;
        if (start < 0) start = 0;
        if (start < line_len)
            draw_substr(e, line_buf, start, line_len - start, text_x + start * e->char_w, text_y,
                        TOKEN_COLORS[TOK_DEFAULT]);
    }
}

/** Dibuja las líneas de texto visibles. */
static void render_text_area(Editor *e, int left_offset, int text_top, int visible_lines,
                             int total_lines) {
    int text_x = left_offset + GUTTER_WIDTH + PADDING_LEFT;
    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi;
        if (li >= total_lines) break;
        render_text_line(e, li, text_top + vi * LINE_HEIGHT, text_x);
    }
}

/** Dibuja el gutter (margen con los números de línea). */
static void render_gutter(Editor *e, int left_offset, int text_top, int text_height,
                          int visible_lines, int total_lines) {
    set_color(e->renderer, COL_GUTTER);
    fill_rect(e->renderer, left_offset, text_top, GUTTER_WIDTH, text_height);

    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi;
        if (li >= total_lines) break;
        char num[16];
        snprintf(num, sizeof(num), "%4d", li + 1);
        int y = text_top + vi * LINE_HEIGHT;
        draw_text(e, num, left_offset + GUTTER_NUM_PAD, y + (LINE_HEIGHT - FONT_SIZE) / 2,
                  TXT_GUTTER_NUM);
    }
}

/** Dibuja el cursor de texto si está dentro del área visible. */
static void render_cursor(Editor *e, int left_offset, int text_top, int visible_lines) {
    int vis_line = e->cursor_line - e->scroll_line;
    int vis_col = e->cursor_col - e->scroll_col;
    if (vis_line < 0 || vis_line >= visible_lines || vis_col < 0) return;

    int cx = left_offset + GUTTER_WIDTH + PADDING_LEFT + vis_col * e->char_w;
    int cy = text_top + vis_line * LINE_HEIGHT;
    set_color(e->renderer, COL_CURSOR);
    fill_rect(e->renderer, cx, cy, CURSOR_W, LINE_HEIGHT);
}

void render_frame(Editor *e) {
    SDL_Renderer *r = e->renderer;
    int left_offset = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    int text_top = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    int text_height = e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT - SHORTCUT_HEIGHT;
    int visible_lines = text_height / LINE_HEIGHT;
    int total_lines = (e->tab_count > 0) ? buf_line_count(e->buf) : 0;

    set_color(r, COL_BG);
    SDL_RenderClear(r);

    if (e->tab_count == 0) {
        render_empty_screen(e);
        return;
    }

    update_lexer_cache(e);

    /* resaltado de la línea activa (solo si no hay selección) */
    if (!e->sel_active) {
        int vi_cursor = e->cursor_line - e->scroll_line;
        if (vi_cursor >= 0 && vi_cursor < visible_lines) {
            set_color(r, COL_CURSOR_LINE);
            fill_rect(r, 0, text_top + vi_cursor * LINE_HEIGHT, e->win_w, LINE_HEIGHT);
        }
    }

    render_selection(e, left_offset, text_top, visible_lines);
    render_text_area(e, left_offset, text_top, visible_lines, total_lines);
    render_gutter(e, left_offset, text_top, text_height, visible_lines, total_lines);
    render_cursor(e, left_offset, text_top, visible_lines);

    char status[128];
    snprintf(status, sizeof(status), " CoffeeCode | Ln %d, Col %d%s |", e->cursor_line + 1,
             e->cursor_col + 1, e->modified ? "  *" : "");
    draw_status_bar(e, status);

    render_scrollbar(e, left_offset);
    if (e->ftree.open)
        render_filetree(e);
    else
        render_filetree_toggle_closed(e);
    render_navbar(e);
    render_tabbar(e);
    render_find_bar(e);
    render_menu(e);

    SDL_RenderPresent(r);
}
