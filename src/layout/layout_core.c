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

/** Recorta @p v al rango [@p lo, @p hi] (con hi degenerado tolerado). */
static int clamp_int(int v, int lo, int hi) {
    if (hi < lo) hi = lo; /* ventana muy estrecha: rango degenerado */
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

int layout_clamp_filetree_w(int desired_w, int win_w) {
    return clamp_int(desired_w, LAYOUT_FTREE_MIN_W, win_w / 2);
}

int layout_clamp_ext_panel_w(int desired_w, int win_w) {
    return clamp_int(desired_w, LAYOUT_EXT_MIN_W, win_w / 2);
}
