/**
 * @file input.c
 * @brief Despachador de eventos de entrada y manejadores de teclado/texto.
 *        Los manejadores de ratón están en input_mouse.c.
 */
#include "input_internal.h"

/* ── Edición de los campos de la barra de búsqueda ────────────────────────── */

/** Vista mutable de un campo de texto de la barra (query o replace). */
typedef struct {
    char *text;
    int *len;
    int *sel_start;
    int *sel_end;
} FindField;

/** Devuelve el campo de la barra que tiene el foco (replace o query). */
static FindField find_active_field(FindBar *f) {
    if (f->replace_focused)
        return (FindField){f->replace, &f->replace_len, &f->replace_sel_start, &f->replace_sel_end};
    return (FindField){f->query, &f->query_len, &f->query_sel_start, &f->query_sel_end};
}

/** Borra el tramo seleccionado del campo (y limpia la selección). */
static void field_delete_selection(FindField *fld) {
    if (*fld->sel_start >= 0 && *fld->sel_end > *fld->sel_start) {
        int start = *fld->sel_start;
        int n = *fld->sel_end - start;
        memmove(fld->text + start, fld->text + *fld->sel_end,
                (size_t)(*fld->len - *fld->sel_end) + 1);
        *fld->len -= n;
    }
    *fld->sel_start = -1;
    *fld->sel_end = -1;
}

/** Inserta @p n caracteres de @p text al final del campo (con límite). */
static void field_insert(FindField *fld, const char *text, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (*fld->len < FIND_BAR_MAX - 1) {
            fld->text[(*fld->len)++] = text[i];
            fld->text[*fld->len] = '\0';
        }
    }
}

/** Borra hacia atrás: la selección si la hay, o el último carácter. */
static void field_backspace(FindField *fld) {
    if (*fld->sel_start >= 0 && *fld->sel_end > *fld->sel_start)
        field_delete_selection(fld);
    else if (*fld->len > 0)
        fld->text[--(*fld->len)] = '\0';
}

/** Selecciona todo el contenido del campo. */
static void field_select_all(FindField *fld) {
    if (*fld->len > 0) {
        *fld->sel_start = 0;
        *fld->sel_end = *fld->len;
    }
}

/** Inserta @p text en el editor en la posición del cursor. */
static void editor_insert_text(Editor *e, const char *text) {
    if (e->tab_count == 0) return;
    buf_insert_str(e->buf, text, strlen(text));
    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
}

/* ── Manejadores ──────────────────────────────────────────────────────────── */

static void on_text_input(Editor *e, SDL_Event *ev, int ctrl) {
    if (e->menu_open) return;
    if (ctrl) return; /* ignorar combos Ctrl+letra (ej. Ctrl+C) */

    /* Barra visible pero sin foco: el texto va al editor */
    if (e->find.visible && e->find.bar_focused) {
        FindField fld = find_active_field(&e->find);
        field_delete_selection(&fld);
        field_insert(&fld, ev->text.text, strlen(ev->text.text));
        if (!e->find.replace_focused)
            find_first(e); /* re-buscar al cambiar la query */
        else
            e->needs_redraw = 1;
        return;
    }

    editor_insert_text(e, ev->text.text);
}

/** Procesa las teclas mientras la barra de búsqueda tiene el foco.
 *  @return 1 si consumió la tecla, 0 para dejarla caer al editor. */
static int find_bar_key(Editor *e, SDL_Keycode key, int ctrl, int shift) {
    if (!e->find.bar_focused) {
        if (ctrl && key == SDLK_F) { /* Ctrl+F re-enfoca la barra */
            e->find.bar_focused = 1;
            e->needs_redraw = 1;
            return 1;
        }
        return 0; /* sin foco: las teclas van al editor */
    }

    switch (key) {
    case SDLK_TAB: /* alternar campo */
        e->find.query_sel_start = e->find.query_sel_end = -1;
        e->find.replace_sel_start = e->find.replace_sel_end = -1;
        e->find.replace_focused = !e->find.replace_focused;
        e->needs_redraw = 1;
        return 1;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (e->find.replace_focused)
            do_replace(e);
        else if (shift)
            find_prev(e);
        else
            find_jump(e);
        return 1;
    case SDLK_BACKSPACE: {
        FindField fld = find_active_field(&e->find);
        field_backspace(&fld);
        if (!e->find.replace_focused)
            find_first(e);
        else
            e->needs_redraw = 1;
        return 1;
    }
    default: break;
    }

    if (ctrl && key == SDLK_F) { /* Ctrl+F repetido = siguiente resultado */
        find_jump(e);
        return 1;
    }
    if (ctrl && key == SDLK_A) { /* seleccionar todo el campo */
        FindField fld = find_active_field(&e->find);
        field_select_all(&fld);
        e->needs_redraw = 1;
        return 1;
    }
    return 1; /* resto de teclas ignoradas mientras la barra tiene foco */
}

