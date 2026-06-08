#include "input_internal.h"

/* Manejadores de teclado y texto. Los de raton estan en input_mouse.c. */

static void on_text_input(Editor *e, SDL_Event *ev, int ctrl) {
    if (e->menu_open) return;
    if (ctrl) return; /* ignorar cuando Ctrl esta pulsado (ej: Ctrl+A, Ctrl+C) */
    if (e->find.visible && !e->find.bar_focused) {
        /* barra visible pero sin foco: el texto va al editor normalmente */
        if (e->tab_count == 0) return;
        buf_insert_str(e->buf, ev->text.text, strlen(ev->text.text));
        editor_sync_cursor(e);
        editor_update_lexer(e, e->cursor_line);
        editor_ensure_visible(e);
        e->modified = 1;
        e->needs_redraw = 1;
        return;
    }
    if (e->find.visible) {
        size_t tlen = strlen(ev->text.text);
        if (e->find.replace_focused == 0) {
            /* Si hay seleccion, borrar primero el tramo seleccionado */
            if (e->find.query_sel_start >= 0 && e->find.query_sel_end > e->find.query_sel_start) {
                int s = e->find.query_sel_start;
                int n = e->find.query_sel_end - s;
                memmove(e->find.query + s, e->find.query + e->find.query_sel_end,
                        (size_t)(e->find.query_len - e->find.query_sel_end) + 1);
                e->find.query_len -= n;
            }
            e->find.query_sel_start = -1;
            e->find.query_sel_end = -1;
            for (size_t i = 0; i < tlen; i++) {
                if (e->find.query_len < FIND_BAR_MAX - 1) {
                    e->find.query[e->find.query_len++] = ev->text.text[i];
                    e->find.query[e->find.query_len] = '\0';
                }
            }
            find_first(e);
        } else {
            /* Si hay seleccion, borrar primero el tramo seleccionado */
            if (e->find.replace_sel_start >= 0 &&
                e->find.replace_sel_end > e->find.replace_sel_start) {
                int s = e->find.replace_sel_start;
                int n = e->find.replace_sel_end - s;
                memmove(e->find.replace + s, e->find.replace + e->find.replace_sel_end,
                        (size_t)(e->find.replace_len - e->find.replace_sel_end) + 1);
                e->find.replace_len -= n;
            }
            e->find.replace_sel_start = -1;
            e->find.replace_sel_end = -1;
            for (size_t i = 0; i < tlen; i++) {
                if (e->find.replace_len < FIND_BAR_MAX - 1) {
                    e->find.replace[e->find.replace_len++] = ev->text.text[i];
                    e->find.replace[e->find.replace_len] = '\0';
                }
            }
            e->needs_redraw = 1;
        }
        return;
    }
    if (e->tab_count == 0) return;
    buf_insert_str(e->buf, ev->text.text, strlen(ev->text.text));
    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
}

