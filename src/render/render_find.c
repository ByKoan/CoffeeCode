/**
 * @file render_find.c
 * @brief Dibujado de la barra de búsqueda / reemplazo (Ctrl+F).
 */
#include "render_internal.h"
#include <stdio.h>
#include <string.h>

/* -- Geometría (px) -------------------------------------------------------- */
#define FB_PAD            10   /* margen interior de la barra              */
#define FB_GAP             8   /* separación entre etiqueta/campo/botón    */
#define FB_FIELD_H        22   /* alto de cada campo de texto              */
#define FB_ROW_GAP         8   /* separación vertical entre las dos filas  */
#define FB_MIN_FIELD_W   200   /* ancho útil mínimo de campo (para el cálculo) */
#define FB_MIN_W         400   /* ancho mínimo de la barra                 */
#define FB_RIGHT_MARGIN   12   /* separación del borde derecho de la ventana */
#define FB_TOP_MARGIN      8   /* separación bajo la barra de pestañas     */
#define FB_BTN_HPAD       20   /* padding horizontal del botón Reemplazar  */
#define FB_TEXT_PAD        4   /* margen interno del texto dentro del campo */
#define FB_NAV_GAP         4   /* separación entre flechas y contador      */
#define FB_SEL_INSET       2   /* margen vertical del resaltado de selección */
#define FB_LABEL_MARGIN    4   /* margen extra de la columna de etiquetas  */
#define FB_NORESULT_OFFSET 30  /* desplazamiento del texto "Sin resultados" */

/* -- Colores de relleno/contorno (RGBA, para set_color) -------------------- */
#define FB_COL_BG           0x1E, 0x22, 0x2A, 255
#define FB_COL_BORDER       0x3A, 0x3F, 0x4A, 255
#define FB_COL_FIELD        0x25, 0x29, 0x31, 255  /* campo sin foco         */
#define FB_COL_FIELD_FOCUS  0x2A, 0x2E, 0x38, 255  /* campo con foco         */
#define FB_COL_ACCENT       0x52, 0x8B, 0xD4, 255  /* borde con foco / acento */
#define FB_COL_SEL          0x26, 0x4F, 0x78, 200  /* resaltado de selección */
#define FB_COL_NAV_BTN      0x2A, 0x2E, 0x38, 255  /* fondo botones ↑/↓      */
#define FB_COL_REPLACE_BTN  0x2C, 0x5F, 0x8C, 255  /* fondo botón Reemplazar */

/* -- Colores de texto (RGB, para draw_text) -------------------------------- */
#define FB_TXT_LABEL    0x88, 0x8C, 0x99
#define FB_TXT_FIELD    220, 220, 220
#define FB_TXT_ARROW    180, 200, 230
#define FB_TXT_COUNTER  97, 175, 239
#define FB_TXT_BTN      210, 230, 255
#define FB_TXT_NORESULT 200, 80, 80

/** Y del texto centrado verticalmente dentro de un campo en la fila @p row_y. */
static int field_text_y(int row_y) {
    return row_y + (FB_FIELD_H - FONT_SIZE) / 2;
}

/** Dibuja la caja de un campo de texto (fondo + borde), resaltado si tiene foco. */
static void draw_field_box(Editor *e, int x, int y, int w, int focused) {
    set_color(e->renderer, focused ? 0x2A : 0x25, focused ? 0x2E : 0x29, focused ? 0x38 : 0x31, 255);
    fill_rect(e->renderer, x, y, w, FB_FIELD_H);
    if (focused)
        set_color(e->renderer, FB_COL_ACCENT);
    else
        set_color(e->renderer, FB_COL_BORDER);
    stroke_rect(e->renderer, x, y, w, FB_FIELD_H);
}

