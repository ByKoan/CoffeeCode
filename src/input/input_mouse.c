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

/* Geometría usada para hit-testing — debe coincidir con la de render.
 * Son medidas fijas (en píxeles) de los rectángulos que dibuja render.c; para
 * saber si un clic cae dentro de un widget las repetimos aquí. Si cambian en
 * render hay que cambiarlas también aquí, o los clics dejarán de cuadrar. */
/* alto de la cabecera del panel del explorador */
#define HIT_FTREE_HEADER_H 26
/* alto del botón que abre/cierra el panel */
#define HIT_FTREE_TOGGLE_H 40
#define HIT_TAB_CLOSE_W 16 /* lado de la "x" para cerrar una pestaña */
/* ancho del botón "+" de nueva pestaña */
#define HIT_NEW_TAB_BTN_W 28
/* alto mínimo del thumb de la scrollbar */
#define HIT_SB_MIN_THUMB_H 20
#define HIT_SCROLLBAR_X 9 /* ancho de la pista + borde (SCROLLBAR_W + 1) */

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
    /* panel abierto: su ancho actual */
    if (e->ftree.open) return e->ftree.width;
    return FTREE_TOGGLE_BTN_W; /* panel cerrado: solo el botón   */
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
    int text_x = get_left_offset(e) + GUTTER_WIDTH + PADDING_LEFT;
    /* ancho de un carácter; si por lo que sea no se midió, usar el de reserva
     */
    int char_px = (e->char_w > 0 ? e->char_w : FALLBACK_CHAR_W);

    /* fila en pantalla: quitar navbar + pestañas y dividir por el alto de línea
     */
    int visual_line = (mouse_y - NAVBAR_HEIGHT - TAB_BAR_HEIGHT) / LINE_HEIGHT;
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
    /* longitud real de la línea (sin el '\n') para no pasarse del final */
    int line_len = (int)(buf_line_end(e->buf, line_start) - line_start);
    if (visual_col > line_len) visual_col = line_len;

    *line = ln;
    *col = visual_col;
}

/**
 * @brief Índice de la entrada visible nº @p target_row del árbol, o -1.
 *
 * En el explorador, las carpetas colapsadas ocultan a sus hijos: el array de
 * entradas tiene más elementos que filas dibujadas. Para mapear "fila N en
 * pantalla" → "índice en el array" hay que recorrer saltándose las entradas no
 * visibles y contar solo las visibles.
 *
 * @param ft         Árbol de archivos.
 * @param target_row Fila visible (0 = primera entrada dibujada del árbol).
 * @return Índice en el array de entradas, o -1 si esa fila no existe.
 */
