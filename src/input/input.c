/**
 * @file input.c
 * @brief Despachador de eventos de entrada y manejadores de teclado/texto.
 *        Los manejadores de ratón están en input_mouse.c.
 *
 * @note SDL para recién llegados. SDL entrega la interacción del usuario como
 * una cola de @c SDL_Event, que es una *unión*: un mismo objeto contiene todos
 * los tipos de evento posibles, y el campo @c ev->type indica cuál es válido.
 * Según el tipo se lee un miembro distinto (@c ev->key, @c ev->text,
 * @c ev->button...). Para el texto, SDL separa dos eventos de teclado:
 *   - @c SDL_EVENT_KEY_DOWN: tecla *cruda* (scancode/keycode), p. ej. "se pulsó
 *     la flecha izquierda" o "se pulsó la A". Sirve para atajos y navegación.
 *   - @c SDL_EVENT_TEXT_INPUT: texto *ya compuesto* (un string UTF-8), p. ej.
 * el resultado de teclear una tecla muerta + vocal para obtener "á", o lo que
 *     produce un IME (chino/japonés). Sirve para insertar caracteres.
 * Se separan porque una pulsación física no equivale a un carácter: hay teclas
 * sin texto (flechas) y caracteres que requieren varias pulsaciones. Para los
 * modificadores se usa @c SDL_GetModState (estado actual de Ctrl/Shift/Alt).
 */
#include "input_internal.h"

/* ── Edición de los campos de la barra de búsqueda ──────────────────────────
 */

/**
 * @brief Vista mutable de un campo de texto de la barra (query o replace).
 *
 * La barra de búsqueda tiene dos campos (el patrón a buscar y el texto de
 * reemplazo), cada uno con su buffer, su longitud y su rango de selección.
 * En vez de duplicar la lógica de edición para los dos, esta estructura agrupa
 * *punteros* a los campos del que tenga el foco, y los helpers @c field_*
 * operan sobre ella indistintamente. Como guarda punteros, escribir a través de
 * ::FindField modifica directamente el ::FindBar real.
 */
typedef struct {
    char *text;     /**< Buffer de caracteres del campo (terminado en '\0'). */
    int *len;       /**< Longitud actual del texto (sin contar el '\0'). */
    int *sel_start; /**< Inicio de la selección dentro del campo, o -1 si no
                       hay. */
    int *sel_end;   /**< Fin (exclusivo) de la selección, o -1 si no hay. */
} FindField;

/**
 * @brief Devuelve el campo de la barra que tiene el foco (replace o query).
 *
 * @param f Barra de búsqueda de la que tomar el campo enfocado.
 * @return Una ::FindField apuntando al campo "replace" si @c replace_focused
 *         está activo, o al campo "query" en caso contrario.
 */
static FindField find_active_field(FindBar *f) {
    if (f->replace_focused)
        return (FindField){f->replace, &f->replace_len, &f->replace_sel_start,
                           &f->replace_sel_end};
    return (FindField){f->query, &f->query_len, &f->query_sel_start,
                       &f->query_sel_end};
}

/**
 * @brief Borra el tramo seleccionado del campo (y limpia la selección).
 *
 * Si hay un rango válido [sel_start, sel_end), lo elimina desplazando con
 * @c memmove el resto del texto sobre el hueco (incluyendo el '\0' final, de
 * ahí el "+1"). Tras borrar, deja la selección en -1/-1 (sin selección).
 *
 * @param fld Campo sobre el que actuar.
 */
static void field_delete_selection(FindField *fld) {
    if (*fld->sel_start >= 0 && *fld->sel_end > *fld->sel_start) {
        int start = *fld->sel_start;
        int n = *fld->sel_end - start; /* nº de caracteres a borrar */
        /* mover la cola sobre el hueco; el +1 arrastra también el '\0' final */
        memmove(fld->text + start, fld->text + *fld->sel_end,
                (size_t)(*fld->len - *fld->sel_end) + 1);
        *fld->len -= n;
    }
    *fld->sel_start = -1; /* ya no hay selección */
    *fld->sel_end = -1;
}

/**
 * @brief Inserta @p n caracteres de @p text al final del campo (con límite).
 *
 * Añade carácter a carácter mientras quede sitio (deja espacio para el '\0'),
 * descartando lo que exceda @c FIND_BAR_MAX. Mantiene el campo terminado en
 * '\0'.
 *
 * @param fld  Campo destino.
 * @param text Caracteres a insertar (no necesita estar terminado en '\0').
 * @param n    Cuántos caracteres de @p text insertar.
 */
static void field_insert(FindField *fld, const char *text, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (*fld->len < FIND_BAR_MAX - 1) { /* dejar hueco para el '\0' */
            fld->text[(*fld->len)++] = text[i];
            fld->text[*fld->len] = '\0';
        }
    }
}

