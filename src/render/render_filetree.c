/**
 * @file render_filetree.c
 * @brief Dibujado del panel lateral del explorador de archivos.
 */
#include "render_internal.h"
#include <stdio.h>
#include <string.h>

/* -- Geometría (px) -------------------------------------------------------- */
#define FTREE_TOGGLE_BTN_H 40 /* alto del botón de plegar/desplegar (« / ») */
#define FTREE_HEADER_H     26 /* alto de la cabecera con el nombre de carpeta */
#define FTREE_PAD           4 /* margen interior izquierdo                   */
#define FTREE_MIN_LABEL     3 /* nº mínimo de caracteres antes de truncar     */
#define FTREE_FALLBACK_CW   8 /* ancho de carácter por defecto                */

/* -- Colores (RGBA para set_color; RGB para draw_text) --------------------- */
#define COL_FTREE_TOGGLE 0x2C, 0x31, 0x3C, 0xFF /* fondo del botón toggle      */
#define COL_FTREE_HEADER 0x17, 0x1A, 0x21, 0xFF /* fondo de la cabecera        */
#define FTREE_TXT_DIR    0xE5, 0xC0, 0x7B       /* carpetas                    */
#define FTREE_TXT_FILE   0xAB, 0xB2, 0xBF       /* archivos                    */
#define FTREE_TXT_ROOT   0x61, 0xAF, 0xEF       /* raíz / títulos              */

/** Dibuja el botón de plegar/desplegar con su glifo (« o »). */
static void draw_toggle_button(Editor *e, int x, int y, const char *glyph) {
    set_color(e->renderer, COL_FTREE_TOGGLE);
    fill_rect(e->renderer, x, y, FTREE_TOGGLE_BTN_W, FTREE_TOGGLE_BTN_H);
    draw_text(e, glyph, x + 2, y + (FTREE_TOGGLE_BTN_H - FONT_SIZE) / 2, FTREE_TXT_ROOT);
}

/** Copia @p name en @p out truncándolo con "..." si excede @p max_chars caracteres. */
static void truncate_name(char *out, size_t out_sz, const char *name, int max_chars) {
    if (max_chars < FTREE_MIN_LABEL) max_chars = FTREE_MIN_LABEL;
    if ((int)strlen(name) > max_chars) {
        strncpy(out, name, (size_t)(max_chars - 2));
        out[max_chars - 2] = '.';
        out[max_chars - 1] = '.';
        out[max_chars] = '\0';
    } else {
        strncpy(out, name, out_sz - 1);
        out[out_sz - 1] = '\0';
    }
}

/** Dibuja una entrada (carpeta/archivo) del árbol en la fila @p row_y. */
static void draw_tree_entry(Editor *e, const FEntry *en, int row_y, int panel_w, int btn_w,
                            int is_hovered) {
    SDL_Renderer *r = e->renderer;
    if (is_hovered) {
        set_color(r, COL_FTREE_HOVER);
        fill_rect(r, 0, row_y, panel_w - btn_w, FTREE_ITEM_H);
    }

    int text_y = row_y + (FTREE_ITEM_H - FONT_SIZE) / 2;
    int x = FTREE_PAD + en->depth * FTREE_INDENT;
    if (en->type == FTYPE_DIR)
        draw_text(e, en->expanded ? "v " : "> ", x, text_y, FTREE_TXT_DIR);
    x += FTREE_ICON_W;

    int char_w = (e->char_w > 0 ? e->char_w : FTREE_FALLBACK_CW);
    int max_chars = (panel_w - btn_w - x - FTREE_PAD) / char_w;
    char label[128];
    truncate_name(label, sizeof(label), en->name, max_chars);

    if (en->type == FTYPE_DIR)
        draw_text(e, label, x, text_y, FTREE_TXT_DIR);
    else
        draw_text(e, label, x, text_y, FTREE_TXT_FILE);
}

/** Devuelve el último componente (nombre de carpeta) de una ruta. */
static const char *path_basename(const char *path) {
    const char *base = path + strlen(path);
    while (base > path && *(base - 1) != '/' && *(base - 1) != '\\')
        base--;
    return base;
}

void render_filetree(Editor *e) {
    FileTree *ft = &e->ftree;
    if (!ft->open) return;
    SDL_Renderer *r = e->renderer;

    int panel_x = 0;
    int panel_y = NAVBAR_HEIGHT;
    int panel_w = ft->width;
    int panel_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;

    set_color(r, COL_FTREE_BG);
    fill_rect(r, panel_x, panel_y, panel_w, panel_h);
    set_color(r, COL_FTREE_SEP);
    fill_rect(r, panel_x + panel_w - 1, panel_y, 1, panel_h);

    /* Botón toggle, centrado en el área visible bajo la barra de pestañas */
    int btn_w = FTREE_TOGGLE_BTN_W;
    int content_top = panel_y + TAB_BAR_HEIGHT;
    int content_h = panel_h - TAB_BAR_HEIGHT;
    int btn_y = content_top + (content_h - FTREE_TOGGLE_BTN_H) / 2;
    draw_toggle_button(e, panel_x + panel_w - btn_w, btn_y, "<");

    /* Cabecera con el nombre de la carpeta raíz (o "CoffeeCode" si no hay) */
    int header_y = content_top;
    set_color(r, COL_FTREE_HEADER);
    fill_rect(r, panel_x, header_y, panel_w - btn_w, FTREE_HEADER_H);

    char root_label[64];
    if (ft->root_path[0])
        snprintf(root_label, sizeof(root_label), " %s", path_basename(ft->root_path));
    else
        snprintf(root_label, sizeof(root_label), " CoffeeCode");
    draw_text(e, root_label, panel_x + FTREE_PAD, header_y + (FTREE_HEADER_H - FONT_SIZE) / 2,
              FTREE_TXT_ROOT);

    /* Ajustar el scroll al rango válido */
    int visible_rows = (content_h - FTREE_HEADER_H) / FTREE_ITEM_H;
    int max_scroll = ftree_visible_count(ft) - visible_rows;
    if (ft->scroll > max_scroll) ft->scroll = max_scroll;
    if (ft->scroll < 0) ft->scroll = 0;

    /* Pintar las filas visibles */
    int drawn = 0;
    for (int i = 0; i < ftree_count(ft) && drawn < visible_rows + ft->scroll; i++) {
        FEntry *en = ftree_entry(ft, i);
        if (!en->visible) continue;
        int vis_idx = drawn++;
        if (vis_idx < ft->scroll) continue;
        int row_y = header_y + FTREE_HEADER_H + (vis_idx - ft->scroll) * FTREE_ITEM_H;
        draw_tree_entry(e, en, row_y, panel_w, btn_w, ft->hovered == i);
    }
}

void render_filetree_toggle_closed(Editor *e) {
    if (e->ftree.open) return;
    SDL_Renderer *r = e->renderer;

    int panel_y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    int panel_h = e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT;
    int btn_y = panel_y + (panel_h - FTREE_TOGGLE_BTN_H) / 2;

    draw_toggle_button(e, 0, btn_y, ">");
    set_color(r, COL_FTREE_SEP);
    fill_rect(r, FTREE_TOGGLE_BTN_W - 1, panel_y, 1, panel_h);
}
