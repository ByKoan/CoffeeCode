/**
 * @file input_commands.c
 * @brief Comandos de archivo y menú: nuevo, guardar, diálogos de abrir
 *        archivo/carpeta, ejecución del menú "Archivo" y toggle del panel.
 *
 * @note Diálogos de archivo asíncronos (Windows). A diferencia de funciones
 * bloqueantes que "esperan" a que el usuario elija, @c SDL_ShowOpenFileDialog
 * y @c SDL_ShowOpenFolderDialog RETORNAN al instante: solo PIDEN al sistema
 * operativo que muestre su diálogo nativo. Cuando el usuario por fin elige (o
 * cancela), SDL llama de vuelta a una función que le pasamos: el CALLBACK.
 * Como el callback se ejecuta más tarde y "fuera" de la llamada original, no
 * puede recibir el Editor por parámetro normal; en su lugar se pasa un puntero
 * opaco @c userdata al pedir el diálogo, y SDL nos lo devuelve tal cual en el
 * callback (donde lo casteamos de vuelta a @c Editor*). Este es el patrón
 * estándar callback + userdata. El callback recibe @c filelist: un array de
 * cadenas terminado en NULL (vacío/NULL si se canceló), del que aquí usamos
 * solo el primer elemento.
 *
 * @note Diálogos de archivo en Linux. @c SDL_ShowOpenFileDialog y
 * @c SDL_ShowOpenFolderDialog dependen de XDG Desktop Portal (D-Bus), que no
 * está disponible en todos los entornos Linux (Kali, TTY, WMs minimalistas…).
 * En Linux se usa en su lugar @c GtkFileChooserDialog, cargando @c libgtk-3
 * en tiempo de ejecución con @c dlopen() sin añadir dependencias al build.
 * El diálogo se abre desde un hilo secundario (@c SDL_CreateThread) para no
 * bloquear el bucle de eventos SDL del hilo principal. El resultado llega al
 * mismo callback (@c file_dialog_cb / @c folder_dialog_cb) que usa la ruta
 * Windows, manteniendo la lógica uniforme.
 */
/* El diálogo moderno IFileOpenDialog requiere que las cabeceras de Windows
 * expongan la API de Vista+: hay que fijar _WIN32_WINNT/NTDDI_VERSION ANTES de
 * cualquier include (windows.h llega indirectamente vía SDL). */
#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 /* Windows Vista */
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06000000 /* NTDDI_VISTA */
#endif
#endif

#include "input_internal.h"

#if !(defined(_WIN32))
#include <dlfcn.h> /* dlopen / dlsym / dlclose */
#endif

/**
 * @brief Índices de los items del menú "Archivo" (orden del menú que dibuja
 *        render_menu). Los usa ::menu_exec.
 */
enum {
    MENU_NEW = 0,     /**< Nuevo.            */
    MENU_OPEN_FILE,   /**< Abrir archivo...  */
    MENU_OPEN_FOLDER, /**< Abrir carpeta...  */
    MENU_SEP,         /**< Separador (sin acción). */
    MENU_SAVE,        /**< Guardar.          */
    MENU_AUTOSAVE,    /**< Autoguardado.     */
    MENU_PREFS,       /**< Preferencias.     */
};

/* El menú "Archivo" lo dibuja render_menu (render_ui.c), que registra el
 * rectángulo de cada item en e->ui (UI_LIST_MENU_ITEM); input lo consulta con
 * ui_hit_idx, así que ya no hay aquí cálculo de geometría del menú. */

/**
 * @brief Crea una pestaña nueva y vacía.
 *
 * Abre una pestaña en blanco y limpia la ruta/flag de modificación tanto en
 * el editor como en la pestaña activa, dejando el título de la ventana por
 * defecto.
 *
 * @param e Editor.
 */
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

/**
 * @brief Guarda el archivo actual (usa @c "untitled.c" si no tiene nombre).
 *
 * Escribe el buffer a disco. Si tiene éxito, baja el flag de modificación,
 * pone la ruta en el título y sincroniza ruta/flag/mtime de vuelta a la
 * pestaña activa (el @c mtime se relee con @c stat para detectar cambios
 * externos más adelante). Si la escritura falla, solo pide redibujar.
 *
 * @param e Editor.
 */
