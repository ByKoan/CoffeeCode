/**
 * @file test_float.c
 * @brief Pruebas unitarias de la logica PURA de los paneles flotantes.
 *
 * Ejercita solo las funciones puras de dock/float.h (sin SDL ni struct Editor),
 * asi que corre en headless.  Verifica:
 *   1. Los sub-rectangulos (titulo, botones, tira de pestanas, contenido,
 *      esquina de redimension) salen donde deben dado el marco.
 *   2. El hit-test clasifica cada region con la prioridad correcta.
 *   3. El clamp de movimiento mantiene el flotante dentro de los limites.
 *   4. El clamp de redimension respeta el minimo y no se sale de los limites.
 */
#include "ctests.h"
#include "dock/float.h"

/* -- Sub-rectangulos del marco --------------------------------------------- */

static void test_subrects(void) {
    FloatPanel p = {{100, 50, 400, 300}, 3};

    Rect tb = float_titlebar_rect(&p);
    EXPECT_EQ_INT(tb.x, 100);
    EXPECT_EQ_INT(tb.y, 50);
    EXPECT_EQ_INT(tb.w, 400);
    EXPECT_EQ_INT(tb.h, FLOAT_TITLEBAR_H);

    /* el boton de cerrar va pegado al borde derecho de la barra de titulo */
    Rect cl = float_close_rect(&p);
    EXPECT_TRUE(cl.x + cl.w <= p.rect.x + p.rect.w);
    EXPECT_TRUE(cl.x > p.rect.x + p.rect.w / 2); /* en la mitad derecha */
    EXPECT_EQ_INT(cl.w, FLOAT_BTN_SZ);

    /* el de acoplar queda a la izquierda del de cerrar, sin solaparse */
    Rect dk = float_dock_rect(&p);
    EXPECT_TRUE(dk.x + dk.w <= cl.x);

    /* el contenido empieza bajo titulo + tira de pestanas */
    Rect ct = float_content_rect(&p);
    EXPECT_TRUE(ct.y > tb.y + tb.h);
    EXPECT_EQ_INT(ct.x, 100);
    EXPECT_EQ_INT(ct.w, 400);

    /* la tira de pestanas va entre el titulo y el contenido */
    Rect tab = float_tabbar_rect(&p);
    EXPECT_EQ_INT(tab.y, p.rect.y + FLOAT_TITLEBAR_H);
    EXPECT_EQ_INT(tab.y + tab.h, ct.y);

    /* la esquina de redimension va en la esquina inferior-derecha */
    Rect rz = float_resize_rect(&p);
    EXPECT_EQ_INT(rz.x + rz.w, p.rect.x + p.rect.w);
    EXPECT_EQ_INT(rz.y + rz.h, p.rect.y + p.rect.h);
}

/* -- Hit-test: cada region y su prioridad ---------------------------------- */

static void test_hit_test(void) {
    FloatPanel p = {{100, 50, 400, 300}, 0};

    /* fuera del marco: ninguna region */
    EXPECT_EQ_INT(float_hit_test(&p, 10, 10), FLOAT_HIT_NONE);
    EXPECT_EQ_INT(float_hit_test(&p, 600, 400), FLOAT_HIT_NONE);

    /* centro de la barra de titulo (lejos de los botones) -> TITULO */
    EXPECT_EQ_INT(float_hit_test(&p, 150, 50 + FLOAT_TITLEBAR_H / 2),
                  FLOAT_HIT_TITLEBAR);

    /* sobre el boton de cerrar -> CLOSE (gana a la barra de titulo) */
    Rect cl = float_close_rect(&p);
    EXPECT_EQ_INT(float_hit_test(&p, cl.x + cl.w / 2, cl.y + cl.h / 2),
                  FLOAT_HIT_CLOSE);

    /* sobre el boton de acoplar -> DOCK */
    Rect dk = float_dock_rect(&p);
    EXPECT_EQ_INT(float_hit_test(&p, dk.x + dk.w / 2, dk.y + dk.h / 2),
                  FLOAT_HIT_DOCK);

    /* sobre la tira de pestanas -> TABBAR */
    Rect tab = float_tabbar_rect(&p);
    EXPECT_EQ_INT(float_hit_test(&p, tab.x + 10, tab.y + tab.h / 2),
                  FLOAT_HIT_TABBAR);

    /* en el cuerpo -> CONTENT */
    Rect ct = float_content_rect(&p);
    EXPECT_EQ_INT(float_hit_test(&p, ct.x + 20, ct.y + 20), FLOAT_HIT_CONTENT);

    /* la esquina de redimension gana al contenido */
    Rect rz = float_resize_rect(&p);
    EXPECT_EQ_INT(float_hit_test(&p, rz.x + rz.w / 2, rz.y + rz.h / 2),
                  FLOAT_HIT_RESIZE);
}

/* -- Clamp de movimiento --------------------------------------------------- */

static void test_clamp_move(void) {
    Rect bounds = {0, 30, 800, 570}; /* ventana usable bajo navbar */
    Rect r = {0, 0, 400, 300};

    /* movimiento normal dentro de los limites: respeta x,y */
    Rect m = float_clamp_move(r, 100, 100, bounds);
    EXPECT_EQ_INT(m.x, 100);
    EXPECT_EQ_INT(m.y, 100);
    EXPECT_EQ_INT(m.w, 400); /* tamano intacto */
    EXPECT_EQ_INT(m.h, 300);

    /* arrastrar a la izquierda/arriba fuera: se pega al borde de bounds */
    m = float_clamp_move(r, -50, -50, bounds);
    EXPECT_EQ_INT(m.x, bounds.x);
    EXPECT_EQ_INT(m.y, bounds.y);

    /* arrastrar a la derecha/abajo fuera: el borde inferior-derecho no sale */
    m = float_clamp_move(r, 1000, 1000, bounds);
    EXPECT_EQ_INT(m.x + m.w, bounds.x + bounds.w);
    EXPECT_EQ_INT(m.y + m.h, bounds.y + bounds.h);
}

/* -- Clamp de redimension -------------------------------------------------- */

static void test_clamp_resize(void) {
    Rect bounds = {0, 30, 800, 570};
    Rect r = {100, 100, 400, 300};

    /* redimension normal: respeta w,h y mantiene el origen */
    Rect m = float_clamp_resize(r, 500, 350, bounds);
    EXPECT_EQ_INT(m.x, 100);
    EXPECT_EQ_INT(m.y, 100);
    EXPECT_EQ_INT(m.w, 500);
    EXPECT_EQ_INT(m.h, 350);

    /* por debajo del minimo: se sube al minimo */
    m = float_clamp_resize(r, 10, 10, bounds);
    EXPECT_EQ_INT(m.w, FLOAT_MIN_W);
    EXPECT_EQ_INT(m.h, FLOAT_MIN_H);

    /* mas grande que el espacio disponible: el borde no se sale de bounds */
    m = float_clamp_resize(r, 5000, 5000, bounds);
    EXPECT_TRUE(m.x + m.w <= bounds.x + bounds.w);
    EXPECT_TRUE(m.y + m.h <= bounds.y + bounds.h);
}

int main(void) {
    tt_suite("float");
    tt_run("sub-rectangulos del marco", test_subrects);
    tt_run("hit-test: regiones y prioridad", test_hit_test);
    tt_run("clamp de movimiento dentro de limites", test_clamp_move);
    tt_run("clamp de redimension: minimo y limites", test_clamp_resize);
    return tt_summary();
}
