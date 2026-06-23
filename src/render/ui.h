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

/* ::UiStyle (estilo de botón) vive en render/theme.h, porque forma parte del
 * tema: los estilos concretos están en e->theme.style_button/primary/nav. */

/** Construye un ::Color a partir de sus componentes. */
static inline Color ui_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Color c = {r, g, b, a};
    return c;
}

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

/**
 * @brief Chip toggle on/off reutilizable (componente generico).
 *
 * Dibuja una caja compacta con @p label, coloreada segun @p on (activo) /
 * apagado, y registra su rectangulo bajo @p id para el hit-test.  Pensado para
 * barras de opciones (p.ej. la vista godbolt del hover) pero usable por
 * cualquier seccion/extension.  No muta estado: el llamante decide que hacer
 * en el click (consultando ui_hit con @p id).
 *
 * @param id     Id de hit-test (::UI_ID_NONE para no registrar).
 * @param x,y    Esquina superior izquierda.
 * @param label  Texto del chip.
 * @param on     1 = activo (resaltado), 0 = apagado (atenuado).
 * @return Ancho total del chip en px (para encadenar varios en una barra).
 */
int ui_toggle(Editor *e, UiId id, int x, int y, const char *label, int on);

/** Callback que dibuja el contenido de una fila de ::ui_list. */
typedef void (*UiRowDraw)(Editor *e, int index, Rect row, int selected,
                          void *ud);

/**
 * @brief Lista vertical con scroll y selección (componente reutilizable).
 *
 * Pinta el panel, las filas visibles (su contenido lo dibuja @p draw_row), el
 * resaltado de la fila seleccionada y un pulgar de scroll. Registra el área
 * (@p area_id) y cada fila visible (@p row_list + índice) en e->ui para que el
 * input resuelva clics y rueda con ui_hit / ui_hit_idx.
 *
 * @param bounds   Rectángulo de la lista.
 * @param area_id  Id para registrar el área completa (rueda del ratón).
 * @param row_list Familia indexada para registrar cada fila.
 * @param count    Nº total de filas.
 * @param row_h    Alto de cada fila en px.
 * @param scroll   [in/out] primera fila visible; se recorta a rango válido.
 * @param selected Fila seleccionada (-1 = ninguna).
 * @param draw_row Callback que pinta el contenido de cada fila.
 * @param ud       Dato opaco que se pasa a @p draw_row.
 */
void ui_list(Editor *e, Rect bounds, UiId area_id, UiList row_list, int count,
             int row_h, int *scroll, int selected, UiRowDraw draw_row,
             void *ud);
