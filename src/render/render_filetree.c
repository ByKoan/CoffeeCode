#include "render_internal.h"
#include <stdio.h>
#include <string.h>

/* -- Panel lateral -------------------------------------------------------- */
void render_filetree(Editor *e) {
    FileTree *ft = &e->ftree;
    if (!ft->open) return;
    SDL_Renderer *r = e->renderer;

    int panel_x = 0;
    int panel_y = NAVBAR_HEIGHT;
    int panel_w = ft->width;
    int panel_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;

    set_color(r, COL_FTREE_BG);
    SDL_FRect bg = {(float)panel_x, (float)panel_y, (float)panel_w, (float)panel_h};
    SDL_RenderFillRect(r, &bg);

    set_color(r, COL_FTREE_SEP);
    SDL_FRect sep = {(float)(panel_x + panel_w - 1), (float)panel_y, 1.0f, (float)panel_h};
    SDL_RenderFillRect(r, &sep);

    int btn_w = FTREE_TOGGLE_BTN_W;
    int btn_h = 40;
    /* El boton toggle se centra en el area visible debajo de la tabbar */
    int content_top = panel_y + TAB_BAR_HEIGHT;
    int content_h = panel_h - TAB_BAR_HEIGHT;
    int btn_y = content_top + (content_h - btn_h) / 2;
    int btn_x = panel_x + panel_w - btn_w;
    set_color(r, 0x2C, 0x31, 0x3C, 0xFF);
    SDL_FRect tbtn = {(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h};
    SDL_RenderFillRect(r, &tbtn);
    draw_text(e, "<", btn_x + 2, btn_y + (btn_h - FONT_SIZE) / 2, 0x61, 0xAF, 0xEF);

    /* Header del panel: debajo de la tabbar para que no quede tapado */
    int header_h = 26;
    int header_y = content_top;
    set_color(r, 0x17, 0x1A, 0x21, 0xFF);
    SDL_FRect hdr = {(float)panel_x, (float)header_y, (float)(panel_w - btn_w), (float)header_h};
    SDL_RenderFillRect(r, &hdr);

    /* Titulo "CoffeeCode" si no hay carpeta, o nombre de carpeta si hay */
    const char *rname = ft->root_path;
    char root_label[64];
    if (rname && rname[0]) {
        const char *s = rname + strlen(rname);
        while (s > rname && *(s - 1) != '/' && *(s - 1) != '\\')
            s--;
        snprintf(root_label, sizeof(root_label), " %s", *s ? s : rname);
    } else {
        snprintf(root_label, sizeof(root_label), " CoffeeCode");
    }
    draw_text(e, root_label, panel_x + 4, header_y + (header_h - FONT_SIZE) / 2, 0x61, 0xAF, 0xEF);

    int visible_rows = (content_h - header_h) / FTREE_ITEM_H;
    int vis_count = ftree_visible_count(ft);
    int max_scroll = vis_count - visible_rows;
    if (ft->scroll > max_scroll) ft->scroll = max_scroll;
    if (ft->scroll < 0) ft->scroll = 0;

    int drawn = 0;
    for (int i = 0; i < ftree_count(ft) && drawn < visible_rows + ft->scroll; i++) {
        FEntry *en = ftree_entry(ft, i);
        if (!en->visible) continue;
        int vis_idx = drawn++;
        if (vis_idx < ft->scroll) continue;
        int row = vis_idx - ft->scroll;

        int ey = header_y + header_h + row * FTREE_ITEM_H;
        int ex = panel_x + 4 + en->depth * FTREE_INDENT;

        if (ft->hovered == i) {
            set_color(r, COL_FTREE_HOVER);
            SDL_FRect hi = {(float)panel_x, (float)ey, (float)(panel_w - btn_w),
                            (float)FTREE_ITEM_H};
            SDL_RenderFillRect(r, &hi);
        }

        if (en->type == FTYPE_DIR) {
            const char *icon = en->expanded ? "v " : "> ";
            draw_text(e, icon, ex, ey + (FTREE_ITEM_H - FONT_SIZE) / 2, 0xE5, 0xC0, 0x7B);
            ex += FTREE_ICON_W;
        } else {
            ex += FTREE_ICON_W;
        }

        char label[128];
        int max_chars = (panel_w - btn_w - ex - 4) / (e->char_w > 0 ? e->char_w : 8);
        if (max_chars < 3) max_chars = 3;
        if ((int)strlen(en->name) > max_chars) {
            strncpy(label, en->name, (size_t)(max_chars - 2));
            label[max_chars - 2] = '.';
            label[max_chars - 1] = '.';
            label[max_chars] = '\0';
        } else {
            strncpy(label, en->name, sizeof(label) - 1);
            label[sizeof(label) - 1] = '\0';
        }

        if (en->type == FTYPE_DIR) {
            draw_text(e, label, ex, ey + (FTREE_ITEM_H - FONT_SIZE) / 2, 0xE5, 0xC0, 0x7B);
        } else {
            draw_text(e, label, ex, ey + (FTREE_ITEM_H - FONT_SIZE) / 2, 0xAB, 0xB2, 0xBF);
        }
    }
}

void render_filetree_toggle_closed(Editor *e) {
    if (e->ftree.open) return;
    SDL_Renderer *r = e->renderer;
    int btn_w = FTREE_TOGGLE_BTN_W;
    int btn_h = 40;
    int panel_y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    int panel_h = e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT;
    int btn_y = panel_y + (panel_h - btn_h) / 2;

    set_color(r, 0x2C, 0x31, 0x3C, 0xFF);
    SDL_FRect tbtn = {0.0f, (float)btn_y, (float)btn_w, (float)btn_h};
    SDL_RenderFillRect(r, &tbtn);

    set_color(r, COL_FTREE_SEP);
    SDL_FRect sep = {(float)(btn_w - 1), (float)panel_y, 1.0f, (float)panel_h};
    SDL_RenderFillRect(r, &sep);

    draw_text(e, ">", 2, btn_y + (btn_h - FONT_SIZE) / 2, 0x61, 0xAF, 0xEF);
}
