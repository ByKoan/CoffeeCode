/**
 * @file input_commands.c
 * @brief Comandos de archivo y menú: nuevo, guardar, diálogos de abrir
 *        archivo/carpeta, ejecución del menú "Archivo" y toggle del panel.
 *
 * @note Diálogos de archivo ASÍNCRONOS. A diferencia de funciones bloqueantes
 * que "esperan" a que el usuario elija, @c SDL_ShowOpenFileDialog y
 * @c SDL_ShowOpenFolderDialog RETORNAN al instante: solo PIDEN al sistema
 * operativo que muestre su diálogo nativo. Cuando el usuario por fin elige (o
 * cancela), SDL llama de vuelta a una función que le pasamos: el CALLBACK. Como
 * el callback se ejecuta más tarde y "fuera" de la llamada original, no puede
 * recibir el Editor por parámetro normal; en su lugar se pasa un puntero opaco
 * @c userdata al pedir el diálogo, y SDL nos lo devuelve tal cual en el
 * callback (donde lo casteamos de vuelta a @c Editor*). Este es el patrón
 * estándar callback + userdata. El callback recibe @c filelist: un array de
 * cadenas terminado en NULL (vacío/NULL si se canceló), del que aquí usamos
 * solo el primer elemento.
 */
#include "input_internal.h"

#define MENU_SEP_H 8 /* alto en px de un separador del menú */

/* Índices de los items del menú "Archivo" (orden de MENU_LABELS). */
enum {
    MENU_NEW = 0,     /* Nuevo            */
    MENU_OPEN_FILE,   /* Abrir archivo... */
    MENU_OPEN_FOLDER, /* Abrir carpeta... */
    MENU_SEP,         /* separador (sin acción) */
    MENU_SAVE,        /* Guardar          */
    MENU_AUTOSAVE,    /* Autoguardado     */
};

/* Etiquetas del menú; NULL marca el separador (no es un item pulsable). */
static const char *MENU_LABELS[MENU_ITEMS] = {
    "Nuevo", "Abrir archivo...", "Abrir carpeta...",
    NULL,    "Guardar",          "Autoguardado"};

/** Alto en px del item @p i (item normal o, si su etiqueta es NULL, separador).
 */
static int menu_item_height(int i) {
    return MENU_LABELS[i] ? MENU_ITEM_H : MENU_SEP_H;
}

/**
 * @brief Índice del item del menú bajo (@p mx, @p my), o -1 si ninguno.
 *
 * Comprueba primero que la X esté dentro del ancho del menú; luego recorre los
 * items de arriba abajo acumulando su alto hasta dar con la banda vertical que
 * contiene @p my. Los separadores (etiqueta NULL) nunca se devuelven como
 * acierto.
 *
 * @param mx Coordenada X del ratón en píxeles.
 * @param my Coordenada Y del ratón en píxeles.
 * @return Índice del item bajo el cursor, o -1 si no hay ninguno.
 */
int menu_item_at(int mx, int my) {
    if (mx < BTN_FILE_X || mx >= BTN_FILE_X + MENU_WIDTH)
        return -1;          /* fuera en X */
    int iy = NAVBAR_HEIGHT; /* el menú empieza justo bajo la navbar */
    for (int i = 0; i < MENU_ITEMS; i++) {
        int h = menu_item_height(i);
        if (MENU_LABELS[i] && my >= iy && my < iy + h)
            return i; /* dentro de este item */
        iy += h;      /* avanzar al siguiente */
    }
    return -1;
}

/** Altura total del menú desplegable (suma de los altos de todos los items). */
int menu_total_h(void) {
    int h = 0;
    for (int i = 0; i < MENU_ITEMS; i++)
        h += menu_item_height(i);
    return h;
}

/**
 * @brief Crea una pestaña nueva y vacía.
 *
 * Abre una pestaña en blanco y limpia la ruta/flag de modificación tanto en el
 * editor como en la pestaña activa, dejando el título de la ventana por
 * defecto.
 *
 * @param e Editor.
 */
void new_file(Editor *e) {
    editor_tab_new(e);
    e->filepath[0] = '\0'; /* sin nombre de archivo */
    e->modified = 0;
    if (e->tab_count > 0) { /* reflejar lo mismo en la pestaña activa */
        e->tabs[e->active_tab].filepath[0] = '\0';
        e->tabs[e->active_tab].modified = 0;
    }
    SDL_SetWindowTitle(e->window, "CoffeeCode"); /* título por defecto */
    e->needs_redraw = 1;
}

/**
 * @brief Guarda el archivo actual (usa "untitled.c" si no tiene nombre).
 *
 * Escribe el buffer a disco. Si tiene éxito, baja el flag de modificación, pone
 * la ruta en el título y sincroniza ruta/flag/mtime de vuelta a la pestaña
 * activa (el
 * @c mtime se relee con @c stat para detectar cambios externos más adelante).
 * Si la escritura falla, solo pide redibujar.
 *
 * @param e Editor.
 */
void save_file(Editor *e) {
    /* sin nombre todavía: usar uno por defecto para poder guardar */
    if (!e->filepath[0])
        strncpy(e->filepath, "untitled.c", sizeof(e->filepath) - 1);
    if (!buf_save_file(e->buf, e->filepath)) { /* falló la escritura: abortar */
        e->needs_redraw = 1;
        return;
    }

    e->modified = 0; /* guardado: ya no hay cambios pendientes */
    SDL_SetWindowTitle(e->window, e->filepath);

    /* sincronizar filepath, modified y mtime de vuelta al tab activo */
    if (e->tab_count > 0) {
        EditorTab *tab = &e->tabs[e->active_tab];
        strncpy(tab->filepath, e->filepath, sizeof(tab->filepath) - 1);
        tab->modified = 0;
        struct stat st;
        /* guardar la fecha de modificación del archivo recién escrito */
        tab->loaded_mtime =
            (stat(e->filepath, &st) == 0) ? (long)st.st_mtime : 0;
    }
    e->needs_redraw = 1;
}

