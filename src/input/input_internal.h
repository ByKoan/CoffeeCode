#pragma once
/**
 * @file input_internal.h
 * @brief Cabecera PRIVADA del módulo input: macros y declaraciones compartidas
 *        entre sus .c (input.c, input_keyboard.c, input_mouse.c, ...).
 *
 * No es API pública: nada de aquí debe incluirse fuera del módulo input. Reúne
 * las dependencias comunes (editor, filetree, render, libc) y declara las
 * funciones que un .c implementa y otro usa, de modo que cada traducción vea
 * solo prototipos sin duplicar definiciones.
 */
/* Header PRIVADO del modulo input: macros y declaraciones compartidas. No es
 * API publica. */
#include "editor/editor.h"
#include "ext/ext_host.h"
#include "filetree/filetree.h"
#include "input/input.h"
#include "layout/layout.h"
#include "render/render.h"
#include <ctype.h>    /* isalnum (clasificación de caracteres de palabra) */
#include <stdbool.h>  /* bool/true/false */
#include <stdio.h>    /* snprintf, fprintf */
#include <stdlib.h>   /* malloc/free */
#include <string.h>   /* memmove, strlen, strcmp, strncmp */
#include <sys/stat.h> /* stat (mtime de archivos al guardar) */

/* La geometría del menú "Archivo" y del botón de la navbar la registra ahora
 * el render en e->ui (UI_BTN_FILE / UI_LIST_MENU_ITEM); el input la consulta
 * con ui_hit/ui_hit_idx, así que ya no se duplica aquí. */

/* ── Geometría / utilidades (input_mouse.c, input.c) ────────────────────────
 */
int get_left_offset(Editor *e); /* ancho del panel lateral izquierdo */
/* clic en el explorador de archivos */
void handle_ftree_click(Editor *e, int mx, int my);
/* hover en el explorador de archivos */
void handle_ftree_hover(Editor *e, int mx, int my);
/* clic en el panel de extensiones; 1 si lo consumio */
int handle_ext_panel_click(Editor *e, int mx, int my);

/* ── Movimiento del cursor y selección (input_keyboard.c) ───────────────────
 */
void move_cursor(Editor *e, int line, int col); /* mover sin tocar sel. */
/* mover; Shift amplía sel. */
void move_cursor_select(Editor *e, int line, int col, int selecting);
void move_line_up(Editor *e, int sel);    /* flecha arriba */
void move_line_down(Editor *e, int sel);  /* flecha abajo */
void move_col_left(Editor *e, int sel);   /* flecha izquierda */
void move_col_right(Editor *e, int sel);  /* flecha derecha */
void move_home(Editor *e, int sel);       /* inicio de línea */
void move_end(Editor *e, int sel);        /* fin de línea */
void move_word_left(Editor *e, int sel);  /* Ctrl+Left: palabra prev. */
void move_word_right(Editor *e, int sel); /* Ctrl+Right: palabra sig. */

/* ── Operaciones de edición (input_keyboard.c) ──────────────────────────────
 */
/* borra la selección activa; 1 si borró algo */
int delete_selection(Editor *e);
void insert_newline(Editor *e); /* Enter: salto de línea con autoindentación */
void insert_tab(Editor *e);     /* Tab: espacios hasta el siguiente tab stop */
void do_backspace(Editor *e);   /* Retroceso: borra hacia atrás */
void do_delete(Editor *e);      /* Supr: borra hacia delante */

/* ── Acciones de archivo / diálogos (input_mouse.c / input_file.c) ──────────
 */
void new_file(Editor *e);  /* crea una pestaña/archivo nuevo y vacío */
void save_file(Editor *e); /* guarda la pestaña activa a disco */
/* Callback de SDL para el diálogo de "abrir archivo": SDL lo invoca con la
 * lista de rutas elegidas. SDLCALL fija la convención de llamada que SDL
 * espera. */
void SDLCALL file_dialog_cb(void *userdata, const char *const *filelist,
                            int filter);
/* abre el diálogo nativo de selección de archivo */
void open_file_dialog(Editor *e);
/* Callback equivalente para el diálogo de "abrir carpeta". */
void SDLCALL folder_dialog_cb(void *userdata, const char *const *filelist,
                              int filter);
/* abre el diálogo nativo de selección de carpeta */
void open_folder_dialog(Editor *e);
/* ejecuta la acción de la entrada de menú @p item */
void menu_exec(Editor *e, int item);

/* ── Ratón / scroll / clics de texto (input_mouse.c) ────────────────────────
 */
void handle_scroll(Editor *e, float dy); /* desplaza el scroll @p dy líneas */
/* coloca el cursor según el clic */
void handle_text_click(Editor *e, int mx, int my);

/* ── Selección, portapapeles y edición de línea (input_keyboard.c) ──────────
 */
void select_all(Editor *e);     /* Ctrl+A: seleccionar todo */
void do_copy(Editor *e);        /* Ctrl+C: copiar selección al portapapeles */
void do_cut(Editor *e);         /* Ctrl+X: cortar selección */
void do_paste(Editor *e);       /* Ctrl+V: pegar del portapapeles */
void duplicate_line(Editor *e); /* Ctrl+D: duplicar la línea actual */
void toggle_line_comment(Editor *e); /* Ctrl+/: comentar/descomentar la línea */
void select_line(Editor *e);         /* Ctrl+L: seleccionar la línea completa */

/* ── Búsqueda y reemplazo (input_find.c) ────────────────────────────────────
 */
/* offset del próximo match desde @p start_pos */
size_t find_next(Editor *e, size_t start_pos);
/* cuenta cuántas coincidencias hay de la query */
void count_matches(Editor *e);
void find_jump(Editor *e);  /* salta al siguiente resultado */
void find_first(Editor *e); /* busca desde el principio (al cambiar la query) */
void find_prev(Editor *e);  /* salta al resultado anterior */
void do_replace(Editor *e); /* reemplaza el match actual por el reemplazo */
void open_find_bar(Editor *e);  /* muestra y enfoca la barra de búsqueda */
void close_find_bar(Editor *e); /* oculta la barra de búsqueda */
void toggle_sidebar(Editor *e); /* abre/cierra el panel explorador */

/* Manejadores de eventos de raton (definidos en input_mouse.c) */
void on_mouse_wheel(Editor *e, SDL_Event *ev); /* rueda del ratón → scroll */
/* movimiento → hover/arrastre/selección */
void on_mouse_motion(Editor *e, SDL_Event *ev);
/* botón pulsado → clic/foco/selección */
void on_mouse_button_down(Editor *e, SDL_Event *ev);
/* botón soltado tras arrastrar una pestaña: si hay arrastre en curso, aplica el
 * drop (mover/dividir) y limpia el estado.  Devuelve 1 si consumio un arrastre
 * (el llamante no debe tratarlo como clic), 0 si no habia arrastre real. */
int on_tab_drag_release(Editor *e, int mx, int my);
