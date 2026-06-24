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
#include <string.h>

/* -- Geometría (px) -------------------------------------------------------- */
#define PREF_HEADER_H 44 /* alto de la cabecera                       */
#define PREF_ROW_H 40    /* alto de cada fila de ajuste              */
#define PREF_COL_MAX 620 /* ancho máximo de la columna de contenido  */
#define PREF_SIDE_PAD 40 /* margen lateral mínimo                    */
#define PREF_CTRL_W 150  /* ancho de la zona de control (derecha)    */
#define PREF_STEP_BTN 28 /* lado de los botones −/+ del stepper      */
#define PREF_RESET_BTN_W 200 /* ancho del botón "Restablecer valores"    */
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

/** Nombre legible del modo de fondo (::BgMode). */
static const char *bg_mode_name(int mode) {
    switch (mode) {
    case BG_MODE_IMAGE: return "Imagen";
    case BG_MODE_COLOR: return "Color";
    case BG_MODE_TRANSPARENT: return "Transparente";
    default:            return "Ninguno";
    }
}

/** Nombre legible del modo de escalado de la imagen (::BgScale). */
static const char *bg_scale_name(int scaling) {
    switch (scaling) {
    case BG_SCALE_FIT:     return "Ajustar";
    case BG_SCALE_FILL:    return "Rellenar";
    case BG_SCALE_STRETCH: return "Estirar";
    case BG_SCALE_CENTER:  return "Centrar";
    case BG_SCALE_TILE:    return "Mosaico";
    default:               return "Estirar";
    }
}

/* -- Lista de fuentes con previsualización --------------------------------- */

/* Abrir una TTF por fila y frame sería costoso: cacheamos las últimas abiertas
 * (por ruta + tamaño). Al llenarse, se vacía entera cerrando las fuentes. */
#define PREVIEW_CACHE 32
static struct {
    char path[512];
    TTF_Font *font;
    int size;
} g_prev[PREVIEW_CACHE];
static int g_prev_n;

/** Devuelve (cacheada) la fuente de @p path al tamaño @p size, o NULL. */
static TTF_Font *preview_font(const char *path, int size) {
    for (int i = 0; i < g_prev_n; i++)
        if (g_prev[i].size == size && strcmp(g_prev[i].path, path) == 0)
            return g_prev[i].font;
    if (g_prev_n == PREVIEW_CACHE) { /* cache llena: vaciarla entera */
        for (int i = 0; i < g_prev_n; i++)
            if (g_prev[i].font) TTF_CloseFont(g_prev[i].font);
        g_prev_n = 0;
    }
    TTF_Font *f = TTF_OpenFont(path, size);
    if (!f) return NULL;
    snprintf(g_prev[g_prev_n].path, sizeof g_prev[g_prev_n].path, "%s", path);
    g_prev[g_prev_n].font = f;
    g_prev[g_prev_n].size = size;
    g_prev_n++;
    return f;
}

/**
 * @brief Callback de ::ui_list para una fila de la lista de fuentes.
 *
 * Dibuja el nombre de la fuente CON su propia fuente (preview), para que se vea
 * su aspecto. La fila 0 es "Predeterminada" (se dibuja con la fuente del
 * editor).
 */
static void font_row(Editor *e, int i, Rect row, int selected, void *ud) {
    (void)ud;
    (void)selected;
    Color c = e->theme.tokens[TOK_DEFAULT];
    int tx = row.x + 8;
    int ty = row.y + (row.h - e->font_size) / 2;
    if (i == 0) {
        draw_text_c(e, "Predeterminada", tx, ty, c);
        return;
    }
    const FontEntry *fe = &e->fonts.items[i - 1];
    TTF_Font *pf = preview_font(fe->path, e->font_size);
    if (pf)
        draw_text_font(e, pf, fe->name, tx, ty, c);
    else
        draw_text_c(e, fe->name, tx, ty, c); /* fallback: fuente del editor */
}

