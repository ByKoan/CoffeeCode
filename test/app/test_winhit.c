/**
 * @file test_winhit.c
 * @brief Tests puros de la resolucion de ventana bajo un punto global del raton
 *        (multi-ventana), sin SDL ni Editor.
 *
 * Modela el "soltar una pestana fuera de su ventana": dada la geometria de
 * pantalla de cada ventana (esquina + tamano), decidir sobre cual cae el cursor
 * global o si cayo en el escritorio (tear-off).  La misma aritmetica que aplica
 * @c app_window_at_global sobre los SDL_Window reales.
 */
#include "ctests.h"
#include "app/winhit.h"

/* El rango es [x,x+w) x [y,y+h): borde sup/izq inclusivo, inf/der exclusivo. */
static void test_in_rect_bordes(void) {
    WinRect r = {10, 20, 100, 50}; /* cubre x:[10,110) y:[20,70) */
    EXPECT_EQ_INT(win_point_in_rect(r, 10, 20), 1); /* esquina sup-izq: dentro */
    EXPECT_EQ_INT(win_point_in_rect(r, 109, 69), 1);/* casi esquina inf-der      */
    EXPECT_EQ_INT(win_point_in_rect(r, 110, 40), 0);/* borde derecho: exclusivo  */
    EXPECT_EQ_INT(win_point_in_rect(r, 50, 70), 0); /* borde inferior: exclusivo */
    EXPECT_EQ_INT(win_point_in_rect(r, 9, 40), 0);  /* a la izquierda: fuera     */
    EXPECT_EQ_INT(win_point_in_rect(r, 50, 19), 0); /* por encima: fuera         */
}

/* Un rect degenerado (w o h <= 0) no contiene ningun punto. */
static void test_in_rect_degenerado(void) {
    WinRect r0 = {0, 0, 0, 50};
    WinRect r1 = {0, 0, 50, 0};
    WinRect r2 = {0, 0, -5, -5};
    EXPECT_EQ_INT(win_point_in_rect(r0, 0, 0), 0);
    EXPECT_EQ_INT(win_point_in_rect(r1, 0, 0), 0);
    EXPECT_EQ_INT(win_point_in_rect(r2, 0, 0), 0);
}

/* Varias ventanas sin solape: el punto resuelve a la que lo contiene, o -1. */
static void test_at_point_varias(void) {
    WinRect rects[3] = {
        {0, 0, 100, 100},     /* ventana 0 */
        {200, 0, 100, 100},   /* ventana 1 (separada en X) */
        {0, 200, 100, 100},   /* ventana 2 (separada en Y) */
    };
    EXPECT_EQ_INT(win_at_point(rects, 3, 50, 50), 0);    /* dentro de la 0 */
    EXPECT_EQ_INT(win_at_point(rects, 3, 250, 50), 1);   /* dentro de la 1 */
    EXPECT_EQ_INT(win_at_point(rects, 3, 50, 250), 2);   /* dentro de la 2 */
    EXPECT_EQ_INT(win_at_point(rects, 3, 150, 50), -1);  /* hueco entre 0 y 1 */
    EXPECT_EQ_INT(win_at_point(rects, 3, 500, 500), -1); /* escritorio (tear-off) */
}

/* Con solape gana el PRIMERO de la lista (el llamante pone la enfocada delante). */
static void test_at_point_solape_primero_gana(void) {
    WinRect rects[2] = {
        {0, 0, 100, 100}, /* enfocada (va primera) */
        {50, 50, 100, 100},
    };
    EXPECT_EQ_INT(win_at_point(rects, 2, 75, 75), 0); /* en el solape: la primera */
    EXPECT_EQ_INT(win_at_point(rects, 2, 120, 120), 1); /* solo en la segunda */
}

/* Lista vacia o nula: siempre -1. */
static void test_at_point_vacio(void) {
    WinRect rects[1] = {{0, 0, 100, 100}};
    EXPECT_EQ_INT(win_at_point(rects, 0, 50, 50), -1);
    EXPECT_EQ_INT(win_at_point(NULL, 3, 50, 50), -1);
}

int main(void) {
    tt_suite("winhit");
    tt_run("punto dentro del rect respeta bordes [x,x+w)", test_in_rect_bordes);
    tt_run("rect degenerado no contiene nada", test_in_rect_degenerado);
    tt_run("resolver ventana entre varias o escritorio", test_at_point_varias);
    tt_run("con solape gana el primero de la lista",
           test_at_point_solape_primero_gana);
    tt_run("lista vacia o nula devuelve -1", test_at_point_vacio);
    return tt_summary();
}