/**
 * @brief Callback de SDL al elegir un archivo en el diálogo de apertura.
 *
 * SDL lo invoca cuando el usuario termina con el diálogo nativo (de forma
 * asíncrona). Recupera el Editor desde @p userdata, y si el usuario eligió algo
 * (filelist no vacío), abre el primer archivo en una pestaña y actualiza el
 * título. Si canceló, solo pide redibujar.
 *
 * @param userdata Puntero opaco que pasamos al pedir el diálogo: aquí, el
 * Editor.
 * @param filelist Array de rutas elegidas terminado en NULL (NULL/vacío =
 * cancelado).
 * @param filter   Índice del filtro elegido (no usado).
 */
void SDLCALL file_dialog_cb(void *userdata, const char *const *filelist,
                            int filter) {
    (void)filter; /* no nos importa qué filtro eligió el usuario */
    Editor *e = (Editor *)userdata;  /* recuperar el Editor del puntero opaco */
    if (!filelist || !filelist[0]) { /* canceló o no eligió nada */
        e->needs_redraw = 1;
        return;
    }
    const char *path = filelist[0]; /* nos quedamos con el primer archivo */
    strncpy(e->filepath, path, sizeof(e->filepath) - 1);
    e->filepath[sizeof(e->filepath) - 1] = '\0'; /* asegurar terminación */
    editor_tab_open(e, path);
    SDL_SetWindowTitle(e->window, path);
}

/**
 * @brief Lanza (asíncronamente) el diálogo nativo para abrir un archivo.
 *
 * Configura los filtros de tipo de archivo y pide a SDL que muestre el diálogo.
 * La llamada retorna de inmediato; el archivo elegido llegará por
 * ::file_dialog_cb. Se pasa @c e como userdata para que el callback recupere el
 * Editor.
 *
 * @param e Editor.
 */
void open_file_dialog(Editor *e) {
    SDL_DialogFileFilter filters[] = {
        {"Archivos de código",
         "c;h;cpp;hpp;py;js;ts;rs;go;java;txt;md;json;toml;yaml;yml"},
        {"Todos los archivos", "*"}};
    /* (callback, userdata=e, ventana, filtros, nº filtros, ruta inicial=NULL,
     * multi=false) */
    SDL_ShowOpenFileDialog(file_dialog_cb, e, e->window, filters, 2, NULL,
                           false);
}

/**
 * @brief Callback de SDL al elegir una carpeta en el diálogo.
 *
 * Análogo a ::file_dialog_cb pero para carpetas: carga el árbol del explorador
 * con la carpeta elegida. Asíncrono; @p userdata es el Editor.
 *
 * @param userdata Puntero opaco con el Editor.
 * @param filelist Array de rutas terminado en NULL (NULL/vacío = cancelado).
 * @param filter   Índice del filtro (no usado).
 */
void SDLCALL folder_dialog_cb(void *userdata, const char *const *filelist,
                              int filter) {
    (void)filter;
    Editor *e = (Editor *)userdata;
    if (!filelist || !filelist[0]) { /* canceló */
        e->needs_redraw = 1;
        return;
    }
    ftree_load(&e->ftree,
               filelist[0]); /* poblar el explorador con esa carpeta */
    e->needs_redraw = 1;
}

/**
 * @brief Lanza (asíncronamente) el diálogo nativo para abrir una carpeta.
 *
 * Retorna de inmediato; la carpeta elegida llegará por ::folder_dialog_cb.
 *
 * @param e Editor.
 */
void open_folder_dialog(Editor *e) {
    /* (callback, userdata=e, ventana, ruta inicial=NULL, multi=false) */
    SDL_ShowOpenFolderDialog(folder_dialog_cb, e, e->window, NULL, false);
}

/**
 * @brief Ejecuta el item @p item del menú "Archivo" y lo cierra.
 *
 * Despacha según el índice del item. "Autoguardado" es un toggle: al activarlo
 * se apunta el instante actual para que el bucle principal respete el
 * intervalo. Tras cualquier acción, cierra el menú.
 *
 * @param e    Editor.
 * @param item Índice del item (ver enum MENU_*).
 */
void menu_exec(Editor *e, int item) {
    switch (item) {
    case MENU_NEW: new_file(e); break;
    case MENU_OPEN_FILE: open_file_dialog(e); break;
    case MENU_OPEN_FOLDER: open_folder_dialog(e); break;
    case MENU_SAVE: save_file(e); break;
    case MENU_AUTOSAVE:
        e->autosave = !e->autosave; /* alternar autoguardado */
        /* al activarlo, marcar "ahora" como última vez (para el intervalo) */
        if (e->autosave) e->autosave_last_ms = SDL_GetTicks();
        break;
    default: break; /* separador u otro: nada */
    }
    e->menu_open = 0; /* cerrar el menú tras ejecutar */
    e->menu_hovered = -1;
    e->needs_redraw = 1;
}

/** Muestra/oculta el panel lateral del explorador. */
void toggle_sidebar(Editor *e) {
    e->ftree.open = !e->ftree.open;
    e->needs_redraw = 1;
}
