/**
 * @file input_mouse.c
 * @brief Entrada de ratón: clics, hover, scroll y arrastres (panel, scrollbar,
 *        selección de texto). Incluye los hit-tests de la UI.
 */
#include "input_internal.h"

#define FALLBACK_CHAR_W        8  /* ancho de carácter por defecto              */
#define SCROLL_LINES_PER_NOTCH 3  /* líneas desplazadas por "muesca" de rueda    */

/* Geometría usada para hit-testing — debe coincidir con la de render. */
#define HIT_FTREE_HEADER_H 26
#define HIT_FTREE_TOGGLE_H 40
#define HIT_TAB_CLOSE_W    16
#define HIT_NEW_TAB_BTN_W  28
#define HIT_SB_MIN_THUMB_H 20
#define HIT_SCROLLBAR_X    9 /* ancho de la pista + borde (SCROLLBAR_W + 1) */

/** Offset horizontal del área de texto (tras el panel y el gutter). */
int get_left_offset(Editor *e) {
    if (e->ftree.open) return e->ftree.width;
    return FTREE_TOGGLE_BTN_W;
}

/**
 * @brief Convierte coordenadas de ratón en una posición (línea, columna) del
 *        texto, recortada al rango válido del archivo.
 * @pre Hay un tab activo con buffer (@c e->buf no nulo).
 */
static void point_to_line_col(Editor *e, int mouse_x, int mouse_y, int *line, int *col) {
    int text_x = get_left_offset(e) + GUTTER_WIDTH + PADDING_LEFT;
    int char_px = (e->char_w > 0 ? e->char_w : FALLBACK_CHAR_W);

    int visual_line = (mouse_y - NAVBAR_HEIGHT - TAB_BAR_HEIGHT) / LINE_HEIGHT;
    if (visual_line < 0) visual_line = 0;
    int ln = e->scroll_line + visual_line;
    int total = buf_line_count(e->buf);
    if (ln < 0) ln = 0;
    if (ln >= total) ln = total - 1;

    int visual_col = (mouse_x - text_x + e->scroll_col * char_px) / char_px;
    if (visual_col < 0) visual_col = 0;
    size_t line_start = editor_pos_from_line_col(e, ln, 0);
    int line_len = (int)(buf_line_end(e->buf, line_start) - line_start);
    if (visual_col > line_len) visual_col = line_len;

    *line = ln;
    *col = visual_col;
}

/** Índice de la entrada visible nº @p target_row del árbol, o -1. */
static int ftree_entry_at_row(FileTree *ft, int target_row) {
    int visible = 0;
    for (int i = 0; i < ftree_count(ft); i++) {
        if (!ftree_entry(ft, i)->visible) continue;
        if (visible == target_row) return i;
        visible++;
    }
    return -1;
}

/** Maneja un clic dentro del panel lateral (botón toggle o entrada del árbol). */
void handle_ftree_click(Editor *e, int mx, int my) {
    FileTree *ft = &e->ftree;
    int content_top = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    int content_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT - TAB_BAR_HEIGHT;

    /* Botón toggle (borde derecho del panel, o x=0 si está cerrado) */
    int toggle_x = ft->open ? (ft->width - FTREE_TOGGLE_BTN_W) : 0;
    if (mx >= toggle_x && mx < toggle_x + FTREE_TOGGLE_BTN_W) {
        int btn_y = content_top + (content_h - HIT_FTREE_TOGGLE_H) / 2;
        if (my >= btn_y && my < btn_y + HIT_FTREE_TOGGLE_H) {
            ft->open = !ft->open;
            e->needs_redraw = 1;
            return;
        }
    }

    if (!ft->open) return;

    int content_y = content_top + HIT_FTREE_HEADER_H;
    if (my < content_y) return;

    int target_row = (my - content_y) / FTREE_ITEM_H + ft->scroll;
    int idx = ftree_entry_at_row(ft, target_row);
    if (idx < 0) return;

    FEntry *en = ftree_entry(ft, idx);
    if (en->type == FTYPE_DIR) {
        ftree_toggle(ft, idx);
    } else {
        editor_tab_open(e, en->path);
        SDL_SetWindowTitle(e->window, en->path);
    }
    e->needs_redraw = 1;
}

/** Actualiza la entrada del árbol bajo el cursor (hover). */
void handle_ftree_hover(Editor *e, int mx, int my) {
    FileTree *ft = &e->ftree;
    if (!ft->open) return;
    (void)mx;

    int content_y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT + HIT_FTREE_HEADER_H;
    int hovered = -1;
    if (my >= content_y)
        hovered = ftree_entry_at_row(ft, (my - content_y) / FTREE_ITEM_H + ft->scroll);

    if (ft->hovered != hovered) {
        ft->hovered = hovered;
        e->needs_redraw = 1;
    }
}

