#pragma once
/**
 * @file bg_core.h
 * @brief Geometria pura del fondo del area de texto (sin SDL).
 *
 * Calcula como encaja una imagen de @c iw x @c ih dentro de un area de
 * @c aw x @c ah segun el modo de escalado (::BgScale).  Es logica de enteros/
 * float pura, sin dependencias de SDL ni del Editor, para poder probarla
 * headless y reutilizarla tanto en el render del editor como en la
 * previsualizacion de la pantalla de fondos.
 */

/** Rectangulo destino en coordenadas de pantalla (float, relativo al area). */
typedef struct {
    float x, y, w, h;
} BgRect;

/**
 * @brief Destino de la imagen para los modos de un solo blit.
 *
 * Cubre AJUSTAR, RELLENAR, ESTIRAR y CENTRAR (no MOSAICO, que repite).  El
 * rectangulo devuelto esta en coordenadas absolutas de pantalla: se le suma el
 * origen (@p ax, @p ay) del area.  El llamante debe recortar (clip) al area
 * cuando @p *out_needs_clip sea 1 (RELLENAR y CENTRAR pueden desbordar).
 *
 * @param scale         Modo de escalado (::BgScale); valores no de blit unico
 *                      (MOSAICO) se tratan como ESTIRAR.
 * @param ax,ay,aw,ah   Origen y tamano del area destino (px).
 * @param iw,ih         Tamano nativo de la imagen (px).
 * @param out_needs_clip [out] 1 si el destino puede desbordar el area.
 * @return Rectangulo destino; w/h 0 si la imagen o el area son degenerados.
 */
BgRect bg_image_dst(int scale, int ax, int ay, int aw, int ah, int iw, int ih,
                    int *out_needs_clip);

/**
 * @brief Numero de repeticiones en cada eje para el modo MOSAICO.
 *
 * @param aw,ah Tamano del area (px).
 * @param iw,ih Tamano nativo de la imagen (px).
 * @param[out] out_cols  Columnas de mosaico necesarias para cubrir el ancho.
 * @param[out] out_rows  Filas de mosaico necesarias para cubrir el alto.
 */
void bg_tile_count(int aw, int ah, int iw, int ih, int *out_cols,
                   int *out_rows);
