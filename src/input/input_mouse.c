/**
 * @file input_mouse.c
 * @brief Entrada de ratón: clics, hover, scroll y arrastres (panel, scrollbar,
 *        selección de texto). Incluye los hit-tests de la UI.
 *
 * @note Eventos de ratón en SDL3. SDL entrega los movimientos y pulsaciones del
 * ratón como eventos en la cola: @c SDL_EVENT_MOUSE_BUTTON_DOWN (se aprieta un
 * botón), @c SDL_EVENT_MOUSE_BUTTON_UP (se suelta), @c SDL_EVENT_MOUSE_MOTION
 * (el cursor se mueve, llega de forma continua) y @c SDL_EVENT_MOUSE_WHEEL
 * (gira la rueda). Cada evento trae las coordenadas en PÍXELES relativas a la
 * esquina superior izquierda de la ventana (origen 0,0 arriba-izquierda, y
 * crece hacia abajo). Este módulo recibe esos píxeles y debe averiguar SOBRE
 * QUÉ elemento de la interfaz cayó el clic (eso es el "hit-testing": comparar
 * el punto contra los rectángulos de cada zona de la UI) y, en el área de
 * texto, traducir el píxel a una posición lógica (línea, columna) del
 * documento. El despacho de eventos lo hace input_handle_event, que llama a las
 * funciones @c on_mouse_* de aquí.
 */
#include "input_internal.h"

#define FALLBACK_CHAR_W 8 /* ancho de carácter por defecto              */
/* líneas desplazadas por "muesca" de rueda */
#define SCROLL_LINES_PER_NOTCH 3

/* Única medida fija que aún necesita el input para el hit-test (el resto de la
 * geometría de controles ya viene del registro e->ui). */
/* alto mínimo del thumb de la scrollbar */
#define HIT_SB_MIN_THUMB_H 20

/* Adelanto: lo usa on_mouse_motion (arrastre de seleccion) antes de su
 * definicion, que vive junto al resto de helpers del panel inferior. */
static int bottom_offset_at(Editor *e, int mx, int my);

/**
 * @brief Offset horizontal del área de texto (tras el panel y el gutter).
 *
 * Todo lo que se dibuja a la derecha del panel lateral arranca en esta X. Si el
 * explorador está abierto, ocupa @c ftree.width píxeles; si está cerrado, solo
 * queda el botón estrecho para reabrirlo (@c FTREE_TOGGLE_BTN_W). Sirve de
 * origen común para situar el gutter, el texto, las pestañas, etc.
 *
 * @param e Editor (consulta el estado del panel lateral).
 * @return X en píxeles donde empieza la zona a la derecha del panel.
 */
int get_left_offset(Editor *e) {
    /* Con el editor dividido, el origen del área de texto es el del panel que se
     * está procesando (lo fija el llamante en e->pane_left con pane_active=1).
     * Sin división (pane_active==0) se usa el origen global de siempre. */
    if (e->pane_active) return e->pane_left;
    /* panel abierto: su ancho actual */
    if (e->ftree.open) return e->ftree.width;
    return FTREE_TOGGLE_BTN_W; /* panel cerrado: solo el botón   */
}

/* ── División del editor (split panes): geometría e hit-test ────────────────
 */

/**
 * @brief Banda horizontal [left, right) del área del editor (entre el explorador
 *        y el panel de extensiones).  Igual que la del panel inferior.
 */
static void editor_area_h(Editor *e, int *left, int *right) {
    *left = get_left_offset(e);
    *right = e->win_w - (e->ext_panel_open ? e->ext_panel_w : 0);
}

/**
 * @brief Grupo del editor dividido bajo la X del cursor (0 = izquierda, 1 =
 *        derecha), o -1 si el editor no está dividido.
 *
 * Usa split_x como frontera.  No comprueba la Y (el llamante ya sabe que el clic
 * cae en el área del editor).
 */
static int editor_group_at_x(Editor *e, int mx) {
    if (e->group_count != 2) return -1;
    return (mx < e->split_x) ? 0 : 1;
}

/**
 * @brief Fija el override de área (e->pane_*) al sub-rect del grupo @p g.
 *
 * Lo usa el input para que get_left_offset/point_to_line_col operen sobre el
 * panel correcto al mapear un clic.  El llamante debe limpiar pane_active tras
 * usarlo (clear_pane_override).
 */
static void set_pane_override(Editor *e, int g) {
    int left, right;
    /* pane_active=0 aquí para que editor_area_h use el origen global */
    e->pane_active = 0;
    editor_area_h(e, &left, &right);
    int px = (g == 0) ? left : e->split_x;
    int pr = (g == 0) ? e->split_x : right;
    e->pane_active = 1;
    e->pane_left = px;
    e->pane_top = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    e->pane_width = pr - px;
    e->pane_height = 0; /* el input no necesita el alto */
}

/** Limpia el override de área tras un mapeo de clic. */
static void clear_pane_override(Editor *e) { e->pane_active = 0; }

/* ── Divisores arrastrables (redimension de paneles) ────────────────────────
 */

/**
 * @brief Aplica el cursor del sistema de redimension (o lo restaura).
 *
 * Cachea el cursor de redimension horizontal (EW) y el normal en estaticos para
 * no recrearlos en cada movimiento.  Si SDL no puede crear el cursor del sistema
 * (entorno sin tema de cursores, etc.), degrada sin tocar el cursor: nunca
 * crashea.
 *
 * @param want_resize 1 para mostrar el cursor de redimension, 0 para el normal.
 */
static void set_divider_cursor(int which) {
    static SDL_Cursor *cur_ew = NULL;    /* cursor de redimension horizontal */
    static SDL_Cursor *cur_ns = NULL;    /* cursor de redimension vertical   */
    static SDL_Cursor *cur_arrow = NULL; /* cursor normal (flecha)           */
    static int tried = 0;                /* ya se intento crear (evita reintentos) */

    if (!tried) { /* crear una sola vez, perezosamente */
        tried = 1;
        cur_ew = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
        cur_ns = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
        cur_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    }

    /* El divisor inferior es horizontal -> cursor NS; los demas son verticales
     * -> cursor EW; DIVIDER_NONE -> flecha normal. */
    SDL_Cursor *target = cur_arrow;
    if (which == DIVIDER_BOTTOM_TOP)
        target = cur_ns;
    else if (which != DIVIDER_NONE)
        target = cur_ew;
    if (target) SDL_SetCursor(target); /* solo si SDL pudo crearlo */
}