/** Desplaza el texto verticalmente @p wheel_dy muescas de rueda. */
void handle_scroll(Editor *e, float wheel_dy) {
    if (e->tab_count == 0 || !e->buf) return;
    e->scroll_line -= (int)(wheel_dy * SCROLL_LINES_PER_NOTCH);
    int total = buf_line_count(e->buf);
    if (e->scroll_line < 0) e->scroll_line = 0;
    if (total > 0 && e->scroll_line >= total) e->scroll_line = total - 1;
    e->needs_redraw = 1;
}

/** Clic en el área de texto: coloca el cursor (sin selección). */
void handle_text_click(Editor *e, int mx, int my) {
    int text_x = get_left_offset(e) + GUTTER_WIDTH + PADDING_LEFT;
    if (mx < text_x || e->tab_count == 0 || !e->buf) return;
    int line, col;
    point_to_line_col(e, mx, my, &line, &col);
    editor_sel_clear(e);
    move_cursor(e, line, col);
}

/* ── Manejadores de eventos de ratón (invocados por input_handle_event) ── */

void on_mouse_wheel(Editor *e, SDL_Event *ev) {
    float cursor_xf = 0.0f, cursor_yf = 0.0f;
    SDL_GetMouseState(&cursor_xf, &cursor_yf);
    int cursor_x = (int)cursor_xf;

    if (e->ftree.open && cursor_x < get_left_offset(e)) {
        /* la rueda sobre el panel desplaza el árbol */
        e->ftree.scroll -= (int)(ev->wheel.y * SCROLL_LINES_PER_NOTCH);
        if (e->ftree.scroll < 0) e->ftree.scroll = 0;
        e->needs_redraw = 1;
    } else {
        handle_scroll(e, ev->wheel.y);
    }
}

/** Actualiza el scroll vertical mientras se arrastra el thumb de la scrollbar. */
static void drag_scrollbar(Editor *e, int mouse_y) {
    if (e->tab_count == 0) return;
    int text_height = e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT - SHORTCUT_HEIGHT;
    int total_lines = buf_line_count(e->buf);
    int visible_lines = text_height / LINE_HEIGHT;
    int max_scroll = total_lines - visible_lines;
    if (max_scroll < 0) max_scroll = 0;

    float thumb_ratio =
        (visible_lines > 0 && total_lines > 0) ? (float)visible_lines / (float)total_lines : 1.0f;
    int thumb_h = (int)(text_height * thumb_ratio);
    if (thumb_h < HIT_SB_MIN_THUMB_H) thumb_h = HIT_SB_MIN_THUMB_H;
    int thumb_range = text_height - thumb_h;
    if (thumb_range < 1) thumb_range = 1;

    float frac = (float)(mouse_y - e->scrollbar_drag_start_y) / (float)thumb_range;
    int new_scroll = e->scrollbar_drag_start_line + (int)(frac * max_scroll);
    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > max_scroll) new_scroll = max_scroll;
    e->scroll_line = new_scroll;
    e->needs_redraw = 1;
}

void on_mouse_motion(Editor *e, SDL_Event *ev) {
    int mouse_x = (int)ev->motion.x;
    int mouse_y = (int)ev->motion.y;

    if (e->menu_open) {
        int prev = e->menu_hovered;
        e->menu_hovered = menu_item_at(mouse_x, mouse_y);
        if (e->menu_hovered != prev) e->needs_redraw = 1;
    }

    if (e->ftree.open && mouse_x < e->ftree.width && mouse_y >= NAVBAR_HEIGHT + TAB_BAR_HEIGHT)
        handle_ftree_hover(e, mouse_x, mouse_y);

    if (e->ftree.dragging_border) {
        int new_w = e->ftree.drag_start_w + (mouse_x - e->ftree.drag_start_x);
        if (new_w < FTREE_MIN_WIDTH) new_w = FTREE_MIN_WIDTH;
        if (new_w > e->win_w / 2) new_w = e->win_w / 2;
        e->ftree.width = new_w;
        e->needs_redraw = 1;
    }

    if (e->scrollbar_dragging) {
        drag_scrollbar(e, mouse_y);
        return;
    }

    /* arrastre para seleccionar texto (el ancla se fijó en BUTTON_DOWN) */
    if (e->mouse_selecting && e->tab_count > 0) {
        int line, col;
        point_to_line_col(e, mouse_x, mouse_y, &line, &col);
        e->sel_active = 1;
        buf_move_to(e->buf, editor_pos_from_line_col(e, line, col));
        editor_sync_cursor(e);
        editor_ensure_visible(e);
        e->needs_redraw = 1;
    }
}

/** Clic en la barra de pestañas: nueva pestaña, cerrar o cambiar de pestaña. */
static void click_tabbar(Editor *e, int mx, int my) {
    if (mx >= e->tab_new_btn_x && mx < e->tab_new_btn_x + HIT_NEW_TAB_BTN_W) {
        editor_tab_new(e);
        return;
    }
    for (int i = 0; i < e->tab_count; i++) {
        EditorTab *t = &e->tabs[i];
        if (mx < t->tab_x || mx >= t->tab_x + t->tab_w) continue;

        int on_close = (mx >= t->close_x && mx < t->close_x + HIT_TAB_CLOSE_W && my >= t->close_y &&
                        my < t->close_y + HIT_TAB_CLOSE_W);
        if (on_close) {
            editor_tab_save_state(e);
            e->active_tab = i;
            editor_tab_close(e);
        } else {
            editor_tab_switch(e, i);
        }
        const char *path = (e->tab_count > 0 && e->tabs[e->active_tab].filepath[0])
                               ? e->tabs[e->active_tab].filepath
                               : "CoffeeCode - Sin título";
        SDL_SetWindowTitle(e->window, path);
        return;
    }
}