void render_settings_view(Editor *e) {
    SDL_Renderer *r = e->renderer;

    set_color_c(r, e->theme.col_bg);
    fill_rect(r, 0, 0, e->win_w, e->win_h);

    /* -- Cabecera: botón Volver + título + botón Restablecer -- */
    set_color_c(r, e->theme.col_navbar_bg);
    fill_rect(r, 0, 0, e->win_w, PREF_HEADER_H);
    Rect back = {12, 8, 110, PREF_HEADER_H - 16};
    ui_button(e, UI_PREF_BACK, back, "< Volver", &e->theme.style_button,
              UI_NORMAL);
    draw_text_c(e, "Preferencias", 140, row_text_y(e, 0, PREF_HEADER_H),
                e->theme.tokens[TOK_DEFAULT]);

    /* botón de reseteo, anclado a la derecha de la cabecera */
    Rect reset = {e->win_w - 12 - PREF_RESET_BTN_W, 8, PREF_RESET_BTN_W,
                  PREF_HEADER_H - 16};
    ui_button(e, UI_PREF_RESET, reset, "Restablecer valores",
              &e->theme.style_button, UI_NORMAL);

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
    /* Una sola fila "Fondo" que muestra el modo actual y abre la sub-pantalla
     * "Fondos" con todos los controles (asi no hay un boton de imagen suelto
     * que se pueda pulsar con el fondo desactivado). */
    pref_choice(e, UI_PREF_BG, x, y, col_w, "Fondo",
                bg_mode_name(e->settings.background_mode));
    y += PREF_ROW_H;
    pref_stepper(e, UI_PREF_FONTSZ_DEC, UI_PREF_FONTSZ_INC, x, y, col_w,
                 "Tamano de fuente", e->settings.font_size);
    y += PREF_ROW_H + 12;

    /* -- Sección Editor -- */
    y = pref_section(e, x, y, "Editor");
    pref_toggle(e, UI_PREF_AUTOSAVE, x, y, col_w, "Autoguardado", e->autosave);
    y += PREF_ROW_H;
    pref_toggle(e, UI_PREF_LINENUM, x, y, col_w, "Numeros de linea",
                e->settings.show_line_numbers);
    y += PREF_ROW_H;
    pref_toggle(e, UI_PREF_HLLINE, x, y, col_w, "Resaltar linea actual",
                e->settings.highlight_current_line);
    y += PREF_ROW_H;
    pref_toggle(e, UI_PREF_SHORTCUTS, x, y, col_w, "Barra de atajos",
                e->settings.show_shortcuts);
    y += PREF_ROW_H;
    pref_stepper(e, UI_PREF_TABW_DEC, UI_PREF_TABW_INC, x, y, col_w,
                 "Ancho de tabulacion", e->settings.tab_width);
    y += PREF_ROW_H + 12;

    /* -- Sección Fuente: lista con scroll, cada nombre en su propia fuente --
     */
    y = pref_section(e, x, y, "Fuente (clic para elegir)");
    int sel = e->settings.font_path[0]
                  ? fonts_index_of(&e->fonts, e->settings.font_path) + 1
                  : 0; /* fila 0 = Predeterminada */
    int row_h = e->font_size + 14;
    int list_h = e->win_h - y - 24;
    if (list_h < row_h * 3) list_h = row_h * 3;
    Rect lb = {x, y, col_w, list_h};
    ui_list(e, lb, UI_PREF_FONT_LIST, UI_LIST_PREF_FONT, e->fonts.count + 1,
            row_h, &e->font_list_scroll, sel, font_row, NULL);
}

/* -- Galeria de miniaturas de fondos --------------------------------------- */

/* Geometria de la rejilla (px). */
#define BG_GAL_COLS 4       /* columnas de la cuadricula                  */
#define BG_GAL_CELL_H 72    /* alto de cada celda                         */
#define BG_GAL_GAP 8        /* separacion entre celdas                    */
#define BG_GAL_ROWS_VIS 3   /* filas visibles antes de necesitar scroll   */
#define BG_GAL_DEL 16       /* lado del boton "x" de quitar en cada celda  */

/**
 * @brief Dibuja una miniatura (cover) dentro de @p cell, recortada a la celda.
 *
 * Escala la imagen para CUBRIR la celda conservando proporcion y recorta el
 * sobrante con un clip rectangular (igual criterio que BG_SCALE_FILL).
 */
static void bg_thumb_draw(SDL_Renderer *r, SDL_Texture *tex, int tw, int th,
                          Rect cell) {
    if (!tex || tw <= 0 || th <= 0) return;
    float AW = (float)cell.w, AH = (float)cell.h;
    float fw = (float)tw, fh = (float)th;
    float sc = AW / fw;
    if (fh * sc < AH) sc = AH / fh; /* el lado mayor manda (cover) */
    float w = fw * sc, h = fh * sc;
    SDL_FRect dst = {cell.x + (AW - w) / 2.0f, cell.y + (AH - h) / 2.0f, w, h};
    SDL_Rect clip = {cell.x, cell.y, cell.w, cell.h};
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    SDL_SetRenderClipRect(r, &clip);
    SDL_RenderTexture(r, tex, NULL, &dst);
    SDL_SetRenderClipRect(r, NULL);
}

