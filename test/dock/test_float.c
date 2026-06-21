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

/* -- Redimension por bordes (cualquier lado/esquina) ----------------------- */

static void test_resize_edges(void) {
    FloatPanel p;
    p.rect = (Rect){100, 100, 400, 300};
    p.group_id = 0;
    /* interior -> ningun borde */
    EXPECT_EQ_INT(float_resize_edges(&p, 300, 250), 0);
    /* cada lado */
    EXPECT_EQ_INT(float_resize_edges(&p, 102, 250), FLOAT_EDGE_LEFT);
    EXPECT_EQ_INT(float_resize_edges(&p, 497, 250), FLOAT_EDGE_RIGHT);
    EXPECT_EQ_INT(float_resize_edges(&p, 300, 102), FLOAT_EDGE_TOP);
    EXPECT_EQ_INT(float_resize_edges(&p, 300, 397), FLOAT_EDGE_BOTTOM);
    /* esquinas: dos bordes combinados */
    EXPECT_EQ_INT(float_resize_edges(&p, 497, 397),
                  FLOAT_EDGE_RIGHT | FLOAT_EDGE_BOTTOM);
    EXPECT_EQ_INT(float_resize_edges(&p, 102, 102),
                  FLOAT_EDGE_LEFT | FLOAT_EDGE_TOP);
    /* fuera del marco -> 0 */
    EXPECT_EQ_INT(float_resize_edges(&p, 50, 50), 0);
}

static void test_clamp_resize_edges(void) {
    Rect huge = {0, 0, 2000, 2000}; /* sin recorte por limites */
    Rect r = {100, 100, 400, 300};

    /* borde derecho sigue al cursor; el origen no se mueve */
    Rect m = float_clamp_resize_edges(r, FLOAT_EDGE_RIGHT, 700, 0, huge);
    EXPECT_EQ_INT(m.x, 100);
    EXPECT_EQ_INT(m.w, 600);
    /* borde izquierdo mueve el origen; el borde derecho queda fijo (500) */
    m = float_clamp_resize_edges(r, FLOAT_EDGE_LEFT, 50, 0, huge);
    EXPECT_EQ_INT(m.x, 50);
    EXPECT_EQ_INT(m.x + m.w, 500);
    /* borde superior mueve el origen Y; el inferior queda fijo (400) */
    m = float_clamp_resize_edges(r, FLOAT_EDGE_TOP, 0, 50, huge);
    EXPECT_EQ_INT(m.y, 50);
    EXPECT_EQ_INT(m.y + m.h, 400);
    /* esquina: dos bordes a la vez */
    m = float_clamp_resize_edges(r, FLOAT_EDGE_RIGHT | FLOAT_EDGE_BOTTOM, 700,
                                 600, huge);
    EXPECT_EQ_INT(m.w, 600);
    EXPECT_EQ_INT(m.h, 500);
    /* minimo: arrastrar el borde derecho hacia dentro respeta FLOAT_MIN_W */
    m = float_clamp_resize_edges(r, FLOAT_EDGE_RIGHT, 150, 0, huge);
    EXPECT_EQ_INT(m.w, FLOAT_MIN_W);
    EXPECT_EQ_INT(m.x, 100); /* origen intacto */
    /* limites: el borde derecho no se sale de bounds */
    Rect narrow = {0, 0, 450, 2000};
    m = float_clamp_resize_edges(r, FLOAT_EDGE_RIGHT, 700, 0, narrow);
    EXPECT_TRUE(m.x + m.w <= 450);
}

int main(void) {
    tt_suite("float");
    tt_run("sub-rectangulos del marco", test_subrects);
    tt_run("hit-test: regiones y prioridad", test_hit_test);
    tt_run("clamp de movimiento dentro de limites", test_clamp_move);
    tt_run("clamp de redimension: minimo y limites", test_clamp_resize);
    tt_run("bordes redimensionables bajo el cursor", test_resize_edges);
    tt_run("clamp de redimension por bordes", test_clamp_resize_edges);
    return tt_summary();
}