/**
 * @brief Actualiza el divisor bajo el cursor y conmuta su cursor del sistema.
 *
 * Pura consulta de hover: no inicia arrastre.  Pide redibujar solo si cambio el
 * resaltado.  No hace nada mientras hay un arrastre en curso (ese caso lo lleva
 * on_mouse_motion).
 *
 * @param e  Editor.
 * @param mx X del raton en pixeles.
 * @param my Y del raton en pixeles.
 */
static void update_divider_hover(Editor *e, int mx, int my) {
    int hit = layout_hit_divider(e, mx, my);
    if (hit != e->hovered_divider) { /* solo trabajo si cambio el estado */
        e->hovered_divider = hit;
        set_divider_cursor(hit);
        e->needs_redraw = 1;
    }
}

/**
 * @brief Si (mx,my) cae sobre un divisor, inicia su arrastre y consume el clic.
 *
 * @param e  Editor.
 * @param mx X del clic en pixeles.
 * @param my Y del clic en pixeles.
 * @return 1 si empezo a arrastrar un divisor (clic consumido), 0 si no.
 */
static int try_start_divider_drag(Editor *e, int mx, int my) {
    int hit = layout_hit_divider(e, mx, my);
    if (hit == DIVIDER_NONE) return 0;
    e->dragging_divider = hit; /* entrar en modo arrastre */
    set_divider_cursor(hit);   /* mantener el cursor de redimension adecuado */
    return 1;
}

/**
 * @brief Convierte coordenadas de ratón en una posición (línea, columna) del
 *        texto, recortada al rango válido del archivo.
 *
 * Esta es la traducción clave píxel → (línea, columna), y debe deshacer todos
 * los desplazamientos que aplica el render al dibujar el texto:
 *   - VERTICAL: las primeras @c NAVBAR_HEIGHT + TAB_BAR_HEIGHT píxeles los
 * ocupan la navbar y la barra de pestañas, así que se restan. Lo que queda,
 * dividido por el alto de línea, da la fila VISIBLE (la de pantalla). Como el
 * documento está desplazado por el scroll vertical, se suma @c scroll_line para
 * pasar de fila visible a línea real del archivo.
 *   - HORIZONTAL: el texto empieza en @c text_x (tras panel + gutter +
 * padding). Se resta esa X y se vuelve a sumar el desplazamiento horizontal ya
 * en píxeles (@c scroll_col * char_px); dividir por el ancho de carácter da la
 *     columna real (la fuente es monoespaciada, por eso basta una división).
 * Tanto la línea como la columna se recortan (clamp) para no salirse del
 * archivo ni del final real de la línea (no se permite colocar el cursor "en el
 * aire").
 *
 * @param e       Editor (aporta scroll, ancho de carácter y el buffer).
 * @param mouse_x Coordenada X del ratón en píxeles (origen en la ventana).
 * @param mouse_y Coordenada Y del ratón en píxeles.
 * @param[out] line Línea real del archivo bajo el ratón (recortada).
 * @param[out] col  Columna real dentro de esa línea (recortada).
 * @pre Hay un tab activo con buffer (@c e->buf no nulo).
 */
static void point_to_line_col(Editor *e, int mouse_x, int mouse_y, int *line,
                              int *col) {
    /* X donde arranca el texto: panel lateral + gutter (números) + padding. */
    int text_x = get_left_offset(e) + editor_gutter_w(e) + PADDING_LEFT;
    /* ancho de un carácter; si por lo que sea no se midió, usar el de reserva
     */
    int char_px = (e->char_w > 0 ? e->char_w : FALLBACK_CHAR_W);

    /* fila en pantalla: quitar navbar + pestañas y dividir por el alto de línea
     */
    int visual_line =
        (mouse_y - NAVBAR_HEIGHT - TAB_BAR_HEIGHT) / e->line_height;
    if (visual_line < 0) visual_line = 0;  /* clic sobre las barras → fila 0 */
    int ln = e->scroll_line + visual_line; /* fila visible → línea real      */
    int total = buf_line_count(e->buf);
    if (ln < 0) ln = 0; /* recortar al rango del archivo  */
    if (ln >= total) ln = total - 1;

    /* columna: quitar text_x, re-añadir el scroll horizontal (en px) y dividir
     */
    int visual_col = (mouse_x - text_x + e->scroll_col * char_px) / char_px;
    if (visual_col < 0) visual_col = 0; /* clic en gutter/padding → col 0 */
    /* offset inicio línea */
    size_t line_start = editor_pos_from_line_col(e, ln, 0);
    /* longitud real de la línea en CARACTERES (no bytes), para no pasarse del
     * final: la columna del fin de línea es justo ese nº de caracteres. */
    size_t line_end = buf_line_end(e->buf, line_start);
    int dummy_line = 0, line_cols = 0;
    buf_line_col(e->buf, line_end, &dummy_line, &line_cols);
    if (visual_col > line_cols) visual_col = line_cols;

    *line = ln;
    *col = visual_col;
}

/**
 * @brief Maneja un clic dentro del panel lateral (botón toggle o entrada del
 * árbol).
 *
 * Primero comprueba si el clic cayó en el botón de abrir/cerrar (que vive en el
 * borde derecho del panel cuando está abierto, o en x=0 cuando está cerrado).
 * Si no, y el panel está abierto, calcula sobre qué FILA del árbol se hizo clic
 * (teniendo en cuenta el scroll del propio panel): una carpeta se
 * expande/colapsa, un archivo se abre en una pestaña nueva.
 *
 * @param e  Editor.
 * @param mx Coordenada X del clic en píxeles.
 * @param my Coordenada Y del clic en píxeles.
 */
