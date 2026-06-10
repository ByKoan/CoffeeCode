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

/* Renderizadores de seccion (invocados desde render_frame) */
void render_navbar(Editor *e);   /* barra superior + título      */
void render_menu(Editor *e);     /* desplegable "Archivo"        */
void render_filetree(Editor *e); /* panel lateral abierto        */
void render_filetree_toggle_closed(Editor *e); /* botón para abrir el panel */
void render_tabbar(Editor *e);    /* barra de pestañas + botón "+"*/
void render_find_bar(Editor *e);  /* barra de búsqueda (Ctrl+F)   */
void render_shortcuts(Editor *e); /* banda de atajos (badges)     */
void render_scrollbar(Editor *e, int left_offset); /* scroll vertical */
/* selección */
void render_selection(Editor *e, int left_offset, int text_top,
                      int visible_lines);
/* pantalla de preferencias (a pantalla completa) */
void render_settings_view(Editor *e);
/* popup del selector de codificación (desde la barra de estado) */
void render_enc_popup(Editor *e);
