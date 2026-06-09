/**
 * @file input_commands.c
 * @brief Comandos de archivo y menú: nuevo, guardar, diálogos de abrir
 *        archivo/carpeta, ejecución del menú "Archivo" y toggle del panel.
 *
 * @note Diálogos en Linux — GTK nativo vía dlopen.
 *
 * En Linux no se usa SDL_ShowOpenFileDialog porque depende de XDG Desktop
 * Portal (D-Bus), que no está disponible en todos los entornos (Kali, TTY,
 * WMs minimalistas...).
 *
 * En su lugar se carga libgtk-3.so en tiempo de ejecución con dlopen() y
 * se construye un GtkFileChooserDialog directamente. Esto es 100 % nativo
 * del sistema.
 * Si GTK no está instalado se imprime un aviso y se cancela sin crashear.
 *
 * El diálogo se ejecuta en un hilo secundario (SDL_CreateThread) para no
 * bloquear el bucle de eventos del hilo principal.
 *
 * En Windows se sigue usando SDL_ShowOpenFileDialog / SDL_ShowOpenFolderDialog
 * que llaman a la API Win32 nativa, sin ningún cambio.
 */
#include "input_internal.h"

/* Includes extra para Linux */
#if !(defined(_WIN32))
#include <dlfcn.h>   /* dlopen / dlsym / dlclose */
#include <stdio.h>
#include <string.h>
#endif

#define MENU_SEP_H 8

enum {
    MENU_NEW = 0,
    MENU_OPEN_FILE,
    MENU_OPEN_FOLDER,
    MENU_SEP,
    MENU_SAVE,
    MENU_AUTOSAVE,
};

static const char *MENU_LABELS[MENU_ITEMS] = {
    "Nuevo", "Abrir archivo...", "Abrir carpeta...",
    NULL,    "Guardar",          "Autoguardado"};

static int menu_item_height(int i) {
    return MENU_LABELS[i] ? MENU_ITEM_H : MENU_SEP_H;
}

int menu_item_at(int mx, int my) {
    if (mx < BTN_FILE_X || mx >= BTN_FILE_X + MENU_WIDTH)
        return -1;
    int iy = NAVBAR_HEIGHT;
    for (int i = 0; i < MENU_ITEMS; i++) {
        int h = menu_item_height(i);
        if (MENU_LABELS[i] && my >= iy && my < iy + h)
            return i;
        iy += h;
    }
    return -1;
}

int menu_total_h(void) {
    int h = 0;
    for (int i = 0; i < MENU_ITEMS; i++)
        h += menu_item_height(i);
    return h;
}

/* Operaciones de archivo */

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

void save_file(Editor *e) {
    if (!e->filepath[0])
        strncpy(e->filepath, "untitled.c", sizeof(e->filepath) - 1);
    if (!buf_save_file(e->buf, e->filepath)) {
        e->needs_redraw = 1;
        return;
    }
    e->modified = 0;
    SDL_SetWindowTitle(e->window, e->filepath);
    if (e->tab_count > 0) {
        EditorTab *tab = &e->tabs[e->active_tab];
        strncpy(tab->filepath, e->filepath, sizeof(tab->filepath) - 1);
        tab->modified = 0;
        struct stat st;
        tab->loaded_mtime =
            (stat(e->filepath, &st) == 0) ? (long)st.st_mtime : 0;
    }
    e->needs_redraw = 1;
}

/* Callbacks (compartidos por todas las plataformas) */