/**
 * @brief Borra hacia atrás: la selección si la hay, o el último carácter.
 *
 * @param fld Campo sobre el que actuar.
 */
static void field_backspace(FindField *fld) {
    if (*fld->sel_start >= 0 && *fld->sel_end > *fld->sel_start)
        field_delete_selection(fld); /* hay selección: bórrala entera */
    else if (*fld->len > 0)
        fld->text[--(*fld->len)] = '\0'; /* si no, quita el último carácter */
}

/**
 * @brief Selecciona todo el contenido del campo.
 *
 * @param fld Campo sobre el que actuar (no hace nada si está vacío).
 */
static void field_select_all(FindField *fld) {
    if (*fld->len > 0) {
        *fld->sel_start = 0;
        *fld->sel_end = *fld->len;
    }
}

/**
 * @brief Inserta @p text en el editor en la posición del cursor.
 *
 * Camino normal de inserción de texto en el documento: escribe en el gap
 * buffer, re-sincroniza el cursor (línea/columna), marca sucio el lexer desde
 * la línea editada, reencuadra el scroll y activa los flags de
 * modificado/redibujar.
 *
 * @param e    Editor destino.
 * @param text Texto (UTF-8 terminado en '\0') a insertar. Si no hay pestañas
 *             abiertas no hace nada.
 */
static void editor_insert_text(Editor *e, const char *text) {
    if (e->tab_count == 0) return; /* sin documento abierto, nada que editar */
    buf_insert_str(e->buf, text, strlen(text));
    editor_sync_cursor(e); /* recalcular (línea, columna) del cursor */
    editor_update_lexer(
        e, e->cursor_line);   /* re-tokenizar desde la línea editada */
    editor_ensure_visible(e); /* asegurar que el cursor sigue visible */
    e->modified = 1;
    e->needs_redraw = 1;
}

/* ── Manejadores ────────────────────────────────────────────────────────────
 */

/**
 * @brief Maneja un evento @c SDL_EVENT_TEXT_INPUT (texto ya compuesto).
 *
 * Este evento entrega caracteres listos para insertar (incluidos acentos e IME)
 * en @c ev->text.text. Se ignora si hay un menú abierto o si @p ctrl está
 * pulsado (los combos Ctrl+letra son atajos, no texto, y se tratan en
 * KEY_DOWN). Si la barra de búsqueda está visible y enfocada, el texto va al
 * campo activo de la barra; en caso contrario, al documento.
 *
 * @param e    Editor destino.
 * @param ev   Evento de texto (se lee @c ev->text.text).
 * @param ctrl 1 si Ctrl está pulsado en este momento (entonces se ignora).
 */
static void on_text_input(Editor *e, SDL_Event *ev, int ctrl) {
    if (e->settings_open) return; /* preferencias: no se escribe en el buffer */
    if (e->menu_open) return;
    if (ctrl) return; /* ignorar combos Ctrl+letra (ej. Ctrl+C) */

    /* Barra visible pero sin foco: el texto va al editor */
    if (e->find.visible && e->find.bar_focused) {
        FindField fld = find_active_field(&e->find);
        field_delete_selection(&fld); /* sobrescribe la selección */
        field_insert(&fld, ev->text.text, strlen(ev->text.text));
        if (!e->find.replace_focused)
            find_first(e); /* re-buscar al cambiar la query */
        else
            e->needs_redraw = 1;
        return;
    }

    editor_insert_text(e, ev->text.text);
}