void handle_ftree_click(Editor *e, int mx, int my) {
    FileTree *ft = &e->ftree;

    /* Botón toggle: su geometría la registró el render (UI_TOGGLE_TREE), así no
     * hay que recalcularla aquí ni mantenerla sincronizada con el dibujo. */
    if (ui_hit(&e->ui, UI_TOGGLE_TREE, mx, my)) {
        ft->open = !ft->open; /* alternar abierto/cerrado */
        e->needs_redraw = 1;
        return;
    }

    if (!ft->open) return; /* panel cerrado: no hay árbol donde clicar */

    /* Fila del árbol bajo el clic: su rect lo registró el render por índice de
     * array, así que ui_hit_idx devuelve directamente el índice de la entrada.
     */
    int idx = ui_hit_idx(&e->ui, UI_LIST_TREE_ROW, mx, my);
    if (idx < 0) return; /* clic en la cabecera o zona vacía */

    FEntry *en = ftree_entry(ft, idx);
    if (en->type == FTYPE_DIR) {
        ftree_toggle(ft, idx); /* carpeta: expandir/colapsar */
    } else {
        editor_tab_open(e, en->path); /* archivo: abrir en pestaña */
        /* y poner su ruta en el título */
        SDL_SetWindowTitle(e->window, en->path);
    }
    e->needs_redraw = 1;
}

/**
 * @brief Actualiza la entrada del árbol bajo el cursor (hover).
 *
 * Consulta qué fila del árbol está bajo el ratón (con la geometría que registró
 * el render) y, si difiere de la resaltada antes, la actualiza y pide
 * redibujar.
 *
 * @param e  Editor.
 * @param mx Coordenada X del ratón en píxeles.
 * @param my Coordenada Y del ratón en píxeles.
 */
void handle_ftree_hover(Editor *e, int mx, int my) {
    FileTree *ft = &e->ftree;
    if (!ft->open) return;

    int hovered = ui_hit_idx(&e->ui, UI_LIST_TREE_ROW, mx, my);
    if (ft->hovered != hovered) { /* solo redibujar si cambió el resaltado */
        ft->hovered = hovered;
        e->needs_redraw = 1;
    }
}

/**
 * @brief Desplaza el texto verticalmente @p wheel_dy muescas de rueda.
 *
 * Cada muesca mueve @c SCROLL_LINES_PER_NOTCH líneas. El signo se invierte
 * porque la rueda da positivo "hacia arriba" pero scroll_line crece "hacia
 * abajo". El resultado se recorta para no pasar del principio ni del final del
 * documento.
 *
 * @param e        Editor.
 * @param wheel_dy Desplazamiento de la rueda (positivo = hacia arriba).
 */
void handle_scroll(Editor *e, float wheel_dy) {
    /* nada que desplazar sin documento */
    if (e->tab_count == 0 || !e->buf) return;
    e->scroll_line -= (int)(wheel_dy * SCROLL_LINES_PER_NOTCH);
    int total = buf_line_count(e->buf);
    if (e->scroll_line < 0) e->scroll_line = 0; /* tope arriba */
    /* abajo */
    if (total > 0 && e->scroll_line >= total) e->scroll_line = total - 1;
    e->needs_redraw = 1;
}

/**
 * @brief Clic en el área de texto: coloca el cursor (sin selección).
 *
 * Versión "simple" del clic: traduce el píxel a (línea, columna), limpia
 * cualquier selección previa y mueve el cursor allí. (start_text_selection hace
 * lo mismo pero además fija el ancla para poder arrastrar y seleccionar.)
 *
 * @param e  Editor.
 * @param mx Coordenada X del clic en píxeles.
 * @param my Coordenada Y del clic en píxeles.
 */
void handle_text_click(Editor *e, int mx, int my) {
    if (e->group_count == 2) {
        int g = editor_group_at_x(e, mx);
        if (g >= 0) editor_focus_group(e, g);
        set_pane_override(e, e->active_group);
    }
    int text_x = get_left_offset(e) + editor_gutter_w(e) + PADDING_LEFT;
    /* fuera del texto */
    if (mx < text_x || e->tab_count == 0 || !e->buf) {
        clear_pane_override(e);
        return;
    }
    int line, col;
    point_to_line_col(e, mx, my, &line, &col);
    editor_sel_clear(e);
    move_cursor(e, line, col);
    clear_pane_override(e);
}

/* ── Manejadores de eventos de ratón (invocados por input_handle_event) ── */

/**
 * @brief Maneja el evento @c SDL_EVENT_MOUSE_WHEEL (giro de la rueda).
 *
 * Decide a quién afecta la rueda según DÓNDE está el cursor: si el explorador
 * está abierto y el ratón está sobre él, la rueda desplaza el árbol; en
 * cualquier otro caso, desplaza el texto. Hace falta @c SDL_GetMouseState
 * porque el evento de rueda no trae la posición del cursor, solo el giro.
 *
 * @param e  Editor.
 * @param ev Evento SDL; se usa @c ev->wheel.y (muescas verticales giradas).
 */
void on_mouse_wheel(Editor *e, SDL_Event *ev) {
    /* SDL_GetMouseState: posición actual del cursor (el evento de rueda no la
     * trae) */
    float cursor_xf = 0.0f, cursor_yf = 0.0f;
    SDL_GetMouseState(&cursor_xf, &cursor_yf);
    int cursor_x = (int)cursor_xf;
    int cursor_y = (int)cursor_yf;

    /* Preferencias abiertas: la rueda sobre la lista de fuentes la desplaza
     * (ui_list recorta el scroll a un rango válido al dibujar). */
    if (e->settings_open) {
        if (ui_hit(&e->ui, UI_PREF_FONT_LIST, cursor_x, cursor_y))
            e->font_list_scroll -= (int)ev->wheel.y;
        e->needs_redraw = 1;
        return;
    }

    /* Popup de codificación: la rueda desplaza su lista. */
    if (e->enc_popup) {
        if (ui_hit(&e->ui, UI_ENC_LIST, cursor_x, cursor_y))
            e->enc_popup_scroll -= (int)ev->wheel.y;
        e->needs_redraw = 1;
        return;
    }

    /* Rueda sobre el panel inferior: desplazar el scrollback del canal activo. */
    if (e->bottom_panel_open && ui_hit(&e->ui, UI_BOTTOM_PANEL, cursor_x, cursor_y)) {
        if (e->bottom_active_chan >= 0 &&
            (size_t)e->bottom_active_chan < e->panels.count) {
            PanelChannel *c = &e->panels.chans[e->bottom_active_chan];
            c->scroll -= (int)(ev->wheel.y * SCROLL_LINES_PER_NOTCH);
            if (c->scroll < 0) c->scroll = 0;
            /* el tope inferior lo recorta el render segun las lineas visibles */
        }
        e->needs_redraw = 1;
        return;
    }

    if (e->ftree.open && cursor_x < get_left_offset(e)) {
        /* la rueda sobre el panel desplaza el árbol */
        e->ftree.scroll -= (int)(ev->wheel.y * SCROLL_LINES_PER_NOTCH);
        /* no pasar del principio */
        if (e->ftree.scroll < 0) e->ftree.scroll = 0;
        e->needs_redraw = 1;
    } else {
        handle_scroll(e, ev->wheel.y); /* si no, desplazar el texto */
    }
}

