#include "input.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* ── Geometría del menú (debe coincidir con render.c) ────────────────────── */
#define MENU_ITEM_H   26
#define MENU_WIDTH   160
#define MENU_ITEMS    4
#define BTN_FILE_X    4
#define BTN_FILE_W   70

static const char *MENU_LABELS[MENU_ITEMS] = {
    "Nuevo", "Abrir archivo...", NULL, "Guardar"
};

/* Devuelve el índice de item del menú bajo (mx, my), o -1 si ninguno */
static int menu_item_at(int mx, int my) {
    int menu_x = BTN_FILE_X;
    int menu_y = NAVBAR_HEIGHT;
    if (mx < menu_x || mx >= menu_x + MENU_WIDTH) return -1;

    int iy = menu_y;
    for (int i = 0; i < MENU_ITEMS; i++) {
        int h = MENU_LABELS[i] ? MENU_ITEM_H : 8;
        if (my >= iy && my < iy + h && MENU_LABELS[i])
            return i;
        iy += h;
    }
    return -1;
}

/* Altura total del menú */
static int menu_total_h(void) {
    int h = 0;
    for (int i = 0; i < MENU_ITEMS; i++)
        h += MENU_LABELS[i] ? MENU_ITEM_H : 8;
    return h;
}

/* ── helpers de cursor ───────────────────────────────────────────────────── */
static void move_cursor(Editor *e, int line, int col) {
    int total = buf_line_count(&e->buf);
    if (line < 0) line = 0;
    if (line >= total) line = total - 1;
    if (col  < 0) col  = 0;
    size_t pos = editor_pos_from_line_col(e, line, col);
    buf_move_to(&e->buf, pos);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}
static void move_line_up(Editor *e)   { move_cursor(e, e->cursor_line - 1, e->cursor_col); }
static void move_line_down(Editor *e) { move_cursor(e, e->cursor_line + 1, e->cursor_col); }
static void move_col_left(Editor *e) {
    if (e->cursor_col > 0) {
        move_cursor(e, e->cursor_line, e->cursor_col - 1);
    } else if (e->cursor_line > 0) {
        int prev = e->cursor_line - 1;
        size_t end = buf_line_end(&e->buf, editor_pos_from_line_col(e, prev, 0));
        int lc = (int)(end - editor_pos_from_line_col(e, prev, 0));
        move_cursor(e, prev, lc);
    }
}
static void move_col_right(Editor *e) {
    size_t pos = buf_cursor_pos(&e->buf);
    size_t len = buf_length(&e->buf);
    if (pos < len) {
        buf_move_right(&e->buf);
        editor_sync_cursor(e);
        editor_ensure_visible(e);
        e->needs_redraw = 1;
    }
}
static void move_home(Editor *e) { move_cursor(e, e->cursor_line, 0); }
static void move_end(Editor *e) {
    size_t pos = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t end = buf_line_end(&e->buf, pos);
    move_cursor(e, e->cursor_line, (int)(end - pos));
}

static void insert_newline(Editor *e) {
    buf_insert(&e->buf, '\n');
    editor_sync_cursor(e);
    size_t prev_start = editor_pos_from_line_col(e, e->cursor_line - 1, 0);
    size_t len = buf_length(&e->buf);
    size_t i = prev_start;
    while (i < len) {
        char c = buf_char_at(&e->buf, i);
        if (c == ' ')  { buf_insert(&e->buf, ' ');  i++; }
        else if (c == '\t') { buf_insert(&e->buf, '\t'); i++; }
        else break;
    }
    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line - 1);
    editor_ensure_visible(e);
    e->modified = 1; e->needs_redraw = 1;
}
static void insert_tab(Editor *e) {
    int spaces = TAB_SIZE - (e->cursor_col % TAB_SIZE);
    for (int i = 0; i < spaces; i++) buf_insert(&e->buf, ' ');
    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1; e->needs_redraw = 1;
}
static void do_backspace(Editor *e) {
    if (buf_cursor_pos(&e->buf) == 0) return;
    int prev_line = e->cursor_line;
    buf_delete_before(&e->buf);
    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line < prev_line ? e->cursor_line : prev_line);
    editor_ensure_visible(e);
    e->modified = 1; e->needs_redraw = 1;
}
static void do_delete(Editor *e) {
    if (buf_cursor_pos(&e->buf) >= buf_length(&e->buf)) return;
    buf_delete_after(&e->buf);
    editor_update_lexer(e, e->cursor_line);
    e->modified = 1; e->needs_redraw = 1;
}