/**
 * @brief Escribe el buffer (UTF-8) en @p path codificado en @c e->encoding.
 *
 * El buffer siempre está en UTF-8; aquí se codifica a la codificación de la
 * pestaña (ANSI, UTF-16…) antes de escribir los bytes a disco.
 *
 * @return 1 si se escribió; 0 si falló (reserva, codificación o apertura).
 */
static int save_buffer_encoded(Editor *e, const char *path) {
    size_t n = buf_length(e->buf);
    char *utf8 = malloc(n ? n : 1);
    if (!utf8) return 0;
    if (n) buf_get_text(e->buf, 0, n, utf8); /* volcar el texto UTF-8 */

    unsigned char *bytes = NULL;
    size_t blen = 0;
    int enc_ok = encoding_encode(e->encoding, utf8, n, &bytes, &blen);
    free(utf8);
    if (!enc_ok) return 0;

    FILE *f = fopen(path, "wb"); /* binario: escribir los bytes tal cual */
    if (!f) {
        free(bytes);
        return 0;
    }
    if (blen) fwrite(bytes, 1, blen, f);
    fclose(f);
    free(bytes);
    return 1;
}

void save_file(Editor *e) {
    if (!e->filepath[0])
        strncpy(e->filepath, "untitled.c", sizeof(e->filepath) - 1);
    if (!save_buffer_encoded(e, e->filepath)) {
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

/**
 * @brief Callback de SDL al elegir un archivo en el diálogo de apertura.
 *
 * SDL lo invoca cuando el usuario termina con el diálogo nativo (de forma
 * asíncrona). Recupera el Editor desde @p userdata, y si el usuario eligió
 * algo (@p filelist no vacío), abre el primer archivo en una pestaña y
 * actualiza el título. Si canceló, solo pide redibujar.
 *
 * En Linux este mismo callback es invocado directamente desde el hilo GTK
 * (@c linux_dialog_thread) con la ruta elegida.
 *
 * @param userdata Puntero opaco que pasamos al pedir el diálogo: el Editor.
 * @param filelist Array de rutas elegidas terminado en @c NULL
 *                 (@c NULL/vacío = cancelado).
 * @param filter   Índice del filtro elegido (no usado).
 */
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

/**
 * @brief Callback de SDL al elegir una carpeta en el diálogo.
 *
 * Análogo a ::file_dialog_cb pero para carpetas: carga el árbol del
 * explorador con la carpeta elegida. En Linux es invocado directamente
 * desde el hilo GTK (@c linux_dialog_thread).
 *
 * @param userdata Puntero opaco con el Editor.
 * @param filelist Array de rutas terminado en @c NULL
 *                 (@c NULL/vacío = cancelado).
 * @param filter   Índice del filtro (no usado).
 */
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

/* ── Implementación Linux: GTK3 nativo cargado con dlopen ──────────────── */
#if !(defined(_WIN32))

/**
 * @brief Tipos opacos mínimos de GTK necesarios para el diálogo de archivo.
 *
 * No se incluye @c <gtk/gtk.h> para evitar la dependencia de compilación;
 * GTK se carga en tiempo de ejecución con @c dlopen(). Solo se necesitan
 * los tipos que aparecen en las firmas de las funciones que usamos.
 */
typedef void GtkWidget;     /**< Widget GTK genérico (opaco). */
typedef void GtkFileFilter; /**< Filtro de tipos de archivo GTK (opaco). */
typedef int gint;           /**< Entero de GLib (equivale a @c int). */
typedef char gchar;         /**< Carácter de GLib (equivale a @c char). */
typedef int gboolean;       /**< Booleano de GLib (0 = FALSE, !=0 = TRUE). */
typedef void *gpointer;     /**< Puntero genérico de GLib. */

/** @brief Acción del @c GtkFileChooser: abrir archivo. */
#define GTK_FILE_CHOOSER_ACTION_OPEN 0
/** @brief Acción del @c GtkFileChooser: seleccionar carpeta. */
#define GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER 2
/** @brief Respuesta de @c gtk_dialog_run cuando el usuario acepta. */
#define GTK_RESPONSE_ACCEPT -3
/** @brief Botón "Cancelar" de GTK (stock). */
#define GTK_STOCK_CANCEL "gtk-cancel"
/** @brief Botón "Abrir" de GTK (stock). */
#define GTK_STOCK_OPEN "gtk-open"

/**
 * @brief Punteros a todas las funciones GTK que se cargan con @c dlsym.
 *
 * Agrupa los handles de las bibliotecas (@c lib_gtk, @c lib_glib) y los
 * punteros de función. Se rellena por ::gtk_handles_load y se libera por
 * ::gtk_handles_unload.
 */
typedef struct {
    void *lib_gtk;  /**< Handle de @c libgtk-3 abierto con @c dlopen(). */
    void *lib_glib; /**< Handle de @c libglib-2.0 abierto con @c dlopen(). */

    /** @brief Inicializa GTK; devuelve @c FALSE si no hay display. */
    gboolean (*gtk_init_check)(int *, char ***);
    /** @brief Crea un @c GtkFileChooserDialog con botones (varargs). */
    GtkWidget *(*gtk_file_chooser_dialog_new)(const gchar *, GtkWidget *, gint,
                                              const gchar *, gint,
                                              const gchar *, gint, gpointer);
    /** @brief Ejecuta el diálogo de forma bloqueante; devuelve el ID de
     *         respuesta. */
    gint (*gtk_dialog_run)(GtkWidget *);
    /** @brief Obtiene la ruta elegida en el diálogo (heap; liberar con
     *         @c g_free). */
    gchar *(*gtk_file_chooser_get_filename)(GtkWidget *);
    /** @brief Activa o desactiva la selección múltiple en el diálogo. */
    void (*gtk_file_chooser_set_select_multiple)(GtkWidget *, gboolean);
    /** @brief Destruye el widget y libera sus recursos. */
    void (*gtk_widget_destroy)(GtkWidget *);
    /** @brief Procesa una iteración del bucle de eventos GTK. */
    void (*gtk_main_iteration_do)(gboolean);
    /** @brief Devuelve @c TRUE si hay eventos GTK pendientes. */
    gboolean (*gtk_events_pending)(void);
    /** @brief Crea un nuevo @c GtkFileFilter vacío. */
    GtkFileFilter *(*gtk_file_filter_new)(void);
    /** @brief Asigna el nombre visible del filtro (p. ej. "Archivos C"). */
    void (*gtk_file_filter_set_name)(GtkFileFilter *, const gchar *);
    /** @brief Añade un patrón glob al filtro (p. ej. @c "*.c"). */
    void (*gtk_file_filter_add_pattern)(GtkFileFilter *, const gchar *);
    /** @brief Añade el filtro al @c GtkFileChooser. */
    void (*gtk_file_chooser_add_filter)(GtkWidget *, GtkFileFilter *);
    /** @brief Libera memoria reservada por GLib. */
    void (*g_free)(gpointer);
} GtkHandles;

/**
 * @brief Carga @c libgtk-3 y @c libglib-2.0 y resuelve todos los símbolos
 *        necesarios para mostrar el diálogo de archivo.
 *
 * Prueba los nombres de biblioteca más comunes en distintas distribuciones
 * Linux. Si algún símbolo falta imprime el error en @c stderr y cierra los
 * handles abiertos antes de retornar.
 *
 * @param g Estructura @c GtkHandles a rellenar.
 * @return 1 si todos los símbolos se cargaron correctamente, 0 si algo
 *         falló (GTK no instalado o símbolo no encontrado).
 */
static int gtk_handles_load(GtkHandles *g) {
    static const char *gtk_names[] = {"libgtk-3.so.0", "libgtk-3.so", NULL};
    static const char *glib_names[] = {"libglib-2.0.so.0", "libglib-2.0.so",
                                       NULL};

    g->lib_gtk = NULL;
    for (int i = 0; gtk_names[i]; i++) {
        g->lib_gtk = dlopen(gtk_names[i], RTLD_LAZY | RTLD_LOCAL);
        if (g->lib_gtk) break;
    }
    if (!g->lib_gtk) {
        fprintf(stderr,
                "CoffeeCode: no se pudo cargar libgtk-3: %s\n"
                "  Instala GTK3: sudo apt install libgtk-3-0\n",
                dlerror());
        return 0;
    }

    g->lib_glib = NULL;
    for (int i = 0; glib_names[i]; i++) {
        g->lib_glib = dlopen(glib_names[i], RTLD_LAZY | RTLD_LOCAL);
        if (g->lib_glib) break;
    }

/* Macro interna: resuelve el símbolo @p sym desde @p handle y lo guarda en
 * @c g->sym. Si @c dlsym falla, imprime el error y libera los handles. */
#define LOAD(handle, sym)                                                      \
    g->sym = dlsym(handle, #sym);                                              \
    if (!g->sym) {                                                             \
        fprintf(stderr, "CoffeeCode: dlsym(%s): %s\n", #sym, dlerror());       \
        dlclose(g->lib_gtk);                                                   \
        if (g->lib_glib) dlclose(g->lib_glib);                                 \
        return 0;                                                              \
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

    /* g_free puede vivir en glib o quedar re-exportada desde gtk */
    g->g_free = g->lib_glib ? dlsym(g->lib_glib, "g_free") : NULL;
    if (!g->g_free) g->g_free = dlsym(g->lib_gtk, "g_free");
    if (!g->g_free) {
        fprintf(stderr, "CoffeeCode: dlsym(g_free): %s\n", dlerror());
        dlclose(g->lib_gtk);
        if (g->lib_glib) dlclose(g->lib_glib);
        return 0;
    }

    return 1;
}

/**
 * @brief Cierra los handles de @c libgtk-3 y @c libglib-2.0.
 *
 * @param g Estructura @c GtkHandles cuyos handles se van a cerrar.
 */
static void gtk_handles_unload(GtkHandles *g) {
    if (g->lib_glib) dlclose(g->lib_glib);
    if (g->lib_gtk) dlclose(g->lib_gtk);
}

/**
 * @brief Datos que se pasan al hilo del diálogo GTK.
 *
 * El hilo necesita saber a qué @c Editor entregar el resultado y si debe
 * mostrar un selector de archivo o de carpeta.
 */
typedef struct {
    Editor *editor; /**< Editor al que entregar la ruta elegida. */
    int is_folder;  /**< 0 = diálogo de archivo, 1 = diálogo de carpeta. */
} LinuxDialogData;

/**
 * @brief Hilo que muestra el @c GtkFileChooserDialog y entrega el resultado.
 *
 * Se lanza con @c SDL_CreateThread para no bloquear el bucle de eventos SDL
 * del hilo principal. Flujo:
 *   1. Carga @c libgtk-3 dinámicamente con ::gtk_handles_load.
 *   2. Inicializa GTK (@c gtk_init_check).
 *   3. Crea el @c GtkFileChooserDialog con la acción y los filtros adecuados.
 *   4. Llama a @c gtk_dialog_run (bloqueante dentro del hilo).
 *   5. Entrega la ruta al callback correspondiente
 *      (::file_dialog_cb o ::folder_dialog_cb) y descarga GTK.
 *
 * @param data Puntero a @c LinuxDialogData asignado con @c SDL_malloc;
 *             este hilo es el dueño y lo libera con @c SDL_free.
 * @return 0 siempre (valor de retorno del hilo no usado).
 */
static int SDLCALL linux_dialog_thread(void *data) {
    LinuxDialogData *d = (LinuxDialogData *)data;
    Editor *e = d->editor;
    int is_folder = d->is_folder;
    SDL_free(d);

    GtkHandles g;
    if (!gtk_handles_load(&g)) {
        if (is_folder)
            folder_dialog_cb(e, NULL, 0);
        else
            file_dialog_cb(e, NULL, 0);
        return 0;
    }

    /* Inicializar GTK; puede fallar si no hay DISPLAY/Wayland, pero en ese
     * caso gtk_dialog_run también fallará de forma controlada. */
    int argc = 0;
    char *argv_buf[] = {NULL};
    char **argv_ptr = argv_buf;
    g.gtk_init_check(&argc, &argv_ptr);

    gint action = is_folder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
                            : GTK_FILE_CHOOSER_ACTION_OPEN;

    GtkWidget *dialog = g.gtk_file_chooser_dialog_new(
        is_folder ? "Abrir carpeta" : "Abrir archivo",
        NULL, /* ventana padre: NULL porque estamos en un hilo separado */
        action, GTK_STOCK_CANCEL,
        GTK_RESPONSE_ACCEPT - 3, /* GTK_RESPONSE_CANCEL */
        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
        NULL /* centinela que termina la lista de botones */
    );

    if (!dialog) {
        fprintf(stderr,
                "CoffeeCode: gtk_file_chooser_dialog_new devolvió NULL\n");
        gtk_handles_unload(&g);
        if (is_folder)
            folder_dialog_cb(e, NULL, 0);
        else
            file_dialog_cb(e, NULL, 0);
        return 0;
    }

    g.gtk_file_chooser_set_select_multiple(dialog, 0 /* FALSE */);

    /* Añadir filtros de tipo de archivo solo al diálogo de archivo */
    if (!is_folder) {
        static const char *patterns[] = {
            "*.c",    "*.h",    "*.cpp",  "*.hpp",  "*.py",  "*.js",
            "*.ts",   "*.rs",   "*.go",   "*.java", "*.txt", "*.md",
            "*.json", "*.toml", "*.yaml", "*.yml",  NULL};
        GtkFileFilter *f_code = g.gtk_file_filter_new();
        g.gtk_file_filter_set_name(f_code, "Archivos de código");
        for (int i = 0; patterns[i]; i++)
            g.gtk_file_filter_add_pattern(f_code, patterns[i]);
        g.gtk_file_chooser_add_filter(dialog, f_code);

        GtkFileFilter *f_all = g.gtk_file_filter_new();
        g.gtk_file_filter_set_name(f_all, "Todos los archivos");
        g.gtk_file_filter_add_pattern(f_all, "*");
        g.gtk_file_chooser_add_filter(dialog, f_all);
    }

    gint response = g.gtk_dialog_run(dialog);

    if (response == GTK_RESPONSE_ACCEPT) {
        gchar *path = g.gtk_file_chooser_get_filename(dialog);
        if (path) {
            static char path_copy[4096]; /* estático: vive hasta que el
                                            callback termina de usarlo */
            strncpy(path_copy, path, sizeof(path_copy) - 1);
            path_copy[sizeof(path_copy) - 1] = '\0';
            g.g_free(path);
            g.gtk_widget_destroy(dialog);
            while (g.gtk_events_pending())
                g.gtk_main_iteration_do(0 /* FALSE: no bloqueante */);
            gtk_handles_unload(&g);
            const char *list[2] = {path_copy, NULL};
            if (is_folder)
                folder_dialog_cb(e, list, 0);
            else
                file_dialog_cb(e, list, 0);
            return 0;
        }
    }

    /* Usuario canceló o no eligió nada */
    g.gtk_widget_destroy(dialog);
    while (g.gtk_events_pending())
        g.gtk_main_iteration_do(0);
    gtk_handles_unload(&g);

    if (is_folder)
        folder_dialog_cb(e, NULL, 0);
    else
        file_dialog_cb(e, NULL, 0);
    return 0;
}

/**
 * @brief Lanza ::linux_dialog_thread en un hilo secundario y retorna.
 *
 * El hilo se desvincula con @c SDL_DetachThread: se limpia solo al terminar
 * y no es necesario hacer @c SDL_WaitThread. Si la creación del hilo o la
 * reserva de memoria fallan, se llama al callback directamente con @c NULL
 * (equivalente a cancelar) para que el editor quede en un estado consistente.
 *
 * @param e         Editor.
 * @param is_folder 0 = diálogo de archivo, 1 = diálogo de carpeta.
 */
static void linux_open_dialog(Editor *e, int is_folder) {
    LinuxDialogData *d = (LinuxDialogData *)SDL_malloc(sizeof(*d));
    if (!d) {
        if (is_folder)
            folder_dialog_cb(e, NULL, 0);
        else
            file_dialog_cb(e, NULL, 0);
        return;
    }
    d->editor = e;
    d->is_folder = is_folder;

    SDL_Thread *t = SDL_CreateThread(linux_dialog_thread, "cc_dialog", d);
    if (!t) {
        SDL_free(d);
        if (is_folder)
            folder_dialog_cb(e, NULL, 0);
        else
            file_dialog_cb(e, NULL, 0);
        return;
    }
    SDL_DetachThread(t); /* el hilo se limpia solo al terminar */
}

#endif /* !_WIN32 */

/* ── Implementación Windows: IFileOpenDialog moderno (Common Item Dialog) ──
 * SDL 3.2.6 usa en Windows los diálogos LEGACY: GetOpenFileNameW para archivo y
 * SHBrowseForFolderW para carpeta (la "pantalla de árbol" poco usable). Aquí se
 * usa en su lugar el diálogo MODERNO estilo Explorador de Windows
 * (@c IFileOpenDialog), lanzado en un hilo aparte para no bloquear el bucle
 * principal — igual que la ruta de Linux. El resultado se entrega a los mismos
 * callbacks ::file_dialog_cb / ::folder_dialog_cb. */
#if defined(_WIN32)
#define COBJMACROS
#include <objbase.h>
#include <shobjidl.h>
#include <windows.h>

typedef struct {
    Editor *editor;
    int is_folder; /* 0 = archivo, 1 = carpeta */
} WinDialogData;

/** HWND nativo de la ventana SDL (para que el diálogo sea modal a ella). */
static HWND win_native_hwnd(Editor *e) {
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(e->window),
                                        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                        NULL);
}

/** Hilo que abre el diálogo moderno y entrega la ruta elegida al callback. */
static int win_dialog_thread(void *ud) {
    WinDialogData *d = (WinDialogData *)ud;
    Editor *e = d->editor;
    int is_folder = d->is_folder;
    SDL_free(d);

    char utf8[4096] = {0};
    int got = 0;

    HRESULT hr =
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int co_ok = SUCCEEDED(hr);

    IFileOpenDialog *dlg = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog, NULL,
                                   CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog,
                                   (void **)&dlg))) {
        /* Opciones: solo elementos del sistema de archivos; FOS_PICKFOLDERS
         * convierte el diálogo en selector de CARPETAS (estilo Explorador). */
        DWORD opts = 0;
        IFileOpenDialog_GetOptions(dlg, &opts);
        opts |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
        if (is_folder) opts |= FOS_PICKFOLDERS;
        IFileOpenDialog_SetOptions(dlg, opts);

        if (!is_folder) {
            COMDLG_FILTERSPEC filt[] = {
                {L"Archivos de código",
                 L"*.c;*.h;*.cpp;*.hpp;*.py;*.js;*.ts;*.rs;*.go;*.java;*.txt;"
                 L"*.md;*.json;*.toml;*.yaml;*.yml"},
                {L"Todos los archivos", L"*.*"}};
            IFileOpenDialog_SetFileTypes(dlg, 2, filt);
        }

        if (SUCCEEDED(IFileOpenDialog_Show(dlg, win_native_hwnd(e)))) {
            IShellItem *item = NULL;
            if (SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item))) {
                PWSTR wpath = NULL;
                if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH,
                                                        &wpath))) {
                    /* ruta wchar -> UTF-8 (lo que usa el resto del editor) */
                    if (WideCharToMultiByte(CP_UTF8, 0, wpath, -1, utf8,
                                            (int)sizeof utf8, NULL, NULL) > 0)
                        got = 1;
                    CoTaskMemFree(wpath);
                }
                IShellItem_Release(item);
            }
        }
        IFileOpenDialog_Release(dlg);
    }
    if (co_ok) CoUninitialize();

    const char *list[2] = {utf8, NULL};
    const char *const *res = got ? list : NULL; /* NULL = cancelado */
    if (is_folder)
        folder_dialog_cb(e, res, 0);
    else
        file_dialog_cb(e, res, 0);
    return 0;
}