/**
 * @brief Actualiza el scroll vertical mientras se arrastra el thumb de la
 * scrollbar.
 *
 * Traduce el movimiento vertical del ratón (en píxeles) a un cambio de
 * scroll_line (en líneas). Para ello modela la barra como en render: el "thumb"
 * (la pieza arrastrable) tiene un alto proporcional a la fracción de documento
 * visible, y se mueve dentro de un recorrido @c thumb_range. La fracción
 * avanzada por el ratón dentro de ese recorrido se aplica al máximo de scroll
 * posible. El punto de partida del arrastre (Y y línea iniciales) se guardó al
 * pulsar, en BUTTON_DOWN.
 *
 * @param e       Editor.
 * @param mouse_y Coordenada Y actual del ratón en píxeles.
 */
static void drag_scrollbar(Editor *e, int mouse_y) {
    if (e->tab_count == 0) return;
    /* alto de la pista de la scrollbar = área de texto sin las barras de UI */
    int text_height = e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT -
                      STATUS_HEIGHT - editor_shortcut_h(e);
    int total_lines = buf_line_count(e->buf);
    /* líneas que caben en pantalla */
    int visible_lines = text_height / e->line_height;
    int max_scroll = total_lines - visible_lines; /* scroll máximo alcanzable */
    if (max_scroll < 0) max_scroll = 0; /* todo cabe: no hay scroll     */

    /* alto del thumb ∝ fracción visible del documento (clamp a un mínimo
     * usable) */
    float thumb_ratio = (visible_lines > 0 && total_lines > 0)
                            ? (float)visible_lines / (float)total_lines
                            : 1.0f;
    int thumb_h = (int)(text_height * thumb_ratio);
    if (thumb_h < HIT_SB_MIN_THUMB_H) thumb_h = HIT_SB_MIN_THUMB_H;
    int thumb_range = text_height - thumb_h; /* recorrido libre del thumb    */
    if (thumb_range < 1) thumb_range = 1;    /* evitar dividir por cero      */

    /* fracción avanzada desde el inicio del arrastre → desplazamiento en líneas
     */
    float frac =
        (float)(mouse_y - e->scrollbar_drag_start_y) / (float)thumb_range;
    int new_scroll = e->scrollbar_drag_start_line + (int)(frac * max_scroll);
    if (new_scroll < 0) new_scroll = 0; /* recortar al rango válido     */
    if (new_scroll > max_scroll) new_scroll = max_scroll;
    e->scroll_line = new_scroll;
    e->needs_redraw = 1;
}

/**
 * @brief Maneja el evento @c SDL_EVENT_MOUSE_MOTION (el cursor se ha movido).
 *
 * Un mismo movimiento puede afectar a varias cosas, que se atienden en orden:
 * resaltado del menú abierto, hover del árbol, redimensionado del panel lateral
 * (si se está arrastrando su borde), arrastre del thumb de la scrollbar y, por
 * último, ampliación de la selección de texto si se arrastra con el botón
 * pulsado.
 *
 * @param e  Editor.
 * @param ev Evento SDL; se usan @c ev->motion.x / @c ev->motion.y.
 */
void on_mouse_motion(Editor *e, SDL_Event *ev) {
    int mouse_x = (int)ev->motion.x;
    int mouse_y = (int)ev->motion.y;

    /* Arrastrando un divisor: redimensiona el panel y nada mas (prioridad
     * sobre todo el resto del hit-testing). */
    if (e->dragging_divider != DIVIDER_NONE) {
        layout_apply_divider_drag(e, e->dragging_divider, mouse_x, mouse_y);
        e->needs_redraw = 1;
        return;
    }

    /* Hover sobre divisores: cambia el cursor a redimension cuando procede. */
    update_divider_hover(e, mouse_x, mouse_y);

    if (e->menu_open) { /* menú desplegado: actualizar el item resaltado */
        int prev = e->menu_hovered;
        e->menu_hovered =
            ui_hit_idx(&e->ui, UI_LIST_MENU_ITEM, mouse_x, mouse_y);
        if (e->menu_hovered != prev) e->needs_redraw = 1; /* solo si cambió */
    }

    /* hover del árbol cuando el ratón está sobre el panel (bajo
     * navbar+pestañas) */
    if (e->ftree.open && mouse_x < e->ftree.width &&
        mouse_y >= NAVBAR_HEIGHT + TAB_BAR_HEIGHT)
        handle_ftree_hover(e, mouse_x, mouse_y);

    if (e->ftree.dragging_border) { /* redimensionando el panel por su borde */
        /* nuevo ancho = ancho al empezar + cuánto se ha movido el ratón en X */
        int new_w = e->ftree.drag_start_w + (mouse_x - e->ftree.drag_start_x);
        /* mínimo legible */
        if (new_w < FTREE_MIN_WIDTH) new_w = FTREE_MIN_WIDTH;
        if (new_w > e->win_w / 2) new_w = e->win_w / 2; /* máx. media ventana */
        e->ftree.width = new_w;
        e->needs_redraw = 1;
    }

    if (e->scrollbar_dragging) { /* arrastrando el thumb: desplazar y salir */
        drag_scrollbar(e, mouse_y);
        return;
    }

    /* arrastre para seleccionar caracteres en el panel inferior */
    if (e->bottom_selecting) {
        int off = bottom_offset_at(e, mouse_x, mouse_y);
        if (off >= 0) e->bottom_sel_caret = off;
        e->bottom_sel_active = 1;
        e->needs_redraw = 1;
        return;
    }

    /* arrastre para seleccionar texto (el ancla se fijó en BUTTON_DOWN) */
    if (e->mouse_selecting && e->tab_count > 0) {
        /* la selección sigue en el panel del grupo enfocado */
        if (e->group_count == 2) set_pane_override(e, e->active_group);
        int line, col;
        /* punto bajo el ratón */
        point_to_line_col(e, mouse_x, mouse_y, &line, &col);
        e->sel_active = 1; /* hay selección viva */
        /* mover el cursor (extremo móvil de la selección) al punto actual */
        buf_move_to(e->buf, editor_pos_from_line_col(e, line, col));
        editor_sync_cursor(e); /* reflejar el cursor en (línea, columna) */
        /* auto-scroll si se arrastra fuera de la vista */
        editor_ensure_visible(e);
        clear_pane_override(e);
        e->needs_redraw = 1;
    }
}

