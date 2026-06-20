/**
 * @file layout.c
 * @brief Envoltorios de los divisores arrastrables sobre el estado del Editor.
 *
 * La logica PURA (hit-test y clampeo sobre enteros) vive en layout_core.c, que
 * no depende de SDL y se prueba en headless.  Aqui solo se deriva la geometria
 * de cada region desde el Editor y se delega en las funciones puras.
 */
#include "layout/layout.h"
#include "editor/editor.h"     /* struct Editor completo (win_w, ftree, ...) */
#include "filetree/filetree.h" /* FTREE_MIN_WIDTH (para el static assert)    */

/* El minimo del explorador esta duplicado en layout.h (LAYOUT_FTREE_MIN_W) para
 * que la logica pura quede libre de SDL.  Garantizamos que no se desincronizan
 * con la definicion real de filetree.h. */
_Static_assert(LAYOUT_FTREE_MIN_W == FTREE_MIN_WIDTH,
               "LAYOUT_FTREE_MIN_W debe coincidir con FTREE_MIN_WIDTH");

/** Banda vertical (top, bottom) que ocupa el explorador. */
static void filetree_band(Editor *e, int *top, int *bottom) {
    *top = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    *bottom = e->win_h - STATUS_HEIGHT;
}

/** Banda vertical (top, bottom) que ocupa el panel de extensiones. */
static void ext_panel_band(Editor *e, int *top, int *bottom) {
    *top = NAVBAR_HEIGHT;
    *bottom = e->win_h - STATUS_HEIGHT;
}

int layout_hit_divider(Editor *e, int mx, int my) {
    /* Panel de extensiones primero: vive a la derecha y su borde izquierdo no
     * solapa con el del explorador, asi que el orden no es critico, pero lo
     * comprobamos antes por estar mas cerca del cursor en el caso comun. */
    if (e->ext_panel_open) {
        int edge_x = e->win_w - e->ext_panel_w; /* borde izquierdo del panel */
        int top, bottom;
        ext_panel_band(e, &top, &bottom);
        if (layout_point_on_vertical_edge(mx, my, edge_x, top, bottom))
            return DIVIDER_EXT_PANEL_LEFT;
    }

    if (e->ftree.open) {
        int edge_x = e->ftree.width; /* borde derecho del explorador */
        int top, bottom;
        filetree_band(e, &top, &bottom);
        if (layout_point_on_vertical_edge(mx, my, edge_x, top, bottom))
            return DIVIDER_FILETREE_RIGHT;
    }

    return DIVIDER_NONE;
}

void layout_apply_divider_drag(Editor *e, int which, int mx, int my) {
    (void)my; /* los divisores actuales son verticales: la Y no influye */
    switch (which) {
    case DIVIDER_FILETREE_RIGHT:
        /* el ancho del explorador es justo la X del cursor (su borde derecho) */
        e->ftree.width = layout_clamp_filetree_w(mx, e->win_w);
        break;
    case DIVIDER_EXT_PANEL_LEFT:
        /* el panel se ancla a la derecha: su ancho = ventana - X del cursor */
        e->ext_panel_w = layout_clamp_ext_panel_w(e->win_w - mx, e->win_w);
        break;
    default: break; /* DIVIDER_NONE u otro: nada que hacer */
    }
}
