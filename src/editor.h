#pragma once
#include "buffer.h"
#include "lexer.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

/* ── Constantes de UI ───────────────────────────────────────────────────── */
#define FONT_SIZE        16
#define LINE_HEIGHT      20
#define GUTTER_WIDTH     52
#define PADDING_LEFT      8
#define STATUS_HEIGHT    24
#define NAVBAR_HEIGHT    30
#define TAB_SIZE          4

/* ── Estado global del editor ───────────────────────────────────────────── */
typedef struct {
    /* SDL */
    SDL_Window   *window;
    SDL_Renderer *renderer;
    TTF_Font     *font;
    int           win_w, win_h;
    int           char_w;

    /* buffer de texto */
    Buffer        buf;
    LexerCache    lex;

    /* vista */
    int           scroll_line;
    int           scroll_col;
    int           cursor_line;
    int           cursor_col;

    /* selección */
    int           sel_active;
    int           sel_anchor_line;
    int           sel_anchor_col;

    /* archivo */
    char          filepath[512];
    int           modified;

    /* navbar / menú archivo */
    int           menu_open;     /* 1 = desplegable visible */
    int           menu_hovered;  /* índice del item bajo el cursor, -1 = ninguno */

    /* estado */
    int           running;
    int           needs_redraw;
} Editor;

/* ── Ciclo de vida ──────────────────────────────────────────────────────── */
int  editor_init   (Editor *e, const char *filepath);
void editor_free   (Editor *e);
void editor_run    (Editor *e);

/* ── Lógica interna (usada entre módulos) ───────────────────────────────── */
void editor_update_lexer (Editor *e, int from_line);
void editor_sync_cursor  (Editor *e);  /* actualiza cursor_line/col desde buf */
void editor_ensure_visible(Editor *e);
size_t editor_pos_from_line_col(Editor *e, int line, int col);
