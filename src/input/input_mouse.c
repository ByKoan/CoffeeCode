#include "input_internal.h"

int get_left_offset(Editor *e) {
    if (e->ftree.open) return e->ftree.width;
    return FTREE_TOGGLE_BTN_W;
}

/* Maneja click en el panel lateral */

void handle_ftree_click(Editor *e, int mx, int my) {
    FileTree *ft = &e->ftree;
    int panel_y    = NAVBAR_HEIGHT;
    int panel_h    = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;
    int btn_w      = FTREE_TOGGLE_BTN_W;
    int content_top = panel_y + TAB_BAR_HEIGHT;
    int content_h   = panel_h - TAB_BAR_HEIGHT;

    /* Click en el botón toggle (borde derecho del panel) */
    int toggle_x = ft->open ? (ft->width - btn_w) : 0;
    if (mx >= toggle_x && mx < toggle_x + btn_w) {
        int btn_h = 40;
        int btn_y = content_top + (content_h - btn_h) / 2;
        if (my >= btn_y && my < btn_y + btn_h) {
            ft->open = !ft->open;
            e->needs_redraw = 1;
            return;
        }
    }

    if (!ft->open) return;

    /* Click en un item del árbol */
    int header_h   = 26;
    int content_y  = content_top + header_h;
    if (my < content_y) return;

    int row = (my - content_y) / FTREE_ITEM_H;
    int actual_row = row + ft->scroll;

    /* Encontrar la entrada visible número actual_row */
    int vis = 0;
    for (int i = 0; i < ftree_count(ft); i++) {
        if (!ftree_entry(ft, i)->visible) continue;
        if (vis == actual_row) {
            FEntry *en = ftree_entry(ft, i);
            if (en->type == FTYPE_DIR) {
                ftree_toggle(ft, i);
            } else {
                /* Abrir archivo en el editor */
                editor_tab_open(e, en->path);
                SDL_SetWindowTitle(e->window, en->path);
            }
            e->needs_redraw = 1;
            return;
        }
        vis++;
    }
}

/* Maneja hover sobre el panel lateral */

void handle_ftree_hover(Editor *e, int mx, int my) {
    FileTree *ft = &e->ftree;
    if (!ft->open) return;

    int header_h  = 26;
    int content_y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT + header_h;
    if (my < content_y) {
        if (ft->hovered != -1) { ft->hovered = -1; e->needs_redraw = 1; }
        return;
    }

    int row = (my - content_y) / FTREE_ITEM_H;
    int actual_row = row + ft->scroll;

    int vis = 0, found = -1;
    for (int i = 0; i < ftree_count(ft); i++) {
        if (!ftree_entry(ft, i)->visible) continue;
        if (vis == actual_row) { found = i; break; }
        vis++;
    }
    if (ft->hovered != found) {
        ft->hovered = found;
        e->needs_redraw = 1;
    }
}

/* -- helpers de cursor ----------------------------------------------------- */

void handle_scroll(Editor *e, float dy) {
    if (e->tab_count == 0 || !e->buf) return;
    int lines = (int)(dy * 3);
    e->scroll_line -= lines;
    int total = buf_line_count(e->buf);
    if (e->scroll_line < 0) e->scroll_line = 0;
    if (total > 0 && e->scroll_line >= total) e->scroll_line = total - 1;
    e->needs_redraw = 1;
}

/* -- click en el área de texto --------------------------------------------- */

void handle_text_click(Editor *e, int mx, int my) {
    int left = get_left_offset(e);
    int text_x = left + GUTTER_WIDTH + PADDING_LEFT;
    if (mx < text_x) return;
    int cw = (e->char_w > 0 ? e->char_w : 8);
    int vis_line = (my - NAVBAR_HEIGHT - TAB_BAR_HEIGHT) / LINE_HEIGHT;
    if (vis_line < 0) vis_line = 0;
    int line = e->scroll_line + vis_line;
    int total = buf_line_count(e->buf);
    if (line < 0) line = 0;
    if (line >= total) line = total - 1;
    /* columna: desplazar por scroll_col */
    int vis_col = (mx - text_x + e->scroll_col * cw) / cw;
    if (vis_col < 0) vis_col = 0;
    /* limitar al largo real de la línea */
    size_t ls = editor_pos_from_line_col(e, line, 0);
    size_t le = buf_line_end(e->buf, ls);
    int line_len = (int)(le - ls);
    if (vis_col > line_len) vis_col = line_len;
    editor_sel_clear(e);
    move_cursor(e, line, vis_col);
}

/* -- Ctrl+A — seleccionar todo ------------------------------------- */
