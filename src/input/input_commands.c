/**
 * @file input_commands.c
 * @brief Comandos de archivo y menú: nuevo, guardar, diálogos de abrir
 *        archivo/carpeta, ejecución del menú "Archivo" y toggle del panel.
 */
#include "input_internal.h"

#define MENU_SEP_H 8 /* alto en px de un separador del menú */

/* Índices de los items del menú "Archivo" (orden de MENU_LABELS). */
enum {
    MENU_NEW = 0,
    MENU_OPEN_FILE,
    MENU_OPEN_FOLDER,
    MENU_SEP, /* separador */
    MENU_SAVE,
    MENU_AUTOSAVE,
};

static const char *MENU_LABELS[MENU_ITEMS] = {"Nuevo", "Abrir archivo...", "Abrir carpeta...",
                                              NULL,    "Guardar",          "Autoguardado"};

/** Alto en px del item @p i (item normal o separador). */
static int menu_item_height(int i) {
    return MENU_LABELS[i] ? MENU_ITEM_H : MENU_SEP_H;
}

/** Índice del item del menú bajo (@p mx, @p my), o -1 si ninguno. */
int menu_item_at(int mx, int my) {
    if (mx < BTN_FILE_X || mx >= BTN_FILE_X + MENU_WIDTH) return -1;
    int iy = NAVBAR_HEIGHT;
    for (int i = 0; i < MENU_ITEMS; i++) {
        int h = menu_item_height(i);
        if (MENU_LABELS[i] && my >= iy && my < iy + h) return i;
        iy += h;
    }
    return -1;
}

/** Altura total del menú desplegable. */
int menu_total_h(void) {
    int h = 0;
    for (int i = 0; i < MENU_ITEMS; i++)
        h += menu_item_height(i);
    return h;
}

/** Crea una pestaña nueva y vacía. */
void new_file(Editor *e) {
    editor_tab_new(e);
    e->filepath[0] = '\0';
    e->modified = 0;
    if (e->tab_count > 0) {
        e->tabs[e->active_tab].filepath[0] = '\0';
        e->tabs[e->active_tab].modified = 0;
    }
    SDL_SetWindowTitle(e->window, "CoffeeCode");
    e->needs_redraw = 1;
}

/** Guarda el archivo actual (usa "untitled.c" si no tiene nombre). */
void save_file(Editor *e) {
    if (!e->filepath[0]) strncpy(e->filepath, "untitled.c", sizeof(e->filepath) - 1);
    if (!buf_save_file(e->buf, e->filepath)) {
        e->needs_redraw = 1;
        return;
    }

    e->modified = 0;
    SDL_SetWindowTitle(e->window, e->filepath);

    /* sincronizar filepath, modified y mtime de vuelta al tab activo */
    if (e->tab_count > 0) {
        EditorTab *tab = &e->tabs[e->active_tab];
        strncpy(tab->filepath, e->filepath, sizeof(tab->filepath) - 1);
        tab->modified = 0;
        struct stat st;
        tab->loaded_mtime = (stat(e->filepath, &st) == 0) ? (long)st.st_mtime : 0;
    }
    e->needs_redraw = 1;
}

/** Callback de SDL al elegir un archivo en el diálogo de apertura. */
void SDLCALL file_dialog_cb(void *userdata, const char *const *filelist, int filter) {
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
        {"Archivos de código", "c;h;cpp;hpp;py;js;ts;rs;go;java;txt;md;json;toml;yaml;yml"},
        {"Todos los archivos", "*"}};
    SDL_ShowOpenFileDialog(file_dialog_cb, e, e->window, filters, 2, NULL, false);
}

/** Callback de SDL al elegir una carpeta en el diálogo. */
void SDLCALL folder_dialog_cb(void *userdata, const char *const *filelist, int filter) {
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

/** Ejecuta el item @p item del menú "Archivo" y lo cierra. */
void menu_exec(Editor *e, int item) {
    switch (item) {
    case MENU_NEW:         new_file(e);          break;
    case MENU_OPEN_FILE:   open_file_dialog(e);  break;
    case MENU_OPEN_FOLDER: open_folder_dialog(e); break;
    case MENU_SAVE:        save_file(e);         break;
    case MENU_AUTOSAVE:
        e->autosave = !e->autosave;
        if (e->autosave) e->autosave_last_ms = SDL_GetTicks();
        break;
    default: break;
    }
    e->menu_open = 0;
    e->menu_hovered = -1;
    e->needs_redraw = 1;
}

/** Muestra/oculta el panel lateral del explorador. */
void toggle_sidebar(Editor *e) {
    e->ftree.open = !e->ftree.open;
    e->needs_redraw = 1;
}
