/**
 * @file float.c
 * @brief Geometria pura de los paneles flotantes (ver dock/float.h).
 *
 * Implementa el calculo de sub-rectangulos de un flotante (titulo, botones,
 * tira de pestanas, contenido, esquina de redimension), su hit-test y el recorte
 * (clamp) del movimiento/redimension dentro de la ventana.  Sin dependencias de
 * SDL ni de la struct Editor: todo opera sobre ::Rect, asi que se prueba en
 * headless.
 */
#include "dock/float.h"

Rect float_titlebar_rect(const FloatPanel *p) {
    /* franja superior del marco, a todo el ancho */
    Rect r = {p->rect.x, p->rect.y, p->rect.w, FLOAT_TITLEBAR_H};
    return r;
}

Rect float_close_rect(const FloatPanel *p) {
    /* cuadrado pegado al borde derecho de la barra de titulo, centrado vertical */
    int y = p->rect.y + (FLOAT_TITLEBAR_H - FLOAT_BTN_SZ) / 2;
    int x = p->rect.x + p->rect.w - FLOAT_BTN_SZ - 4; /* 4 px de margen derecho */
    Rect r = {x, y, FLOAT_BTN_SZ, FLOAT_BTN_SZ};
    return r;
}

Rect float_dock_rect(const FloatPanel *p) {
    /* a la izquierda del boton de cerrar, con una pequena separacion */
    int y = p->rect.y + (FLOAT_TITLEBAR_H - FLOAT_BTN_SZ) / 2;
    int x = p->rect.x + p->rect.w - FLOAT_BTN_SZ * 2 - 4 - 4; /* dos botones */
    Rect r = {x, y, FLOAT_BTN_SZ, FLOAT_BTN_SZ};
    return r;
}

Rect float_tabbar_rect(const FloatPanel *p) {
    /* bajo la barra de titulo, a todo el ancho; alto = TAB_BAR_HEIGHT del editor,
     * pero aqui se define con su propia constante para no acoplar al editor.  El
     * llamante (render/input) usa TAB_BAR_HEIGHT real; mantenemos coherencia
     * tomando el alto de TAB_BAR a traves del propio marco: la tira ocupa desde
     * el final del titulo hasta el inicio del contenido. */
    Rect content = float_content_rect(p);
    int y = p->rect.y + FLOAT_TITLEBAR_H;
    Rect r = {p->rect.x, y, p->rect.w, content.y - y};
    if (r.h < 0) r.h = 0;
    return r;
}

/* Alto de la tira de pestanas de un flotante.  Se define igual que la barra de
 * pestanas del editor (TAB_BAR_HEIGHT == 28) sin incluir el header del editor
 * para mantener este modulo puro; si aquel cambiara, ajustar aqui. */
#define FLOAT_TABBAR_H 28

Rect float_content_rect(const FloatPanel *p) {
    int top = p->rect.y + FLOAT_TITLEBAR_H + FLOAT_TABBAR_H;
    int h = p->rect.h - FLOAT_TITLEBAR_H - FLOAT_TABBAR_H;
    if (h < 0) h = 0;
    Rect r = {p->rect.x, top, p->rect.w, h};
    return r;
}

Rect float_resize_rect(const FloatPanel *p) {
    /* esquina inferior-derecha del marco */
    int x = p->rect.x + p->rect.w - FLOAT_RESIZE_SZ;
    int y = p->rect.y + p->rect.h - FLOAT_RESIZE_SZ;
    Rect r = {x, y, FLOAT_RESIZE_SZ, FLOAT_RESIZE_SZ};
    return r;
}

FloatHit float_hit_test(const FloatPanel *p, int mx, int my) {
    /* fuera del marco completo: ninguna region */
    if (!rect_has(p->rect, mx, my)) return FLOAT_HIT_NONE;

    /* la esquina de resize tiene prioridad sobre el contenido / la tira */
    if (rect_has(float_resize_rect(p), mx, my)) return FLOAT_HIT_RESIZE;

    /* botones de la barra de titulo antes que la propia barra */
    if (rect_has(float_close_rect(p), mx, my)) return FLOAT_HIT_CLOSE;
    if (rect_has(float_dock_rect(p), mx, my)) return FLOAT_HIT_DOCK;
    if (rect_has(float_titlebar_rect(p), mx, my)) return FLOAT_HIT_TITLEBAR;

    if (rect_has(float_tabbar_rect(p), mx, my)) return FLOAT_HIT_TABBAR;
    if (rect_has(float_content_rect(p), mx, my)) return FLOAT_HIT_CONTENT;

    /* dentro del marco pero en ningun sub-rect concreto (bordes): tratar como
     * contenido para no dejar "huecos muertos" que filtren el clic al dock. */
    return FLOAT_HIT_CONTENT;
}