/**
 * @brief Procesa las teclas mientras la barra de búsqueda tiene el foco.
 *
 * Filtra/consume las pulsaciones cuando el usuario está escribiendo en la
 * barra: Tab alterna entre query y replace; Enter busca/reemplaza; Retroceso
 * borra; Ctrl+F salta al siguiente; Ctrl+A selecciona todo. Mientras la barra
 * tiene foco se "tragan" el resto de teclas para que no lleguen al documento.
 *
 * @param e     Editor (su @c e->find guarda el estado de la barra).
 * @param key   Keycode de la tecla pulsada (SDLK_*).
 * @param ctrl  1 si Ctrl está pulsado.
 * @param shift 1 si Shift está pulsado.
 * @return 1 si consumió la tecla, 0 para dejarla caer al editor.
 */
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
        /* al cambiar de campo se descartan las selecciones de ambos */
        e->find.query_sel_start = e->find.query_sel_end = -1;
        e->find.replace_sel_start = e->find.replace_sel_end = -1;
        e->find.replace_focused = !e->find.replace_focused;
        e->needs_redraw = 1;
        return 1;
    case SDLK_RETURN:   /* Enter del teclado principal */
    case SDLK_KP_ENTER: /* Enter del teclado numérico */
        if (e->find.replace_focused)
            do_replace(e); /* en el campo replace: reemplazar */
        else if (shift)
            find_prev(e); /* Shift+Enter: resultado anterior */
        else
            find_jump(e); /* Enter: siguiente resultado */
        return 1;
    case SDLK_BACKSPACE: {
        FindField fld = find_active_field(&e->find);
        field_backspace(&fld);
        if (!e->find.replace_focused)
            find_first(e); /* cambió la query: re-buscar */
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

/**
 * @brief Shift+Tab: quita hasta TAB_SIZE espacios al inicio de la línea actual.
 *
 * Recorre el inicio de la línea borrando espacios uno a uno (registrando cada
 * borrado en el undo) hasta TAB_SIZE o hasta encontrar un no-espacio. Solo
 * refresca el estado si llegó a borrar algo.
 *
 * @param e Editor.
 */
static void dedent_line(Editor *e) {
    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    int removed = 0;
    for (int i = 0; i < e->settings.tab_width; i++) {
        if (buf_char_at(e->buf, line_start) != ' ') break; /* solo espacios */
        char space = ' ';
        editor_undo_push_delete(e, line_start, &space,
                                1); /* registrar para undo */
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

/**
 * @brief Líneas visibles del área de texto (para Re Pág / Av Pág).
 *
 * @param e Editor (usa el alto de ventana y las zonas de UI).
 * @return Número aproximado de líneas que caben en el área de edición.
 */
static int page_lines(Editor *e) {
    return (e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT) / e->line_height;
}

/**
 * @brief Atajos con Ctrl pulsado (archivo, edición, navegación).
 *
 * Despacha los atajos Ctrl+tecla. Se divide en dos switch: el primero atiende
 * acciones que pueden funcionar sin documento abierto (nuevo/abrir/guardar/
 * cerrar/buscar/barra lateral/cambiar pestaña); si tras él no hay buffer activo
 * se vuelve, y el segundo switch cubre lo que requiere texto (undo, copiar,
 * mover, etc.).
 *
 * @param e     Editor.
 * @param key   Keycode de la tecla pulsada junto a Ctrl.
 * @param shift 1 si además Shift está pulsado (amplía selección en
 * movimientos).
 */
static void ctrl_key(Editor *e, SDL_Keycode key, int shift) {
    switch (key) {
    case SDLK_N: new_file(e); break;
    case SDLK_O: open_file_dialog(e); break;
    case SDLK_K: open_folder_dialog(e); break;
    case SDLK_S: save_file(e); break;
    case SDLK_Q: e->running = 0; break; /* salir del bucle principal */
    case SDLK_COMMA:                    /* Ctrl+, abre las preferencias */
        e->settings_open = 1;
        e->needs_redraw = 1;
        break;
    case SDLK_F: open_find_bar(e); break;
    case SDLK_B: toggle_sidebar(e); break;
    case SDLK_W: editor_tab_close(e); break;
    case SDLK_TAB: /* Ctrl+Tab: pasar a la siguiente pestaña (cíclico) */
        if (e->tab_count > 0)
            editor_tab_switch(e, (e->active_tab + 1) % e->tab_count);
        break;
    case SDLK_BACKSLASH:
        /* Ctrl+\: dividir la hoja enfocada en vertical (lado a lado).
         * Ctrl+Shift+\: dividirla en horizontal (arriba/abajo). */
        editor_split_dir(e, shift ? DOCK_HORIZONTAL : DOCK_VERTICAL);
        break;
    case SDLK_C:
        /* Ctrl+C con el panel inferior enfocado: copiar su canal (funciona
         * aunque no haya ningun archivo abierto). */
        if (e->bottom_panel_open && e->bottom_focused) {
            do_copy(e);
            return;
        }
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
    case SDLK_HOME:
        move_cursor_select(e, 0, 0, shift);
        break;       /* Ctrl+Inicio: al doc */
    case SDLK_END: { /* Ctrl+Fin: fin del doc */
        int last = buf_line_count(e->buf) - 1;
        size_t start = editor_pos_from_line_col(e, last, 0);
        int col = (int)(buf_line_end(e->buf, start) -
                        start); /* columna del fin de línea */
        move_cursor_select(e, last, col, shift);
        break;
    }
    case SDLK_LEFT:
        move_word_left(e, shift);
        break; /* Ctrl+Left: palabra anterior */
    case SDLK_RIGHT:
        move_word_right(e, shift);
        break; /* Ctrl+Right: palabra siguiente */
    case SDLK_UP:
        move_cursor_select(e, e->cursor_line - 5, e->cursor_col, shift);
        break;
    case SDLK_DOWN:
        move_cursor_select(e, e->cursor_line + 5, e->cursor_col, shift);
        break;
    default: break;
    }
}

/**
 * @brief Teclas de edición y navegación sin Ctrl.
 *
 * Maneja flechas, Inicio/Fin, Enter, Tab/Shift+Tab, Retroceso/Supr y Re/Av Pág.
 * No hace nada si hay un menú abierto o no hay buffer activo.
 *
 * @param e     Editor.
 * @param key   Keycode de la tecla pulsada.
 * @param shift 1 si Shift está pulsado (amplía la selección en los
 * movimientos).
 */
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
            dedent_line(e); /* Shift+Tab: quitar sangría */
        else
            insert_tab(e); /* Tab: añadir sangría */
        break;
    case SDLK_BACKSPACE: do_backspace(e); break;
    case SDLK_DELETE: do_delete(e); break;
    case SDLK_PAGEUP: /* Re Pág: subir una pantalla de líneas */
        move_cursor_select(e, e->cursor_line - page_lines(e), e->cursor_col,
                           shift);
        break;
    case SDLK_PAGEDOWN: /* Av Pág: bajar una pantalla de líneas */
        move_cursor_select(e, e->cursor_line + page_lines(e), e->cursor_col,
                           shift);
        break;
    default: break;
    }
}

/**
 * @brief Maneja un evento @c SDL_EVENT_KEY_DOWN (tecla cruda pulsada).
 *
 * Orquesta el reparto del teclado: Escape cierra (en orden) la barra de
 * búsqueda, el menú o la selección; si la barra está visible y consume la
 * tecla, termina; si no, según haya Ctrl o no, se va a los atajos (::ctrl_key)
 * o a la edición/navegación (::edit_key).
 *
 * @param e     Editor.
 * @param ev    Evento de teclado (se lee @c ev->key.key).
 * @param ctrl  1 si Ctrl está pulsado.
 * @param shift 1 si Shift está pulsado.
 */
static void on_key_down(Editor *e, SDL_Event *ev, int ctrl, int shift) {
    SDL_Keycode key = ev->key.key; /* tecla lógica (keycode) de la pulsación */

    /* Preferencias abiertas: pantalla modal; solo ESC la cierra. */
    if (e->settings_open) {
        if (key == SDLK_ESCAPE) {
            e->settings_open = 0;
            e->needs_redraw = 1;
        }
        return;
    }

    /* Popup de codificación abierto: ESC lo cierra. */
    if (e->enc_popup && key == SDLK_ESCAPE) {
        e->enc_popup = 0;
        e->needs_redraw = 1;
        return;
    }

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

/**
 * @brief Despachador principal: enruta un evento de SDL al manejador adecuado.
 *
 * Calcula primero el estado de los modificadores con @c SDL_GetModState (una
 * máscara de bits con Ctrl/Shift/Alt/...) y luego hace switch sobre @c
 * ev->type. Recuerda que @c SDL_Event es una unión: en cada caso se accede al
 * miembro que corresponde a ese tipo (@c ev->window para resize, @c ev->button
 * para clic,
 * @c ev->text para texto, @c ev->key para teclado...).
 *
 * @param e  Editor cuyo estado se actualiza.
 * @param ev Evento de SDL ya leído de la cola.
 */
void input_handle_event(Editor *e, SDL_Event *ev) {
    SDL_Keymod mods = SDL_GetModState(); /* máscara de modificadores actuales */
    int ctrl = (mods & SDL_KMOD_CTRL) != 0;   /* ¿algún Ctrl pulsado? */
    int shift = (mods & SDL_KMOD_SHIFT) != 0; /* ¿algún Shift pulsado? */

    switch (ev->type) {
    case SDL_EVENT_QUIT: e->running = 0; break; /* cerrar la ventana → salir */
    case SDL_EVENT_WINDOW_RESIZED:
        e->win_w = ev->window.data1; /* nuevo ancho en píxeles */
        e->win_h = ev->window.data2; /* nuevo alto en píxeles */
        e->needs_redraw = 1;
        break;
    case SDL_EVENT_MOUSE_WHEEL: on_mouse_wheel(e, ev); break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (ev->button.button == SDL_BUTTON_LEFT) {
            /* soltar el botón izquierdo termina cualquier arrastre en curso */
            e->ftree.dragging_border = 0;
            e->dragging_divider = DIVIDER_NONE; /* fin del arrastre de divisor */
            e->dock_drag_split = DOCK_NONE;     /* fin del arrastre de dock     */
            e->mouse_selecting = 0;
            e->scrollbar_dragging = 0;
            e->bottom_selecting = 0; /* fin de la seleccion del panel inferior */
        }
        break;
    case SDL_EVENT_MOUSE_MOTION: on_mouse_motion(e, ev); break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: on_mouse_button_down(e, ev); break;
    case SDL_EVENT_TEXT_INPUT:
        on_text_input(e, ev, ctrl);
        break; /* texto compuesto */
    case SDL_EVENT_KEY_DOWN:
        on_key_down(e, ev, ctrl, shift);
        break; /* tecla cruda */
    default: break;
    }
}
