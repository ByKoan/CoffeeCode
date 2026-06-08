#include "input/input.h"
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

static const char *MENU_LABELS[MENU_ITEMS] = {
    "Nuevo", "Abrir archivo...", "Abrir carpeta...", NULL, "Guardar", "Autoguardado"
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


/* Devuelve el offset izquierdo actual según el estado del panel */
static int get_left_offset(Editor *e) {
    if (e->ftree.open) return e->ftree.width;
    return FTREE_TOGGLE_BTN_W;
}

/* Maneja click en el panel lateral */
static void handle_ftree_click(Editor *e, int mx, int my) {
    FileTree *ft = &e->ftree;
    int panel_y    = NAVBAR_HEIGHT;
    int panel_h    = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;
    int btn_w      = FTREE_TOGGLE_BTN_W;
    int content_top = panel_y + TAB_BAR_HEIGHT;
    int content_h   = panel_h - TAB_BAR_HEIGHT;

    /* Click en el botón toggle (borde derecho del panel) */
    int toggle_x = ft->open ? (ft->width - btn_w) : 0;
    if (mx >= toggle_x && mx < toggle_x + btn_w) {
        int btn_h = 40;
        int btn_y = content_top + (content_h - btn_h) / 2;
        if (my >= btn_y && my < btn_y + btn_h) {
            ft->open = !ft->open;
            e->needs_redraw = 1;
            return;
        }
    }

    if (!ft->open) return;

    /* Click en un item del árbol */
    int header_h   = 26;
    int content_y  = content_top + header_h;
    if (my < content_y) return;

    int row = (my - content_y) / FTREE_ITEM_H;
    int actual_row = row + ft->scroll;

    /* Encontrar la entrada visible número actual_row */
    int vis = 0;
    for (int i = 0; i < ft->count; i++) {
        if (!ft->entries[i].visible) continue;
        if (vis == actual_row) {
            FEntry *en = &ft->entries[i];
            if (en->type == FTYPE_DIR) {
                ftree_toggle(ft, i);
            } else {
                /* Abrir archivo en el editor */
                editor_tab_open(e, en->path);
                SDL_SetWindowTitle(e->window, en->path);
            }
            e->needs_redraw = 1;
            return;
        }
        vis++;
    }
}

/* Maneja hover sobre el panel lateral */
static void handle_ftree_hover(Editor *e, int mx, int my) {
    FileTree *ft = &e->ftree;
    if (!ft->open) return;

    int header_h  = 26;
    int content_y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT + header_h;
    if (my < content_y) {
        if (ft->hovered != -1) { ft->hovered = -1; e->needs_redraw = 1; }
        return;
    }

    int row = (my - content_y) / FTREE_ITEM_H;
    int actual_row = row + ft->scroll;

    int vis = 0, found = -1;
    for (int i = 0; i < ft->count; i++) {
        if (!ft->entries[i].visible) continue;
        if (vis == actual_row) { found = i; break; }
        vis++;
    }
    if (ft->hovered != found) {
        ft->hovered = found;
        e->needs_redraw = 1;
    }
}

/* -- helpers de cursor ----------------------------------------------------- */
static void move_cursor(Editor *e, int line, int col) {
    int total = buf_line_count(e->buf);
    if (line < 0) line = 0;
    if (line >= total) line = total - 1;
    if (col  < 0) col  = 0;
    size_t pos = editor_pos_from_line_col(e, line, col);
    buf_move_to(e->buf, pos);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

/* -- move_cursor con selección Shift -------------------------------- */
static void move_cursor_select(Editor *e, int line, int col, int selecting) {
    if (selecting) {
        if (!e->sel_active) {
            /* ancla en posición actual */
            e->sel_active      = 1;
            e->sel_anchor_line = e->cursor_line;
            e->sel_anchor_col  = e->cursor_col;
        }
    } else {
        editor_sel_clear(e);
    }
    move_cursor(e, line, col);
}

static void move_line_up(Editor *e, int sel)   { move_cursor_select(e, e->cursor_line - 1, e->cursor_col, sel); }
static void move_line_down(Editor *e, int sel) { move_cursor_select(e, e->cursor_line + 1, e->cursor_col, sel); }

static void move_col_left(Editor *e, int sel) {
    if (sel) {
        if (!e->sel_active) {
            e->sel_active      = 1;
            e->sel_anchor_line = e->cursor_line;
            e->sel_anchor_col  = e->cursor_col;
        }
    } else {
        /* si había selección, saltar al inicio de la misma */
        if (e->sel_active) {
            size_t from, to;
            if (editor_sel_range(e, &from, &to)) {
                editor_sel_clear(e);
                buf_move_to(e->buf, from);
                editor_sync_cursor(e);
                editor_ensure_visible(e);
                e->needs_redraw = 1;
                return;
            }
            editor_sel_clear(e);
        }
    }
    if (e->cursor_col > 0) {
        move_cursor(e, e->cursor_line, e->cursor_col - 1);
    } else if (e->cursor_line > 0) {
        int prev = e->cursor_line - 1;
        size_t end = buf_line_end(e->buf, editor_pos_from_line_col(e, prev, 0));
        int lc = (int)(end - editor_pos_from_line_col(e, prev, 0));
        move_cursor(e, prev, lc);
    }
}

static void move_col_right(Editor *e, int sel) {
    if (sel) {
        if (!e->sel_active) {
            e->sel_active      = 1;
            e->sel_anchor_line = e->cursor_line;
            e->sel_anchor_col  = e->cursor_col;
        }
    } else {
        /* si había selección, saltar al final */
        if (e->sel_active) {
            size_t from, to;
            if (editor_sel_range(e, &from, &to)) {
                editor_sel_clear(e);
                buf_move_to(e->buf, to);
                editor_sync_cursor(e);
                editor_ensure_visible(e);
                e->needs_redraw = 1;
                return;
            }
            editor_sel_clear(e);
        }
    }
    size_t pos = buf_cursor_pos(e->buf);
    size_t len = buf_length(e->buf);
    if (pos < len) {
        buf_move_right(e->buf);
        editor_sync_cursor(e);
        editor_ensure_visible(e);
        e->needs_redraw = 1;
    }
}

static void move_home(Editor *e, int sel) {
    move_cursor_select(e, e->cursor_line, 0, sel);
}

static void move_end(Editor *e, int sel) {
    size_t pos = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t end = buf_line_end(e->buf, pos);
    int col = (int)(end - pos);
    move_cursor_select(e, e->cursor_line, col, sel);
}

/* -- salto de palabra (Ctrl+Left / Ctrl+Right) --------------------- */
static void move_word_left(Editor *e, int sel) {
    if (sel && !e->sel_active) {
        e->sel_active      = 1;
        e->sel_anchor_line = e->cursor_line;
        e->sel_anchor_col  = e->cursor_col;
    } else if (!sel) {
        editor_sel_clear(e);
    }
    size_t pos = buf_cursor_pos(e->buf);
    if (pos == 0) return;
    pos--;
    /* salta espacios/no-word */
    while (pos > 0 && !isalnum((unsigned char)buf_char_at(e->buf, pos)) &&
           buf_char_at(e->buf, pos) != '_')
        pos--;
    /* salta la palabra */
    while (pos > 0 && (isalnum((unsigned char)buf_char_at(e->buf, pos - 1)) ||
                       buf_char_at(e->buf, pos - 1) == '_'))
        pos--;
    buf_move_to(e->buf, pos);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

static void move_word_right(Editor *e, int sel) {
    if (sel && !e->sel_active) {
        e->sel_active      = 1;
        e->sel_anchor_line = e->cursor_line;
        e->sel_anchor_col  = e->cursor_col;
    } else if (!sel) {
        editor_sel_clear(e);
    }
    size_t pos = buf_cursor_pos(e->buf);
    size_t len = buf_length(e->buf);
    /* salta la palabra actual */
    while (pos < len && (isalnum((unsigned char)buf_char_at(e->buf, pos)) ||
                         buf_char_at(e->buf, pos) == '_'))
        pos++;
    /* salta espacios/no-word */
    while (pos < len && !isalnum((unsigned char)buf_char_at(e->buf, pos)) &&
           buf_char_at(e->buf, pos) != '_')
        pos++;
    buf_move_to(e->buf, pos);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

/* -- operaciones de edición ----------------------------------------------- */

/* Borra la selección activa y la registra en undo. Devuelve 1 si borró algo */
static int delete_selection(Editor *e) {
    size_t from, to;
    if (!editor_sel_range(e, &from, &to)) return 0;

    size_t len = to - from;
    char *tmp = malloc(len + 1);
    if (tmp) {
        buf_get_text(e->buf, from, to, tmp);
        editor_undo_push_delete(e, from, tmp, len);
        free(tmp);
    }
    buf_delete_range(e->buf, from, to);
    buf_move_to(e->buf, from);
    editor_sel_clear(e);
    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
    return 1;
}

static void insert_newline(Editor *e) {
    delete_selection(e);
    size_t pos = buf_cursor_pos(e->buf);
    editor_undo_push_insert(e, pos, "\n", 1);
    buf_insert(e->buf, '\n');
    editor_sync_cursor(e);
    size_t prev_start = editor_pos_from_line_col(e, e->cursor_line - 1, 0);
    size_t len = buf_length(e->buf);
    size_t i = prev_start;
    while (i < len) {
        char c = buf_char_at(e->buf, i);
        if (c == ' ')  { buf_insert(e->buf, ' ');  i++; }
        else if (c == '\t') { buf_insert(e->buf, '\t'); i++; }
        else break;
    }
    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line - 1);
    editor_ensure_visible(e);
    e->modified = 1; e->needs_redraw = 1;
}

static void insert_tab(Editor *e) {
    delete_selection(e);
    int spaces = TAB_SIZE - (e->cursor_col % TAB_SIZE);
    size_t pos = buf_cursor_pos(e->buf);
    char tmp[TAB_SIZE + 1];
    for (int i = 0; i < spaces; i++) tmp[i] = ' ';
    tmp[spaces] = '\0';
    editor_undo_push_insert(e, pos, tmp, spaces);
    for (int i = 0; i < spaces; i++) buf_insert(e->buf, ' ');
    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1; e->needs_redraw = 1;
}

static void do_backspace(Editor *e) {
    if (e->sel_active) { delete_selection(e); return; }
    if (buf_cursor_pos(e->buf) == 0) return;
    int prev_line = e->cursor_line;
    size_t pos = buf_cursor_pos(e->buf) - 1;
    char c = buf_char_at(e->buf, pos);
    editor_undo_push_delete(e, pos, &c, 1);
    buf_delete_before(e->buf);
    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line < prev_line ? e->cursor_line : prev_line);
    editor_ensure_visible(e);
    e->modified = 1; e->needs_redraw = 1;
}

static void do_delete(Editor *e) {
    if (e->sel_active) { delete_selection(e); return; }
    if (buf_cursor_pos(e->buf) >= buf_length(e->buf)) return;
    size_t pos = buf_cursor_pos(e->buf);
    char c = buf_char_at(e->buf, pos);
    editor_undo_push_delete(e, pos, &c, 1);
    buf_delete_after(e->buf);
    editor_update_lexer(e, e->cursor_line);
    e->modified = 1; e->needs_redraw = 1;
}

/* -- Nuevo archivo --------------------------------------------------------- */
static void new_file(Editor *e) {
    editor_tab_new(e);
    /* Asegurar que el tab nuevo queda limpio independientemente del estado anterior */
    e->filepath[0] = '\0';
    e->modified    = 0;
    if (e->tab_count > 0) {
        e->tabs[e->active_tab].filepath[0] = '\0';
        e->tabs[e->active_tab].modified    = 0;
    }
    SDL_SetWindowTitle(e->window, "CoffeeCode");
    e->needs_redraw = 1;
}

/* -- Guardar --------------------------------------------------------------- */
static void save_file(Editor *e) {
    if (!e->filepath[0])
        strncpy(e->filepath, "untitled.c", sizeof(e->filepath) - 1);
    if (buf_save_file(e->buf, e->filepath)) {
        e->modified = 0;
        SDL_SetWindowTitle(e->window, e->filepath);
        /* sync filepath, modified y mtime de vuelta al tab */
        if (e->tab_count > 0) {
            EditorTab *_t = &e->tabs[e->active_tab];
            strncpy(_t->filepath, e->filepath, 511);
            _t->modified = 0;
            { struct stat _st; _t->loaded_mtime = (stat(e->filepath, &_st) == 0) ? (long)_st.st_mtime : 0; }
        }
    }
    e->needs_redraw = 1;
}

/* -- Callback del diálogo de apertura de archivos -------------------------- */
static void SDLCALL file_dialog_cb(void *userdata,
                                   const char * const *filelist,
                                   int filter)
{
    (void)filter;
    Editor *e = (Editor *)userdata;
    if (!filelist || !filelist[0]) {
        e->needs_redraw = 1;
        return;
    }
    const char *path = filelist[0];
    strncpy(e->filepath, path, sizeof(e->filepath) - 1);
    e->filepath[sizeof(e->filepath) - 1] = '\0';

    editor_tab_open(e, path);
    SDL_SetWindowTitle(e->window, path);
}

static void open_file_dialog(Editor *e) {
    SDL_DialogFileFilter filters[] = {
        { "Archivos de código", "c;h;cpp;hpp;py;js;ts;rs;go;java;txt;md;json;toml;yaml;yml" },
        { "Todos los archivos", "*" }
    };
    SDL_ShowOpenFileDialog(file_dialog_cb, e, e->window,
                           filters, 2, NULL, false);
}

/* -- Ejecutar un item del menú --------------------------------------------- */
static void SDLCALL folder_dialog_cb(void *userdata,
                                    const char * const *filelist,
                                    int filter)
{
    (void)filter;
    Editor *e = (Editor *)userdata;
    if (!filelist || !filelist[0]) {
        e->needs_redraw = 1;
        return;
    }
    ftree_load(&e->ftree, filelist[0]);
    e->needs_redraw = 1;
}

static void open_folder_dialog(Editor *e) {
    SDL_ShowOpenFolderDialog(folder_dialog_cb, e, e->window, NULL, false);
}

static void menu_exec(Editor *e, int item) {
    switch (item) {
    case 0: new_file(e);          break;
    case 1: open_file_dialog(e);  break;
    case 2: open_folder_dialog(e); break;
    /* case 3: separador */
    case 4: save_file(e);         break;
    case 5:
        e->autosave = !e->autosave;
        if (e->autosave)
            e->autosave_last_ms = SDL_GetTicks();
        break;
    default: break;
    }
    e->menu_open    = 0;
    e->menu_hovered = -1;
    e->needs_redraw = 1;
}

/* -- scroll ---------------------------------------------------------------- */
static void handle_scroll(Editor *e, float dy) {
    if (e->tab_count == 0 || !e->buf) return;
    int lines = (int)(dy * 3);
    e->scroll_line -= lines;
    int total = buf_line_count(e->buf);
    if (e->scroll_line < 0) e->scroll_line = 0;
    if (total > 0 && e->scroll_line >= total) e->scroll_line = total - 1;
    e->needs_redraw = 1;
}

/* -- click en el área de texto --------------------------------------------- */
static void handle_text_click(Editor *e, int mx, int my) {
    int left = get_left_offset(e);
    int text_x = left + GUTTER_WIDTH + PADDING_LEFT;
    if (mx < text_x) return;
    int cw = (e->char_w > 0 ? e->char_w : 8);
    int vis_line = (my - NAVBAR_HEIGHT - TAB_BAR_HEIGHT) / LINE_HEIGHT;
    if (vis_line < 0) vis_line = 0;
    int line = e->scroll_line + vis_line;
    int total = buf_line_count(e->buf);
    if (line < 0) line = 0;
    if (line >= total) line = total - 1;
    /* columna: desplazar por scroll_col */
    int vis_col = (mx - text_x + e->scroll_col * cw) / cw;
    if (vis_col < 0) vis_col = 0;
    /* limitar al largo real de la línea */
    size_t ls = editor_pos_from_line_col(e, line, 0);
    size_t le = buf_line_end(e->buf, ls);
    int line_len = (int)(le - ls);
    if (vis_col > line_len) vis_col = line_len;
    editor_sel_clear(e);
    move_cursor(e, line, vis_col);
}

/* -- Ctrl+A — seleccionar todo ------------------------------------- */
static void select_all(Editor *e) {
    /* ancla en posición lógica 0 → línea 0, col 0 */
    e->sel_anchor_line = 0;
    e->sel_anchor_col  = 0;
    e->sel_active      = 1;
    /* mover cursor al final del documento */
    size_t end_pos = buf_length(e->buf);
    buf_move_to(e->buf, end_pos);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

/* -- Clipboard ------------------------------------------------------ */
static void do_copy(Editor *e) {
    size_t from, to;
    if (!editor_sel_range(e, &from, &to)) return;
    size_t len = to - from;
    char *tmp = malloc(len + 1);
    if (!tmp) return;
    buf_get_text(e->buf, from, to, tmp);
    tmp[len] = '\0';
    SDL_SetClipboardText(tmp);
    free(tmp);
}

static void do_cut(Editor *e) {
    do_copy(e);
    delete_selection(e);
}

static void do_paste(Editor *e) {
    if (!SDL_HasClipboardText()) return;
    char *text = SDL_GetClipboardText();
    if (!text) return;
    size_t len = strlen(text);
    if (len == 0) { SDL_free(text); return; }

    delete_selection(e);  /* borra selección si la hay */

    size_t pos = buf_cursor_pos(e->buf);
    editor_undo_push_insert(e, pos, text, len);
    buf_insert_str(e->buf, text, len);
    SDL_free(text);
    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1; e->needs_redraw = 1;
}

/* -- Ctrl+D — duplicar línea actual --------------------------------- */
static void duplicate_line(Editor *e) {
    /* Obtener texto de la línea actual */
    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t line_end   = buf_line_end(e->buf, line_start);
    size_t len        = line_end - line_start;

    char *tmp = malloc(len + 2);
    if (!tmp) return;
    buf_get_text(e->buf, line_start, line_end, tmp);
    tmp[len]     = '\n';
    tmp[len + 1] = '\0';

    /* Insertar al final de la línea */
    buf_move_to(e->buf, line_end);
    editor_undo_push_insert(e, line_end, tmp, len + 1);
    buf_insert_str(e->buf, tmp, len + 1);
    free(tmp);

    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1; e->needs_redraw = 1;
}

/* -- Ctrl+/ — comentar/descomentar línea --------------------------- */
static void toggle_line_comment(Editor *e) {
    /* Detecta el comentario según la extensión del archivo */
    const char *prefix = "// ";  /* default C/C++/JS */
    const char *ext    = strrchr(e->filepath, '.');
    if (ext) {
        if (strcmp(ext, ".py") == 0 || strcmp(ext, ".sh") == 0 ||
            strcmp(ext, ".rb") == 0 || strcmp(ext, ".yaml") == 0 ||
            strcmp(ext, ".yml") == 0 || strcmp(ext, ".toml") == 0)
            prefix = "# ";
        else if (strcmp(ext, ".lua") == 0)
            prefix = "-- ";
        else if (strcmp(ext, ".sql") == 0)
            prefix = "-- ";
    }
    size_t plen = strlen(prefix);

    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t line_end   = buf_line_end(e->buf, line_start);
    size_t line_len   = line_end - line_start;

    char *line_text = malloc(line_len + 1);
    if (!line_text) return;
    buf_get_text(e->buf, line_start, line_end, line_text);
    line_text[line_len] = '\0';

    /* Omitir espacios iniciales para el check */
    size_t indent = 0;
    while (indent < line_len && (line_text[indent] == ' ' || line_text[indent] == '\t'))
        indent++;

    if (line_len - indent >= plen &&
        strncmp(line_text + indent, prefix, plen) == 0) {
        /* ya está comentado → quitar prefijo */
        size_t del_pos = line_start + indent;
        editor_undo_push_delete(e, del_pos, prefix, plen);
        buf_delete_range(e->buf, del_pos, del_pos + plen);
        buf_move_to(e->buf, del_pos);
    } else {
        /* sin comentar → añadir prefijo en la posición de sangría */
        size_t ins_pos = line_start + indent;
        editor_undo_push_insert(e, ins_pos, prefix, plen);
        buf_move_to(e->buf, ins_pos);
        buf_insert_str(e->buf, prefix, plen);
        buf_move_to(e->buf, ins_pos + plen);
    }
    free(line_text);

    editor_sync_cursor(e);
    editor_update_lexer(e, e->cursor_line);
    editor_ensure_visible(e);
    e->modified = 1; e->needs_redraw = 1;
}

/* -- Ctrl+L — seleccionar línea completa ---------------------------- */
static void select_line(Editor *e) {
    size_t line_start = editor_pos_from_line_col(e, e->cursor_line, 0);
    size_t line_end   = buf_line_end(e->buf, line_start);
    /* si no es la última línea, incluye el \n */
    if (line_end < buf_length(e->buf)) line_end++;

    e->sel_active      = 1;
    e->sel_anchor_line = e->cursor_line;
    e->sel_anchor_col  = 0;
    buf_move_to(e->buf, line_end);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    e->needs_redraw = 1;
}

/* -- Ctrl+G — ir a línea -------------------------------------------- */
/* Implementación simple: el número se teclea en la barra find reutilizada.
   Se activa con un flag especial y se interpreta el texto como número de línea. */
#define GOTO_MODE_PREFIX "Ir a línea: "

/* -- Ctrl+F — barra de búsqueda ------------------------------------ */

/* Busca hacia adelante desde `start_pos` (exclusivo).
   Devuelve la posición lógica del match o (size_t)-1 si no encontrado. */
static size_t find_next(Editor *e, size_t start_pos) {
    FindBar *f = &e->find;
    if (f->query_len == 0) return (size_t)-1;
    size_t len = buf_length(e->buf);
    size_t qlen = (size_t)f->query_len;
    for (size_t i = start_pos; i + qlen <= len; i++) {
        int match = 1;
        for (size_t j = 0; j < qlen && match; j++) {
            char bc = buf_char_at(e->buf, i + j);
            char qc = f->query[j];
            /* búsqueda case-insensitive */
            if (tolower((unsigned char)bc) != tolower((unsigned char)qc))
                match = 0;
        }
        if (match) return i;
    }
    return (size_t)-1;
}

/* Cuenta todas las coincidencias y actualiza match_count / match_index
   según la posición actual del cursor. */
static void count_matches(Editor *e) {
    FindBar *f = &e->find;
    f->match_count = 0;
    f->match_index = 0;
    if (f->query_len == 0) return;
    size_t len  = buf_length(e->buf);
    size_t qlen = (size_t)f->query_len;
    size_t cur  = buf_cursor_pos(e->buf);
    /* cursor está al FINAL del match actual (hit + qlen), así que
       el inicio del match actual es cur - qlen */
    size_t match_start = (cur >= qlen) ? cur - qlen : 0;
    int idx = 0;
    for (size_t i = 0; i + qlen <= len; i++) {
        int match = 1;
        for (size_t j = 0; j < qlen && match; j++) {
            if (tolower((unsigned char)buf_char_at(e->buf, i + j)) !=
                tolower((unsigned char)f->query[j]))
                match = 0;
        }
        if (match) {
            if (i < match_start) idx = f->match_count;
            else if (i == match_start) idx = f->match_count;
            f->match_count++;
            i += qlen - 1;
        }
    }
    f->match_index = idx;
}

static void find_jump(Editor *e) {
    if (e->find.query_len == 0) {
        /* query vacio: limpiar seleccion y resultado */
        editor_sel_clear(e);
        e->find.result_line = -1;
        e->needs_redraw = 1;
        return;
    }
    size_t from = buf_cursor_pos(e->buf) + 1;
    size_t hit  = find_next(e, from);
    if (hit == (size_t)-1) {
        /* wrap around */
        hit = find_next(e, 0);
    }
    if (hit == (size_t)-1) {
        editor_sel_clear(e);
        e->find.result_line = -1;
        e->needs_redraw = 1;
        return;
    }
    /* posicionar cursor en el inicio del match para calcular linea/col del ancla */
    buf_move_to(e->buf, hit);
    editor_sync_cursor(e);
    e->find.result_line = e->cursor_line;
    e->find.result_col  = e->cursor_col;
    /* fijar ancla ANTES de mover cursor al final */
    e->sel_active      = 1;
    e->sel_anchor_line = e->cursor_line;
    e->sel_anchor_col  = e->cursor_col;
    /* ahora mover cursor al final del match */
    buf_move_to(e->buf, hit + (size_t)e->find.query_len);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    count_matches(e);
    e->needs_redraw = 1;
}

/* Igual que find_jump pero siempre empieza desde el principio del documento.
   Se usa al escribir en el campo query para mostrar el primer resultado. */
static void find_first(Editor *e) {
    if (e->find.query_len == 0) {
        editor_sel_clear(e);
        e->find.result_line = -1;
        e->needs_redraw = 1;
        return;
    }
    size_t hit = find_next(e, 0);
    if (hit == (size_t)-1) {
        editor_sel_clear(e);
        e->find.result_line = -1;
        e->needs_redraw = 1;
        return;
    }
    buf_move_to(e->buf, hit);
    editor_sync_cursor(e);
    e->find.result_line = e->cursor_line;
    e->find.result_col  = e->cursor_col;
    e->sel_active      = 1;
    e->sel_anchor_line = e->cursor_line;
    e->sel_anchor_col  = e->cursor_col;
    buf_move_to(e->buf, hit + (size_t)e->find.query_len);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    count_matches(e);
    e->needs_redraw = 1;
}

/* Salta a la coincidencia ANTERIOR (hacia atrás). */
static void find_prev(Editor *e) {
    if (e->find.query_len == 0) {
        editor_sel_clear(e);
        e->find.result_line = -1;
        e->needs_redraw = 1;
        return;
    }
    /* La posición de inicio de la selección actual es el ancla.
       Busca el último match que termine ANTES de esa posición. */
    size_t qlen = (size_t)e->find.query_len;
    size_t len  = buf_length(e->buf);
    /* cursor actual apunta al final del match; retroceder al inicio */
    size_t cur  = buf_cursor_pos(e->buf);
    size_t search_end = (cur >= qlen) ? cur - qlen : 0; /* excluye match actual */

    size_t hit = (size_t)-1;
    /* Buscar hacia atrás: iteramos todos los matches y nos quedamos con el último < search_end */
    for (size_t i = 0; i + qlen <= len; i++) {
        int match = 1;
        for (size_t j = 0; j < qlen && match; j++) {
            if (tolower((unsigned char)buf_char_at(e->buf, i + j)) !=
                tolower((unsigned char)e->find.query[j]))
                match = 0;
        }
        if (match) {
            if (i < search_end) hit = i;
            i += qlen - 1;
        }
    }
    if (hit == (size_t)-1) {
        /* wrap around: último match del documento */
        for (size_t i = 0; i + qlen <= len; i++) {
            int match = 1;
            for (size_t j = 0; j < qlen && match; j++) {
                if (tolower((unsigned char)buf_char_at(e->buf, i + j)) !=
                    tolower((unsigned char)e->find.query[j]))
                    match = 0;
            }
            if (match) { hit = i; i += qlen - 1; }
        }
    }
    if (hit == (size_t)-1) {
        editor_sel_clear(e);
        e->find.result_line = -1;
        e->needs_redraw = 1;
        return;
    }
    buf_move_to(e->buf, hit);
    editor_sync_cursor(e);
    e->find.result_line = e->cursor_line;
    e->find.result_col  = e->cursor_col;
    e->sel_active      = 1;
    e->sel_anchor_line = e->cursor_line;
    e->sel_anchor_col  = e->cursor_col;
    buf_move_to(e->buf, hit + qlen);
    editor_sync_cursor(e);
    editor_ensure_visible(e);
    count_matches(e);
    e->needs_redraw = 1;
}
static void do_replace(Editor *e) {
    FindBar *f = &e->find;
    if (f->query_len == 0) return;

    /* Si el campo reemplazar está vacío, simplemente enfocar ese campo
       para que el usuario sepa que debe escribir el texto de reemplazo.
       Así evitamos borrar texto accidentalmente. */
    if (f->replace_len == 0 && !f->replace_focused) {
        f->replace_focused = 1;
        f->bar_focused     = 1;
        e->needs_redraw = 1;
        return;
    }

    /* Buscar el match actual: si hay seleccion activa que coincide, usarla;
       si no, buscar desde el principio para encontrar el match mas cercano. */
    size_t hit = (size_t)-1;
    size_t qlen = (size_t)f->query_len;

    if (e->sel_active) {
        size_t from, to;
        if (editor_sel_range(e, &from, &to) && (to - from) == qlen) {
            int match = 1;
            for (size_t j = 0; j < qlen && match; j++) {
                if (tolower((unsigned char)buf_char_at(e->buf, from + j)) !=
                    tolower((unsigned char)f->query[j]))
                    match = 0;
            }
            if (match) hit = from;
        }
    }

    /* Si no hay seleccion valida, buscar desde el cursor actual */
    if (hit == (size_t)-1) {
        size_t cur = buf_cursor_pos(e->buf);
        hit = find_next(e, cur);
        if (hit == (size_t)-1)
            hit = find_next(e, 0);
    }

    if (hit == (size_t)-1) {
        f->result_line = -1;
        e->needs_redraw = 1;
        return;
    }

    /* Borrar el match y escribir el reemplazo */
    buf_delete_range(e->buf, hit, hit + qlen);
    buf_move_to(e->buf, hit);
    if (f->replace_len > 0)
        buf_insert_str(e->buf, f->replace, (size_t)f->replace_len);
    editor_sync_cursor(e);
    editor_update_lexer(e, 0);
    e->modified = 1;

    /* Saltar al siguiente resultado */
    find_jump(e);
}

static void open_find_bar(Editor *e) {
    e->find.visible            = 1;
    e->find.query[0]           = '\0';
    e->find.query_len          = 0;
    e->find.replace[0]         = '\0';
    e->find.replace_len        = 0;
    e->find.replace_focused    = 0;
    e->find.bar_focused        = 1;
    e->find.result_line        = -1;
    e->find.query_sel_start    = -1;
    e->find.query_sel_end      = -1;
    e->find.replace_sel_start  = -1;
    e->find.replace_sel_end    = -1;
    e->find.match_count        = 0;
    e->find.match_index        = 0;
    e->find.prev_btn_w         = 0;
    e->find.next_btn_w         = 0;
    e->needs_redraw = 1;
}

static void close_find_bar(Editor *e) {
    e->find.visible = 0;
    e->needs_redraw = 1;
}

/* -- Ctrl+B — toggle panel lateral --------------------------------- */
static void toggle_sidebar(Editor *e) {
    e->ftree.open = !e->ftree.open;
    e->needs_redraw = 1;
}

/* -- dispatcher principal -------------------------------------------------- */
void input_handle_event(Editor *e, SDL_Event *ev) {
    SDL_Keymod mods  = SDL_GetModState();
    int ctrl  = (mods & SDL_KMOD_CTRL)  != 0;
    int shift = (mods & SDL_KMOD_SHIFT) != 0;

    switch (ev->type) {

    case SDL_EVENT_QUIT:
        e->running = 0;
        break;

    case SDL_EVENT_WINDOW_RESIZED:
        e->win_w = ev->window.data1;
        e->win_h = ev->window.data2;
        e->needs_redraw = 1;
        break;

    case SDL_EVENT_MOUSE_WHEEL: {
        float mx2f = 0.0f, my2f = 0.0f;
        SDL_GetMouseState(&mx2f, &my2f);
        int mx2 = (int)mx2f;
        int left2 = get_left_offset(e);
        if (e->ftree.open && mx2 < left2) {
            e->ftree.scroll -= (int)(ev->wheel.y * 3);
            if (e->ftree.scroll < 0) e->ftree.scroll = 0;
            e->needs_redraw = 1;
        } else {
            handle_scroll(e, ev->wheel.y);
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (ev->button.button == SDL_BUTTON_LEFT) {
            e->ftree.dragging_border  = 0;
            e->mouse_selecting        = 0;
            e->scrollbar_dragging     = 0;
        }
        break;

    case SDL_EVENT_MOUSE_MOTION: {
        int mx = (int)ev->motion.x;
        int my = (int)ev->motion.y;
        if (e->menu_open) {
            int prev = e->menu_hovered;
            e->menu_hovered = menu_item_at(mx, my);
            if (e->menu_hovered != prev) e->needs_redraw = 1;
        }
        if (e->ftree.open && mx < e->ftree.width && my >= NAVBAR_HEIGHT + TAB_BAR_HEIGHT)
            handle_ftree_hover(e, mx, my);
        if (e->ftree.dragging_border) {
            int new_w = e->ftree.drag_start_w + (mx - e->ftree.drag_start_x);
            if (new_w < FTREE_MIN_WIDTH)  new_w = FTREE_MIN_WIDTH;
            if (new_w > e->win_w / 2)     new_w = e->win_w / 2;
            e->ftree.width = new_w;
            e->needs_redraw = 1;
        }

        /* arratre de scrollbar */
        if (e->scrollbar_dragging) {
            int text_height   = e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT - SHORTCUT_HEIGHT;
            if (e->tab_count == 0) break;
            int total_lines   = buf_line_count(e->buf);
            int visible_lines = text_height / LINE_HEIGHT;
            int max_scroll    = total_lines - visible_lines;
            if (max_scroll < 0) max_scroll = 0;
            float thumb_h_ratio = (visible_lines > 0 && total_lines > 0)
                                  ? (float)visible_lines / (float)total_lines : 1.0f;
            int thumb_h = (int)(text_height * thumb_h_ratio);
            if (thumb_h < 20) thumb_h = 20;
            int thumb_range = text_height - thumb_h;
            if (thumb_range < 1) thumb_range = 1;
            float frac = (float)(my - e->scrollbar_drag_start_y) / (float)thumb_range;
            int new_scroll = e->scrollbar_drag_start_line + (int)(frac * max_scroll);
            if (new_scroll < 0)          new_scroll = 0;
            if (new_scroll > max_scroll) new_scroll = max_scroll;
            e->scroll_line = new_scroll;
            e->needs_redraw = 1;
        }

        /* arrastre para seleccionar texto */
        if (e->mouse_selecting && e->tab_count > 0) {
            int left    = get_left_offset(e);
            int text_x  = left + GUTTER_WIDTH + PADDING_LEFT;
            int cw      = (e->char_w > 0 ? e->char_w : 8);
            /* calcular línea visual */
            int vis_line = (my - NAVBAR_HEIGHT - TAB_BAR_HEIGHT) / LINE_HEIGHT;
            if (vis_line < 0) vis_line = 0;
            int line = e->scroll_line + vis_line;
            int total = buf_line_count(e->buf);
            if (line < 0)       line = 0;
            if (line >= total)  line = total - 1;
            /* calcular columna — limitar al largo real de la línea */
            int vis_col = (mx - text_x + e->scroll_col * cw) / cw;
            if (vis_col < 0) vis_col = 0;
            /* obtener longitud real de la línea */
            size_t ls = editor_pos_from_line_col(e, line, 0);
            size_t le = buf_line_end(e->buf, ls);
            int line_len = (int)(le - ls);
            if (vis_col > line_len) vis_col = line_len;
            int col = vis_col;
            /* activar selección manteniendo el ancla fijada en BUTTON_DOWN */
            e->sel_active = 1;
            /* mover cursor sin tocar el ancla */
            size_t pos = editor_pos_from_line_col(e, line, col);
            buf_move_to(e->buf, pos);
            editor_sync_cursor(e);
            editor_ensure_visible(e);
            e->needs_redraw = 1;
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int mx = (int)ev->button.x;
        int my = (int)ev->button.y;
        if (ev->button.button != SDL_BUTTON_LEFT) break;

        /* -- clic en la barra de tabs -- */
        if (my >= NAVBAR_HEIGHT && my < NAVBAR_HEIGHT + TAB_BAR_HEIGHT) {
            /* botón + nuevo tab */
            if (mx >= e->tab_new_btn_x && mx < e->tab_new_btn_x + 28) {
                editor_tab_new(e);
                break;
            }
            /* clic en tab existente */
            for (int i = 0; i < e->tab_count; i++) {
                EditorTab *t = &e->tabs[i];
                if (mx >= t->tab_x && mx < t->tab_x + t->tab_w) {
                    /* botón × cerrar */
                    if (mx >= t->close_x && mx < t->close_x + 16 &&
                        my >= t->close_y  && my < t->close_y  + 16) {
                        editor_tab_save_state(e);
                        e->active_tab = i;
                        editor_tab_close(e);
                        const char *title = e->filepath[0] ? e->filepath : "CoffeeCode - Sin título";
                        SDL_SetWindowTitle(e->window, title);
                    } else {
                        editor_tab_switch(e, i);
                        const char *title = e->tabs[i].filepath[0] ? e->tabs[i].filepath : "CoffeeCode - Sin título";
                        SDL_SetWindowTitle(e->window, title);
                    }
                    break;
                }
            }
            break;
        }

        /* clic en el menú abierto */
        if (e->menu_open) {
            int item = menu_item_at(mx, my);
            if (item >= 0) {
                menu_exec(e, item);
            } else {
                int in_btn = (mx >= BTN_FILE_X && mx < BTN_FILE_X + BTN_FILE_W
                              && my >= 0 && my < NAVBAR_HEIGHT);
                e->menu_open    = in_btn ? 0 : 0;
                e->menu_hovered = -1;
                e->needs_redraw = 1;
            }
            break;
        }

        /* clic en botón "Archivo" */
        if (my >= 0 && my < NAVBAR_HEIGHT &&
            mx >= BTN_FILE_X && mx < BTN_FILE_X + BTN_FILE_W) {
            e->menu_open    = 1;
            e->menu_hovered = -1;
            e->needs_redraw = 1;
            break;
        }

        /* clic en la barra de búsqueda */
        if (e->find.visible) {
            FindBar *fb = &e->find;
            /* clic en botón Reemplazar */
            if (mx >= fb->replace_btn_x && mx < fb->replace_btn_x + fb->replace_btn_w &&
                my >= fb->replace_btn_y && my < fb->replace_btn_y + fb->replace_btn_h) {
                fb->bar_focused = 1;
                do_replace(e);
                break;
            }
            /* clic en botón ↑ (prev) */
            if (fb->prev_btn_w > 0 &&
                mx >= fb->prev_btn_x && mx < fb->prev_btn_x + fb->prev_btn_w &&
                my >= fb->prev_btn_y && my < fb->prev_btn_y + fb->prev_btn_h) {
                fb->bar_focused = 1;
                find_prev(e);
                break;
            }
            /* clic en botón ↓ (next) */
            if (fb->next_btn_w > 0 &&
                mx >= fb->next_btn_x && mx < fb->next_btn_x + fb->next_btn_w &&
                my >= fb->next_btn_y && my < fb->next_btn_y + fb->next_btn_h) {
                fb->bar_focused = 1;
                find_jump(e);
                break;
            }
            /* clic en campo buscar */
            if (mx >= fb->field_x && mx < fb->bar_x + fb->bar_w &&
                my >= fb->row1_y  && my < fb->row1_y + fb->field_h) {
                fb->replace_focused = 0;
                fb->bar_focused     = 1;
                e->needs_redraw = 1;
                break;
            }
            /* clic en campo reemplazar */
            if (mx >= fb->field_x && mx < fb->bar_x + fb->bar_w &&
                my >= fb->row2_y  && my < fb->row2_y + fb->field_h) {
                fb->replace_focused = 1;
                fb->bar_focused     = 1;
                e->needs_redraw = 1;
                break;
            }
            /* clic dentro de la barra pero fuera de campos */
            if (mx >= fb->bar_x && mx < fb->bar_x + fb->bar_w &&
                my >= fb->bar_y  && my < fb->bar_y  + fb->bar_h) {
                break;
            }
            /* clic fuera de la barra: foco vuelve al editor */
            if (fb->bar_focused) {
                fb->bar_focused = 0;
                e->needs_redraw = 1;
            }
        }

        /* clic fuera de navbar y menú */
        if (my >= NAVBAR_HEIGHT + TAB_BAR_HEIGHT) {
            if (e->menu_open) {
                int menu_y = NAVBAR_HEIGHT;
                int mh     = menu_total_h();
                if (!(mx >= BTN_FILE_X && mx < BTN_FILE_X + MENU_WIDTH
                      && my >= menu_y && my < menu_y + mh)) {
                    e->menu_open = 0;
                    e->needs_redraw = 1;
                }
            }

            /* clic en la scrollbar */
            {
                int sb_x = e->win_w - 9; /* SCROLLBAR_W=8 + 1px borde */
                if (mx >= sb_x) {
                    e->scrollbar_dragging        = 1;
                    e->scrollbar_drag_start_y    = my;
                    e->scrollbar_drag_start_line = e->scroll_line;
                    break;
                }
            }

            int left = get_left_offset(e);
            if (mx < left) {
                handle_ftree_click(e, mx, my);
            } else {
                /* iniciar selección con ratón */
                if (e->tab_count == 0) break;  /* sin tabs, nada que hacer */
                int text_x = left + GUTTER_WIDTH + PADDING_LEFT;
                int cw     = (e->char_w > 0 ? e->char_w : 8);
                int vis_line = (my - NAVBAR_HEIGHT - TAB_BAR_HEIGHT) / LINE_HEIGHT;
                if (vis_line < 0) vis_line = 0;
                int line = e->scroll_line + vis_line;
                int total = buf_line_count(e->buf);
                if (line < 0)      line = 0;
                if (line >= total) line = total - 1;
                /* columna con scroll y limitada al largo real */
                int vis_col = (mx - text_x + e->scroll_col * cw) / cw;
                if (vis_col < 0) vis_col = 0;
                size_t ls = editor_pos_from_line_col(e, line, 0);
                size_t le = buf_line_end(e->buf, ls);
                int line_len = (int)(le - ls);
                if (vis_col > line_len) vis_col = line_len;
                int col = vis_col;

                editor_sel_clear(e);
                /* fijar ancla ANTES de mover el cursor, usando las coords calculadas */
                e->sel_anchor_line = line;
                e->sel_anchor_col  = col;
                e->mouse_selecting = 1;
                size_t pos = editor_pos_from_line_col(e, line, col);
                buf_move_to(e->buf, pos);
                editor_sync_cursor(e);
                editor_ensure_visible(e);
                e->needs_redraw = 1;
            }
        }
        break;
    }

    /* -- entrada de texto — va a la barra de búsqueda si está visible */
    case SDL_EVENT_TEXT_INPUT:
        if (e->menu_open) break;
        if (ctrl) break;  /* ignorar cuando Ctrl esta pulsado (ej: Ctrl+A, Ctrl+C) */
        if (e->find.visible && !e->find.bar_focused) {
            /* barra visible pero sin foco: el texto va al editor normalmente */
            if (e->tab_count == 0) break;
            buf_insert_str(e->buf, ev->text.text, strlen(ev->text.text));
            editor_sync_cursor(e);
            editor_update_lexer(e, e->cursor_line);
            editor_ensure_visible(e);
            e->modified = 1; e->needs_redraw = 1;
            break;
        }
        if (e->find.visible) {
            size_t tlen = strlen(ev->text.text);
            if (e->find.replace_focused == 0) {
                /* Si hay seleccion, borrar primero el tramo seleccionado */
                if (e->find.query_sel_start >= 0 &&
                    e->find.query_sel_end > e->find.query_sel_start) {
                    int s = e->find.query_sel_start;
                    int n = e->find.query_sel_end - s;
                    memmove(e->find.query + s,
                            e->find.query + e->find.query_sel_end,
                            (size_t)(e->find.query_len - e->find.query_sel_end) + 1);
                    e->find.query_len -= n;
                }
                e->find.query_sel_start = -1;
                e->find.query_sel_end   = -1;
                for (size_t i = 0; i < tlen; i++) {
                    if (e->find.query_len < FIND_BAR_MAX - 1) {
                        e->find.query[e->find.query_len++] = ev->text.text[i];
                        e->find.query[e->find.query_len]   = '\0';
                    }
                }
                find_first(e);
            } else {
                /* Si hay seleccion, borrar primero el tramo seleccionado */
                if (e->find.replace_sel_start >= 0 &&
                    e->find.replace_sel_end > e->find.replace_sel_start) {
                    int s = e->find.replace_sel_start;
                    int n = e->find.replace_sel_end - s;
                    memmove(e->find.replace + s,
                            e->find.replace + e->find.replace_sel_end,
                            (size_t)(e->find.replace_len - e->find.replace_sel_end) + 1);
                    e->find.replace_len -= n;
                }
                e->find.replace_sel_start = -1;
                e->find.replace_sel_end   = -1;
                for (size_t i = 0; i < tlen; i++) {
                    if (e->find.replace_len < FIND_BAR_MAX - 1) {
                        e->find.replace[e->find.replace_len++] = ev->text.text[i];
                        e->find.replace[e->find.replace_len]   = '\0';
                    }
                }
                e->needs_redraw = 1;
            }
            break;
        }
        if (e->tab_count == 0) break;
        buf_insert_str(e->buf, ev->text.text, strlen(ev->text.text));
        editor_sync_cursor(e);
        editor_update_lexer(e, e->cursor_line);
        editor_ensure_visible(e);
        e->modified = 1; e->needs_redraw = 1;
        break;

    case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode key = ev->key.key;

        /* Escape cierra barras / menú */
        if (key == SDLK_ESCAPE) {
            if (e->find.visible) { close_find_bar(e); break; }
            if (e->menu_open)    { e->menu_open = 0; e->menu_hovered = -1; e->needs_redraw = 1; break; }
            editor_sel_clear(e); e->needs_redraw = 1;
            break;
        }

        /* -- Barra de busqueda activa: teclas especiales -- */
        if (e->find.visible) {
            /* Si la barra no tiene foco, las teclas van al editor.
               Ctrl+F vuelve a enfocar la barra. */
            if (!e->find.bar_focused) {
                if (ctrl && key == SDLK_F) {
                    e->find.bar_focused = 1;
                    e->needs_redraw = 1;
                    break;
                }
                /* dejar caer al bloque normal del editor */
                goto editor_keys;
            }
            if (key == SDLK_TAB) {
                /* Tab alterna campo; limpia seleccion de ambos */
                e->find.query_sel_start   = -1;
                e->find.query_sel_end     = -1;
                e->find.replace_sel_start = -1;
                e->find.replace_sel_end   = -1;
                e->find.replace_focused   = !e->find.replace_focused;
                e->needs_redraw = 1;
                break;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                if (e->find.replace_focused) {
                    do_replace(e);
                } else if (shift) {
                    find_prev(e);
                } else {
                    find_jump(e);
                }
                break;
            }
            if (key == SDLK_BACKSPACE) {
                if (e->find.replace_focused == 0) {
                    if (e->find.query_sel_start >= 0 &&
                        e->find.query_sel_end > e->find.query_sel_start) {
                        /* borrar seleccion completa */
                        int s = e->find.query_sel_start;
                        int n = e->find.query_sel_end - s;
                        memmove(e->find.query + s,
                                e->find.query + e->find.query_sel_end,
                                (size_t)(e->find.query_len - e->find.query_sel_end) + 1);
                        e->find.query_len -= n;
                        e->find.query_sel_start = -1;
                        e->find.query_sel_end   = -1;
                    } else if (e->find.query_len > 0) {
                        e->find.query[--e->find.query_len] = '\0';
                    }
                    find_first(e);
                } else {
                    if (e->find.replace_sel_start >= 0 &&
                        e->find.replace_sel_end > e->find.replace_sel_start) {
                        /* borrar seleccion completa */
                        int s = e->find.replace_sel_start;
                        int n = e->find.replace_sel_end - s;
                        memmove(e->find.replace + s,
                                e->find.replace + e->find.replace_sel_end,
                                (size_t)(e->find.replace_len - e->find.replace_sel_end) + 1);
                        e->find.replace_len -= n;
                        e->find.replace_sel_start = -1;
                        e->find.replace_sel_end   = -1;
                    } else if (e->find.replace_len > 0) {
                        e->find.replace[--e->find.replace_len] = '\0';
                    }
                    e->needs_redraw = 1;
                }
                break;
            }
            /* Ctrl+F de nuevo = siguiente resultado */
            if (ctrl && key == SDLK_F) { find_jump(e); break; }
            /* Ctrl+A — seleccionar todo el texto del campo activo */
            if (ctrl && key == SDLK_A) {
                if (e->find.replace_focused == 0) {
                    if (e->find.query_len > 0) {
                        e->find.query_sel_start = 0;
                        e->find.query_sel_end   = e->find.query_len;
                        e->needs_redraw = 1;
                    }
                } else {
                    if (e->find.replace_len > 0) {
                        e->find.replace_sel_start = 0;
                        e->find.replace_sel_end   = e->find.replace_len;
                        e->needs_redraw = 1;
                    }
                }
                break;
            }
            break;  /* resto de teclas ignoradas mientras find esta abierto */
        }

        editor_keys:
        if (ctrl) {
            switch (key) {
            /* -- Archivo -- */
            case SDLK_N: new_file(e);          break;
            case SDLK_O: open_file_dialog(e);  break;
            case SDLK_K: open_folder_dialog(e); break;
            case SDLK_S: save_file(e);         break;
            case SDLK_Q: e->running = 0;       break;

            /* -- Edición -- */
            case SDLK_Z: if (e->buf) editor_undo(e);       break;
            case SDLK_Y: if (e->buf) editor_redo(e);       break;
            case SDLK_A: if (e->buf) select_all(e);        break;
            case SDLK_C: if (e->buf) do_copy(e);           break;
            case SDLK_X: if (e->buf) do_cut(e);            break;
            case SDLK_V: if (e->buf) do_paste(e);          break;
            case SDLK_D: if (e->buf) duplicate_line(e);    break;
            case SDLK_SLASH: if (e->buf) toggle_line_comment(e); break;
            case SDLK_L: if (e->buf) select_line(e);       break;

            /* -- Navegación -- */
            case SDLK_F: open_find_bar(e);     break;
            case SDLK_B: toggle_sidebar(e);    break;
            case SDLK_W: editor_tab_close(e);  break;
            case SDLK_TAB: if (e->tab_count > 0) editor_tab_switch(e, (e->active_tab+1) % e->tab_count); break;
            case SDLK_HOME: if (e->buf) move_cursor_select(e, 0, 0, shift); break;
            case SDLK_END: {
                if (!e->buf) break;
                int t = buf_line_count(e->buf) - 1;
                size_t ep = buf_line_end(e->buf,
                    editor_pos_from_line_col(e, t, 0));
                size_t sp = editor_pos_from_line_col(e, t, 0);
                move_cursor_select(e, t, (int)(ep - sp), shift);
                break;
            }
            /* -- salto de palabra -- */
            case SDLK_LEFT:  if (e->buf) move_word_left(e, shift);  break;
            case SDLK_RIGHT: if (e->buf) move_word_right(e, shift); break;
            case SDLK_UP:    if (e->buf) move_cursor_select(e, e->cursor_line - 5, e->cursor_col, shift); break;
            case SDLK_DOWN:  if (e->buf) move_cursor_select(e, e->cursor_line + 5, e->cursor_col, shift); break;
            default: break;
            }
        } else {
            if (e->menu_open) break;
            if (!e->buf) break;  /* sin buffer activo, ignorar teclas de edición */
            switch (key) {
            case SDLK_UP:        move_line_up(e, shift);   break;
            case SDLK_DOWN:      move_line_down(e, shift);  break;
            case SDLK_LEFT:      move_col_left(e, shift);   break;
            case SDLK_RIGHT:     move_col_right(e, shift);  break;
            case SDLK_HOME:      move_home(e, shift);       break;
            case SDLK_END:       move_end(e, shift);        break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:  insert_newline(e);         break;
            case SDLK_TAB:
                if (shift) {
                    /* Shift+Tab: quitar sangría */
                    size_t ls = editor_pos_from_line_col(e, e->cursor_line, 0);
                    int removed = 0;
                    for (int i = 0; i < TAB_SIZE; i++) {
                        if (buf_char_at(e->buf, ls) == ' ') {
                            char c = ' ';
                            editor_undo_push_delete(e, ls, &c, 1);
                            buf_delete_range(e->buf, ls, ls + 1);
                            removed++;
                        } else break;
                    }
                    if (removed) {
                        buf_move_to(e->buf, ls);
                        editor_sync_cursor(e);
                        editor_update_lexer(e, e->cursor_line);
                        editor_ensure_visible(e);
                        e->modified = 1; e->needs_redraw = 1;
                    }
                } else {
                    insert_tab(e);
                }
                break;
            case SDLK_BACKSPACE: do_backspace(e);    break;
            case SDLK_DELETE:    do_delete(e);       break;
            case SDLK_PAGEUP: {
                int text_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;
                int vis = text_h / LINE_HEIGHT;
                move_cursor_select(e, e->cursor_line - vis, e->cursor_col, shift);
                break;
            }
            case SDLK_PAGEDOWN: {
                int text_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;
                int vis = text_h / LINE_HEIGHT;
                move_cursor_select(e, e->cursor_line + vis, e->cursor_col, shift);
                break;
            }
            default: break;
            }
        }
        break;
    }

    default: break;
    }
}