static int ftree_entry_at_row(FileTree *ft, int target_row) {
    int visible = 0; /* cuántas entradas visibles llevamos vistas */
    for (int i = 0; i < ftree_count(ft); i++) {
        /* saltar entradas ocultas */
        if (!ftree_entry(ft, i)->visible) continue;
        if (visible == target_row) return i; /* es la fila buscada      */
        visible++;
    }
    return -1; /* la fila pedida cae más allá de la última entrada visible */
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
    int content_top = NAVBAR_HEIGHT + TAB_BAR_HEIGHT; /* inicio Y del panel */

    /* Botón toggle: su geometría la registró el render (UI_TOGGLE_TREE), así no
     * hay que recalcularla aquí ni mantenerla sincronizada con el dibujo. */
    if (ui_hit(&e->ui, UI_TOGGLE_TREE, mx, my)) {
        ft->open = !ft->open; /* alternar abierto/cerrado */
        e->needs_redraw = 1;
        return;
    }

    if (!ft->open) return; /* panel cerrado: no hay árbol donde clicar */

    /* primera fila tras cabecera */
    int content_y = content_top + HIT_FTREE_HEADER_H;
    if (my < content_y) return; /* clic en la cabecera */

    /* fila pulsada = (offset Y / alto de fila) + scroll del propio panel */
    int target_row = (my - content_y) / FTREE_ITEM_H + ft->scroll;
    /* fila visible → índice real */
    int idx = ftree_entry_at_row(ft, target_row);
    if (idx < 0) return; /* fila vacía */

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
 * Calcula qué fila del árbol está bajo el ratón y, si difiere de la resaltada
 * antes, la actualiza y pide redibujar (para mostrar el resaltado de hover). La
 * X no importa aquí: basta estar dentro del panel, por eso se ignora con
 * (void).
 *
 * @param e  Editor.
 * @param mx Coordenada X del ratón (no usada, solo Y determina la fila).
 * @param my Coordenada Y del ratón en píxeles.
 */
void handle_ftree_hover(Editor *e, int mx, int my) {
    FileTree *ft = &e->ftree;
    if (!ft->open) return;
    (void)mx; /* la fila depende solo de Y */

    int content_y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT + HIT_FTREE_HEADER_H;
    int hovered = -1;
    /* por debajo de la cabecera: hay fila bajo el cursor */
    if (my >= content_y)
        hovered = ftree_entry_at_row(ft, (my - content_y) / FTREE_ITEM_H +
                                             ft->scroll);

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
    int text_x = get_left_offset(e) + GUTTER_WIDTH + PADDING_LEFT;
    /* fuera del texto */
    if (mx < text_x || e->tab_count == 0 || !e->buf) return;
    int line, col;
    point_to_line_col(e, mx, my, &line, &col);
    editor_sel_clear(e);
    move_cursor(e, line, col);
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
                      STATUS_HEIGHT - SHORTCUT_HEIGHT;
    int total_lines = buf_line_count(e->buf);
    /* líneas que caben en pantalla */
    int visible_lines = text_height / LINE_HEIGHT;
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

    if (e->menu_open) { /* menú desplegado: actualizar el item resaltado */
        int prev = e->menu_hovered;
        e->menu_hovered = menu_item_at(mouse_x, mouse_y);
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

    /* arrastre para seleccionar texto (el ancla se fijó en BUTTON_DOWN) */
    if (e->mouse_selecting && e->tab_count > 0) {
        int line, col;
        /* punto bajo el ratón */
        point_to_line_col(e, mouse_x, mouse_y, &line, &col);
        e->sel_active = 1; /* hay selección viva */
        /* mover el cursor (extremo móvil de la selección) al punto actual */
        buf_move_to(e->buf, editor_pos_from_line_col(e, line, col));
        editor_sync_cursor(e); /* reflejar el cursor en (línea, columna) */
        /* auto-scroll si se arrastra fuera de la vista */
        editor_ensure_visible(e);
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
    if (mx >= e->tab_new_btn_x && mx < e->tab_new_btn_x + HIT_NEW_TAB_BTN_W) {
        editor_tab_new(e); /* botón "+": pestaña nueva */
        return;
    }
    for (int i = 0; i < e->tab_count; i++) {
        EditorTab *t = &e->tabs[i];
        /* no es esta */
        if (mx < t->tab_x || mx >= t->tab_x + t->tab_w) continue;

        /* ¿el clic cayó dentro del cuadradito de cerrar de esta pestaña? */
        int on_close = (mx >= t->close_x && mx < t->close_x + HIT_TAB_CLOSE_W &&
                        my >= t->close_y && my < t->close_y + HIT_TAB_CLOSE_W);
        if (on_close) {
            editor_tab_save_state(e); /* guardar estado de la pestaña actual */
            e->active_tab = i;        /* apuntar a la que se va a cerrar      */
            editor_tab_close(e);
        } else {
            editor_tab_switch(e, i); /* cambiar a la pestaña pulsada */
        }
        /* título = ruta de la pestaña activa, o texto por defecto si no hay/sin
         * nombre */
        const char *path =
            (e->tab_count > 0 && e->tabs[e->active_tab].filepath[0])
                ? e->tabs[e->active_tab].filepath
                : "CoffeeCode - Sin título";
        SDL_SetWindowTitle(e->window, path);
        return;
    }
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
void on_mouse_button_down(Editor *e, SDL_Event *ev) {
    int mx = (int)ev->button.x;
    int my = (int)ev->button.y;
    if (ev->button.button != SDL_BUTTON_LEFT) return; /* solo botón izquierdo */

    /* Menú "Archivo" abierto: tiene prioridad máxima sobre cualquier otra zona.
     * Debe comprobarse ANTES de la barra de pestañas porque el menú se dibuja
     * por encima de ella y sus items empiezan en y=NAVBAR_HEIGHT, que coincide
     * con el rango de la tab bar; sin esta guarda, click_tabbar absorbería
     * los clics sobre los items del menú antes de llegar aquí. */
    if (e->menu_open) {
        int item = menu_item_at(mx, my);
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

    /* Botón "Archivo" en la navbar */
    if (my >= 0 && my < NAVBAR_HEIGHT && mx >= BTN_FILE_X &&
        mx < BTN_FILE_X + BTN_FILE_W) {
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

    /* Clic en la scrollbar: iniciar arrastre del thumb */
    if (mx >= e->win_w - HIT_SCROLLBAR_X) {
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