/**
 * @brief Clic en la barra de pestañas: nueva pestaña, cerrar o cambiar de
 * pestaña.
 *
 * Primero mira si se pulsó el botón "+" (nueva pestaña). Si no, busca la
 * pestaña cuyo rectángulo contiene la X del clic: dentro de ella distingue
 * entre pulsar la "x" de cerrar (cierra esa pestaña) o el resto (cambia a esa
 * pestaña). En cualquier caso actualiza el título de la ventana a la ruta de la
 * pestaña activa.
 *
 * @param e  Editor.
 * @param mx Coordenada X del clic en píxeles.
 * @param my Coordenada Y del clic en píxeles.
 */
static void click_tabbar(Editor *e, int mx, int my) {
    /* Botón "+" del editor dividido (por grupo): crea una pestaña en ese grupo. */
    if (e->group_count == 2) {
        int gnew = ui_hit_idx(&e->ui, UI_LIST_SPLIT_NEW, mx, my);
        if (gnew >= 0) {
            editor_focus_group(e, gnew); /* enfocar ese grupo */
            editor_tab_new(e);           /* nueva pestaña en él */
            return;
        }
    }
    if (ui_hit(&e->ui, UI_TAB_NEW, mx, my)) {
        editor_tab_new(e); /* botón "+": pestaña nueva */
        return;
    }
    /* La geometría de cada pestaña y de su "x" la registró el render por
     * índice; el botón de cerrar está dentro de la pestaña, así que se
     * comprueba antes. */
    int close_i = ui_hit_idx(&e->ui, UI_LIST_TAB_CLOSE, mx, my);
    int tab_i = ui_hit_idx(&e->ui, UI_LIST_TAB, mx, my);
    if (close_i < 0 && tab_i < 0) return; /* no se pulsó ninguna pestaña */

    if (close_i >= 0) {           /* "x": cerrar esa pestaña */
        editor_tab_save_state(e); /* guardar estado de la pestaña actual */
        /* enfocar el grupo de la pestaña que se cierra (split-aware) */
        if (e->group_count == 2) e->active_group = e->tabs[close_i].group;
        e->active_tab = close_i;  /* apuntar a la que se va a cerrar      */
        editor_tab_close(e);
    } else {
        /* enfocar primero el grupo de la pestaña pulsada para no robarla al
         * otro panel (editor_tab_switch la asigna al grupo enfocado) */
        if (e->group_count == 2) editor_focus_group(e, e->tabs[tab_i].group);
        editor_tab_switch(e, tab_i); /* cuerpo: cambiar a esa pestaña */
    }
    /* título = ruta de la pestaña activa, o texto por defecto si no hay/sin
     * nombre */
    const char *path = (e->tab_count > 0 && e->tabs[e->active_tab].filepath[0])
                           ? e->tabs[e->active_tab].filepath
                           : "CoffeeCode - Sin título";
    SDL_SetWindowTitle(e->window, path);
}

/**
 * @brief Procesa un clic sobre la barra de búsqueda.
 *
 * Comprueba en orden los botones (reemplazar, anterior, siguiente) y los dos
 * campos de texto (buscar / reemplazar), dando foco al que corresponda. Un clic
 * dentro del marco pero fuera de los widgets se consume igualmente (para no
 * caer en el editor de debajo); un clic fuera de la barra le quita el foco.
 *
 * @param e  Editor.
 * @param mx Coordenada X del clic en píxeles.
 * @param my Coordenada Y del clic en píxeles.
 * @return 1 si el clic fue consumido por la barra; 0 si debe seguir al editor.
 */
static int click_find_bar(Editor *e, int mx, int my) {
    FindBar *fb = &e->find;
    if (!fb->visible) return 0; /* barra oculta: no consume nada */

    /* La geometría de cada control la registró el render en e->ui; aquí solo se
     * pregunta con ui_hit. Los botones prev/next solo están registrados cuando
     * se dibujan (hay coincidencias), así que ui_hit devuelve 0 si no los hay.
     */
    if (ui_hit(&e->ui, UI_FIND_REPLACE, mx, my)) {
        fb->bar_focused = 1;
        do_replace(e);
        return 1;
    }
    if (ui_hit(&e->ui, UI_FIND_PREV, mx, my)) {
        fb->bar_focused = 1;
        find_prev(e);
        return 1;
    }
    if (ui_hit(&e->ui, UI_FIND_NEXT, mx, my)) {
        fb->bar_focused = 1;
        find_jump(e);
        return 1;
    }
    if (ui_hit(&e->ui, UI_FIND_QUERY, mx, my)) { /* campo buscar → foco */
        fb->replace_focused = 0;
        fb->bar_focused = 1;
        e->needs_redraw = 1;
        return 1;
    }
    if (ui_hit(&e->ui, UI_FIND_REPL, mx, my)) { /* campo reemplazar → foco */
        fb->replace_focused = 1;
        fb->bar_focused = 1;
        e->needs_redraw = 1;
        return 1;
    }
    /* Dentro del marco de la barra pero fuera de un control: consumir el clic.
     */
    if (ui_hit(&e->ui, UI_FIND_BAR, mx, my)) return 1;

    if (fb->bar_focused) { /* clic fuera: el foco vuelve al editor */
        fb->bar_focused = 0;
        e->needs_redraw = 1;
    }
    return 0; /* no consumido: el clic sigue su curso hacia el editor */
}

