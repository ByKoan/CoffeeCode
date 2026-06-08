#pragma once
/* Header PRIVADO del modulo input: macros y declaraciones compartidas. No es API publica. */
#include "input/input.h"
#include "editor/editor.h"
#include "filetree/filetree.h"
#include "render/render.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <ctype.h>

/* -- Geometría del menú (debe coincidir con render.c) ---------------------- */
#define MENU_ITEM_H   26
#define MENU_WIDTH   210
#define MENU_ITEMS    6
#define BTN_FILE_X    4
#define BTN_FILE_W   90

int menu_item_at(int mx, int my);
int menu_total_h(void);
int get_left_offset(Editor *e);
void handle_ftree_click(Editor *e, int mx, int my);
void handle_ftree_hover(Editor *e, int mx, int my);
void move_cursor(Editor *e, int line, int col);
void move_cursor_select(Editor *e, int line, int col, int selecting);
void move_line_up(Editor *e, int sel);
void move_line_down(Editor *e, int sel);
void move_col_left(Editor *e, int sel);
void move_col_right(Editor *e, int sel);
void move_home(Editor *e, int sel);
void move_end(Editor *e, int sel);
void move_word_left(Editor *e, int sel);
void move_word_right(Editor *e, int sel);
int delete_selection(Editor *e);
void insert_newline(Editor *e);
void insert_tab(Editor *e);
void do_backspace(Editor *e);
void do_delete(Editor *e);
void new_file(Editor *e);
void save_file(Editor *e);
void SDLCALL file_dialog_cb(void *userdata, const char * const *filelist, int filter);
void open_file_dialog(Editor *e);
void SDLCALL folder_dialog_cb(void *userdata, const char * const *filelist, int filter);
void open_folder_dialog(Editor *e);
void menu_exec(Editor *e, int item);
void handle_scroll(Editor *e, float dy);
void handle_text_click(Editor *e, int mx, int my);
void select_all(Editor *e);
void do_copy(Editor *e);
void do_cut(Editor *e);
void do_paste(Editor *e);
void duplicate_line(Editor *e);
void toggle_line_comment(Editor *e);
void select_line(Editor *e);
size_t find_next(Editor *e, size_t start_pos);
void count_matches(Editor *e);
void find_jump(Editor *e);
void find_first(Editor *e);
void find_prev(Editor *e);
void do_replace(Editor *e);
void open_find_bar(Editor *e);
void close_find_bar(Editor *e);
void toggle_sidebar(Editor *e);

/* Manejadores de eventos de raton (definidos en input_mouse.c) */
void on_mouse_wheel(Editor *e, SDL_Event *ev);
void on_mouse_motion(Editor *e, SDL_Event *ev);
void on_mouse_button_down(Editor *e, SDL_Event *ev);
