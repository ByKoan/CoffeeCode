#pragma once
/* Header PRIVADO del modulo render: paleta de colores + utilidades y
   renderizadores de seccion compartidos entre los .c de render. No es API publica. */
#include "render/render.h"
#include "editor/editor.h"
#include "filetree/filetree.h"

/* -- Colores del tema ------------------------------------------------------ */
#define COL_BG          0x28, 0x2C, 0x34, 0xFF
#define COL_GUTTER      0x21, 0x25, 0x2B, 0xFF
#define COL_CURSOR_LINE 0x2C, 0x31, 0x3C, 0xFF
#define COL_CURSOR      0x52, 0x8B, 0xFF, 0xFF
#define COL_STATUS_BG   0x21, 0x25, 0x2B, 0xFF
#define COL_SEL_BG      0x26, 0x4F, 0x78, 0xFF   /* azul selección */

/* Navbar */
#define COL_NAVBAR_BG   0x1A, 0x1D, 0x23, 0xFF
#define COL_NAVBAR_BTN  0x2C, 0x31, 0x3C, 0xFF  /* hover del botón */
#define COL_MENU_BG     0x1E, 0x22, 0x2A, 0xFF
#define COL_MENU_HOVER  0x3E, 0x44, 0x55, 0xFF
#define COL_MENU_SEP    0x3A, 0x3F, 0x4A, 0xFF
#define COL_MENU_BORDER 0x3A, 0x3F, 0x4A, 0xFF

/* Colores panel lateral */
#define COL_FTREE_BG    0x1E, 0x22, 0x2A, 0xFF
#define COL_FTREE_HOVER 0x2C, 0x31, 0x3C, 0xFF
#define COL_FTREE_SEP   0x3A, 0x3F, 0x4A, 0xFF
#define COL_FTREE_DIR   0xE5, 0xC0, 0x7B, 0xFF
#define COL_FTREE_FILE  0xAB, 0xB2, 0xBF, 0xFF
#define COL_FTREE_ROOT  0x61, 0xAF, 0xEF, 0xFF

/* -- Scrollbar ------------------------------------------------------------- */
#define SCROLLBAR_W     8    /* ancho de la barra de scroll (px) */

/* Utilidades de dibujo compartidas */
void set_color(SDL_Renderer *r, uint8_t R, uint8_t G, uint8_t B, uint8_t A);
int  draw_text(Editor *e, const char *text, int x, int y,
               uint8_t R, uint8_t G, uint8_t B);
int  get_line_text(Editor *e, int line, char *out, int max);

/* Renderizadores de seccion (invocados desde render_frame) */
void render_navbar(Editor *e);
void render_menu(Editor *e);
void render_filetree(Editor *e);
void render_filetree_toggle_closed(Editor *e);
void render_tabbar(Editor *e);
void render_find_bar(Editor *e);
void render_shortcuts(Editor *e);
void render_scrollbar(Editor *e, int left_offset);
void render_selection(Editor *e, int left_offset, int text_top, int visible_lines);
