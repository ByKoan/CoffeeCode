/**
 * @file winhit.c
 * @brief Implementacion pura de la resolucion de ventana bajo un punto global
 *        del raton (ver app/winhit.h).  Sin SDL ni Editor: probada en headless.
 */
#include "app/winhit.h"

int win_point_in_rect(WinRect r, int gx, int gy) {
    if (r.w <= 0 || r.h <= 0) return 0; /* rect degenerado: no contiene nada */
    return gx >= r.x && gx < r.x + r.w && gy >= r.y && gy < r.y + r.h;
}

int win_at_point(const WinRect *rects, int n, int gx, int gy) {
    if (!rects) return -1;
    for (int i = 0; i < n; i++)
        if (win_point_in_rect(rects[i], gx, gy)) return i; /* primero gana */
    return -1;
}