/** Procesa un clic sobre la barra de búsqueda. Devuelve 1 si lo consumió. */
static int click_find_bar(Editor *e, int mx, int my) {
    FindBar *fb = &e->find;
    if (!fb->visible) return 0;

    if (mx >= fb->replace_btn_x && mx < fb->replace_btn_x + fb->replace_btn_w &&
        my >= fb->replace_btn_y && my < fb->replace_btn_y + fb->replace_btn_h) {
        fb->bar_focused = 1;
        do_replace(e);
        return 1;
    }
    if (fb->prev_btn_w > 0 && mx >= fb->prev_btn_x && mx < fb->prev_btn_x + fb->prev_btn_w &&
        my >= fb->prev_btn_y && my < fb->prev_btn_y + fb->prev_btn_h) {
        fb->bar_focused = 1;
        find_prev(e);
        return 1;
    }
    if (fb->next_btn_w > 0 && mx >= fb->next_btn_x && mx < fb->next_btn_x + fb->next_btn_w &&
        my >= fb->next_btn_y && my < fb->next_btn_y + fb->next_btn_h) {
        fb->bar_focused = 1;
        find_jump(e);
        return 1;
    }
    if (mx >= fb->field_x && mx < fb->bar_x + fb->bar_w && my >= fb->row1_y &&
        my < fb->row1_y + fb->field_h) {
        fb->replace_focused = 0;
        fb->bar_focused = 1;
        e->needs_redraw = 1;
        return 1;
    }
    if (mx >= fb->field_x && mx < fb->bar_x + fb->bar_w && my >= fb->row2_y &&
        my < fb->row2_y + fb->field_h) {
        fb->replace_focused = 1;
        fb->bar_focused = 1;
        e->needs_redraw = 1;
        return 1;
    }
    if (mx >= fb->bar_x && mx < fb->bar_x + fb->bar_w && my >= fb->bar_y &&
        my < fb->bar_y + fb->bar_h) {
        return 1; /* clic dentro de la barra pero fuera de campos: lo consume */
    }
    if (fb->bar_focused) { /* clic fuera: el foco vuelve al editor */
        fb->bar_focused = 0;
        e->needs_redraw = 1;
    }
    return 0;
}

/** Comienza una selección de texto con el ratón en (mx, my). */
static void start_text_selection(Editor *e, int mx, int my) {
    if (e->tab_count == 0) return;
    int line, col;
    point_to_line_col(e, mx, my, &line, &col);
    editor_sel_clear(e);
    e->sel_anchor_line = line; /* ancla ANTES de mover el cursor */
    e->sel_anchor_col = col;
    e->mouse_selecting = 1;
    buf_move_to(e->buf, editor_pos_from_line_col(e, line, col));
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

void on_mouse_button_down(Editor *e, SDL_Event *ev) {
    int mx = (int)ev->button.x;
    int my = (int)ev->button.y;
    if (ev->button.button != SDL_BUTTON_LEFT) return;

    /* Barra de pestañas */
    if (my >= NAVBAR_HEIGHT && my < NAVBAR_HEIGHT + TAB_BAR_HEIGHT) {
        click_tabbar(e, mx, my);
        return;
    }

    /* Menú "Archivo" abierto */
    if (e->menu_open) {
        int item = menu_item_at(mx, my);
        if (item >= 0)
            menu_exec(e, item);
        else {
            e->menu_open = 0;
            e->menu_hovered = -1;
            e->needs_redraw = 1;
        }
        return;
    }

    /* Botón "Archivo" en la navbar */
    if (my >= 0 && my < NAVBAR_HEIGHT && mx >= BTN_FILE_X && mx < BTN_FILE_X + BTN_FILE_W) {
        e->menu_open = 1;
        e->menu_hovered = -1;
        e->needs_redraw = 1;
        return;
    }

    /* Barra de búsqueda */
    if (click_find_bar(e, mx, my)) return;

    /* Área principal (bajo navbar + pestañas) */
    if (my < NAVBAR_HEIGHT + TAB_BAR_HEIGHT) return;

    /* Clic en la scrollbar: iniciar arrastre del thumb */
    if (mx >= e->win_w - HIT_SCROLLBAR_X) {
        e->scrollbar_dragging = 1;
        e->scrollbar_drag_start_y = my;
        e->scrollbar_drag_start_line = e->scroll_line;
        return;
    }

    if (mx < get_left_offset(e))
        handle_ftree_click(e, mx, my);
    else
        start_text_selection(e, mx, my);
}
