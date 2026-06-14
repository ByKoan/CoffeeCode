#pragma once
/**
 * @file settings.h
 * @brief Preferencias persistentes del editor (tema, fuente, tabulación,
 *        autoguardado, números de línea).
 *
 * Se cargan al arrancar y se guardan al cambiar, en un archivo @c settings.ini
 * (formato @c clave=valor) dentro de la carpeta de configuración del usuario
 * que proporciona @c SDL_GetPrefPath (multiplataforma y escribible).
 */
#include <stddef.h>

/* Rangos válidos para los ajustes numéricos (se recortan al cargar). */
#define SETTINGS_TAB_MIN 1
#define SETTINGS_TAB_MAX 16
#define SETTINGS_FONT_MIN 10
#define SETTINGS_FONT_MAX 24

/** Preferencias del editor. */
typedef struct {
    int theme;     /**< Índice del preset de tema (0 = oscuro).        */
    int tab_width; /**< Ancho de tabulación en espacios [1..16].       */
    int font_size; /**< Tamaño de fuente en px [8..48].                */
    int autosave;  /**< 1 = autoguardado activado.                     */
    int show_line_numbers; /**< 1 = mostrar el gutter de números de línea. */
    int highlight_current_line; /**< 1 = resaltar la banda de la línea activa.
                                 */
    int show_shortcuts;  /**< 1 = mostrar la barra de atajos inferior.       */
    char font_path[512]; /**< Ruta a la fuente; "" = fuente por defecto.     */
    char background_path[512]; /**< Ruta a imagen de fondo personalizado.     */
    int background_enabled; /**< 1 = usar fondo personalizado.                */
} Settings;

/** Rellena @p s con los valores por defecto. */
void settings_defaults(Settings *s);

/** Carga las preferencias: parte de los defaults y aplica lo que haya en
 *  @c settings.ini (las claves ausentes mantienen su valor por defecto). */
void settings_load(Settings *s);

/** Guarda todas las preferencias en @c settings.ini (lo crea/sobrescribe). */
void settings_save(const Settings *s);
