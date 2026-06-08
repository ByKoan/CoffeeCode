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

/* === Manejadores de eventos de raton (invocados por input_handle_event) === */

void on_mouse_wheel(Editor *e, SDL_Event *ev) {
        float mx2f = 0.0f, my2f = 0.0f;
        SDL_GetMouseState(&mx2f, &my2f);
        int mx2 = (int)mx2f;
        int left2 = get_left_offset(e);
        if (e->ftree.open && mx2 < left2) {
            e->ftree.scroll -= (int)(ev->wheel.y * 3);
            if (e->ftree.scroll < 0) e->ftree.scroll = 0;
            e->needs_redraw = 1;
        } else {
            handle_scroll(e, ev->wheel.y);
        }
}

void on_mouse_motion(Editor *e, SDL_Event *ev) {
        int mx = (int)ev->motion.x;
        int my = (int)ev->motion.y;
        if (e->menu_open) {
            int prev = e->menu_hovered;
            e->menu_hovered = menu_item_at(mx, my);
            if (e->menu_hovered != prev) e->needs_redraw = 1;
        }
        if (e->ftree.open && mx < e->ftree.width && my >= NAVBAR_HEIGHT + TAB_BAR_HEIGHT)
            handle_ftree_hover(e, mx, my);
        if (e->ftree.dragging_border) {
            int new_w = e->ftree.drag_start_w + (mx - e->ftree.drag_start_x);
            if (new_w < FTREE_MIN_WIDTH)  new_w = FTREE_MIN_WIDTH;
            if (new_w > e->win_w / 2)     new_w = e->win_w / 2;
            e->ftree.width = new_w;
            e->needs_redraw = 1;
        }

        /* arratre de scrollbar */
        if (e->scrollbar_dragging) {
            int text_height   = e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT - SHORTCUT_HEIGHT;
            if (e->tab_count == 0) return;
            int total_lines   = buf_line_count(e->buf);
            int visible_lines = text_height / LINE_HEIGHT;
            int max_scroll    = total_lines - visible_lines;
            if (max_scroll < 0) max_scroll = 0;
            float thumb_h_ratio = (visible_lines > 0 && total_lines > 0)
                                  ? (float)visible_lines / (float)total_lines : 1.0f;
            int thumb_h = (int)(text_height * thumb_h_ratio);
            if (thumb_h < 20) thumb_h = 20;
            int thumb_range = text_height - thumb_h;
            if (thumb_range < 1) thumb_range = 1;
            float frac = (float)(my - e->scrollbar_drag_start_y) / (float)thumb_range;
            int new_scroll = e->scrollbar_drag_start_line + (int)(frac * max_scroll);
            if (new_scroll < 0)          new_scroll = 0;
            if (new_scroll > max_scroll) new_scroll = max_scroll;
            e->scroll_line = new_scroll;
            e->needs_redraw = 1;
        }

        /* arrastre para seleccionar texto */
        if (e->mouse_selecting && e->tab_count > 0) {
            int left    = get_left_offset(e);
            int text_x  = left + GUTTER_WIDTH + PADDING_LEFT;
            int cw      = (e->char_w > 0 ? e->char_w : 8);
            /* calcular línea visual */
            int vis_line = (my - NAVBAR_HEIGHT - TAB_BAR_HEIGHT) / LINE_HEIGHT;
            if (vis_line < 0) vis_line = 0;
            int line = e->scroll_line + vis_line;
            int total = buf_line_count(e->buf);
            if (line < 0)       line = 0;
            if (line >= total)  line = total - 1;
            /* calcular columna — limitar al largo real de la línea */
            int vis_col = (mx - text_x + e->scroll_col * cw) / cw;
            if (vis_col < 0) vis_col = 0;
            /* obtener longitud real de la línea */
            size_t ls = editor_pos_from_line_col(e, line, 0);
            size_t le = buf_line_end(e->buf, ls);
            int line_len = (int)(le - ls);
            if (vis_col > line_len) vis_col = line_len;
            int col = vis_col;
            /* activar selección manteniendo el ancla fijada en BUTTON_DOWN */
            e->sel_active = 1;
            /* mover cursor sin tocar el ancla */
            size_t pos = editor_pos_from_line_col(e, line, col);
            buf_move_to(e->buf, pos);
            editor_sync_cursor(e);
            editor_ensure_visible(e);
            e->needs_redraw = 1;
        }
}

