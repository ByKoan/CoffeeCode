/**
 * @file test_layout.c
 * @brief Pruebas unitarias de la logica PURA de divisores arrastrables.
 *
 * Solo ejercita las funciones puras de layout.h (enteros crudos), que no
 * dependen de SDL ni de la struct Editor, asi que el test corre en headless.
 * Verifica:
 *   1. layout_point_on_vertical_edge: detecta el borde dentro de su franja y la
 *      banda vertical, y lo rechaza fuera.
 *   2. layout_clamp_ext_panel_w / layout_clamp_filetree_w: recortan el ancho a
 *      [min, win_w/2] por ambos extremos.
 */
#include "ctests.h"
#include "layout/layout.h"

/* -- Hit-test del borde vertical ------------------------------------------- */

/** Un punto justo sobre el borde, dentro de la banda, cae sobre la franja. */
static void test_edge_centro(void) {
    /* borde en x=200, banda vertical [50, 400) */
    EXPECT_EQ_INT(layout_point_on_vertical_edge(200, 100, 200, 50, 400), 1);
}

/** Dentro de la franja agarrable (+-LAYOUT_DIVIDER_GRAB) sigue contando. */
static void test_edge_dentro_franja(void) {
    int gx = 200;
    /* extremos de la franja: gx-GRAB y gx+GRAB inclusive */
    EXPECT_EQ_INT(
        layout_point_on_vertical_edge(gx - LAYOUT_DIVIDER_GRAB, 100, gx, 50, 400),
        1);
    EXPECT_EQ_INT(
        layout_point_on_vertical_edge(gx + LAYOUT_DIVIDER_GRAB, 100, gx, 50, 400),
        1);
}

/** Lejos del borde en X: no cae sobre la franja. */
static void test_edge_fuera_x(void) {
    EXPECT_EQ_INT(
        layout_point_on_vertical_edge(200 + LAYOUT_DIVIDER_GRAB + 1, 100, 200,
                                      50, 400),
        0);
    EXPECT_EQ_INT(
        layout_point_on_vertical_edge(200 - LAYOUT_DIVIDER_GRAB - 1, 100, 200,
                                      50, 400),
        0);
}

/** Fuera de la banda vertical [top, bottom): no cuenta aunque la X coincida. */
static void test_edge_fuera_y(void) {
    EXPECT_EQ_INT(layout_point_on_vertical_edge(200, 49, 200, 50, 400), 0);
    EXPECT_EQ_INT(layout_point_on_vertical_edge(200, 400, 200, 50, 400),
                  0); /* bottom es exclusivo */
}

/* -- Clampeo del panel de extensiones -------------------------------------- */

/** Pedir menos del minimo deja el ancho en LAYOUT_EXT_MIN_W. */
static void test_ext_clamp_min(void) {
    int w = layout_clamp_ext_panel_w(10, 1000); /* 10 < 180 */
    EXPECT_EQ_INT(w, LAYOUT_EXT_MIN_W);
    EXPECT_TRUE(w >= LAYOUT_EXT_MIN_W);
}

/** Pedir mas de win_w/2 deja el ancho en win_w/2. */
static void test_ext_clamp_max(void) {
    int win = 1000;
    int w = layout_clamp_ext_panel_w(9000, win); /* 9000 > 500 */
    EXPECT_EQ_INT(w, win / 2);
    EXPECT_TRUE(w <= win / 2);
}

/** Un valor dentro del rango se respeta tal cual. */
static void test_ext_clamp_dentro(void) {
    EXPECT_EQ_INT(layout_clamp_ext_panel_w(300, 1000), 300);
}

/* -- Clampeo del explorador ------------------------------------------------ */

/** Pedir muy poco respeta el minimo del explorador (no baja de el). */
static void test_ftree_clamp_min(void) {
    int w = layout_clamp_filetree_w(1, 1000); /* 1 < FTREE_MIN_WIDTH */
    EXPECT_TRUE(w > 1);                        /* se subio al minimo */
    EXPECT_TRUE(w <= 1000 / 2);
}

/** Pedir mas de win_w/2 lo recorta a la mitad de la ventana. */
static void test_ftree_clamp_max(void) {
    int win = 600;
    int w = layout_clamp_filetree_w(5000, win);
    EXPECT_EQ_INT(w, win / 2);
}

/** Un valor intermedio razonable se respeta. */
static void test_ftree_clamp_dentro(void) {
    EXPECT_EQ_INT(layout_clamp_filetree_w(150, 1000), 150);
}

int main(void) {
    tt_suite("layout");
    tt_run("borde: punto sobre el centro", test_edge_centro);
    tt_run("borde: dentro de la franja agarrable", test_edge_dentro_franja);
    tt_run("borde: fuera en X", test_edge_fuera_x);
    tt_run("borde: fuera de la banda vertical", test_edge_fuera_y);
    tt_run("ext: clamp al minimo", test_ext_clamp_min);
    tt_run("ext: clamp al maximo (win/2)", test_ext_clamp_max);
    tt_run("ext: valor dentro del rango", test_ext_clamp_dentro);
    tt_run("ftree: clamp al minimo", test_ftree_clamp_min);
    tt_run("ftree: clamp al maximo (win/2)", test_ftree_clamp_max);
    tt_run("ftree: valor dentro del rango", test_ftree_clamp_dentro);
    return tt_summary();
}
