#pragma once
/**
 * @file layout.h
 * @brief Logica de divisores arrastrables entre las regiones de la interfaz.
 *
 * CoffeeCode dibuja sus regiones (explorador a la izquierda, editor al centro,
 * panel de extensiones a la derecha) con anchos guardados en el estado del
 * editor.  Este modulo concentra el hit-testing y el clampeo de esos anchos
 * cuando el usuario arrastra el borde de un panel para redimensionarlo.
 *
 * Es la base de un futuro sistema de "dock" (paneles divididos / flotantes /
 * multi-ventana): por eso la logica de calculo vive aqui en funciones LIMPIAS y
 * reutilizables, no enredada en el render ni en el input.  De momento solo hay
 * divisores arrastrables sobre las regiones actuales (sin arbol de dock).
 *
 * El nucleo son funciones PURAS que reciben enteros crudos (ancho de ventana,
 * ancho actual del panel, coordenadas del raton): no dependen de SDL ni de la
 * struct Editor, asi que se pueden probar en headless.  Encima hay envoltorios
 * finos que toman un Editor y delegan en las puras.
 */

/* Declaracion hacia delante del Editor: los envoltorios solo necesitan el
 * puntero.  Asi esta cabecera NO arrastra editor/editor.h (que incluye SDL), y
 * las funciones PURAS de abajo se pueden probar en headless sin SDL. */
typedef struct Editor Editor;

/**
 * @brief Identificadores de los divisores arrastrables.
 *
 * Cada divisor es la franja fina del borde interior de un panel que el usuario
 * puede agarrar para redimensionarlo.
 */
typedef enum {
    DIVIDER_NONE = -1,       /**< ningun divisor bajo el cursor             */
    DIVIDER_FILETREE_RIGHT,  /**< borde derecho del explorador (vertical)   */
    DIVIDER_EXT_PANEL_LEFT,  /**< borde izquierdo del panel de extensiones  */
} LayoutDivider;

/* -- Geometria de los divisores ------------------------------------------- */

/** Anchura (px) de la franja agarrable a cada lado del borde de un panel. */
#define LAYOUT_DIVIDER_GRAB 5

/* Limites de redimension (px). */
#define LAYOUT_EXT_MIN_W 180 /**< ancho minimo del panel de extensiones */
#define LAYOUT_EXT_DEFAULT_W 280 /**< ancho inicial del panel de extensiones */
/* Ancho minimo del explorador.  Debe coincidir con FTREE_MIN_WIDTH de
 * filetree.h; layout.c lo comprueba con un static assert para evitar drift.
 * Se define aqui (y no se incluye filetree.h) para que la logica pura quede
 * libre de SDL y testeable en headless. */
#define LAYOUT_FTREE_MIN_W 80

/* ===========================================================================
 *  Funciones puras (enteros crudos; testeables sin SDL ni Editor)
 * =========================================================================== */

/**
 * @brief Decide si una coordenada X cae sobre una franja vertical agarrable.
 *
 * La franja esta centrada en @p edge_x y tiene un ancho total de
 * 2*::LAYOUT_DIVIDER_GRAB; ademas el cursor debe estar dentro de la banda
 * vertical [@p region_top, @p region_bottom) del panel.
 *
 * @param mx           X del raton en pixeles.
 * @param my           Y del raton en pixeles.
 * @param edge_x       X del borde (centro de la franja agarrable).
 * @param region_top   Y donde empieza la region redimensionable.
 * @param region_bottom Y donde termina la region redimensionable (exclusivo).
 * @return 1 si (mx,my) cae sobre la franja, 0 si no.
 */
int layout_point_on_vertical_edge(int mx, int my, int edge_x, int region_top,
                                  int region_bottom);

/**
 * @brief Recorta el ancho del explorador a su rango valido.
 *
 * @param desired_w Ancho propuesto en pixeles.
 * @param win_w     Ancho de la ventana en pixeles.
 * @return Ancho recortado a [FTREE_MIN_WIDTH, win_w/2].
 */
int layout_clamp_filetree_w(int desired_w, int win_w);

/**
 * @brief Recorta el ancho del panel de extensiones a su rango valido.
 *
 * @param desired_w Ancho propuesto en pixeles.
 * @param win_w     Ancho de la ventana en pixeles.
 * @return Ancho recortado a [::LAYOUT_EXT_MIN_W, win_w/2].
 */
int layout_clamp_ext_panel_w(int desired_w, int win_w);

/* ===========================================================================
 *  Envoltorios sobre Editor (resuelven la geometria desde su estado)
 * =========================================================================== */

/**
 * @brief Devuelve el divisor que esta bajo el cursor, o ::DIVIDER_NONE.
 *
 * Solo considera divisores de paneles que esten ABIERTOS; un panel cerrado no
 * tiene borde agarrable.
 *
 * @param e  Editor (aporta tamanos de ventana y de los paneles).
 * @param mx X del raton en pixeles.
 * @param my Y del raton en pixeles.
 * @return El ::LayoutDivider bajo el cursor o ::DIVIDER_NONE.
 */
int layout_hit_divider(Editor *e, int mx, int my);

/**
 * @brief Aplica un arrastre de divisor: ajusta el ancho del panel afectado.
 *
 * Recalcula el ancho del panel segun la posicion del cursor y lo recorta a su
 * rango valido con las funciones de clampeo puras.  No toca @c needs_redraw (eso
 * lo decide el llamador).
 *
 * @param e     Editor a modificar.
 * @param which Divisor que se esta arrastrando (::LayoutDivider).
 * @param mx    X actual del raton en pixeles.
 * @param my    Y actual del raton en pixeles (sin uso en divisores verticales).
 */
void layout_apply_divider_drag(Editor *e, int which, int mx, int my);
