#include "input_internal.h"

void input_handle_event(Editor *e, SDL_Event *ev) {
    SDL_Keymod mods  = SDL_GetModState();
    int ctrl  = (mods & SDL_KMOD_CTRL)  != 0;
    int shift = (mods & SDL_KMOD_SHIFT) != 0;

    switch (ev->type) {

    case SDL_EVENT_QUIT:
        e->running = 0;
        break;

    case SDL_EVENT_WINDOW_RESIZED:
        e->win_w = ev->window.data1;
        e->win_h = ev->window.data2;
        e->needs_redraw = 1;
        break;

    case SDL_EVENT_MOUSE_WHEEL: {
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
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (ev->button.button == SDL_BUTTON_LEFT) {
            e->ftree.dragging_border  = 0;
            e->mouse_selecting        = 0;
            e->scrollbar_dragging     = 0;
        }
        break;

    case SDL_EVENT_MOUSE_MOTION: {
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
            if (e->tab_count == 0) break;
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
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int mx = (int)ev->button.x;
        int my = (int)ev->button.y;
        if (ev->button.button != SDL_BUTTON_LEFT) break;

        /* -- clic en la barra de tabs -- */
        if (my >= NAVBAR_HEIGHT && my < NAVBAR_HEIGHT + TAB_BAR_HEIGHT) {
            /* botón + nuevo tab */
            if (mx >= e->tab_new_btn_x && mx < e->tab_new_btn_x + 28) {
                editor_tab_new(e);
                break;
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
            break;
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
            break;
        }

        /* clic en botón "Archivo" */
        if (my >= 0 && my < NAVBAR_HEIGHT &&
            mx >= BTN_FILE_X && mx < BTN_FILE_X + BTN_FILE_W) {
            e->menu_open    = 1;
            e->menu_hovered = -1;
            e->needs_redraw = 1;
            break;
        }

        /* clic en la barra de búsqueda */
        if (e->find.visible) {
            FindBar *fb = &e->find;
            /* clic en botón Reemplazar */
            if (mx >= fb->replace_btn_x && mx < fb->replace_btn_x + fb->replace_btn_w &&
                my >= fb->replace_btn_y && my < fb->replace_btn_y + fb->replace_btn_h) {
                fb->bar_focused = 1;
                do_replace(e);
                break;
            }
            /* clic en botón ↑ (prev) */
            if (fb->prev_btn_w > 0 &&
                mx >= fb->prev_btn_x && mx < fb->prev_btn_x + fb->prev_btn_w &&
                my >= fb->prev_btn_y && my < fb->prev_btn_y + fb->prev_btn_h) {
                fb->bar_focused = 1;
                find_prev(e);
                break;
            }
            /* clic en botón ↓ (next) */
            if (fb->next_btn_w > 0 &&
                mx >= fb->next_btn_x && mx < fb->next_btn_x + fb->next_btn_w &&
                my >= fb->next_btn_y && my < fb->next_btn_y + fb->next_btn_h) {
                fb->bar_focused = 1;
                find_jump(e);
                break;
            }
            /* clic en campo buscar */
            if (mx >= fb->field_x && mx < fb->bar_x + fb->bar_w &&
                my >= fb->row1_y  && my < fb->row1_y + fb->field_h) {
                fb->replace_focused = 0;
                fb->bar_focused     = 1;
                e->needs_redraw = 1;
                break;
            }
            /* clic en campo reemplazar */
            if (mx >= fb->field_x && mx < fb->bar_x + fb->bar_w &&
                my >= fb->row2_y  && my < fb->row2_y + fb->field_h) {
                fb->replace_focused = 1;
                fb->bar_focused     = 1;
                e->needs_redraw = 1;
                break;
            }
            /* clic dentro de la barra pero fuera de campos */
            if (mx >= fb->bar_x && mx < fb->bar_x + fb->bar_w &&
                my >= fb->bar_y  && my < fb->bar_y  + fb->bar_h) {
                break;
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
                    break;
                }
            }

            int left = get_left_offset(e);
            if (mx < left) {
                handle_ftree_click(e, mx, my);
            } else {
                /* iniciar selección con ratón */
                if (e->tab_count == 0) break;  /* sin tabs, nada que hacer */
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
        break;
    }

    /* -- entrada de texto — va a la barra de búsqueda si está visible */
    case SDL_EVENT_TEXT_INPUT:
        if (e->menu_open) break;
        if (ctrl) break;  /* ignorar cuando Ctrl esta pulsado (ej: Ctrl+A, Ctrl+C) */
        if (e->find.visible && !e->find.bar_focused) {
            /* barra visible pero sin foco: el texto va al editor normalmente */
            if (e->tab_count == 0) break;
            buf_insert_str(e->buf, ev->text.text, strlen(ev->text.text));
            editor_sync_cursor(e);
            editor_update_lexer(e, e->cursor_line);
            editor_ensure_visible(e);
            e->modified = 1; e->needs_redraw = 1;
            break;
        }
        if (e->find.visible) {
            size_t tlen = strlen(ev->text.text);
            if (e->find.replace_focused == 0) {
                /* Si hay seleccion, borrar primero el tramo seleccionado */
                if (e->find.query_sel_start >= 0 &&
                    e->find.query_sel_end > e->find.query_sel_start) {
                    int s = e->find.query_sel_start;
                    int n = e->find.query_sel_end - s;
                    memmove(e->find.query + s,
                            e->find.query + e->find.query_sel_end,
                            (size_t)(e->find.query_len - e->find.query_sel_end) + 1);
                    e->find.query_len -= n;
                }
                e->find.query_sel_start = -1;
                e->find.query_sel_end   = -1;
                for (size_t i = 0; i < tlen; i++) {
                    if (e->find.query_len < FIND_BAR_MAX - 1) {
                        e->find.query[e->find.query_len++] = ev->text.text[i];
                        e->find.query[e->find.query_len]   = '\0';
                    }
                }
                find_first(e);
            } else {
                /* Si hay seleccion, borrar primero el tramo seleccionado */
                if (e->find.replace_sel_start >= 0 &&
                    e->find.replace_sel_end > e->find.replace_sel_start) {
                    int s = e->find.replace_sel_start;
                    int n = e->find.replace_sel_end - s;
                    memmove(e->find.replace + s,
                            e->find.replace + e->find.replace_sel_end,
                            (size_t)(e->find.replace_len - e->find.replace_sel_end) + 1);
                    e->find.replace_len -= n;
                }
                e->find.replace_sel_start = -1;
                e->find.replace_sel_end   = -1;
                for (size_t i = 0; i < tlen; i++) {
                    if (e->find.replace_len < FIND_BAR_MAX - 1) {
                        e->find.replace[e->find.replace_len++] = ev->text.text[i];
                        e->find.replace[e->find.replace_len]   = '\0';
                    }
                }
                e->needs_redraw = 1;
            }
            break;
        }
        if (e->tab_count == 0) break;
        buf_insert_str(e->buf, ev->text.text, strlen(ev->text.text));
        editor_sync_cursor(e);
        editor_update_lexer(e, e->cursor_line);
        editor_ensure_visible(e);
        e->modified = 1; e->needs_redraw = 1;
        break;

    case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode key = ev->key.key;

        /* Escape cierra barras / menú */
        if (key == SDLK_ESCAPE) {
            if (e->find.visible) { close_find_bar(e); break; }
            if (e->menu_open)    { e->menu_open = 0; e->menu_hovered = -1; e->needs_redraw = 1; break; }
            editor_sel_clear(e); e->needs_redraw = 1;
            break;
        }

        /* -- Barra de busqueda activa: teclas especiales -- */
        if (e->find.visible) {
            /* Si la barra no tiene foco, las teclas van al editor.
               Ctrl+F vuelve a enfocar la barra. */
            if (!e->find.bar_focused) {
                if (ctrl && key == SDLK_F) {
                    e->find.bar_focused = 1;
                    e->needs_redraw = 1;
                    break;
                }
                /* dejar caer al bloque normal del editor */
                goto editor_keys;
            }
            if (key == SDLK_TAB) {
                /* Tab alterna campo; limpia seleccion de ambos */
                e->find.query_sel_start   = -1;
                e->find.query_sel_end     = -1;
                e->find.replace_sel_start = -1;
                e->find.replace_sel_end   = -1;
                e->find.replace_focused   = !e->find.replace_focused;
                e->needs_redraw = 1;
                break;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                if (e->find.replace_focused) {
                    do_replace(e);
                } else if (shift) {
                    find_prev(e);
                } else {
                    find_jump(e);
                }
                break;
            }
            if (key == SDLK_BACKSPACE) {
                if (e->find.replace_focused == 0) {
                    if (e->find.query_sel_start >= 0 &&
                        e->find.query_sel_end > e->find.query_sel_start) {
                        /* borrar seleccion completa */
                        int s = e->find.query_sel_start;
                        int n = e->find.query_sel_end - s;
                        memmove(e->find.query + s,
                                e->find.query + e->find.query_sel_end,
                                (size_t)(e->find.query_len - e->find.query_sel_end) + 1);
                        e->find.query_len -= n;
                        e->find.query_sel_start = -1;
                        e->find.query_sel_end   = -1;
                    } else if (e->find.query_len > 0) {
                        e->find.query[--e->find.query_len] = '\0';
                    }
                    find_first(e);
                } else {
                    if (e->find.replace_sel_start >= 0 &&
                        e->find.replace_sel_end > e->find.replace_sel_start) {
                        /* borrar seleccion completa */
                        int s = e->find.replace_sel_start;
                        int n = e->find.replace_sel_end - s;
                        memmove(e->find.replace + s,
                                e->find.replace + e->find.replace_sel_end,
                                (size_t)(e->find.replace_len - e->find.replace_sel_end) + 1);
                        e->find.replace_len -= n;
                        e->find.replace_sel_start = -1;
                        e->find.replace_sel_end   = -1;
                    } else if (e->find.replace_len > 0) {
                        e->find.replace[--e->find.replace_len] = '\0';
                    }
                    e->needs_redraw = 1;
                }
                break;
            }
            /* Ctrl+F de nuevo = siguiente resultado */
            if (ctrl && key == SDLK_F) { find_jump(e); break; }
            /* Ctrl+A — seleccionar todo el texto del campo activo */
            if (ctrl && key == SDLK_A) {
                if (e->find.replace_focused == 0) {
                    if (e->find.query_len > 0) {
                        e->find.query_sel_start = 0;
                        e->find.query_sel_end   = e->find.query_len;
                        e->needs_redraw = 1;
                    }
                } else {
                    if (e->find.replace_len > 0) {
                        e->find.replace_sel_start = 0;
                        e->find.replace_sel_end   = e->find.replace_len;
                        e->needs_redraw = 1;
                    }
                }
                break;
            }
            break;  /* resto de teclas ignoradas mientras find esta abierto */
        }

        editor_keys:
        if (ctrl) {
            switch (key) {
            /* -- Archivo -- */
            case SDLK_N: new_file(e);          break;
            case SDLK_O: open_file_dialog(e);  break;
            case SDLK_K: open_folder_dialog(e); break;
            case SDLK_S: save_file(e);         break;
            case SDLK_Q: e->running = 0;       break;

            /* -- Edición -- */
            case SDLK_Z: if (e->buf) editor_undo(e);       break;
            case SDLK_Y: if (e->buf) editor_redo(e);       break;
            case SDLK_A: if (e->buf) select_all(e);        break;
            case SDLK_C: if (e->buf) do_copy(e);           break;
            case SDLK_X: if (e->buf) do_cut(e);            break;
            case SDLK_V: if (e->buf) do_paste(e);          break;
            case SDLK_D: if (e->buf) duplicate_line(e);    break;
            case SDLK_SLASH: if (e->buf) toggle_line_comment(e); break;
            case SDLK_L: if (e->buf) select_line(e);       break;

            /* -- Navegación -- */
            case SDLK_F: open_find_bar(e);     break;
            case SDLK_B: toggle_sidebar(e);    break;
            case SDLK_W: editor_tab_close(e);  break;
            case SDLK_TAB: if (e->tab_count > 0) editor_tab_switch(e, (e->active_tab+1) % e->tab_count); break;
            case SDLK_HOME: if (e->buf) move_cursor_select(e, 0, 0, shift); break;
            case SDLK_END: {
                if (!e->buf) break;
                int t = buf_line_count(e->buf) - 1;
                size_t ep = buf_line_end(e->buf,
                    editor_pos_from_line_col(e, t, 0));
                size_t sp = editor_pos_from_line_col(e, t, 0);
                move_cursor_select(e, t, (int)(ep - sp), shift);
                break;
            }
            /* -- salto de palabra -- */
            case SDLK_LEFT:  if (e->buf) move_word_left(e, shift);  break;
            case SDLK_RIGHT: if (e->buf) move_word_right(e, shift); break;
            case SDLK_UP:    if (e->buf) move_cursor_select(e, e->cursor_line - 5, e->cursor_col, shift); break;
            case SDLK_DOWN:  if (e->buf) move_cursor_select(e, e->cursor_line + 5, e->cursor_col, shift); break;
            default: break;
            }
        } else {
            if (e->menu_open) break;
            if (!e->buf) break;  /* sin buffer activo, ignorar teclas de edición */
            switch (key) {
            case SDLK_UP:        move_line_up(e, shift);   break;
            case SDLK_DOWN:      move_line_down(e, shift);  break;
            case SDLK_LEFT:      move_col_left(e, shift);   break;
            case SDLK_RIGHT:     move_col_right(e, shift);  break;
            case SDLK_HOME:      move_home(e, shift);       break;
            case SDLK_END:       move_end(e, shift);        break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:  insert_newline(e);         break;
            case SDLK_TAB:
                if (shift) {
                    /* Shift+Tab: quitar sangría */
                    size_t ls = editor_pos_from_line_col(e, e->cursor_line, 0);
                    int removed = 0;
                    for (int i = 0; i < TAB_SIZE; i++) {
                        if (buf_char_at(e->buf, ls) == ' ') {
                            char c = ' ';
                            editor_undo_push_delete(e, ls, &c, 1);
                            buf_delete_range(e->buf, ls, ls + 1);
                            removed++;
                        } else break;
                    }
                    if (removed) {
                        buf_move_to(e->buf, ls);
                        editor_sync_cursor(e);
                        editor_update_lexer(e, e->cursor_line);
                        editor_ensure_visible(e);
                        e->modified = 1; e->needs_redraw = 1;
                    }
                } else {
                    insert_tab(e);
                }
                break;
            case SDLK_BACKSPACE: do_backspace(e);    break;
            case SDLK_DELETE:    do_delete(e);       break;
            case SDLK_PAGEUP: {
                int text_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;
                int vis = text_h / LINE_HEIGHT;
                move_cursor_select(e, e->cursor_line - vis, e->cursor_col, shift);
                break;
            }
            case SDLK_PAGEDOWN: {
                int text_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;
                int vis = text_h / LINE_HEIGHT;
                move_cursor_select(e, e->cursor_line + vis, e->cursor_col, shift);
                break;
            }
            default: break;
            }
        }
        break;
    }

    default: break;
    }
}