/**
 * @brief Dibuja la galeria de fondos (rejilla de miniaturas + "Anyadir").
 *
 * Construye la cache de miniaturas si hace falta, pinta el boton de anyadir,
 * y una cuadricula con scroll donde cada celda muestra una imagen de
 * @c settings.background_gallery (recortada estilo cover).  Resalta la celda de
 * la imagen ACTIVA, dibuja un placeholder para las que no cargaron y registra
 * en e->ui cada celda (UI_LIST_BG_THUMB) y su boton de quitar
 * (UI_LIST_BG_THUMB_DEL), mas el area de la rejilla (UI_BG_GALLERY) para la
 * rueda del raton.
 *
 * @return La Y (px) justo debajo de la galeria, para seguir colocando filas.
 */
static int render_bg_gallery(Editor *e, int x, int y, int col_w) {
    SDL_Renderer *r = e->renderer;
    Settings *s = &e->settings;

    editor_bg_thumbs_build(e); /* perezoso: solo trabaja si esta invalidada */

    /* Boton "Anyadir imagen...", a la izquierda de la fila. */
    Rect add = {x, y, 180, PREF_ROW_H - 8};
    ui_button(e, UI_BG_ADD, add, "Anyadir imagen...", &e->theme.style_button,
              UI_NORMAL);
    /* contador de imagenes, a la derecha */
    {
        char info[48];
        snprintf(info, sizeof info, "%d imagen(es)", s->background_gallery_count);
        draw_text_c(e, info, x + col_w - 120, row_text_y(e, y, PREF_ROW_H - 8),
                    e->theme.col_ftree_file);
    }
    y += PREF_ROW_H;

    int n = s->background_gallery_count;
    int cell_w = (col_w - (BG_GAL_COLS - 1) * BG_GAL_GAP) / BG_GAL_COLS;
    if (cell_w < 1) cell_w = 1;
    int rows = (n + BG_GAL_COLS - 1) / BG_GAL_COLS;
    if (rows < 1) rows = 1; /* reservar siempre algo de alto, aunque vacia */
    int vis_rows = rows < BG_GAL_ROWS_VIS ? rows : BG_GAL_ROWS_VIS;
    int grid_h = vis_rows * BG_GAL_CELL_H + (vis_rows - 1) * BG_GAL_GAP;

    Rect grid = {x, y, col_w, grid_h};
    /* registrar el area para la rueda del raton (scroll de la galeria) */
    ui_put(&e->ui, UI_BG_GALLERY, grid);
    /* fondo del area de la rejilla */
    set_color_c(r, e->theme.col_sb_track);
    fill_rect(r, grid.x, grid.y, grid.w, grid.h);

    /* acotar el scroll al rango valido (en filas) */
    int max_scroll = rows - vis_rows;
    if (max_scroll < 0) max_scroll = 0;
    if (e->bg_gallery_scroll < 0) e->bg_gallery_scroll = 0;
    if (e->bg_gallery_scroll > max_scroll) e->bg_gallery_scroll = max_scroll;

    if (n == 0) {
        /* galeria vacia: mensaje guia centrado en el area */
        const char *msg = "Sin imagenes. Pulsa \"Anyadir imagen...\".";
        int mw = 0, mh = 0;
        TTF_GetStringSize(e->font, msg, 0, &mw, &mh);
        draw_text_c(e, msg, grid.x + (grid.w - mw) / 2,
                    grid.y + (grid.h - e->font_size) / 2,
                    e->theme.col_ftree_file);
        return y + grid_h + 12;
    }

    /* recortar el dibujo de las celdas al area de la rejilla */
    SDL_Rect clip = {grid.x, grid.y, grid.w, grid.h};
    SDL_SetRenderClipRect(r, &clip);

    int active = settings_gallery_index_of(s, s->background_path);
    int first = e->bg_gallery_scroll * BG_GAL_COLS;
    for (int i = first; i < n; i++) {
        int rel = i - first;
        int rrow = rel / BG_GAL_COLS;
        int ccol = rel % BG_GAL_COLS;
        if (rrow >= vis_rows) break; /* fuera del area visible */
        Rect cell = {grid.x + ccol * (cell_w + BG_GAL_GAP),
                     grid.y + rrow * (BG_GAL_CELL_H + BG_GAL_GAP), cell_w,
                     BG_GAL_CELL_H};
        /* registrar la celda y su boton de quitar para el hit-test del input */
        ui_put_idx(&e->ui, UI_LIST_BG_THUMB, i, cell);

        /* miniatura o placeholder */
        if (i < e->bg_thumb_count && e->bg_thumb[i]) {
            bg_thumb_draw(r, e->bg_thumb[i], e->bg_thumb_w[i], e->bg_thumb_h[i],
                          cell);
        } else {
            /* placeholder: recuadro con una marca (imagen no disponible) */
            set_color_c(r, e->theme.col_bg);
            fill_rect(r, cell.x, cell.y, cell.w, cell.h);
            const char *ph = "?";
            int pw = 0, phh = 0;
            TTF_GetStringSize(e->font, ph, 0, &pw, &phh);
            draw_text_c(e, ph, cell.x + (cell.w - pw) / 2,
                        cell.y + (cell.h - e->font_size) / 2,
                        e->theme.col_status_sep);
        }

        /* borde: acento grueso si es la imagen activa, fino si no */
        if (i == active) {
            set_color_c(r, e->theme.style_primary.bg);
            stroke_rect(r, cell.x, cell.y, cell.w, cell.h);
            stroke_rect(r, cell.x + 1, cell.y + 1, cell.w - 2, cell.h - 2);
        } else {
            set_color_c(r, e->theme.col_status_sep);
            stroke_rect(r, cell.x, cell.y, cell.w, cell.h);
        }

        /* boton "x" para quitar, en la esquina superior derecha de la celda */
        Rect del = {cell.x + cell.w - BG_GAL_DEL - 2, cell.y + 2, BG_GAL_DEL,
                    BG_GAL_DEL};
        ui_put_idx(&e->ui, UI_LIST_BG_THUMB_DEL, i, del);
        set_color_c(r, e->theme.col_navbar_bg);
        fill_rect(r, del.x, del.y, del.w, del.h);
        set_color_c(r, e->theme.col_status_sep);
        stroke_rect(r, del.x, del.y, del.w, del.h);
        {
            int cw = 0, chh = 0;
            TTF_GetStringSize(e->font, "x", 0, &cw, &chh);
            draw_text_c(e, "x", del.x + (del.w - cw) / 2,
                        del.y + (del.h - e->font_size) / 2 + 1,
                        e->theme.tokens[TOK_DEFAULT]);
        }
    }
    SDL_SetRenderClipRect(r, NULL);

    return y + grid_h + 12;
}

