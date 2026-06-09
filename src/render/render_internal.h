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

/* -- Colores del tema (editor) --------------------------------------------- */
#define COL_BG 0x28, 0x2C, 0x34, 0xFF     /* fondo general del editor       */
#define COL_GUTTER 0x21, 0x25, 0x2B, 0xFF /* fondo del gutter (nº de línea) */
/* banda de la línea activa       */
#define COL_CURSOR_LINE 0x2C, 0x31, 0x3C, 0xFF
#define COL_CURSOR 0x52, 0x8B, 0xFF, 0xFF /* barra del cursor (azul)        */
/* fondo de la barra de estado    */
#define COL_STATUS_BG 0x21, 0x25, 0x2B, 0xFF
#define COL_SEL_BG 0x26, 0x4F, 0x78, 0xFF /* azul selección */

/* Navbar y menú */
#define COL_NAVBAR_BG 0x1A, 0x1D, 0x23, 0xFF
#define COL_NAVBAR_BTN 0x2C, 0x31, 0x3C, 0xFF /* hover del botón */
#define COL_MENU_BG 0x1E, 0x22, 0x2A, 0xFF    /* fondo del desplegable        */
#define COL_MENU_HOVER 0x3E, 0x44, 0x55, 0xFF /* entrada bajo el ratón */
#define COL_MENU_SEP 0x3A, 0x3F, 0x4A, 0xFF   /* línea separadora del menú    */
/* borde del menú               */
#define COL_MENU_BORDER 0x3A, 0x3F, 0x4A, 0xFF

/* Colores panel lateral (filetree) */
#define COL_FTREE_BG 0x1E, 0x22, 0x2A, 0xFF /* fondo del panel              */
/* entrada bajo el ratón        */
#define COL_FTREE_HOVER 0x2C, 0x31, 0x3C, 0xFF
#define COL_FTREE_SEP 0x3A, 0x3F, 0x4A, 0xFF  /* separador del panel  */
#define COL_FTREE_DIR 0xE5, 0xC0, 0x7B, 0xFF  /* nombre de carpeta (ámbar)  */
#define COL_FTREE_FILE 0xAB, 0xB2, 0xBF, 0xFF /* nombre de archivo (gris) */
#define COL_FTREE_ROOT 0x61, 0xAF, 0xEF, 0xFF /* raíz del árbol (azul) */

/* -- Scrollbar ------------------------------------------------------------- */
#define SCROLLBAR_W 8 /* ancho de la barra de scroll (px) */

/* Utilidades de dibujo compartidas (implementadas en render.c) */
/* color actual */
void set_color(SDL_Renderer *r, uint8_t R, uint8_t G, uint8_t B, uint8_t A);
/* relleno, color actual  */
void fill_rect(SDL_Renderer *r, int x, int y, int w, int h);
/* contorno 1px, color actual */
void stroke_rect(SDL_Renderer *r, int x, int y, int w, int h);
/* draw_text: rasteriza texto con SDL_ttf y devuelve su ancho en px (0 si nada).
 */
int draw_text(Editor *e, const char *text, int x, int y, uint8_t R, uint8_t G,
              uint8_t B);
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