/**
 * @brief Comienza una selección de texto con el ratón en (mx, my).
 *
 * Fija el ANCLA de la selección (el extremo fijo) en el punto pulsado y activa
 * el modo @c mouse_selecting, para que los siguientes MOUSE_MOTION amplíen la
 * selección desde ese ancla hasta el cursor. Importante: el ancla se guarda
 * ANTES de mover el cursor del buffer, porque si no quedarían en el mismo
 * sitio.
 *
 * @param e  Editor.
 * @param mx Coordenada X del clic en píxeles.
 * @param my Coordenada Y del clic en píxeles.
 */
static void start_text_selection(Editor *e, int mx, int my) {
    if (e->tab_count == 0) return;
    /* Con el editor dividido, enfocar el grupo del panel pulsado y mapear el
     * clic con la geometría de ese panel. */
    if (e->group_count == 2) {
        int g = editor_group_at_x(e, mx);
        if (g >= 0) editor_focus_group(e, g);
        set_pane_override(e, e->active_group);
    }
    int line, col;
    point_to_line_col(e, mx, my, &line, &col);
    editor_sel_clear(e);
    e->sel_anchor_line = line; /* ancla ANTES de mover el cursor */
    e->sel_anchor_col = col;
    /* activar arrastre-selección (lo lee MOUSE_MOTION) */
    e->mouse_selecting = 1;
    /* cursor al punto */
    buf_move_to(e->buf, editor_pos_from_line_col(e, line, col));
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    clear_pane_override(e);
    e->needs_redraw = 1;
}

/**
 * @brief Maneja el evento @c SDL_EVENT_MOUSE_BUTTON_DOWN (botón pulsado).
 *
 * Solo reacciona al botón izquierdo. Despacha el clic a la zona donde cayó,
 * comprobándolas en orden de prioridad por bandas verticales y casos
 * especiales: barra de pestañas → menú abierto → botón "Archivo" → barra de
 * búsqueda → área principal (scrollbar, panel del explorador o selección de
 * texto). El primero que coincide consume el clic y retorna.
 *
 * @param e  Editor.
 * @param ev Evento SDL; se usan @c ev->button.x/y y @c ev->button.button.
 */
/**
 * @brief Procesa un clic en la pantalla de preferencias.
 *
 * Los controles (botones y steppers) los registró render_settings_view en
 * e->ui; aquí se resuelven con ui_hit, se aplica el cambio y se persiste con
 * settings_save.
 */
static void handle_settings_click(Editor *e, int mx, int my) {
    Settings *s = &e->settings;
    if (ui_hit(&e->ui, UI_PREF_BACK, mx, my)) {
        e->settings_open = 0; /* volver al editor */
    } else if (ui_hit(&e->ui, UI_PREF_THEME, mx, my)) {
        s->theme = (s->theme + 1) % THEME_COUNT; /* siguiente preset */
        e->theme = theme_preset(s->theme);       /* aplicar al instante */
        settings_save(s);
    } else if (ui_hit(&e->ui, UI_PREF_AUTOSAVE, mx, my)) {
        e->autosave = !e->autosave;
        if (e->autosave) e->autosave_last_ms = SDL_GetTicks();
        s->autosave = e->autosave;
        settings_save(s);
    } else if (ui_hit(&e->ui, UI_PREF_LINENUM, mx, my)) {
        s->show_line_numbers = !s->show_line_numbers;
        settings_save(s);
    } else if (ui_hit(&e->ui, UI_PREF_HLLINE, mx, my)) {
        s->highlight_current_line = !s->highlight_current_line;
        settings_save(s);
    } else if (ui_hit(&e->ui, UI_PREF_SHORTCUTS, mx, my)) {
        s->show_shortcuts = !s->show_shortcuts;
        settings_save(s);
    } else if (ui_hit(&e->ui, UI_PREF_TABW_DEC, mx, my)) {
        if (s->tab_width > SETTINGS_TAB_MIN) s->tab_width--;
        settings_save(s);
    } else if (ui_hit(&e->ui, UI_PREF_TABW_INC, mx, my)) {
        if (s->tab_width < SETTINGS_TAB_MAX) s->tab_width++;
        settings_save(s);
    } else if (ui_hit(&e->ui, UI_PREF_FONTSZ_DEC, mx, my)) {
        if (s->font_size > SETTINGS_FONT_MIN) s->font_size--;
        editor_reload_font(e); /* recargar la fuente al nuevo tamaño */
        settings_save(s);
    } else if (ui_hit(&e->ui, UI_PREF_FONTSZ_INC, mx, my)) {
        if (s->font_size < SETTINGS_FONT_MAX) s->font_size++;
        editor_reload_font(e);
        settings_save(s);
    } else {
        /* Lista de fuentes: un clic sobre una fila la selecciona. La fila 0 es
         * "Predeterminada" (vuelve a la fuente por defecto). */
        int frow = ui_hit_idx(&e->ui, UI_LIST_PREF_FONT, mx, my);
        if (frow == 0) {
            s->font_path[0] = '\0';
            editor_reload_font(e);
            settings_save(s);
        } else if (frow > 0) {
            snprintf(s->font_path, sizeof s->font_path, "%s",
                     e->fonts.items[frow - 1].path);
            editor_reload_font(e);
            settings_save(s);
        }
    }
    e->needs_redraw = 1;
}

/**
 * @brief Procesa un clic en el popup del selector de codificación.
 *
 * Botones de modo (Reabrir/Guardar como) cambian @c enc_popup_mode; un clic en
 * una fila aplica esa codificación (reabrir desde disco o guardar) y cierra; un
 * clic fuera del popup lo cierra.
 */
static void handle_enc_popup_click(Editor *e, int mx, int my) {
    if (ui_hit(&e->ui, UI_ENC_MODE_REOPEN, mx, my)) {
        e->enc_popup_mode = 0;
    } else if (ui_hit(&e->ui, UI_ENC_MODE_SAVE, mx, my)) {
        e->enc_popup_mode = 1;
    } else {
        int row = ui_hit_idx(&e->ui, UI_LIST_ENC, mx, my);
        if (row >= 0) {
            TextEncoding enc = (TextEncoding)row;
            if (e->enc_popup_mode == 0) {
                editor_reopen_with_encoding(e, enc); /* re-decodificar disco */
            } else {
                e->encoding = enc; /* guardar con esta codificación */
                save_file(e);
            }
            e->enc_popup = 0;
        } else if (!ui_hit(&e->ui, UI_ENC_LIST, mx, my)) {
            e->enc_popup = 0; /* clic fuera del popup: cerrar */
        }
    }
    e->needs_redraw = 1;
}

