#pragma once
/**
 * @file winhit.h
 * @brief Geometria PURA (sin SDL ni Editor) para resolver que ventana del
 *        escritorio contiene un punto global del raton (multi-ventana).
 *
 * Al arrastrar una pestana FUERA de su ventana, hay que decidir sobre que
 * ventana se solto (para moverla ahi) o si cayo en el escritorio (para crear una
 * ventana nueva, tear-off).  Cada ventana del SO ocupa un rectangulo en
 * coordenadas de PANTALLA: esquina (x,y) + tamano (w,h).  Esta cabecera modela
 * el "punto dentro del rect de una ventana" y la busqueda lineal "que ventana
 * (de una lista de rects) contiene el punto" sobre enteros planos, para poder
 * probarla en headless; la misma aritmetica que aplica @c app_window_at_global
 * sobre los SDL_Window reales.
 */

/** Rectangulo de una ventana en coordenadas de pantalla (px). */
typedef struct {
    int x; /**< borde izquierdo en pantalla   */
    int y; /**< borde superior en pantalla     */
    int w; /**< ancho                          */
    int h; /**< alto                           */
} WinRect;

/**
 * @brief Indica si el punto global (@p gx,@p gy) cae dentro de @p r.
 *
 * El borde superior/izquierdo es inclusivo y el inferior/derecho exclusivo
 * (rango [x, x+w) x [y, y+h)).  Un rect de ancho/alto <= 0 no contiene ningun
 * punto.
 *
 * @return 1 si el punto esta dentro; 0 si no.
 */
int win_point_in_rect(WinRect r, int gx, int gy);

/**
 * @brief Devuelve el indice del PRIMER rect de @p rects (longitud @p n) que
 *        contiene el punto global (@p gx,@p gy), o -1 si ninguno.
 *
 * La preferencia "primero gana" deja que el llamante ordene la lista por
 * prioridad (p.ej. la ventana enfocada o la de mas arriba en el z-order
 * primero).  Sin solapes el orden es indiferente.
 *
 * @param rects Array de rects de ventana en coordenadas de pantalla.
 * @param n     Numero de rects.
 * @param gx    X global del cursor.
 * @param gy    Y global del cursor.
 * @return Indice del rect que contiene el punto, o -1.
 */
int win_at_point(const WinRect *rects, int n, int gx, int gy);
