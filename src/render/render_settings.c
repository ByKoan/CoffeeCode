/**
 * @file render_settings.c
 * @brief Pantalla de preferencias a pantalla completa (se dibuja cuando
 *        @c e->settings_open). Construida con la capa de componentes (ui.h).
 *
 * Sustituye al editor mientras está abierta: cabecera con "Volver" + título y,
 * debajo, secciones con filas "etiqueta + control" (toggles y steppers). Cada
 * control registra su rect en e->ui; input los resuelve con ui_hit y aplica +
 * guarda el cambio.
 */
#include "render_internal.h"
#include "ui.h"
#include <stdio.h>

/* -- Geometría (px) -------------------------------------------------------- */
#define PREF_HEADER_H 44 /* alto de la cabecera                       */
#define PREF_ROW_H 40    /* alto de cada fila de ajuste              */
#define PREF_COL_MAX 620 /* ancho máximo de la columna de contenido  */
#define PREF_SIDE_PAD 40 /* margen lateral mínimo                    */
#define PREF_CTRL_W 150  /* ancho de la zona de control (derecha)    */
#define PREF_STEP_BTN 28 /* lado de los botones −/+ del stepper      */
/* Los colores se leen del tema (e->theme.*): fondo=col_bg,
   cabecera=col_navbar_bg, sección=col_ftree_root, etiqueta=tokens[TOK_DEFAULT],
   valor=col_ftree_file. */

/** Y para centrar verticalmente texto de @c FONT_SIZE en una fila de alto @p h.
 */
static int row_text_y(Editor *e, int row_y, int h) {
    return row_y + (h - e->font_size) / 2;
}

/**
 * @brief Dibuja una fila "etiqueta + toggle [Activado/Desactivado]".
 *
 * @param id   Id de hit-test del botón toggle.
 * @param on   Estado actual (1 = activado).
 */
static void pref_toggle(Editor *e, UiId id, int x, int y, int w,
                        const char *label, int on) {
    draw_text_c(e, label, x, row_text_y(e, y, PREF_ROW_H),
                e->theme.tokens[TOK_DEFAULT]);
    Rect box = {x + w - PREF_CTRL_W, y + 6, PREF_CTRL_W, PREF_ROW_H - 12};
    ui_button(e, id, box, on ? "Activado" : "Desactivado",
              on ? &e->theme.style_primary : &e->theme.style_button, UI_NORMAL);
}

/**
 * @brief Dibuja una fila "etiqueta + stepper [ − valor + ]".
 *
 * @param dec/inc Ids de hit-test de los botones − y +.
 * @param value   Valor numérico a mostrar entre ambos.
 */
static void pref_stepper(Editor *e, UiId dec, UiId inc, int x, int y, int w,
                         const char *label, int value) {
    draw_text_c(e, label, x, row_text_y(e, y, PREF_ROW_H),
                e->theme.tokens[TOK_DEFAULT]);

    int by = y + (PREF_ROW_H - PREF_STEP_BTN) / 2;
    int right = x + w;
    Rect inc_box = {right - PREF_STEP_BTN, by, PREF_STEP_BTN, PREF_STEP_BTN};
    Rect dec_box = {right - PREF_CTRL_W, by, PREF_STEP_BTN, PREF_STEP_BTN};
    ui_button(e, dec, dec_box, "-", &e->theme.style_button, UI_NORMAL);
    ui_button(e, inc, inc_box, "+", &e->theme.style_button, UI_NORMAL);

    /* valor centrado en el hueco entre los dos botones */
    char val[16];
    snprintf(val, sizeof val, "%d", value);
    int vw = 0, vh = 0;
    TTF_GetStringSize(e->font, val, 0, &vw, &vh);
    int gap_x = dec_box.x + PREF_STEP_BTN;
    int gap_w = inc_box.x - gap_x;
    draw_text_c(e, val, gap_x + (gap_w - vw) / 2, row_text_y(e, y, PREF_ROW_H),
                e->theme.col_ftree_file);
}

/** Dibuja un título de sección y devuelve la Y de la primera fila. */
static int pref_section(Editor *e, int x, int y, const char *title) {
    draw_text_c(e, title, x, y, e->theme.col_ftree_root);
    return y + 28;
}

/**
 * @brief Dibuja una fila "etiqueta + selector [valor]".
 *
 * El botón muestra el valor actual; al pulsarlo, input pasa al siguiente.
 *
 * @param id    Id de hit-test del selector.
 * @param value Texto del valor actual.
 */
static void pref_choice(Editor *e, UiId id, int x, int y, int w,
                        const char *label, const char *value) {
    draw_text_c(e, label, x, row_text_y(e, y, PREF_ROW_H),
                e->theme.tokens[TOK_DEFAULT]);
    Rect box = {x + w - PREF_CTRL_W, y + 6, PREF_CTRL_W, PREF_ROW_H - 12};
    ui_button(e, id, box, value, &e->theme.style_button, UI_NORMAL);
}

void render_settings_view(Editor *e) {
    SDL_Renderer *r = e->renderer;

    set_color_c(r, e->theme.col_bg);
    fill_rect(r, 0, 0, e->win_w, e->win_h);

    /* -- Cabecera: botón Volver + título -- */
    set_color_c(r, e->theme.col_navbar_bg);
    fill_rect(r, 0, 0, e->win_w, PREF_HEADER_H);
    Rect back = {12, 8, 110, PREF_HEADER_H - 16};
    ui_button(e, UI_PREF_BACK, back, "< Volver", &e->theme.style_button,
              UI_NORMAL);
    draw_text_c(e, "Preferencias", 140, row_text_y(e, 0, PREF_HEADER_H),
                e->theme.tokens[TOK_DEFAULT]);

    /* -- Columna de contenido centrada (ancho acotado) -- */
    int col_w = e->win_w - 2 * PREF_SIDE_PAD;
    if (col_w > PREF_COL_MAX) col_w = PREF_COL_MAX;
    int x = (e->win_w - col_w) / 2;
    int y = PREF_HEADER_H + 24;

    /* -- Sección Apariencia -- */
    y = pref_section(e, x, y, "Apariencia");
    pref_choice(e, UI_PREF_THEME, x, y, col_w, "Tema",
                theme_name(e->settings.theme));
    y += PREF_ROW_H;
    pref_stepper(e, UI_PREF_FONTSZ_DEC, UI_PREF_FONTSZ_INC, x, y, col_w,
                 "Tamano de fuente", e->settings.font_size);
    y += PREF_ROW_H + 12;

    /* -- Sección Editor -- */
    y = pref_section(e, x, y, "Editor");
    pref_toggle(e, UI_PREF_AUTOSAVE, x, y, col_w, "Autoguardado", e->autosave);
    y += PREF_ROW_H;
    pref_stepper(e, UI_PREF_TABW_DEC, UI_PREF_TABW_INC, x, y, col_w,
                 "Ancho de tabulacion", e->settings.tab_width);
    y += PREF_ROW_H;
}