/**
 * @brief Vuelca un fallo de accion del panel (recargar/descargar) a la UI.
 *
 * Toma el motivo concreto de @c ext_host_last_error y lo muestra en el panel
 * de salida y en la barra de estado, para que el usuario VEA por que la accion
 * no surtio efecto en lugar de quedarse en silencio.
 */
static void ext_panel_report_error(Editor *e, const char *action,
                                   const char *id) {
    CoffeeHost *host = (CoffeeHost *)e->ext_host;
    if (!host) return;
    const CoffeeApi *api = ext_host_api(host);
    if (!api) return;
    const char *why = ext_host_last_error(host);
    char msg[512];
    snprintf(msg, sizeof(msg), "%s '%s' fallo: %s", action ? action : "accion",
             id ? id : "?", why && why[0] ? why : "causa desconocida");
    char line[520];
    snprintf(line, sizeof(line), "%s\n", msg);
    api->output_append(host, line); /* panel de salida */
    api->set_status(host, msg);     /* barra de estado */
}

/**
 * @brief Procesa un clic dentro del panel de extensiones.
 *
 * Resuelve, en orden: el boton "Instalar extension" (abre el dialogo de
 * carpeta en modo instalacion), los botones "recargar"/"descargar" de cada
 * fila (por indice de slot del host) y, por ultimo, cualquier clic dentro del
 * marco del panel (se consume para no caer en el editor de debajo).
 *
 * @return 1 si el clic fue consumido por el panel, 0 si no.
 */
int handle_ext_panel_click(Editor *e, int mx, int my) {
    CoffeeHost *host = (CoffeeHost *)e->ext_host;

    /* Instalar extension: lanzar el dialogo de carpeta en modo instalacion. */
    if (ui_hit(&e->ui, UI_EXT_INSTALL, mx, my)) {
        e->ext_install_mode = 1;
        open_folder_dialog(e);
        return 1;
    }

    /* Recargar la extension de la fila pulsada (idx = slot del host). */
    int rel = ui_hit_idx(&e->ui, UI_LIST_EXT_RELOAD, mx, my);
    if (rel >= 0 && host) {
        const char *id = NULL;
        if (ext_host_info(host, (size_t)rel, &id, NULL, NULL, NULL) && id) {
            /* copiar el id: si el reload falla en el unload, la cadena del
             * host puede quedar liberada antes de poder reportar el fallo. */
            char idbuf[128];
            snprintf(idbuf, sizeof(idbuf), "%s", id);
            int rc = ext_host_reload(host, idbuf);
            if (rc != 0) ext_panel_report_error(e, "recargar", idbuf);
        }
        e->needs_redraw = 1;
        return 1;
    }

    /* Descargar la extension de la fila pulsada. */
    int unl = ui_hit_idx(&e->ui, UI_LIST_EXT_UNLOAD, mx, my);
    if (unl >= 0 && host) {
        const char *id = NULL;
        /* copiar el id: ext_host_unload libera la cadena del host */
        if (ext_host_info(host, (size_t)unl, &id, NULL, NULL, NULL) && id) {
            char idbuf[128];
            snprintf(idbuf, sizeof(idbuf), "%s", id);
            int rc = ext_host_unload(host, idbuf);
            if (rc != 0) ext_panel_report_error(e, "descargar", idbuf);
        }
        e->needs_redraw = 1;
        return 1;
    }

    /* Clic en cualquier otra parte del marco del panel: consumirlo. */
    if (ui_hit(&e->ui, UI_EXT_PANEL, mx, my)) return 1;
    return 0;
}

/* margen interior del cuerpo del panel (debe coincidir con render_bottom.c) */
#define BOTTOM_BODY_PAD 6

/**
 * @brief Columnas (en bytes) que caben en el cuerpo del panel inferior.
 *
 * Usa el ancho del cuerpo registrado por el render (UI_BOTTOM_BODY) y el ancho
 * de caracter monoespaciado.  Debe replicar exactamente el calculo de
 * render_bottom.c::body_cols para que el clic cuadre con lo dibujado.
 */
static int bottom_body_cols(Editor *e, int width) {
    int char_w = (e->char_w > 0 ? e->char_w : FALLBACK_CHAR_W);
    int cols = (width - 2 * BOTTOM_BODY_PAD) / char_w;
    if (cols < 1) cols = 1;
    return cols;
}

/**
 * @brief Byte-offset del canal activo bajo el cursor dentro del cuerpo del panel.
 *
 * Traduce (mx,my) a una posicion visual (fila, columna) usando la geometria
 * registrada por el render (UI_BOTTOM_BODY) + el scroll del canal, y luego a un
 * byte-offset del texto con el MISMO layout de envoltura que el render
 * (panel_rowcol_to_offset).  Devuelve un offset >= 0, o -1 si el panel no esta
 * abierto, no hay cuerpo o no hay canal.
 */
static int bottom_offset_at(Editor *e, int mx, int my) {
    if (!e->bottom_panel_open) return -1;
    Rect body = e->ui.single[UI_BOTTOM_BODY];
    if (body.w <= 0) return -1;
    const PanelChannel *c = panel_at(&e->panels, (size_t)e->bottom_active_chan);
    if (!c) return -1;

    int char_w = (e->char_w > 0 ? e->char_w : FALLBACK_CHAR_W);
    int line_h = e->line_height > 0 ? e->line_height : 16;
    int cols = bottom_body_cols(e, body.w);

    int scroll = c->scroll;
    if (scroll < 0) scroll = 0;

    int rel_y = my - body.y;
    if (rel_y < 0) rel_y = 0;
    int row = scroll + rel_y / line_h; /* fila visual del documento */

    int rel_x = mx - (body.x + BOTTOM_BODY_PAD);
    if (rel_x < 0) rel_x = 0;
    int col = rel_x / char_w; /* columna dentro de la fila */

    return (int)panel_rowcol_to_offset(c->text, cols, row, col);
}