/* ── Nuevo archivo ───────────────────────────────────────────────────────── */
static void new_file(Editor *e) {
    buf_free(&e->buf);
    buf_init(&e->buf);
    e->filepath[0] = '\0';
    e->modified    = 0;
    e->cursor_line = e->cursor_col = 0;
    e->scroll_line = e->scroll_col = 0;
    int total = buf_line_count(&e->buf);
    lexer_cache_free(&e->lex);
    lexer_cache_init(&e->lex, total > 0 ? total : 1);
    SDL_SetWindowTitle(e->window, "SDL3 IDE");
    e->needs_redraw = 1;
}

/* ── Guardar ─────────────────────────────────────────────────────────────── */
static void save_file(Editor *e) {
    if (!e->filepath[0])
        strncpy(e->filepath, "untitled.c", sizeof(e->filepath) - 1);
    if (buf_save_file(&e->buf, e->filepath)) {
        e->modified = 0;
        SDL_SetWindowTitle(e->window, e->filepath);
    }
    e->needs_redraw = 1;
}

/* ── Callback del diálogo de apertura de archivos ────────────────────────── */
static void SDLCALL file_dialog_cb(void *userdata,
                                   const char * const *filelist,
                                   int filter)
{
    (void)filter;
    Editor *e = (Editor *)userdata;
    if (!filelist || !filelist[0]) {
        /* cancelado */
        e->needs_redraw = 1;
        return;
    }
    const char *path = filelist[0];
    strncpy(e->filepath, path, sizeof(e->filepath) - 1);
    e->filepath[sizeof(e->filepath) - 1] = '\0';

    buf_free(&e->buf);
    buf_init(&e->buf);
    buf_load_file(&e->buf, path);
    e->cursor_line = e->cursor_col = 0;
    e->scroll_line = e->scroll_col = 0;
    e->modified = 0;

    int total = buf_line_count(&e->buf);
    lexer_cache_free(&e->lex);
    lexer_cache_init(&e->lex, total > 0 ? total : 1);
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);

    SDL_SetWindowTitle(e->window, path);
    e->needs_redraw = 1;
}

static void open_file_dialog(Editor *e) {
    SDL_DialogFileFilter filters[] = {
        { "Archivos de código", "c;h;cpp;hpp;py;js;ts;rs;go;java;txt;md;json;toml;yaml;yml" },
        { "Todos los archivos", "*" }
    };
    SDL_ShowOpenFileDialog(file_dialog_cb, e, e->window,
                           filters, 2, NULL, false);
}

/* ── Ejecutar un item del menú ───────────────────────────────────────────── */
static void menu_exec(Editor *e, int item) {
    switch (item) {
    case 0: new_file(e);         break;
    case 1: open_file_dialog(e); break;
    /* case 2: separador */
    case 3: save_file(e);        break;
    default: break;
    }
    e->menu_open    = 0;
    e->menu_hovered = -1;
    e->needs_redraw = 1;
}

/* ── scroll ──────────────────────────────────────────────────────────────── */
static void handle_scroll(Editor *e, float dy) {
    int lines = (int)(dy * 3);
    e->scroll_line -= lines;
    int total = buf_line_count(&e->buf);
    if (e->scroll_line < 0) e->scroll_line = 0;
    if (e->scroll_line >= total) e->scroll_line = total - 1;
    e->needs_redraw = 1;
}

/* ── click en el área de texto ───────────────────────────────────────────── */
static void handle_text_click(Editor *e, int mx, int my) {
    int text_x = GUTTER_WIDTH + PADDING_LEFT;
    if (mx < text_x) return;
    int vis_line = (my - NAVBAR_HEIGHT) / LINE_HEIGHT;
    int vis_col  = (mx - text_x) / e->char_w;
    int line = e->scroll_line + vis_line;
    int col  = e->scroll_col  + vis_col;
    int total = buf_line_count(&e->buf);
    if (line >= total) line = total - 1;
    move_cursor(e, line, col);
}

