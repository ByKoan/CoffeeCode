#pragma once
/* Header PRIVADO del modulo render: paleta de colores + utilidades y
   renderizadores de seccion compartidos entre los .c de render. No es API
   publica. Cada COL_* / TXT_* se expande a los componentes de color
   "desempaquetados" (R, G, B, A para los COL_*; R, G, B para los TXT_*),
   pensados para pasarse directamente como argumentos a set_color() y
   draw_text(). */
#include "editor/editor.h"
#include "filetree/filetree.h"
#include "render/render.h"

/* Los colores ya no son macros: viven en el tema en runtime (render/theme.h),
   se leen como e->theme.<campo> y se pintan con set_color_c / draw_text_c. */

/* -- Scrollbar ------------------------------------------------------------- */
#define SCROLLBAR_W 8 /* ancho de la barra de scroll (px) */

/* Utilidades de dibujo compartidas (implementadas en render.c) */
/* color actual */
void set_color(SDL_Renderer *r, uint8_t R, uint8_t G, uint8_t B, uint8_t A);
/* color actual desde un Color del tema (e->theme.*) */
void set_color_c(SDL_Renderer *r, Color c);
/* relleno, color actual  */
void fill_rect(SDL_Renderer *r, int x, int y, int w, int h);
/* contorno 1px, color actual */
void stroke_rect(SDL_Renderer *r, int x, int y, int w, int h);
/* draw_text: rasteriza texto con SDL_ttf y devuelve su ancho en px (0 si nada).
 */
int draw_text(Editor *e, const char *text, int x, int y, uint8_t R, uint8_t G,
              uint8_t B);
/* draw_text desde un Color del tema (usa c.r/c.g/c.b; ignora el alfa) */
int draw_text_c(Editor *e, const char *text, int x, int y, Color c);
/* draw_text con una fuente concreta (para previsualizar fuentes del sistema) */
int draw_text_font(Editor *e, TTF_Font *font, const char *text, int x, int y,
                   Color c);
/* get_line_text: copia la línea a 'out' expandiendo tabs; devuelve su longitud.
 */
int get_line_text(Editor *e, int line, char *out, int max);

/* Bordes del area de contenido del editor (respetan la division en paneles).
 * Sin division devuelven 0 y e->win_w; con division, el sub-rect del panel. */
int render_content_left(Editor *e);
int render_content_right(Editor *e);

/* Renderizadores de seccion (invocados desde render_frame) */
void render_navbar(Editor *e);   /* barra superior + título      */
void render_menu(Editor *e);     /* desplegable "Archivo"        */
void render_filetree(Editor *e); /* panel lateral abierto        */
void render_filetree_toggle_closed(Editor *e); /* botón para abrir el panel */
void render_tabbar(Editor *e);    /* barra de pestañas + botón "+"*/
/* Barra de pestañas de UNA hoja del editor dividido: dibuja solo las pestañas
 * cuyo tab.group == @p group dentro de la franja [pane_left, pane_right) y a la
 * altura @p bar_y (borde superior del rect de la hoja), y registra su geometría
 * (UI_LIST_TAB / UI_LIST_TAB_CLOSE por índice global, y UI_LIST_SPLIT_NEW por
 * número de grupo para el botón "+"). */
void render_tabbar_group(Editor *e, int group, int bar_y, int pane_left,
                         int pane_right);
/* Guia visual del arrastre de una pestana (drag-to-dock): superpone un overlay
 * translucido sobre la zona destino (hoja completa para CENTER, mitad/banda para
 * los bordes) y un "fantasma" del titulo junto al cursor.  No dibuja nada si no
 * hay un arrastre en curso (e->dragging_tab == 0). */
void render_tab_drag(Editor *e);
/* Multi-ventana: resalta esta ventana como DESTINO de una pestana arrastrada
 * desde otra ventana (velo translucido + marco de acento de "soltar aqui").  No
 * dibuja nada si e->drag_hover_highlight == 0 (caso por defecto y con una sola
 * ventana): cero regresion. */
void render_drag_window_highlight(Editor *e);
/* Dibuja los paneles flotantes (overlay dentro de la ventana) ENCIMA del dock:
 * por cada flotante en z-order (atras->delante) su marco, barra de titulo (con
 * nombre de la pestana activa + botones acoplar y cerrar), su tira de pestanas y
 * su contenido recortado, y la esquina de redimension.  No dibuja nada si no hay
 * flotantes (e->float_count == 0): cero regresion. */
void render_floats(Editor *e);
/* Guia visual del re-acople de un flotante por arrastre de su barra de titulo:
 * superpone el MISMO overlay de zona destino que render_tab_drag sobre la hoja
 * del dock donde caera (hoja completa para CENTER, mitad/banda para los bordes).
 * No dibuja nada si no hay un flotante en arrastre con destino de acople
 * (e->float_dock_target_group < 0). */
void render_float_dock_guide(Editor *e);
void render_find_bar(Editor *e);  /* barra de búsqueda (Ctrl+F)   */
void render_shortcuts(Editor *e); /* banda de atajos (badges)     */
void render_scrollbar(Editor *e, int left_offset); /* scroll vertical */
/* selección */
void render_selection(Editor *e, int left_offset, int text_top,
                      int visible_lines);
/* pantalla de preferencias (a pantalla completa) */
void render_settings_view(Editor *e);
/* sub-pantalla "Fondos" de preferencias (a pantalla completa) */
void render_background_view(Editor *e);
/* dibuja el fondo configurado bajo el area de texto del editor */
void render_background_area(Editor *e);
/* pinta el fondo configurado (color/imagen) dentro de un rectangulo dado;
 * lo usa la previsualizacion de la sub-pantalla "Fondos" */
void render_background_preview(Editor *e, SDL_FRect area);
/* popup del selector de codificación (desde la barra de estado) */
void render_enc_popup(Editor *e);

/* -- Sistema de extensiones ----------------------------------------- */
/* Dibuja las vistas registradas por extensiones (register_view): paneles y
 * sidebars de extension, con un CoffeePainter recortado a su area. */
void render_ext_views(Editor *e);
/* Dibuja el panel de extensiones del IDE (lista de cargadas + acciones). */
void render_ext_panel(Editor *e);
/* Geometria de la X (px) donde el panel de extensiones empieza, para que el
 * resto del cromo no lo pise. 0 si el panel esta cerrado. */
int render_ext_panel_width(Editor *e);

/* -- Panel inferior (Salida/Logs/Terminal) -------------------------------- */
/* Dibuja el panel inferior con su tira de pestanas y el canal activo. */
void render_bottom_panel(Editor *e);
/* Alto (px) que ocupa el panel inferior, 0 si esta cerrado. */
int render_bottom_panel_height(Editor *e);
/* Alto (px) de la tira de pestanas del panel inferior. */
#define BOTTOM_TAB_H 26
