#pragma once
/**
 * @file fonts.h
 * @brief Descubrimiento de fuentes instaladas en el sistema.
 *
 * Escanea las carpetas estándar de fuentes del sistema operativo (Windows:
 * @c %WINDIR%\\Fonts y las del usuario; Linux: @c /usr/share/fonts, @c
 * ~/.fonts, etc.) en busca de archivos @c .ttf/@c .ttc/@c .otf y construye una
 * lista ordenada de fuentes seleccionables desde las preferencias.
 */
#include <stddef.h>

/** Una fuente encontrada: nombre legible + ruta completa al archivo. */
typedef struct {
    char name[96];  /**< Nombre a mostrar (archivo sin extensión). */
    char path[512]; /**< Ruta completa al archivo de la fuente.     */
} FontEntry;

/** Lista dinámica de fuentes encontradas. */
typedef struct {
    FontEntry *items; /**< Array de fuentes (malloc'd).      */
    int count;        /**< Nº de fuentes.                    */
    int cap;          /**< Capacidad reservada.              */
} FontList;

/** Escanea el sistema y rellena @p list (ordenada alfabéticamente, sin dupes).
 */
void fonts_scan(FontList *list);

/** Libera la memoria de @p list y la deja vacía. */
void fonts_free(FontList *list);

/** Índice de la fuente cuya ruta es @p path, o -1 si no está (o @p path vacío).
 */
int fonts_index_of(const FontList *list, const char *path);

/** Nombre a mostrar para @p path, o "Predeterminada" si está vacío/no listado.
 */
const char *fonts_name_for(const FontList *list, const char *path);