/** Resalta el tramo seleccionado [sel_start, sel_end) del texto del campo. */
static void draw_field_selection(Editor *e, int field_x, int row_y, const char *content, int len,
                                 int sel_start, int sel_end) {
    if (!(sel_start >= 0 && sel_end > sel_start && sel_end <= len)) return;

    char before[FIND_BAR_MAX], selected[FIND_BAR_MAX];
    int before_len = sel_start;
    int sel_len = sel_end - sel_start;
    memcpy(before, content, before_len);
    before[before_len] = '\0';
    memcpy(selected, content + before_len, sel_len);
    selected[sel_len] = '\0';

    int before_w = 0, sel_w = 0, dummy_h = 0;
    if (before_len > 0) TTF_GetStringSize(e->font, before, 0, &before_w, &dummy_h);
    if (sel_len > 0) TTF_GetStringSize(e->font, selected, 0, &sel_w, &dummy_h);

    set_color(e->renderer, FB_COL_SEL);
    fill_rect(e->renderer, field_x + FB_TEXT_PAD + before_w, row_y + FB_SEL_INSET, sel_w,
              FB_FIELD_H - 2 * FB_SEL_INSET);
}

/** Dibuja el contenido de un campo, con cursor parpadeante si @p show_caret. */
static void draw_field_text(Editor *e, int field_x, int row_y, const char *content, int show_caret) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s%s", content, show_caret ? "|" : "");
    draw_text(e, buf, field_x + FB_TEXT_PAD, field_text_y(row_y), FB_TXT_FIELD);
}

/** Dibuja un botón cuadrado de flecha (↑/↓) y devuelve su ancho. */
static void draw_arrow_button(Editor *e, int x, int row_y, int size, const char *glyph) {
    SDL_Renderer *r = e->renderer;
    set_color(r, FB_COL_NAV_BTN);
    fill_rect(r, x, row_y, size, FB_FIELD_H);
    set_color(r, FB_COL_BORDER);
    stroke_rect(r, x, row_y, size, FB_FIELD_H);
    int glyph_w = 0, glyph_h = 0;
    TTF_GetStringSize(e->font, glyph, 0, &glyph_w, &glyph_h);
    draw_text(e, glyph, x + (size - glyph_w) / 2, field_text_y(row_y), FB_TXT_ARROW);
}

/** Dibuja la navegación de coincidencias (↑ X/N ↓) a la derecha del campo de búsqueda
 *  y guarda la geometría de los botones para la detección de clics. */
static void draw_match_nav(Editor *e, int field_x, int field_w, int row_y) {
    int arrow_w = FB_FIELD_H; /* botones cuadrados */

    char counter[32];
    snprintf(counter, sizeof(counter), "%d/%d", e->find.match_index + 1, e->find.match_count);
    int counter_w = 0, counter_h = 0;
    TTF_GetStringSize(e->font, counter, 0, &counter_w, &counter_h);

    int area_w = arrow_w + FB_NAV_GAP + counter_w + FB_NAV_GAP + arrow_w + FB_NAV_GAP;
    int prev_x = field_x + field_w - area_w;
    int next_x = prev_x + arrow_w + FB_NAV_GAP + counter_w + FB_NAV_GAP;

    e->find.prev_btn_x = prev_x;
    e->find.prev_btn_y = row_y;
    e->find.prev_btn_w = arrow_w;
    e->find.prev_btn_h = FB_FIELD_H;
    e->find.next_btn_x = next_x;
    e->find.next_btn_y = row_y;
    e->find.next_btn_w = arrow_w;
    e->find.next_btn_h = FB_FIELD_H;

    draw_arrow_button(e, prev_x, row_y, arrow_w, "↑");
    draw_text(e, counter, prev_x + arrow_w + FB_NAV_GAP, field_text_y(row_y), FB_TXT_COUNTER);
    draw_arrow_button(e, next_x, row_y, arrow_w, "↓");
}

