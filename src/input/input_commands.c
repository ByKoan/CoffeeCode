#include "input_internal.h"

static const char *MENU_LABELS[MENU_ITEMS] = {
    "Nuevo", "Abrir archivo...", "Abrir carpeta...", NULL, "Guardar", "Autoguardado"
};

int menu_item_at(int mx, int my) {
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

int menu_total_h(void) {
    int h = 0;
    for (int i = 0; i < MENU_ITEMS; i++)
        h += MENU_LABELS[i] ? MENU_ITEM_H : 8;
    return h;
}


/* Devuelve el offset izquierdo actual según el estado del panel */

void new_file(Editor *e) {
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

void save_file(Editor *e) {
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

void SDLCALL file_dialog_cb(void *userdata,
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


void open_file_dialog(Editor *e) {
    SDL_DialogFileFilter filters[] = {
        { "Archivos de código", "c;h;cpp;hpp;py;js;ts;rs;go;java;txt;md;json;toml;yaml;yml" },
        { "Todos los archivos", "*" }
    };
    SDL_ShowOpenFileDialog(file_dialog_cb, e, e->window,
                           filters, 2, NULL, false);
}

/* -- Ejecutar un item del menú --------------------------------------------- */

void SDLCALL folder_dialog_cb(void *userdata,
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


void open_folder_dialog(Editor *e) {
    SDL_ShowOpenFolderDialog(folder_dialog_cb, e, e->window, NULL, false);
}


void menu_exec(Editor *e, int item) {
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

void toggle_sidebar(Editor *e) {
    e->ftree.open = !e->ftree.open;
    e->needs_redraw = 1;
}

/* -- dispatcher principal -------------------------------------------------- */