void on_mouse_button_down(Editor *e, SDL_Event *ev) {
        int mx = (int)ev->button.x;
        int my = (int)ev->button.y;
        if (ev->button.button != SDL_BUTTON_LEFT) return;

        /* -- clic en la barra de tabs -- */
        if (my >= NAVBAR_HEIGHT && my < NAVBAR_HEIGHT + TAB_BAR_HEIGHT) {
            /* botón + nuevo tab */
            if (mx >= e->tab_new_btn_x && mx < e->tab_new_btn_x + 28) {
                editor_tab_new(e);
                return;
            }
            /* clic en tab existente */
            for (int i = 0; i < e->tab_count; i++) {
                EditorTab *t = &e->tabs[i];
                if (mx >= t->tab_x && mx < t->tab_x + t->tab_w) {
                    /* botón × cerrar */
                    if (mx >= t->close_x && mx < t->close_x + 16 &&
                        my >= t->close_y  && my < t->close_y  + 16) {
                        editor_tab_save_state(e);
                        e->active_tab = i;
                        editor_tab_close(e);
                        const char *title = e->filepath[0] ? e->filepath : "CoffeeCode - Sin título";
                        SDL_SetWindowTitle(e->window, title);
                    } else {
                        editor_tab_switch(e, i);
                        const char *title = e->tabs[i].filepath[0] ? e->tabs[i].filepath : "CoffeeCode - Sin título";
                        SDL_SetWindowTitle(e->window, title);
                    }
                    break;
                }
            }
            return;
        }

        /* clic en el menú abierto */
        if (e->menu_open) {
            int item = menu_item_at(mx, my);
            if (item >= 0) {
                menu_exec(e, item);
            } else {
                int in_btn = (mx >= BTN_FILE_X && mx < BTN_FILE_X + BTN_FILE_W
                              && my >= 0 && my < NAVBAR_HEIGHT);
                e->menu_open    = in_btn ? 0 : 0;
                e->menu_hovered = -1;
                e->needs_redraw = 1;
            }
            return;
        }

        /* clic en botón "Archivo" */
        if (my >= 0 && my < NAVBAR_HEIGHT &&
            mx >= BTN_FILE_X && mx < BTN_FILE_X + BTN_FILE_W) {
            e->menu_open    = 1;
            e->menu_hovered = -1;
            e->needs_redraw = 1;
            return;
        }

        /* clic en la barra de búsqueda */
        if (e->find.visible) {
            FindBar *fb = &e->find;
            /* clic en botón Reemplazar */
            if (mx >= fb->replace_btn_x && mx < fb->replace_btn_x + fb->replace_btn_w &&
                my >= fb->replace_btn_y && my < fb->replace_btn_y + fb->replace_btn_h) {
                fb->bar_focused = 1;
                do_replace(e);
                return;
            }
            /* clic en botón ↑ (prev) */
            if (fb->prev_btn_w > 0 &&
                mx >= fb->prev_btn_x && mx < fb->prev_btn_x + fb->prev_btn_w &&
                my >= fb->prev_btn_y && my < fb->prev_btn_y + fb->prev_btn_h) {
                fb->bar_focused = 1;
                find_prev(e);
                return;
            }
            /* clic en botón ↓ (next) */
            if (fb->next_btn_w > 0 &&
                mx >= fb->next_btn_x && mx < fb->next_btn_x + fb->next_btn_w &&
                my >= fb->next_btn_y && my < fb->next_btn_y + fb->next_btn_h) {
                fb->bar_focused = 1;
                find_jump(e);
                return;
            }
            /* clic en campo buscar */
            if (mx >= fb->field_x && mx < fb->bar_x + fb->bar_w &&
                my >= fb->row1_y  && my < fb->row1_y + fb->field_h) {
                fb->replace_focused = 0;
                fb->bar_focused     = 1;
                e->needs_redraw = 1;
                return;
            }
            /* clic en campo reemplazar */
            if (mx >= fb->field_x && mx < fb->bar_x + fb->bar_w &&
                my >= fb->row2_y  && my < fb->row2_y + fb->field_h) {
                fb->replace_focused = 1;
                fb->bar_focused     = 1;
                e->needs_redraw = 1;
                return;
            }
            /* clic dentro de la barra pero fuera de campos */
            if (mx >= fb->bar_x && mx < fb->bar_x + fb->bar_w &&
                my >= fb->bar_y  && my < fb->bar_y  + fb->bar_h) {
                return;
            }
            /* clic fuera de la barra: foco vuelve al editor */
            if (fb->bar_focused) {
                fb->bar_focused = 0;
                e->needs_redraw = 1;
            }
        }

        /* clic fuera de navbar y menú */
        if (my >= NAVBAR_HEIGHT + TAB_BAR_HEIGHT) {
            if (e->menu_open) {
                int menu_y = NAVBAR_HEIGHT;
                int mh     = menu_total_h();
                if (!(mx >= BTN_FILE_X && mx < BTN_FILE_X + MENU_WIDTH
                      && my >= menu_y && my < menu_y + mh)) {
                    e->menu_open = 0;
                    e->needs_redraw = 1;
                }
            }

            /* clic en la scrollbar */
            {
                int sb_x = e->win_w - 9; /* SCROLLBAR_W=8 + 1px borde */
                if (mx >= sb_x) {
                    e->scrollbar_dragging        = 1;
                    e->scrollbar_drag_start_y    = my;
                    e->scrollbar_drag_start_line = e->scroll_line;
                    return;
                }
            }

            int left = get_left_offset(e);
            if (mx < left) {
                handle_ftree_click(e, mx, my);
            } else {
                /* iniciar selección con ratón */
                if (e->tab_count == 0) return;  /* sin tabs, nada que hacer */
                int text_x = left + GUTTER_WIDTH + PADDING_LEFT;
                int cw     = (e->char_w > 0 ? e->char_w : 8);
                int vis_line = (my - NAVBAR_HEIGHT - TAB_BAR_HEIGHT) / LINE_HEIGHT;
                if (vis_line < 0) vis_line = 0;
                int line = e->scroll_line + vis_line;
                int total = buf_line_count(e->buf);
                if (line < 0)      line = 0;
                if (line >= total) line = total - 1;
                /* columna con scroll y limitada al largo real */
                int vis_col = (mx - text_x + e->scroll_col * cw) / cw;
                if (vis_col < 0) vis_col = 0;
                size_t ls = editor_pos_from_line_col(e, line, 0);
                size_t le = buf_line_end(e->buf, ls);
                int line_len = (int)(le - ls);
                if (vis_col > line_len) vis_col = line_len;
                int col = vis_col;

                editor_sel_clear(e);
                /* fijar ancla ANTES de mover el cursor, usando las coords calculadas */
                e->sel_anchor_line = line;
                e->sel_anchor_col  = col;
                e->mouse_selecting = 1;
                size_t pos = editor_pos_from_line_col(e, line, col);
                buf_move_to(e->buf, pos);
                editor_sync_cursor(e);
                editor_ensure_visible(e);
                e->needs_redraw = 1;
            }
        }
}