static void on_key_down(Editor *e, SDL_Event *ev, int ctrl, int shift) {
    SDL_Keycode key = ev->key.key;

    /* Escape cierra barras / menú */
    if (key == SDLK_ESCAPE) {
        if (e->find.visible) {
            close_find_bar(e);
            return;
        }
        if (e->menu_open) {
            e->menu_open = 0;
            e->menu_hovered = -1;
            e->needs_redraw = 1;
            return;
        }
        editor_sel_clear(e);
        e->needs_redraw = 1;
        return;
    }

    /* -- Barra de busqueda activa: teclas especiales -- */
    if (e->find.visible) {
        /* Si la barra no tiene foco, las teclas van al editor.
           Ctrl+F vuelve a enfocar la barra. */
        if (!e->find.bar_focused) {
            if (ctrl && key == SDLK_F) {
                e->find.bar_focused = 1;
                e->needs_redraw = 1;
                return;
            }
            /* dejar caer al bloque normal del editor */
            goto editor_keys;
        }
        if (key == SDLK_TAB) {
            /* Tab alterna campo; limpia seleccion de ambos */
            e->find.query_sel_start = -1;
            e->find.query_sel_end = -1;
            e->find.replace_sel_start = -1;
            e->find.replace_sel_end = -1;
            e->find.replace_focused = !e->find.replace_focused;
            e->needs_redraw = 1;
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (e->find.replace_focused) {
                do_replace(e);
            } else if (shift) {
                find_prev(e);
            } else {
                find_jump(e);
            }
            return;
        }
        if (key == SDLK_BACKSPACE) {
            if (e->find.replace_focused == 0) {
                if (e->find.query_sel_start >= 0 &&
                    e->find.query_sel_end > e->find.query_sel_start) {
                    /* borrar seleccion completa */
                    int s = e->find.query_sel_start;
                    int n = e->find.query_sel_end - s;
                    memmove(e->find.query + s, e->find.query + e->find.query_sel_end,
                            (size_t)(e->find.query_len - e->find.query_sel_end) + 1);
                    e->find.query_len -= n;
                    e->find.query_sel_start = -1;
                    e->find.query_sel_end = -1;
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
                    memmove(e->find.replace + s, e->find.replace + e->find.replace_sel_end,
                            (size_t)(e->find.replace_len - e->find.replace_sel_end) + 1);
                    e->find.replace_len -= n;
                    e->find.replace_sel_start = -1;
                    e->find.replace_sel_end = -1;
                } else if (e->find.replace_len > 0) {
                    e->find.replace[--e->find.replace_len] = '\0';
                }
                e->needs_redraw = 1;
            }
            return;
        }
        /* Ctrl+F de nuevo = siguiente resultado */
        if (ctrl && key == SDLK_F) {
            find_jump(e);
            return;
        }
        /* Ctrl+A — seleccionar todo el texto del campo activo */
        if (ctrl && key == SDLK_A) {
            if (e->find.replace_focused == 0) {
                if (e->find.query_len > 0) {
                    e->find.query_sel_start = 0;
                    e->find.query_sel_end = e->find.query_len;
                    e->needs_redraw = 1;
                }
            } else {
                if (e->find.replace_len > 0) {
                    e->find.replace_sel_start = 0;
                    e->find.replace_sel_end = e->find.replace_len;
                    e->needs_redraw = 1;
                }
            }
            return;
        }
        return; /* resto de teclas ignoradas mientras find esta abierto */
    }

editor_keys:
    if (ctrl) {
        switch (key) {
        /* -- Archivo -- */
        case SDLK_N: new_file(e); break;
        case SDLK_O: open_file_dialog(e); break;
        case SDLK_K: open_folder_dialog(e); break;
        case SDLK_S: save_file(e); break;
        case SDLK_Q: e->running = 0; break;

        /* -- Edición -- */
        case SDLK_Z:
            if (e->buf) editor_undo(e);
            break;
        case SDLK_Y:
            if (e->buf) editor_redo(e);
            break;
        case SDLK_A:
            if (e->buf) select_all(e);
            break;
        case SDLK_C:
            if (e->buf) do_copy(e);
            break;
        case SDLK_X:
            if (e->buf) do_cut(e);
            break;
        case SDLK_V:
            if (e->buf) do_paste(e);
            break;
        case SDLK_D:
            if (e->buf) duplicate_line(e);
            break;
        case SDLK_SLASH:
            if (e->buf) toggle_line_comment(e);
            break;
        case SDLK_L:
            if (e->buf) select_line(e);
            break;

        /* -- Navegación -- */
        case SDLK_F: open_find_bar(e); break;
        case SDLK_B: toggle_sidebar(e); break;
        case SDLK_W: editor_tab_close(e); break;
        case SDLK_TAB:
            if (e->tab_count > 0) editor_tab_switch(e, (e->active_tab + 1) % e->tab_count);
            break;
        case SDLK_HOME:
            if (e->buf) move_cursor_select(e, 0, 0, shift);
            break;
        case SDLK_END: {
            if (!e->buf) break;
            int t = buf_line_count(e->buf) - 1;
            size_t ep = buf_line_end(e->buf, editor_pos_from_line_col(e, t, 0));
            size_t sp = editor_pos_from_line_col(e, t, 0);
            move_cursor_select(e, t, (int)(ep - sp), shift);
            break;
        }
        /* -- salto de palabra -- */
        case SDLK_LEFT:
            if (e->buf) move_word_left(e, shift);
            break;
        case SDLK_RIGHT:
            if (e->buf) move_word_right(e, shift);
            break;
        case SDLK_UP:
            if (e->buf) move_cursor_select(e, e->cursor_line - 5, e->cursor_col, shift);
            break;
        case SDLK_DOWN:
            if (e->buf) move_cursor_select(e, e->cursor_line + 5, e->cursor_col, shift);
            break;
        default: break;
        }
    } else {
        if (e->menu_open) return;
        if (!e->buf) return; /* sin buffer activo, ignorar teclas de edición */
        switch (key) {
        case SDLK_UP: move_line_up(e, shift); break;
        case SDLK_DOWN: move_line_down(e, shift); break;
        case SDLK_LEFT: move_col_left(e, shift); break;
        case SDLK_RIGHT: move_col_right(e, shift); break;
        case SDLK_HOME: move_home(e, shift); break;
        case SDLK_END: move_end(e, shift); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: insert_newline(e); break;
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
                    } else
                        break;
                }
                if (removed) {
                    buf_move_to(e->buf, ls);
                    editor_sync_cursor(e);
                    editor_update_lexer(e, e->cursor_line);
                    editor_ensure_visible(e);
                    e->modified = 1;
                    e->needs_redraw = 1;
                }
            } else {
                insert_tab(e);
            }
            break;
        case SDLK_BACKSPACE: do_backspace(e); break;
        case SDLK_DELETE: do_delete(e); break;
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
}

void input_handle_event(Editor *e, SDL_Event *ev) {
    SDL_Keymod mods = SDL_GetModState();
    int ctrl = (mods & SDL_KMOD_CTRL) != 0;
    int shift = (mods & SDL_KMOD_SHIFT) != 0;

    switch (ev->type) {
    case SDL_EVENT_QUIT: e->running = 0; break;
    case SDL_EVENT_WINDOW_RESIZED:
        e->win_w = ev->window.data1;
        e->win_h = ev->window.data2;
        e->needs_redraw = 1;
        break;
    case SDL_EVENT_MOUSE_WHEEL: on_mouse_wheel(e, ev); break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (ev->button.button == SDL_BUTTON_LEFT) {
            e->ftree.dragging_border = 0;
            e->mouse_selecting = 0;
            e->scrollbar_dragging = 0;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION: on_mouse_motion(e, ev); break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: on_mouse_button_down(e, ev); break;
    case SDL_EVENT_TEXT_INPUT: on_text_input(e, ev, ctrl); break;
    case SDL_EVENT_KEY_DOWN: on_key_down(e, ev, ctrl, shift); break;
    default: break;
    }
}