/** Lanza el diálogo moderno en un hilo desvinculado (no bloquea el editor). */
static void win_open_dialog(Editor *e, int is_folder) {
    WinDialogData *d = (WinDialogData *)SDL_malloc(sizeof(*d));
    if (!d) {
        if (is_folder)
            folder_dialog_cb(e, NULL, 0);
        else
            file_dialog_cb(e, NULL, 0);
        return;
    }
    d->editor = e;
    d->is_folder = is_folder;
    SDL_Thread *t = SDL_CreateThread(win_dialog_thread, "cc_dialog", d);
    if (!t) {
        SDL_free(d);
        if (is_folder)
            folder_dialog_cb(e, NULL, 0);
        else
            file_dialog_cb(e, NULL, 0);
        return;
    }
    SDL_DetachThread(t); /* se limpia solo al terminar */
}
#endif /* _WIN32 */

/**
 * @brief Lanza (asíncronamente) el diálogo nativo para abrir un archivo.
 *
 * En Windows usa el diálogo moderno @c IFileOpenDialog (::win_open_dialog).
 * En Linux lanza ::linux_open_dialog, que abre un @c GtkFileChooserDialog
 * desde un hilo secundario y entrega el resultado a ::file_dialog_cb.
 *
 * @param e Editor.
 */