void render_find_bar(Editor *e) {
    if (!e->find.visible) return;
    SDL_Renderer *r = e->renderer;
    FindBar *fb = &e->find;

    /* -- Cálculo del layout (medimos etiquetas con la fuente real) -- */
    int search_label_w = 0, replace_label_w = 0, dummy_h = 0;
    TTF_GetStringSize(e->font, "Buscar:", 0, &search_label_w, &dummy_h);
    TTF_GetStringSize(e->font, "Reemplazar:", 0, &replace_label_w, &dummy_h);
    int label_col_w =
        (search_label_w > replace_label_w ? search_label_w : replace_label_w) + FB_LABEL_MARGIN;

    int replace_btn_label_w = 0;
    TTF_GetStringSize(e->font, "Reemplazar", 0, &replace_btn_label_w, &dummy_h);
    int replace_btn_w = replace_btn_label_w + FB_BTN_HPAD;

    int bar_w = FB_PAD + label_col_w + FB_GAP + FB_MIN_FIELD_W + FB_GAP + replace_btn_w + FB_PAD;
    if (bar_w < FB_MIN_W) bar_w = FB_MIN_W;
    int bar_h = FB_PAD + FB_FIELD_H + FB_ROW_GAP + FB_FIELD_H + FB_PAD;
    int bar_x = e->win_w - bar_w - FB_RIGHT_MARGIN;
    int bar_y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT + FB_TOP_MARGIN;

    int label_x = bar_x + FB_PAD;
    int field_x = label_x + label_col_w + FB_GAP;
    int search_field_w = bar_w - FB_PAD - label_col_w - FB_GAP - FB_PAD; /* fila 1: ocupa todo */
    int replace_field_w = search_field_w - FB_GAP - replace_btn_w;      /* fila 2: deja el botón */
    int replace_btn_x = field_x + replace_field_w + FB_GAP;

    int row1_y = bar_y + FB_PAD;
    int row2_y = row1_y + FB_FIELD_H + FB_ROW_GAP;

    int search_focused = (fb->bar_focused && fb->replace_focused == 0);
    int replace_focused = (fb->bar_focused && fb->replace_focused == 1);
    int blink = (SDL_GetTicks() / 500) % 2;

    /* -- Fondo + borde de la barra -- */
    set_color(r, FB_COL_BG);
    fill_rect(r, bar_x, bar_y, bar_w, bar_h);
    set_color(r, FB_COL_BORDER);
    stroke_rect(r, bar_x, bar_y, bar_w, bar_h);

    /* -- Fila 1: Buscar -- */
    draw_text(e, "Buscar:", label_x, field_text_y(row1_y), FB_TXT_LABEL);
    draw_field_box(e, field_x, row1_y, search_field_w, search_focused);
    draw_field_selection(e, field_x, row1_y, fb->query, fb->query_len, fb->query_sel_start,
                         fb->query_sel_end);
    draw_field_text(e, field_x, row1_y, fb->query,
                    search_focused && fb->query_sel_start < 0 && blink);

    if (fb->result_line >= 0 && fb->match_count > 0) {
        draw_match_nav(e, field_x, search_field_w, row1_y);
    } else if (fb->query_len > 0 && fb->match_count == 0) {
        draw_text(e, "Sin resultados", field_x + search_field_w / 2 - FB_NORESULT_OFFSET,
                  field_text_y(row1_y), FB_TXT_NORESULT);
    }

    /* -- Fila 2: Reemplazar -- */
    draw_text(e, "Reemplazar:", label_x, field_text_y(row2_y), FB_TXT_LABEL);
    draw_field_box(e, field_x, row2_y, replace_field_w, replace_focused);
    draw_field_selection(e, field_x, row2_y, fb->replace, fb->replace_len, fb->replace_sel_start,
                         fb->replace_sel_end);
    draw_field_text(e, field_x, row2_y, fb->replace,
                    replace_focused && fb->replace_sel_start < 0 && blink);

    /* Botón "Reemplazar" */
    set_color(r, FB_COL_REPLACE_BTN);
    fill_rect(r, replace_btn_x, row2_y, replace_btn_w, FB_FIELD_H);
    set_color(r, FB_COL_ACCENT);
    stroke_rect(r, replace_btn_x, row2_y, replace_btn_w, FB_FIELD_H);
    {
        int text_w = 0, text_h = 0;
        TTF_GetStringSize(e->font, "Reemplazar", 0, &text_w, &text_h);
        draw_text(e, "Reemplazar", replace_btn_x + (replace_btn_w - text_w) / 2,
                  field_text_y(row2_y), FB_TXT_BTN);
    }

    /* -- Geometría para la detección de clics (la usa input_mouse.c) -- */
    fb->replace_btn_x = replace_btn_x;
    fb->replace_btn_y = row2_y;
    fb->replace_btn_w = replace_btn_w;
    fb->replace_btn_h = FB_FIELD_H;
    fb->bar_x = bar_x;
    fb->bar_y = bar_y;
    fb->bar_w = bar_w;
    fb->bar_h = bar_h;
    fb->field_x = field_x;
    fb->row1_y = row1_y;
    fb->row2_y = row2_y;
    fb->field_h = FB_FIELD_H;
}