/**
 * @brief Procesa un clic dentro del panel inferior (pestanas + cuerpo).
 *
 * Da el foco al panel (para que Ctrl+C copie su canal), cambia de pestana si se
 * pulso una, o inicia una seleccion de lineas si el clic cayo en el cuerpo.
 *
 * @return 1 si el clic fue consumido por el panel, 0 si no.
 */
static int handle_bottom_panel_click(Editor *e, int mx, int my) {
    if (!e->bottom_panel_open) return 0;
    if (!ui_hit(&e->ui, UI_BOTTOM_PANEL, mx, my)) {
        /* clic fuera del panel: pierde el foco (sin consumir el clic) */
        if (e->bottom_focused) {
            e->bottom_focused = 0;
            e->needs_redraw = 1;
        }
        return 0;
    }

    /* el clic esta dentro del panel: tomar el foco */
    e->bottom_focused = 1;

    /* pestana pulsada (por indice de canal) */
    int tab = ui_hit_idx(&e->ui, UI_LIST_BOTTOM_TAB, mx, my);
    if (tab >= 0) {
        e->bottom_active_chan = tab;
        e->bottom_sel_active = 0; /* limpiar seleccion al cambiar de canal */
        e->bottom_sel_anchor = -1;
        e->bottom_sel_caret = -1;
        e->needs_redraw = 1;
        return 1;
    }

    /* clic en el cuerpo: fijar el ancla de la seleccion (vacia hasta arrastrar).
     * anchor==caret => sin resaltado: un clic simple no resalta nada. */
    if (ui_hit(&e->ui, UI_BOTTOM_BODY, mx, my)) {
        int off = bottom_offset_at(e, mx, my);
        e->bottom_sel_anchor = off;
        e->bottom_sel_caret = off;
        e->bottom_sel_active = 1;   /* viva, pero vacia (anchor==caret) */
        e->bottom_selecting = 1;
        e->needs_redraw = 1;
        return 1;
    }

    /* clic en otra parte del marco (tira de pestanas vacia): consumir */
    return 1;
}

void on_mouse_button_down(Editor *e, SDL_Event *ev) {
    int mx = (int)ev->button.x;
    int my = (int)ev->button.y;
    if (ev->button.button != SDL_BUTTON_LEFT) return; /* solo botón izquierdo */

    /* Preferencias abiertas: la pantalla es modal y consume todo el ratón. */
    if (e->settings_open) {
        handle_settings_click(e, mx, my);
        return;
    }

    /* Popup de codificación abierto: consume el ratón. */
    if (e->enc_popup) {
        handle_enc_popup_click(e, mx, my);
        return;
    }

    /* Divisor de panel bajo el cursor: empezar a arrastrarlo.  Tiene prioridad
     * sobre los clics de los paneles (handle_ext_panel_click / explorador) y
     * del editor, para no robar el clic del borde redimensionable. */
    if (try_start_divider_drag(e, mx, my)) return;

    /* Clic en la codificación de la barra de estado: abrir el selector. */
    if (ui_hit(&e->ui, UI_STATUS_ENC, mx, my)) {
        e->enc_popup = 1;
        e->enc_popup_mode = 0;
        e->needs_redraw = 1;
        return;
    }

    /* Boton "Extensiones" de la navbar: abrir/cerrar el panel. */
    if (ui_hit(&e->ui, UI_EXT_TOGGLE, mx, my)) {
        e->ext_panel_open = !e->ext_panel_open;
        e->needs_redraw = 1;
        return;
    }

    /* Boton "Panel" de la navbar: abrir/cerrar el panel inferior. */
    if (ui_hit(&e->ui, UI_BOTTOM_TOGGLE, mx, my)) {
        e->bottom_panel_open = !e->bottom_panel_open;
        e->needs_redraw = 1;
        return;
    }

    /* Clic dentro del panel inferior: foco + pestanas + seleccion. */
    if (e->bottom_panel_open && handle_bottom_panel_click(e, mx, my)) return;

    /* Clic dentro del panel de extensiones: acciones + consumir el clic. */
    if (e->ext_panel_open && handle_ext_panel_click(e, mx, my)) return;

    /* Menú "Archivo" abierto: tiene prioridad máxima sobre cualquier otra zona.
     * Debe comprobarse ANTES de la barra de pestañas porque el menú se dibuja
     * por encima de ella y sus items empiezan en y=NAVBAR_HEIGHT, que coincide
     * con el rango de la tab bar; sin esta guarda, click_tabbar absorbería
     * los clics sobre los items del menú antes de llegar aquí. */
    if (e->menu_open) {
        int item = ui_hit_idx(&e->ui, UI_LIST_MENU_ITEM, mx, my);
        /* clic en un item: ejecutarlo */
        if (item >= 0)
            menu_exec(e, item);
        else {
            /* clic fuera del menú: cerrarlo sin hacer nada */
            e->menu_open = 0;
            e->menu_hovered = -1;
            e->needs_redraw = 1;
        }
        return;
    }

    /* Barra de pestañas */
    if (my >= NAVBAR_HEIGHT && my < NAVBAR_HEIGHT + TAB_BAR_HEIGHT) {
        click_tabbar(e, mx, my);
        return;
    }

    /* Botón "Archivo" en la navbar (geometría registrada por render) */
    if (ui_hit(&e->ui, UI_BTN_FILE, mx, my)) {
        e->menu_open = 1; /* abrir el menú desplegable */
        e->menu_hovered = -1;
        e->needs_redraw = 1;
        return;
    }

    /* Barra de búsqueda */
    /* si la barra consumió el clic, fin */
    if (click_find_bar(e, mx, my)) return;

    /* Área principal (bajo navbar + pestañas) */
    if (my < NAVBAR_HEIGHT + TAB_BAR_HEIGHT) return; /* clic en navbar vacía */

    /* Clic en la scrollbar (registrada por render solo si existe): arrastrar */
    if (ui_hit(&e->ui, UI_SCROLLBAR, mx, my)) {
        e->scrollbar_dragging = 1;      /* activar modo arrastre del thumb  */
        e->scrollbar_drag_start_y = my; /* Y de partida del arrastre        */
        e->scrollbar_drag_start_line = e->scroll_line; /* scroll de partida */
        return;
    }

    /* clic en el panel del explorador */
    if (mx < get_left_offset(e)) handle_ftree_click(e, mx, my);
    /* clic en el texto: empezar selección */
    else
        start_text_selection(e, mx, my);
}