/** Shift+Tab: quita hasta TAB_SIZE espacios al inicio de la línea actual. */
static void dedent_line(Editor *e) {
    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    int removed = 0;
    for (int i = 0; i < TAB_SIZE; i++) {
        if (buf_char_at(e->buf, line_start) != ' ') break;
        char space = ' ';
        editor_undo_push_delete(e, line_start, &space, 1);
        buf_delete_range(e->buf, line_start, line_start + 1);
        removed++;
    }
    if (removed) {
        buf_move_to(e->buf, line_start);
        editor_sync_cursor(e);
        editor_update_lexer(e, e->cursor_line);
        editor_ensure_visible(e);
        e->modified = 1;
        e->needs_redraw = 1;
    }
}

/** Líneas visibles del área de texto (para Re Pág / Av Pág). */
static int page_lines(Editor *e) {
    return (e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT) / LINE_HEIGHT;
}

/** Atajos con Ctrl pulsado (archivo, edición, navegación). */
static void ctrl_key(Editor *e, SDL_Keycode key, int shift) {
    switch (key) {
    case SDLK_N: new_file(e); break;
    case SDLK_O: open_file_dialog(e); break;
    case SDLK_K: open_folder_dialog(e); break;
    case SDLK_S: save_file(e); break;
    case SDLK_Q: e->running = 0; break;
    case SDLK_F: open_find_bar(e); break;
    case SDLK_B: toggle_sidebar(e); break;
    case SDLK_W: editor_tab_close(e); break;
    case SDLK_TAB:
        if (e->tab_count > 0) editor_tab_switch(e, (e->active_tab + 1) % e->tab_count);
        break;
    default: break;
    }
    if (!e->buf) return; /* el resto requiere un buffer activo */
    switch (key) {
    case SDLK_Z: editor_undo(e); break;
    case SDLK_Y: editor_redo(e); break;
    case SDLK_A: select_all(e); break;
    case SDLK_C: do_copy(e); break;
    case SDLK_X: do_cut(e); break;
    case SDLK_V: do_paste(e); break;
    case SDLK_D: duplicate_line(e); break;
    case SDLK_SLASH: toggle_line_comment(e); break;
    case SDLK_L: select_line(e); break;
    case SDLK_HOME: move_cursor_select(e, 0, 0, shift); break;
    case SDLK_END: {
        int last = buf_line_count(e->buf) - 1;
        size_t start = editor_pos_from_line_col(e, last, 0);
        int col = (int)(buf_line_end(e->buf, start) - start);
        move_cursor_select(e, last, col, shift);
        break;
    }
    case SDLK_LEFT: move_word_left(e, shift); break;
    case SDLK_RIGHT: move_word_right(e, shift); break;
    case SDLK_UP: move_cursor_select(e, e->cursor_line - 5, e->cursor_col, shift); break;
    case SDLK_DOWN: move_cursor_select(e, e->cursor_line + 5, e->cursor_col, shift); break;
    default: break;
    }
}

/** Teclas de edición y navegación sin Ctrl. */
static void edit_key(Editor *e, SDL_Keycode key, int shift) {
    if (e->menu_open || !e->buf) return;
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
        if (shift)
            dedent_line(e);
        else
            insert_tab(e);
        break;
    case SDLK_BACKSPACE: do_backspace(e); break;
    case SDLK_DELETE: do_delete(e); break;
    case SDLK_PAGEUP:
        move_cursor_select(e, e->cursor_line - page_lines(e), e->cursor_col, shift);
        break;
    case SDLK_PAGEDOWN:
        move_cursor_select(e, e->cursor_line + page_lines(e), e->cursor_col, shift);
        break;
    default: break;
    }
}

static void on_key_down(Editor *e, SDL_Event *ev, int ctrl, int shift) {
    SDL_Keycode key = ev->key.key;

    if (key == SDLK_ESCAPE) { /* cierra barra / menú / selección */
        if (e->find.visible)
            close_find_bar(e);
        else if (e->menu_open) {
            e->menu_open = 0;
            e->menu_hovered = -1;
            e->needs_redraw = 1;
        } else {
            editor_sel_clear(e);
            e->needs_redraw = 1;
        }
        return;
    }

    /* Si la barra de búsqueda consume la tecla, terminar */
    if (e->find.visible && find_bar_key(e, key, ctrl, shift)) return;

    if (ctrl)
        ctrl_key(e, key, shift);
    else
        edit_key(e, key, shift);
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
