/**
 * @file detached_geom.c
 * @brief Geometria pura de las ventanas desprendidas (ver detached/detached.h).
 *
 * Calcula los sub-rectangulos de una ventana desprendida (tira de pestanas y
 * area de contenido) a partir de su tamano.  Sin dependencias de SDL ni de la
 * struct Editor: todo opera sobre ::Rect y enteros, asi que se prueba en
 * headless (ver test/detached/test_detached.c).
 */
#include "detached/detached.h"

Rect detached_tabbar_rect(int win_w, int win_h) {
    (void)win_h;                    /* la tira solo depende del ancho */
    if (win_w < 0) win_w = 0;
    Rect r = {0, 0, win_w, DETACHED_TABBAR_H};
    return r;
}

Rect detached_content_rect(int win_w, int win_h) {
    if (win_w < 0) win_w = 0;
    int top = DETACHED_TABBAR_H;    /* el contenido empieza bajo la tira */
    int h = win_h - DETACHED_TABBAR_H;
    if (h < 0) h = 0;               /* ventana mas baja que la tira: sin contenido */
    Rect r = {0, top, win_w, h};
    return r;
}
