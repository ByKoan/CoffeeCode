/**
 * @file layout_core.c
 * @brief Logica PURA de los divisores: hit-test y clampeo sobre enteros crudos.
 *
 * No depende de SDL ni de la struct Editor, asi que se compila y prueba en
 * headless (ver test/layout/test_layout.c).  Los envoltorios que la alimentan
 * con la geometria del Editor viven en layout.c.
 */
#include "layout/layout.h"

int layout_point_on_vertical_edge(int mx, int my, int edge_x, int region_top,
                                  int region_bottom) {
    /* fuera de la banda vertical del panel: no es agarrable */
    if (my < region_top || my >= region_bottom) return 0;
    /* dentro de la franja [edge_x - GRAB, edge_x + GRAB] */
    return (mx >= edge_x - LAYOUT_DIVIDER_GRAB &&
            mx <= edge_x + LAYOUT_DIVIDER_GRAB);
}

int layout_point_on_horizontal_edge(int mx, int my, int edge_y, int region_left,
                                    int region_right) {
    /* fuera de la banda horizontal del panel: no es agarrable */
    if (mx < region_left || mx >= region_right) return 0;
    /* dentro de la franja [edge_y - GRAB, edge_y + GRAB] */
    return (my >= edge_y - LAYOUT_DIVIDER_GRAB &&
            my <= edge_y + LAYOUT_DIVIDER_GRAB);
}

/** Recorta @p v al rango [@p lo, @p hi] (con hi degenerado tolerado). */
static int clamp_int(int v, int lo, int hi) {
    if (hi < lo) hi = lo; /* ventana muy estrecha: rango degenerado */
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

/* Tope superior de un panel: ocupar casi todo, dejando solo LAYOUT_MIN_OPPOSITE
 * px para la region opuesta.  Nunca por debajo del minimo del propio panel. */
static int max_with_opposite(int total, int self_min) {
    int hi = total - LAYOUT_MIN_OPPOSITE;
    if (hi < self_min) hi = self_min; /* ventana minuscula: rango degenerado */
    return hi;
}

int layout_clamp_filetree_w(int desired_w, int win_w) {
    return clamp_int(desired_w, LAYOUT_FTREE_MIN_W,
                     max_with_opposite(win_w, LAYOUT_FTREE_MIN_W));
}

int layout_clamp_ext_panel_w(int desired_w, int win_w) {
    return clamp_int(desired_w, LAYOUT_EXT_MIN_W,
                     max_with_opposite(win_w, LAYOUT_EXT_MIN_W));
}

int layout_clamp_bottom_h(int desired_h, int win_h) {
    return clamp_int(desired_h, LAYOUT_BOTTOM_MIN_H,
                     max_with_opposite(win_h, LAYOUT_BOTTOM_MIN_H));
}

int layout_clamp_split_x(int desired_x, int area_left, int area_right) {
    int lo = area_left + LAYOUT_SPLIT_MIN_W;  /* X minima del divisor */
    int hi = area_right - LAYOUT_SPLIT_MIN_W; /* X maxima del divisor */
    /* Area demasiado estrecha para dos minimos: caer al punto medio. */
    if (hi < lo) {
        int mid = (area_left + area_right) / 2;
        return mid;
    }
    return clamp_int(desired_x, lo, hi);
}
