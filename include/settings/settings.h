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

/** Modo del fondo del area de texto. */
typedef enum {
    BG_MODE_NONE = 0,  /**< Sin fondo: solo el color del tema.        */
    BG_MODE_IMAGE = 1, /**< Imagen cargada desde @c background_path.  */
    BG_MODE_COLOR = 2  /**< Color solido @c background_color.         */
} BgMode;

/** Forma de encajar la imagen de fondo dentro del area de texto. */
typedef enum {
    BG_SCALE_FIT = 0,     /**< Ajustar: cabe entera, conserva proporcion. */
    BG_SCALE_FILL = 1,    /**< Rellenar: cubre el area, recorta sobrante. */
    BG_SCALE_STRETCH = 2, /**< Estirar: deforma hasta llenar el area.     */
    BG_SCALE_CENTER = 3,  /**< Centrar: tamano nativo, recortado al area. */
    BG_SCALE_TILE = 4     /**< Mosaico: repite el tamano nativo.          */
} BgScale;

/** Color de fondo por defecto en modo color (gris muy oscuro 0xRRGGBB). */
#define SETTINGS_BG_COLOR_DEFAULT 0x101015u

/** Opacidad por defecto del fondo (0..255); coincide con el valor historico. */
#define SETTINGS_BG_OPACITY_DEFAULT 180

/** Tope de imagenes guardadas en la galeria de fondos. */
#define BG_GALLERY_MAX 32

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
    char background_path[512]; /**< Ruta a la imagen de fondo activa.         */
    int background_mode; /**< Modo del fondo (::BgMode).                     */
    unsigned int background_color; /**< Color solido 0xRRGGBB (modo color).  */
    int background_opacity; /**< Opacidad del fondo [0..255].                */
    int background_scaling; /**< Encaje de la imagen (::BgScale).            */
    /** Galeria de imagenes de fondo: rutas que el usuario va acumulando para
     *  elegir entre ellas en la sub-pantalla "Fondos". */
    char background_gallery[BG_GALLERY_MAX][512];
    int background_gallery_count; /**< Numero de rutas validas en la galeria. */
} Settings;

/**
 * @brief Anyade @p path a la galeria de fondos si no esta ya (dedup) y hay sitio.
 *
 * Ignora rutas nulas o vacias.  No persiste (el llamante decide cuando guardar).
 *
 * @return Indice de la entrada (nueva o existente), o -1 si no se pudo anyadir
 *         (ruta invalida o galeria llena).
 */
int settings_gallery_add(Settings *s, const char *path);

/**
 * @brief Quita la entrada @p index de la galeria, compactando el resto.
 *        No hace nada si @p index esta fuera de rango.
 */
void settings_gallery_remove(Settings *s, int index);

/**
 * @brief Indice de @p path en la galeria, o -1 si no esta (o es vacio).
 */
int settings_gallery_index_of(const Settings *s, const char *path);

/** Rellena @p s con los valores por defecto. */
void settings_defaults(Settings *s);

/** Carga las preferencias: parte de los defaults y aplica lo que haya en
 *  @c settings.ini (las claves ausentes mantienen su valor por defecto). */
void settings_load(Settings *s);

/** Guarda todas las preferencias en @c settings.ini (lo crea/sobrescribe). */
void settings_save(const Settings *s);