Rect float_clamp_move(Rect rect, int new_x, int new_y, Rect bounds) {
    rect.x = new_x;
    rect.y = new_y;
    /* recortar el borde derecho/inferior dentro de bounds */
    if (rect.x + rect.w > bounds.x + bounds.w) rect.x = bounds.x + bounds.w - rect.w;
    if (rect.y + rect.h > bounds.y + bounds.h) rect.y = bounds.y + bounds.h - rect.h;
    /* y el borde izquierdo/superior (prioriza que el titulo quede accesible) */
    if (rect.x < bounds.x) rect.x = bounds.x;
    if (rect.y < bounds.y) rect.y = bounds.y;
    return rect;
}

Rect float_clamp_resize(Rect rect, int new_w, int new_h, Rect bounds) {
    if (new_w < FLOAT_MIN_W) new_w = FLOAT_MIN_W;
    if (new_h < FLOAT_MIN_H) new_h = FLOAT_MIN_H;
    /* no dejar que el borde inferior-derecho salga de bounds (origen fijo) */
    int max_w = bounds.x + bounds.w - rect.x;
    int max_h = bounds.y + bounds.h - rect.y;
    if (max_w >= FLOAT_MIN_W && new_w > max_w) new_w = max_w;
    if (max_h >= FLOAT_MIN_H && new_h > max_h) new_h = max_h;
    rect.w = new_w;
    rect.h = new_h;
    return rect;
}

int float_resize_edges(const FloatPanel *p, int mx, int my) {
    Rect r = p->rect;
    if (!rect_has(r, mx, my)) return 0; /* fuera del marco */
    int b = FLOAT_RESIZE_BORDER;
    int edges = 0;
    if (mx < r.x + b) edges |= FLOAT_EDGE_LEFT;
    else if (mx >= r.x + r.w - b) edges |= FLOAT_EDGE_RIGHT;
    if (my < r.y + b) edges |= FLOAT_EDGE_TOP;
    else if (my >= r.y + r.h - b) edges |= FLOAT_EDGE_BOTTOM;
    return edges;
}

Rect float_clamp_resize_edges(Rect rect, int edges, int mx, int my,
                              Rect bounds) {
    /* trabajar con los cuatro bordes; mover solo los que esten en la mascara */
    int left = rect.x, top = rect.y;
    int right = rect.x + rect.w, bottom = rect.y + rect.h;
    if (edges & FLOAT_EDGE_LEFT) left = mx;
    if (edges & FLOAT_EDGE_RIGHT) right = mx;
    if (edges & FLOAT_EDGE_TOP) top = my;
    if (edges & FLOAT_EDGE_BOTTOM) bottom = my;

    /* recortar a los limites de la ventana */
    if (left < bounds.x) left = bounds.x;
    if (top < bounds.y) top = bounds.y;
    if (right > bounds.x + bounds.w) right = bounds.x + bounds.w;
    if (bottom > bounds.y + bounds.h) bottom = bounds.y + bounds.h;

    /* respetar el tamano minimo empujando el borde que se esta moviendo */
    if (right - left < FLOAT_MIN_W) {
        if (edges & FLOAT_EDGE_LEFT) left = right - FLOAT_MIN_W;
        else right = left + FLOAT_MIN_W;
    }
    if (bottom - top < FLOAT_MIN_H) {
        if (edges & FLOAT_EDGE_TOP) top = bottom - FLOAT_MIN_H;
        else bottom = top + FLOAT_MIN_H;
    }

    rect.x = left;
    rect.y = top;
    rect.w = right - left;
    rect.h = bottom - top;
    return rect;
}