/* -- Sub-pantalla "Fondos" ------------------------------------------------- */

void render_background_view(Editor *e) {
    SDL_Renderer *r = e->renderer;
    Settings *s = &e->settings;

    set_color_c(r, e->theme.col_bg);
    fill_rect(r, 0, 0, e->win_w, e->win_h);

    /* -- Cabecera: boton Volver + titulo -- */
    set_color_c(r, e->theme.col_navbar_bg);
    fill_rect(r, 0, 0, e->win_w, PREF_HEADER_H);
    Rect back = {12, 8, 110, PREF_HEADER_H - 16};
    ui_button(e, UI_BG_BACK, back, "< Volver", &e->theme.style_button,
              UI_NORMAL);
    draw_text_c(e, "Fondo del editor", 140, row_text_y(e, 0, PREF_HEADER_H),
                e->theme.tokens[TOK_DEFAULT]);

    /* -- Columna de contenido centrada -- */
    int col_w = e->win_w - 2 * PREF_SIDE_PAD;
    if (col_w > PREF_COL_MAX) col_w = PREF_COL_MAX;
    int x = (e->win_w - col_w) / 2;
    int y = PREF_HEADER_H + 24;

    /* Selector de modo (Ninguno / Imagen / Color), aplica al instante. */
    y = pref_section(e, x, y, "Apariencia del fondo");
    pref_choice(e, UI_BG_MODE, x, y, col_w, "Modo",
                bg_mode_name(s->background_mode));
    y += PREF_ROW_H;

    /* Controles condicionales segun el modo activo. */
    if (s->background_mode == BG_MODE_IMAGE) {
        /* Escalado (afecta a la imagen ACTIVA y a la previsualizacion). */
        pref_choice(e, UI_BG_SCALE, x, y, col_w, "Escalado",
                    bg_scale_name(s->background_scaling));
        y += PREF_ROW_H;
        /* Galeria de miniaturas + boton "Anyadir imagen...". */
        y = render_bg_gallery(e, x, y, col_w);
    } else if (s->background_mode == BG_MODE_COLOR) {
        /* Editor de color por componentes + muestra del color resultante. */
        pref_stepper(e, UI_BG_R_DEC, UI_BG_R_INC, x, y, col_w, "Rojo",
                     (int)((s->background_color >> 16) & 0xFF));
        y += PREF_ROW_H;
        pref_stepper(e, UI_BG_G_DEC, UI_BG_G_INC, x, y, col_w, "Verde",
                     (int)((s->background_color >> 8) & 0xFF));
        y += PREF_ROW_H;
        pref_stepper(e, UI_BG_B_DEC, UI_BG_B_INC, x, y, col_w, "Azul",
                     (int)(s->background_color & 0xFF));
        y += PREF_ROW_H;
        /* swatch del color (con su valor RGB) */
        {
            Color sw = {(Uint8)((s->background_color >> 16) & 0xFF),
                        (Uint8)((s->background_color >> 8) & 0xFF),
                        (Uint8)(s->background_color & 0xFF), 255};
            draw_text_c(e, "Color", x, row_text_y(e, y, PREF_ROW_H),
                        e->theme.tokens[TOK_DEFAULT]);
            Rect box = {x + col_w - PREF_CTRL_W, y + 6, PREF_CTRL_W,
                        PREF_ROW_H - 12};
            set_color_c(r, sw);
            fill_rect(r, box.x, box.y, box.w, box.h);
            stroke_rect(r, box.x, box.y, box.w, box.h);
            y += PREF_ROW_H;
        }
    }

    /* Opacidad: stepper + barra de progreso (todos los modos que dibujan). */
    if (s->background_mode != BG_MODE_NONE) {
        pref_stepper(e, UI_BG_OPACITY_DEC, UI_BG_OPACITY_INC, x, y, col_w,
                     "Opacidad", s->background_opacity);
        y += PREF_ROW_H;
        /* barra visual del nivel (0..255) bajo la fila del stepper */
        {
            int bar_w = col_w;
            int bar_h = 8;
            int bx = x, by = y;
            set_color_c(r, e->theme.col_sb_track);
            fill_rect(r, bx, by, bar_w, bar_h);
            int fill = (int)((long)bar_w * s->background_opacity / 255);
            if (fill < 0) fill = 0;
            if (fill > bar_w) fill = bar_w;
            set_color_c(r, e->theme.style_primary.bg);
            fill_rect(r, bx, by, fill, bar_h);
            y += bar_h + 16;
        }
    }

    /* -- Previsualizacion -- */
    y = pref_section(e, x, y, "Previsualizacion");
    int prev_h = e->win_h - y - 24;
    if (prev_h > 200) prev_h = 200;
    if (prev_h < 60) prev_h = 60;
    Rect pb = {x, y, col_w, prev_h};
    SDL_FRect parea = {(float)pb.x, (float)pb.y, (float)pb.w, (float)pb.h};
    if (s->background_mode == BG_MODE_TRANSPARENT) {
        /* En modo Transparente no se puede mostrar el escritorio real dentro de
         * la muestra; se dibuja un tablero de ajedrez (gris claro/oscuro) que lo
         * representa y encima el color del tema con el alfa = opacidad. */
        int cell = 14; /* lado de cada casilla del tablero */
        SDL_SetRenderClipRect(r, &(SDL_Rect){pb.x, pb.y, pb.w, pb.h});
        for (int ty = 0; ty * cell < pb.h; ty++) {
            for (int tx = 0; tx * cell < pb.w; tx++) {
                int dark = ((tx + ty) & 1);
                set_color(r, dark ? 0x60 : 0x90, dark ? 0x60 : 0x90,
                          dark ? 0x60 : 0x90, 0xFF);
                fill_rect(r, pb.x + tx * cell, pb.y + ty * cell, cell, cell);
            }
        }
        SDL_SetRenderClipRect(r, NULL);
        int op = s->background_opacity;
        if (op < 0) op = 0;
        if (op > 255) op = 255;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        set_color(r, e->theme.col_bg.r, e->theme.col_bg.g, e->theme.col_bg.b,
                  (Uint8)op);
        fill_rect(r, pb.x, pb.y, pb.w, pb.h);
    } else {
        /* fondo base de la muestra = color del tema, para imitar el editor */
        set_color_c(r, e->theme.col_bg);
        fill_rect(r, pb.x, pb.y, pb.w, pb.h);
        render_background_preview(e, parea);
    }
    /* marco de la muestra */
    set_color_c(r, e->theme.col_status_sep);
    stroke_rect(r, pb.x, pb.y, pb.w, pb.h);
}
