#pragma once

/* Debe definirse antes de cualquier include de SDL para que SDL3
   no redefina main() en Windows */
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#include "buffer.h"
#include "lexer.h"
#include "filetree.h"
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
#define TAB_BAR_HEIGHT   28    /* altura de la barra de pestañas */
#define MAX_TABS         16    /* máximo de archivos abiertos    */

/* ── Undo/Redo ──────────────────────────────────────────────────────────── */
#define UNDO_MAX  256   /* máximo de entradas en la pila de undo */

typedef enum {
    UNDO_INSERT,        /* se insertaron `len` bytes en `pos`  */
    UNDO_DELETE         /* se borraron `len` bytes en `pos`    */
} UndoType;

typedef struct {
    UndoType type;
    size_t   pos;       /* posición lógica en el buffer        */
    char    *text;      /* bytes afectados (malloc'd)          */
    size_t   len;       /* longitud en bytes                   */
    int      cursor_line_after;
    int      cursor_col_after;
} UndoEntry;

typedef struct {
    UndoEntry entries[UNDO_MAX];
    int       head;     /* próxima posición de escritura (circular)  */
    int       count;    /* entradas válidas almacenadas              */
    int       redo_top; /* cuántas entradas se pueden hacer redo     */
} UndoStack;

/* ── Pestaña (archivo abierto) ─────────────────────────────────────────── */
typedef struct {
    Buffer     buf;
    LexerCache lex;
    UndoStack  undo;
    char       filepath[512];
    int        modified;
    int        cursor_line, cursor_col;
    int        scroll_line, scroll_col;
    int        sel_active;
    int        sel_anchor_line, sel_anchor_col;
    /* geometría para click detection (calculado en render) */
    int        tab_x, tab_w;
    int        close_x, close_y;
} EditorTab;

/* ── Barra de búsqueda ──────────────────────────────────────────────────── */
#define FIND_BAR_MAX 256

typedef struct {
    int  visible;
    char query[FIND_BAR_MAX];
    int  query_len;
    int  result_line;   /* -1 = sin resultado                        */
    int  result_col;
    /* reemplazo */
    char replace[FIND_BAR_MAX];
    int  replace_len;
    int  replace_focused; /* 0 = foco en buscar, 1 = foco en reemplazar */
    /* geometría para click detection (calculado en render) */
    int  bar_x, bar_y, bar_w, bar_h;
    int  field_x, row1_y, row2_y, field_h;
    int  replace_btn_x, replace_btn_y, replace_btn_w, replace_btn_h;
} FindBar;

/* ── Estado global del editor ───────────────────────────────────────────── */
typedef struct {
    /* SDL */
    SDL_Window   *window;
    SDL_Renderer *renderer;
    TTF_Font     *font;
    int           win_w, win_h;
    int           char_w;

    /* ── pestañas ── */
    EditorTab     tabs[MAX_TABS];
    int           tab_count;
    int           active_tab;

    /* buffer de texto (apunta al tab activo — acceso directo) */
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

    /* explorador de carpetas lateral */
    FileTree      ftree;

    /* ── NUEVO: undo/redo ──────────────────────────────────────────────── */
    UndoStack     undo;

    /* ── NUEVO: barra de búsqueda (Ctrl+F) ────────────────────────────── */
    FindBar       find;

    /* ── selección con ratón ───────────────────────────────────────────── */
    int           mouse_selecting;   /* 1 = botón izq. pulsado sobre texto   */

    /* ── scrollbar draggable ──────────────────────────────────────────── */
    int           tab_new_btn_x;   /* botón + para nuevo tab */
    int           scrollbar_dragging;
    int           scrollbar_drag_start_y;
    int           scrollbar_drag_start_line;

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

/* ── NUEVO: undo/redo API (usada desde input.c) ─────────────────────────── */
void editor_undo_push_insert(Editor *e, size_t pos, const char *text, size_t len);
void editor_undo_push_delete(Editor *e, size_t pos, const char *text, size_t len);
void editor_undo(Editor *e);
void editor_redo(Editor *e);

/* ── NUEVO: helpers de selección ─────────────────────────────────────────── */
/* Devuelve las posiciones lógicas ordenadas del rango seleccionado.
   Retorna 0 si no hay selección activa.                                      */
int  editor_sel_range(Editor *e, size_t *from, size_t *to);
void editor_sel_clear(Editor *e);

/* ── Tab management ──────────────────────────────────────────────────────── */
void editor_tab_new   (Editor *e);
void editor_tab_open  (Editor *e, const char *path);
void editor_tab_close (Editor *e);
void editor_tab_switch(Editor *e, int i);
void editor_tab_save_state(Editor *e);