void SDLCALL file_dialog_cb(void *userdata, const char *const *filelist,
                            int filter) {
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

void SDLCALL folder_dialog_cb(void *userdata, const char *const *filelist,
                              int filter) {
    (void)filter;
    Editor *e = (Editor *)userdata;
    if (!filelist || !filelist[0]) {
        e->needs_redraw = 1;
        return;
    }
    ftree_load(&e->ftree, filelist[0]);
    e->needs_redraw = 1;
}

/* Implementación Linux: GTK3 nativo cargado con dlopen */

#if !(defined(_WIN32))

/* Tipos y constantes mínimos de GTK que necesitamos */

typedef void      GtkWidget;
typedef void      GtkFileFilter;
typedef int       gint;
typedef char      gchar;
typedef int       gboolean;
typedef void *    gpointer;

/* GtkFileChooserAction */
#define GTK_FILE_CHOOSER_ACTION_OPEN          0
#define GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER 2

/* Respuestas de GtkDialog */
#define GTK_RESPONSE_ACCEPT -3
#define GTK_RESPONSE_CANCEL -6

/* GTK_STOCK buttons (GTK3) */
#define GTK_STOCK_CANCEL "gtk-cancel"
#define GTK_STOCK_OPEN   "gtk-open"

/* Sentinel para gtk_file_chooser_dialog_new (varargs) */
#define GTK_BUTTONS_SENTINEL NULL

/* Punteros a las funciones GTK que cargaremos con dlsym */

typedef gboolean  (*fn_gtk_init_check)   (int *, char ***);
typedef GtkWidget*(*fn_gtk_file_chooser_dialog_new)(
                      const gchar *, GtkWidget *, gint,
                      const gchar *, gint,
                      const gchar *, gint,
                      gpointer);
typedef gint      (*fn_gtk_dialog_run)         (GtkWidget *);
typedef gchar *   (*fn_gtk_file_chooser_get_filename)(GtkWidget *);
typedef void      (*fn_gtk_file_chooser_set_select_multiple)(GtkWidget *, gboolean);
typedef void      (*fn_gtk_widget_destroy)     (GtkWidget *);
typedef void      (*fn_gtk_main_iteration_do)  (gboolean);
typedef gboolean  (*fn_gtk_events_pending)     (void);
typedef GtkFileFilter *(*fn_gtk_file_filter_new)(void);
typedef void      (*fn_gtk_file_filter_set_name)(GtkFileFilter *, const gchar *);
typedef void      (*fn_gtk_file_filter_add_pattern)(GtkFileFilter *, const gchar *);
typedef void      (*fn_gtk_file_chooser_add_filter)(GtkWidget *, GtkFileFilter *);
typedef void      (*fn_g_free)                 (gpointer);

/* Estructura con todas las funciones cargadas */

typedef struct {
    void *lib_gtk;
    void *lib_glib;

    fn_gtk_init_check                        gtk_init_check;
    fn_gtk_file_chooser_dialog_new           gtk_file_chooser_dialog_new;
    fn_gtk_dialog_run                        gtk_dialog_run;
    fn_gtk_file_chooser_get_filename         gtk_file_chooser_get_filename;
    fn_gtk_file_chooser_set_select_multiple  gtk_file_chooser_set_select_multiple;
    fn_gtk_widget_destroy                    gtk_widget_destroy;
    fn_gtk_main_iteration_do                 gtk_main_iteration_do;
    fn_gtk_events_pending                    gtk_events_pending;
    fn_gtk_file_filter_new                   gtk_file_filter_new;
    fn_gtk_file_filter_set_name              gtk_file_filter_set_name;
    fn_gtk_file_filter_add_pattern           gtk_file_filter_add_pattern;
    fn_gtk_file_chooser_add_filter           gtk_file_chooser_add_filter;
    fn_g_free                                g_free;
} GtkHandles;

/**
 * @brief Carga libgtk-3 y resuelve todos los símbolos necesarios.
 * @return 1 si tuvo éxito, 0 si GTK no está disponible.
 */
static int gtk_handles_load(GtkHandles *g) {
    /* Nombres de libgtk-3 en distintas distros */
    static const char *gtk_names[] = {
        "libgtk-3.so.0",
        "libgtk-3.so",
        NULL
    };
    static const char *glib_names[] = {
        "libglib-2.0.so.0",
        "libglib-2.0.so",
        NULL
    };

    g->lib_gtk = NULL;
    for (int i = 0; gtk_names[i]; i++) {
        g->lib_gtk = dlopen(gtk_names[i], RTLD_LAZY | RTLD_LOCAL);
        if (g->lib_gtk) break;
    }
    if (!g->lib_gtk) {
        fprintf(stderr, "CoffeeCode: no se pudo cargar libgtk-3: %s\n"
                        "  Instala GTK3: sudo apt install libgtk-3-0\n",
                dlerror());
        return 0;
    }

    g->lib_glib = NULL;
    for (int i = 0; glib_names[i]; i++) {
        g->lib_glib = dlopen(glib_names[i], RTLD_LAZY | RTLD_LOCAL);
        if (g->lib_glib) break;
    }
    /* g_free puede estar también en libgtk, intentamos de ahí si falta glib */

#define LOAD(handle, sym) \
    g->sym = (fn_##sym) dlsym(handle, #sym); \
    if (!g->sym) { \
        fprintf(stderr, "CoffeeCode: dlsym(%s) falló: %s\n", #sym, dlerror()); \
        dlclose(g->lib_gtk); \
        if (g->lib_glib) dlclose(g->lib_glib); \
        return 0; \
    }

    LOAD(g->lib_gtk, gtk_init_check)
    LOAD(g->lib_gtk, gtk_file_chooser_dialog_new)
    LOAD(g->lib_gtk, gtk_dialog_run)
    LOAD(g->lib_gtk, gtk_file_chooser_get_filename)
    LOAD(g->lib_gtk, gtk_file_chooser_set_select_multiple)
    LOAD(g->lib_gtk, gtk_widget_destroy)
    LOAD(g->lib_gtk, gtk_main_iteration_do)
    LOAD(g->lib_gtk, gtk_events_pending)
    LOAD(g->lib_gtk, gtk_file_filter_new)
    LOAD(g->lib_gtk, gtk_file_filter_set_name)
    LOAD(g->lib_gtk, gtk_file_filter_add_pattern)
    LOAD(g->lib_gtk, gtk_file_chooser_add_filter)
#undef LOAD

    /* g_free: preferimos glib, caemos a gtk si no hay */
    if (g->lib_glib)
        g->g_free = (fn_g_free) dlsym(g->lib_glib, "g_free");
    if (!g->g_free)
        g->g_free = (fn_g_free) dlsym(g->lib_gtk, "g_free");
    if (!g->g_free) {
        fprintf(stderr, "CoffeeCode: dlsym(g_free) falló\n");
        dlclose(g->lib_gtk);
        if (g->lib_glib) dlclose(g->lib_glib);
        return 0;
    }

    return 1;
}

static void gtk_handles_unload(GtkHandles *g) {
    if (g->lib_glib) dlclose(g->lib_glib);
    if (g->lib_gtk)  dlclose(g->lib_gtk);
}

/* Datos para el hilo */

typedef struct {
    Editor *editor;
    int     is_folder;
} LinuxDialogData;

/**
 * @brief Hilo que abre el GtkFileChooserDialog y entrega el resultado.
 *
 * Carga GTK dinámicamente, muestra el diálogo, lee la ruta elegida y
 * llama al callback correspondiente (file_dialog_cb / folder_dialog_cb).
 */
static int SDLCALL linux_dialog_thread(void *data) {
    LinuxDialogData *d = (LinuxDialogData *)data;
    Editor *e           = d->editor;
    int     is_folder   = d->is_folder;
    SDL_free(d);

    GtkHandles g;
    if (!gtk_handles_load(&g)) {
        if (is_folder) folder_dialog_cb(e, NULL, 0);
        else           file_dialog_cb(e, NULL, 0);
        return 0;
    }

    /* Inicializar GTK (puede fallar si no hay DISPLAY/Wayland, lo toleramos) */
    int   argc = 0;
    char *argv_dummy[] = {NULL};
    char **argv_ptr = argv_dummy;
    g.gtk_init_check(&argc, &argv_ptr);

    /* Crear el diálogo */
    gint action = is_folder
        ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
        : GTK_FILE_CHOOSER_ACTION_OPEN;

    const char *title = is_folder ? "Abrir carpeta" : "Abrir archivo";

    GtkWidget *dialog = g.gtk_file_chooser_dialog_new(
        title,
        NULL,          /* ventana padre: NULL (hilo separado) */
        action,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_OPEN,   GTK_RESPONSE_ACCEPT,
        GTK_BUTTONS_SENTINEL
    );

    if (!dialog) {
        fprintf(stderr, "CoffeeCode: gtk_file_chooser_dialog_new devolvió NULL\n");
        gtk_handles_unload(&g);
        if (is_folder) folder_dialog_cb(e, NULL, 0);
        else           file_dialog_cb(e, NULL, 0);
        return 0;
    }

    g.gtk_file_chooser_set_select_multiple(dialog, 0 /* FALSE */);

    /* Filtros de tipo de archivo (solo para diálogo de archivo) */
    if (!is_folder) {
        static const char *code_patterns[] = {
            "*.c", "*.h", "*.cpp", "*.hpp", "*.py", "*.js", "*.ts",
            "*.rs", "*.go", "*.java", "*.txt", "*.md",
            "*.json", "*.toml", "*.yaml", "*.yml", NULL
        };

        GtkFileFilter *f_code = g.gtk_file_filter_new();
        g.gtk_file_filter_set_name(f_code, "Archivos de código");
        for (int i = 0; code_patterns[i]; i++)
            g.gtk_file_filter_add_pattern(f_code, code_patterns[i]);
        g.gtk_file_chooser_add_filter(dialog, f_code);

        GtkFileFilter *f_all = g.gtk_file_filter_new();
        g.gtk_file_filter_set_name(f_all, "Todos los archivos");
        g.gtk_file_filter_add_pattern(f_all, "*");
        g.gtk_file_chooser_add_filter(dialog, f_all);
    }

    /* Mostrar y esperar respuesta */
    gint response = g.gtk_dialog_run(dialog);

    if (response == GTK_RESPONSE_ACCEPT) {
        gchar *path = g.gtk_file_chooser_get_filename(dialog);
        if (path) {
            /* Copiar antes de destruir el diálogo */
            static char path_copy[4096];
            strncpy(path_copy, path, sizeof(path_copy) - 1);
            path_copy[sizeof(path_copy) - 1] = '\0';
            g.g_free(path);

            g.gtk_widget_destroy(dialog);

            /* Vaciar eventos GTK pendientes */
            while (g.gtk_events_pending())
                g.gtk_main_iteration_do(0 /* FALSE: no bloqueante */);

            gtk_handles_unload(&g);

            const char *list[2] = {path_copy, NULL};
            if (is_folder) folder_dialog_cb(e, list, 0);
            else           file_dialog_cb(e, list, 0);
            return 0;
        }
    }

    /* Cancelado o sin selección */
    g.gtk_widget_destroy(dialog);
    while (g.gtk_events_pending())
        g.gtk_main_iteration_do(0);

    gtk_handles_unload(&g);

    if (is_folder) folder_dialog_cb(e, NULL, 0);
    else           file_dialog_cb(e, NULL, 0);
    return 0;
}

/**
 * @brief Lanza el diálogo GTK en un hilo secundario.
 */
static void linux_open_dialog(Editor *e, int is_folder) {
    LinuxDialogData *d = (LinuxDialogData *)SDL_malloc(sizeof(*d));
    if (!d) {
        if (is_folder) folder_dialog_cb(e, NULL, 0);
        else           file_dialog_cb(e, NULL, 0);
        return;
    }
    d->editor    = e;
    d->is_folder = is_folder;

    SDL_Thread *t = SDL_CreateThread(linux_dialog_thread, "cc_dialog", d);
    if (!t) {
        SDL_free(d);
        if (is_folder) folder_dialog_cb(e, NULL, 0);
        else           file_dialog_cb(e, NULL, 0);
        return;
    }
    SDL_DetachThread(t);
}

#endif /* !_WIN32 */

/* API pública de diálogos */

void open_file_dialog(Editor *e) {
#if defined(_WIN32)
    SDL_DialogFileFilter filters[] = {
        {"Archivos de código",
         "c;h;cpp;hpp;py;js;ts;rs;go;java;txt;md;json;toml;yaml;yml"},
        {"Todos los archivos", "*"}};
    SDL_ShowOpenFileDialog(file_dialog_cb, e, e->window, filters, 2, NULL,
                           false);
#else
    linux_open_dialog(e, 0);
#endif
}

void open_folder_dialog(Editor *e) {
#if defined(_WIN32)
    SDL_ShowOpenFolderDialog(folder_dialog_cb, e, e->window, NULL, false);
#else
    linux_open_dialog(e, 1);
#endif
}

/* Menú y sidebar */

void menu_exec(Editor *e, int item) {
    switch (item) {
    case MENU_NEW:         new_file(e);           break;
    case MENU_OPEN_FILE:   open_file_dialog(e);   break;
    case MENU_OPEN_FOLDER: open_folder_dialog(e); break;
    case MENU_SAVE:        save_file(e);           break;
    case MENU_AUTOSAVE:
        e->autosave = !e->autosave;
        if (e->autosave) e->autosave_last_ms = SDL_GetTicks();
        break;
    default: break;
    }
    e->menu_open    = 0;
    e->menu_hovered = -1;
    e->needs_redraw = 1;
}

void toggle_sidebar(Editor *e) {
    e->ftree.open = !e->ftree.open;
    e->needs_redraw = 1;
}