void open_file_dialog(Editor *e) {
#if defined(_WIN32)
    win_open_dialog(e, 0);
#else
    linux_open_dialog(e, 0);
#endif
}

/**
 * @brief Lanza (asíncronamente) el diálogo nativo para abrir una carpeta.
 *
 * En Windows usa el diálogo moderno @c IFileOpenDialog con @c FOS_PICKFOLDERS
 * (::win_open_dialog), en lugar del @c SHBrowseForFolder legacy de SDL.
 * En Linux lanza ::linux_open_dialog (GtkFileChooserDialog).
 *
 * @param e Editor.
 */
void open_folder_dialog(Editor *e) {
#if defined(_WIN32)
    win_open_dialog(e, 1);
#else
    linux_open_dialog(e, 1);
#endif
}

/**
 * @brief Ejecuta el item @p item del menú "Archivo" y lo cierra.
 *
 * Despacha según el índice del item. "Autoguardado" es un toggle: al
 * activarlo se apunta el instante actual para que el bucle principal respete
 * el intervalo. Tras cualquier acción, cierra el menú.
 *
 * @param e    Editor.
 * @param item Índice del item (ver enum @c MENU_*).
 */
void menu_exec(Editor *e, int item) {
    switch (item) {
    case MENU_NEW: new_file(e); break;
    case MENU_OPEN_FILE: open_file_dialog(e); break;
    case MENU_OPEN_FOLDER: open_folder_dialog(e); break;
    case MENU_SAVE: save_file(e); break;
    case MENU_AUTOSAVE:
        e->autosave = !e->autosave;
        if (e->autosave) e->autosave_last_ms = SDL_GetTicks();
        e->settings.autosave = e->autosave; /* persistir el cambio */
        settings_save(&e->settings);
        break;
    case MENU_PREFS:
        e->settings_open = 1; /* abrir la pantalla de preferencias */
        break;
    default: break;
    }
    e->menu_open = 0;
    e->menu_hovered = -1;
    e->needs_redraw = 1;
}

/**
 * @brief Muestra u oculta el panel lateral del explorador de archivos.
 *
 * @param e Editor.
 */
void toggle_sidebar(Editor *e) {
    e->ftree.open = !e->ftree.open;
    e->needs_redraw = 1;
}
