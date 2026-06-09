#pragma once
/**
 * @file ui.h
 * @brief Capa de componentes de UI inmediata (privada del módulo render).
 *
 * Construida sobre los primitivos de render (set_color/fill_rect/stroke_rect/
 * draw_text) y el registro de hit-test (render/ui_hit.h). Evita reimplementar
 * en cada sección el mismo esqueleto "caja + borde + label medido + registrar
 * para el click". El render dibuja con estas funciones; input consulta la
 * geometría con ui_hit()/ui_hit_idx().
 */
#include "editor/editor.h"
#include "render/ui_hit.h"

/** Estado visual de un control interactivo. */
typedef enum { UI_NORMAL, UI_HOVER, UI_ACTIVE, UI_DISABLED } UiState;

/** Estilo de una caja/botón: colores de fondo por estado, borde y texto. */
typedef struct {
    Color bg;        /**< Fondo en estado normal.            */
    Color bg_hover;  /**< Fondo bajo el ratón.               */
    Color bg_active; /**< Fondo pulsado/activo.              */
    Color border;    /**< Borde (si @c a==0, sin borde).     */
    Color text;      /**< Color del label.                   */
} UiStyle;

/** Construye un ::Color a partir de sus componentes. */
static inline Color ui_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Color c = {r, g, b, a};
    return c;
}

/* ── Tema: estilos de botón compartidos (definidos en ui.c) ──────────────────
 * Centralizan el aspecto de cada tipo de botón en un único sitio: para cambiar
 * el color de, p. ej., todos los botones de acción, se edita aquí. */
extern const UiStyle UI_STYLE_BUTTON; /**< Botón secundario (caja gris).      */
extern const UiStyle UI_STYLE_PRIMARY; /**< Botón de acción (acento azul). */
extern const UiStyle UI_STYLE_NAV; /**< Botón de la barra de navegación.   */

/**
 * @brief Dibuja una caja rellena con borde opcional.
 * @param bg     Color de relleno.
 * @param border Color del borde; si su alfa es 0 no se dibuja borde.
 */
void ui_panel(Editor *e, Rect r, Color bg, Color border);

/**
 * @brief Dibuja @p text en (@p x, @p y) con el color @p c.
 * @return Ancho del texto dibujado en píxeles.
 */
int ui_label(Editor *e, int x, int y, const char *text, Color c);

/**
 * @brief Botón rectangular: fondo según @p state, borde y label centrado.
 * @param id    Id de hit-test a registrar (::UI_ID_NONE para no registrar).
 * @param r     Rectángulo del botón.
 * @param label Texto centrado (puede ser @c NULL/"").
 * @param st    Estilo (colores por estado).
 * @param state Estado visual (normal/hover/activo).
 */
void ui_button(Editor *e, UiId id, Rect r, const char *label, const UiStyle *st,
               UiState state);