/* ── dispatcher principal ────────────────────────────────────────────────── */
void input_handle_event(Editor *e, SDL_Event *ev) {
    SDL_Keymod mods  = SDL_GetModState();
    int ctrl  = (mods & SDL_KMOD_CTRL)  != 0;
    int shift = (mods & SDL_KMOD_SHIFT) != 0;
    (void)shift;

    switch (ev->type) {

    case SDL_EVENT_QUIT:
        e->running = 0;
        break;

    case SDL_EVENT_WINDOW_RESIZED:
        e->win_w = ev->window.data1;
        e->win_h = ev->window.data2;
        e->needs_redraw = 1;
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        handle_scroll(e, ev->wheel.y);
        break;

    case SDL_EVENT_MOUSE_MOTION: {
        int mx = (int)ev->motion.x;
        int my = (int)ev->motion.y;
        if (e->menu_open) {
            int prev = e->menu_hovered;
            e->menu_hovered = menu_item_at(mx, my);
            if (e->menu_hovered != prev) e->needs_redraw = 1;
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int mx = (int)ev->button.x;
        int my = (int)ev->button.y;
        if (ev->button.button != SDL_BUTTON_LEFT) break;

        /* ── clic en el menú abierto ── */
        if (e->menu_open) {
            int item = menu_item_at(mx, my);
            if (item >= 0) {
                menu_exec(e, item);
            } else {
                /* clic fuera del menú → cerrar */
                /* pero si clic en botón "Archivo" → toggle */
                int in_btn = (mx >= BTN_FILE_X && mx < BTN_FILE_X + BTN_FILE_W
                              && my >= 0 && my < NAVBAR_HEIGHT);
                e->menu_open    = in_btn ? 0 : 0;
                e->menu_hovered = -1;
                e->needs_redraw = 1;
            }
            break;
        }

        /* ── clic en botón "Archivo" ── */
        if (my >= 0 && my < NAVBAR_HEIGHT &&
            mx >= BTN_FILE_X && mx < BTN_FILE_X + BTN_FILE_W) {
            e->menu_open    = 1;
            e->menu_hovered = -1;
            e->needs_redraw = 1;
            break;
        }

        /* ── clic fuera de navbar y menú: area de texto ── */
        if (my >= NAVBAR_HEIGHT) {
            /* cerrar menú si estaba abierto */
            if (e->menu_open) {
                /* comprueba si el clic está dentro del desplegable */
                int menu_y = NAVBAR_HEIGHT;
                int mh     = menu_total_h();
                if (!(mx >= BTN_FILE_X && mx < BTN_FILE_X + MENU_WIDTH
                      && my >= menu_y && my < menu_y + mh)) {
                    e->menu_open = 0;
                    e->needs_redraw = 1;
                }
            }
            handle_text_click(e, mx, my);
        }
        break;
    }

    case SDL_EVENT_TEXT_INPUT:
        if (e->menu_open) break;  /* no escribir mientras el menú está abierto */
        buf_insert_str(&e->buf, ev->text.text, strlen(ev->text.text));
        editor_sync_cursor(e);
        editor_update_lexer(e, e->cursor_line);
        editor_ensure_visible(e);
        e->modified = 1; e->needs_redraw = 1;
        break;

    case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode key = ev->key.key;

        /* Escape cierra el menú */
        if (key == SDLK_ESCAPE && e->menu_open) {
            e->menu_open = 0; e->menu_hovered = -1; e->needs_redraw = 1;
            break;
        }

        if (ctrl) {
            switch (key) {
            case SDLK_N: new_file(e);         break;
            case SDLK_O: open_file_dialog(e); break;
            case SDLK_S: save_file(e);        break;
            case SDLK_Q: e->running = 0;      break;
            case SDLK_HOME: move_cursor(e, 0, 0); break;
            case SDLK_END: {
                int t = buf_line_count(&e->buf) - 1;
                move_cursor(e, t, 0); move_end(e); break;
            }
            default: break;
            }
        } else {
            if (e->menu_open) break;
            switch (key) {
            case SDLK_UP:        move_line_up(e);    break;
            case SDLK_DOWN:      move_line_down(e);  break;
            case SDLK_LEFT:      move_col_left(e);   break;
            case SDLK_RIGHT:     move_col_right(e);  break;
            case SDLK_HOME:      move_home(e);       break;
            case SDLK_END:       move_end(e);        break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:  insert_newline(e);  break;
            case SDLK_TAB:       insert_tab(e);      break;
            case SDLK_BACKSPACE: do_backspace(e);    break;
            case SDLK_DELETE:    do_delete(e);       break;
            case SDLK_PAGEUP: {
                int text_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;
                int vis = text_h / LINE_HEIGHT;
                move_cursor(e, e->cursor_line - vis, e->cursor_col); break;
            }
            case SDLK_PAGEDOWN: {
                int text_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;
                int vis = text_h / LINE_HEIGHT;
                move_cursor(e, e->cursor_line + vis, e->cursor_col); break;
            }
            default: break;
            }
        }
        break;
    }

    default: break;
    }
}
