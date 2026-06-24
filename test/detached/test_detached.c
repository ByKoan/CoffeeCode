/**
 * @file test_detached.c
 * @brief Pruebas unitarias de la geometria PURA de las ventanas desprendidas.
 *
 * Ejercita solo las funciones puras de detached/detached.h (sin SDL ni struct
 * Editor), asi que corre en headless.  Verifica que la tira de pestanas y el area
 * de contenido de una ventana desprendida caen donde deben dado su tamano, y que
 * los casos degenerados (ventana mas baja que la tira, ancho negativo) se acotan
 * sin producir rects invalidos.
 */
#include "ctests.h"
#include "detached/detached.h"

/* -- Geometria normal ------------------------------------------------------ */

static void test_geom_normal(void) {
    int w = 640, h = 480;

    /* la tira de pestanas: franja superior a todo el ancho, alto fijo */
    Rect tb = detached_tabbar_rect(w, h);
    EXPECT_EQ_INT(tb.x, 0);
    EXPECT_EQ_INT(tb.y, 0);
    EXPECT_EQ_INT(tb.w, w);
    EXPECT_EQ_INT(tb.h, DETACHED_TABBAR_H);

    /* el contenido empieza justo bajo la tira y ocupa el resto */
    Rect ct = detached_content_rect(w, h);
    EXPECT_EQ_INT(ct.x, 0);
    EXPECT_EQ_INT(ct.y, DETACHED_TABBAR_H);
    EXPECT_EQ_INT(ct.w, w);
    EXPECT_EQ_INT(ct.h, h - DETACHED_TABBAR_H);

    /* tira y contenido se tocan sin solaparse ni dejar hueco */
    EXPECT_EQ_INT(tb.y + tb.h, ct.y);
    /* juntos cubren toda la altura de la ventana */
    EXPECT_EQ_INT(ct.y + ct.h, h);
}

/* -- Casos degenerados ----------------------------------------------------- */

static void test_geom_degenerate(void) {
    /* ventana mas baja que la tira: el contenido queda con alto 0 (no negativo) */
    Rect ct = detached_content_rect(300, 10);
    EXPECT_EQ_INT(ct.h, 0);
    EXPECT_EQ_INT(ct.y, DETACHED_TABBAR_H);
    EXPECT_EQ_INT(ct.w, 300);

    /* ventana exactamente del alto de la tira: contenido de alto 0 */
    ct = detached_content_rect(300, DETACHED_TABBAR_H);
    EXPECT_EQ_INT(ct.h, 0);

    /* ancho negativo: se acota a 0 (rect valido) */
    Rect tb = detached_tabbar_rect(-50, 400);
    EXPECT_EQ_INT(tb.w, 0);
    ct = detached_content_rect(-50, 400);
    EXPECT_EQ_INT(ct.w, 0);
    EXPECT_EQ_INT(ct.h, 400 - DETACHED_TABBAR_H);
}

int main(void) {
    tt_suite("detached");
    tt_run("geometria normal de tira + contenido", test_geom_normal);
    tt_run("casos degenerados acotados", test_geom_degenerate);
    return tt_summary();
}
