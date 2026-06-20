/**
 * @file ext_host.c
 * @brief Implementacion del extension host de CoffeeCode.
 *
 * Contiene:
 *   - El @c CoffeeHost opaco con sus tablas (comandos, suscripciones a eventos,
 *     servicios, extensiones cargadas) y el respaldo del @c CoffeeApi.
 *   - El cargador/descargador de DLLs (LoadLibrary/dlopen) con registro
 *     POR-EXTENSION de todo lo registrado, para revertirlo en el unload.
 *   - Un lector minimal del manifiesto @c coffee-extension.toml (solo las
 *     claves que el host usa: id, entry, abi, dependencies).
 *   - La carga de un directorio completo con orden topologico por dependencias.
 *
 * El host trabaja sobre un @c Buffer* (no el @c Editor), de modo que las
 * operaciones de buffer son SDL-free y testeables headless.  Las acciones de
 * UI se delegan a callbacks opcionales del backend (NULL = stub que loguea).
 */
#include "ext/ext_host.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- Carga dinamica por plataforma ----------------------------------------- */
#if defined(_WIN32)
#include <windows.h>
typedef HMODULE coffee_dll_t; /**< handle de modulo nativo */
#define COFFEE_DLL_EXT ".dll"
/* Por si el SDK de MinGW no define los flags de busqueda dirigida. */
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#endif
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
static coffee_dll_t coffee_dll_open(const char *path) {
    /* LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR hace que el cargador busque las
     * dependencias de la DLL en el directorio de la PROPIA DLL, no solo en el
     * del ejecutable / system32 / PATH.  Es imprescindible para extensiones que
     * traen sus dependencias al lado (p.ej. coffee_vesta.dll junto a
     * libvesta.dll + OpenSSL): sin esto, cargar la extension falla con
     * ERROR_MOD_NOT_FOUND (126) aunque sus DLLs esten en la misma carpeta,
     * porque el cargador no busca ahi por defecto.  Requiere ruta absoluta. */
    coffee_dll_t h = LoadLibraryExA(path, NULL,
                                    LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!h) /* fallback para Windows antiguos sin esos flags */
        h = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    return h;
}
static void *coffee_dll_sym(coffee_dll_t h, const char *name) {
    return (void *)GetProcAddress(h, name);
}
static void coffee_dll_close(coffee_dll_t h) { FreeLibrary(h); }
#define COFFEE_PATH_SEP '\\'
#else
#include <dirent.h>
#include <dlfcn.h>
typedef void *coffee_dll_t;
#define COFFEE_DLL_EXT ".so"
static coffee_dll_t coffee_dll_open(const char *path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}
static void *coffee_dll_sym(coffee_dll_t h, const char *name) {
    return dlsym(h, name);
}
static void coffee_dll_close(coffee_dll_t h) { dlclose(h); }
#define COFFEE_PATH_SEP '/'
#endif

/**
 * @brief Copia en @p out el texto legible del ultimo error de carga de DLL del
 *        sistema operativo.
 *
 * En Windows usa @c GetLastError + @c FormatMessageA (asi se obtiene, por
 * ejemplo, "No se encontro el modulo especificado" cuando faltan dependencias
 * de la propia DLL).  En POSIX usa @c dlerror.  Si no hay texto disponible,
 * deja una cadena generica.  @p out queda siempre null-terminada.
 */
static void coffee_dll_last_error(char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
#if defined(_WIN32)
    DWORD err = GetLastError();
    char buf[256];
    DWORD n = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, (DWORD)sizeof(buf),
        NULL);
    /* recortar el salto de linea final que FormatMessage suele anyadir */
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' ||
                     buf[n - 1] == ' ' || buf[n - 1] == '.'))
        buf[--n] = '\0';
    if (n > 0)
        snprintf(out, cap, "%s (codigo %lu)", buf, (unsigned long)err);
    else
        snprintf(out, cap, "error del sistema %lu", (unsigned long)err);
#else
    const char *de = dlerror();
    snprintf(out, cap, "%s", de ? de : "error desconocido del cargador");
#endif
}

/* ===========================================================================
 *  Estructuras internas del host
 * =========================================================================== */

/** @brief Un comando registrado por una extension. */
typedef struct {
    char *id;             /**< id unico ("editor.format") */
    char *title;          /**< titulo legible */
    CoffeeCommandFn fn;   /**< callback */
    void *userdata;       /**< userdata del callback */
    int owner;            /**< indice de la extension dueña (registro por-ext) */
} HostCommand;

/** @brief Una suscripcion a un evento. */
typedef struct {
    CoffeeEventType ev;
    CoffeeEventFn fn;
    void *userdata;
    int owner; /**< extension dueña */
} HostSub;

/** @brief Un servicio publicado (struct de funciones bajo un nombre). */
typedef struct {
    char *name;
    void *iface;
    int owner; /**< extension dueña */
} HostService;

/** @brief Un par clave/valor de configuracion (almacen simple por host). */
typedef struct {
    char *key;
    char *value;
} HostConfig;

/** @brief Estado de una extension cargada. */
typedef struct {
    char *id;          /**< id del manifiesto */
    char *dir;         /**< directorio de la extension (copia) */
    coffee_dll_t dll;  /**< handle de la DLL */
    int active;        /**< 1 si registrada; 0 si su slot quedo libre */
} HostExtension;

/** @brief Una vista registrada por una extension (panel/overlay/statusbar). */
typedef struct {
    char *id;                /**< id de la vista */
    char *title;             /**< titulo legible */
    CoffeeViewKind kind;     /**< donde vive la vista */
    CoffeePaintFn paint;     /**< callback de pintado */
    CoffeeViewInputFn input; /**< callback de input (puede ser NULL) */
    void *userdata;          /**< userdata de los callbacks */
    int owner;               /**< extension dueña (registro por-ext) */
} HostView;

/**
 * @brief Implementacion concreta del handle opaco @c CoffeeHost.
 *
 * Las extensiones reciben un @c CoffeeHost* y lo pasan de vuelta a cada funcion
 * del @c CoffeeApi; el host lo castea a esta struct.
 */
struct CoffeeHost {
    CoffeeApi api;             /**< vtable que reciben las DLLs */
    CoffeeHostBackend backend; /**< respaldo (buffer + hooks de UI) */

    /* tablas dinamicas; arrays contiguos por localidad de cache */
    HostCommand *cmds;
    size_t cmd_count, cmd_cap;
    HostSub *subs;
    size_t sub_count, sub_cap;
    HostService *svcs;
    size_t svc_count, svc_cap;
    HostConfig *cfgs;
    size_t cfg_count, cfg_cap;
    HostExtension *exts;
    size_t ext_count, ext_cap;
    HostView *views;
    size_t view_count, view_cap;

    /** Indice de la extension que se esta registrando ahora mismo (para
     *  atribuir lo que registre).  -1 cuando no hay registro en curso. */
    int registering;

    char *ext_dir_cache; /**< directorio de datos privado (ultimo consultado) */

    char last_error[256]; /**< ultimo mensaje de error legible (causa de un
                               fallo de carga).  Cadena vacia = sin error. */

    /** 1 mientras se cierra el IDE: las descargas NO hacen FreeLibrary (el
     *  proceso termina y el SO reclama los modulos).  Descargar una DLL que
     *  arrastra un runtime con hilos/atexit puede abortar en su limpieza de
     *  DLL_PROCESS_DETACH; mantenerla cargada al salir evita ese riesgo. */
    int shutting_down;
};

/* -- util: strdup portable (algunos toolchains no exponen strdup en C11) ---- */
static char *host_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* -- util: crece un array generico si esta lleno (capacidad x2) ------------- */
static int host_grow(void **arr, size_t *cap, size_t count, size_t elem) {
    if (count < *cap) return 1;
    size_t ncap = (*cap == 0) ? 8 : (*cap * 2);
    void *np = realloc(*arr, ncap * elem);
    if (!np) return 0;
    *arr = np;
    *cap = ncap;
    return 1;
}

/* -- util: fija el ultimo mensaje de error del host (estilo printf) ---------- */
static void host_set_error(CoffeeHost *h, const char *fmt, ...) {
    if (!h) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(h->last_error, sizeof(h->last_error), fmt, ap);
    va_end(ap);
    /* loguear siempre a stderr para diagnostico headless */
    fprintf(stderr, "[ext-host] %s\n", h->last_error);
}

const char *ext_host_last_error(CoffeeHost *host) {
    return host ? host->last_error : "";
}

/* ===========================================================================
 *  Respaldo del CoffeeApi (las funciones que reciben las extensiones)
 * =========================================================================== */

/* ---- Registro de capacidades ---- */

static int api_register_command(CoffeeHost *h, const char *id, const char *title,
                                CoffeeCommandFn fn, void *userdata) {
    if (!h || !id || !fn) return -1;
    /* rechazar duplicados de id */
    for (size_t i = 0; i < h->cmd_count; ++i)
        if (h->cmds[i].id && strcmp(h->cmds[i].id, id) == 0) return -2;
    if (!host_grow((void **)&h->cmds, &h->cmd_cap, h->cmd_count,
                   sizeof(HostCommand)))
        return -3;
    HostCommand *c = &h->cmds[h->cmd_count++];
    c->id = host_strdup(id);
    c->title = host_strdup(title);
    c->fn = fn;
    c->userdata = userdata;
    c->owner = h->registering;
    return 0;
}

static int api_subscribe_event(CoffeeHost *h, CoffeeEventType ev,
                               CoffeeEventFn fn, void *userdata) {
    if (!h || !fn) return -1;
    if (!host_grow((void **)&h->subs, &h->sub_cap, h->sub_count,
                   sizeof(HostSub)))
        return -2;
    HostSub *s = &h->subs[h->sub_count++];
    s->ev = ev;
    s->fn = fn;
    s->userdata = userdata;
    s->owner = h->registering;
    return 0;
}

/* add_menu_item / bind_key: STUB (loguean; el wiring de menus/atajos del
 * IDE llega mas adelante).  Devuelven 0 para no romper la activacion. */
static int api_add_menu_item(CoffeeHost *h, const char *menu_path,
                             const char *command_id) {
    (void)h;
    fprintf(stderr, "[ext-host] add_menu_item('%s','%s'): stub\n",
            menu_path ? menu_path : "", command_id ? command_id : "");
    return 0;
}
static int api_bind_key(CoffeeHost *h, const char *keychord,
                        const char *command_id) {
    (void)h;
    fprintf(stderr, "[ext-host] bind_key('%s','%s'): stub\n",
            keychord ? keychord : "", command_id ? command_id : "");
    return 0;
}

/* ---- Editor / buffer ACTIVO ---- */

static size_t api_buffer_length(CoffeeHost *h) {
    if (!h || !h->backend.buffer) return 0;
    return buf_length(h->backend.buffer);
}

static size_t api_buffer_get_text(CoffeeHost *h, size_t from, size_t to,
                                  char *out, size_t cap) {
    if (!h || !h->backend.buffer || !out || cap == 0) return 0;
    size_t len = buf_length(h->backend.buffer);
    if (to > len) to = len;
    if (from > to) from = to;
    /* buf_get_text copia [from,to) sin NUL; respetar el cap de salida. */
    size_t want = to - from;
    if (want >= cap) want = cap - 1; /* reservar 1 byte para el NUL */
    size_t n = buf_get_text(h->backend.buffer, from, from + want, out);
    out[n] = '\0';
    return n;
}

static void api_buffer_insert(CoffeeHost *h, const char *utf8) {
    if (!h || !h->backend.buffer || !utf8) return;
    buf_insert_str(h->backend.buffer, utf8, strlen(utf8));
}

static void api_buffer_replace(CoffeeHost *h, size_t from, size_t to,
                               const char *utf8) {
    if (!h || !h->backend.buffer) return;
    Buffer *b = h->backend.buffer;
    size_t len = buf_length(b);
    if (to > len) to = len;
    if (from > to) from = to;
    /* Borrar el rango y, posicionando el cursor en `from`, insertar el nuevo
     * texto.  buf_delete_range no mueve el cursor logico de forma garantizada,
     * asi que lo fijamos explicitamente antes de insertar. */
    if (to > from) buf_delete_range(b, from, to);
    buf_move_to(b, from);
    if (utf8 && *utf8) buf_insert_str(b, utf8, strlen(utf8));
}

static size_t api_cursor_pos(CoffeeHost *h) {
    if (!h || !h->backend.buffer) return 0;
    return buf_cursor_pos(h->backend.buffer);
}

static void api_set_cursor(CoffeeHost *h, size_t pos) {
    if (!h || !h->backend.buffer) return;
    size_t len = buf_length(h->backend.buffer);
    if (pos > len) pos = len;
    buf_move_to(h->backend.buffer, pos);
}

static int api_selection(CoffeeHost *h, size_t *from, size_t *to) {
    (void)h;
    /* El host headless no modela seleccion (vive en el Editor SDL).  Sin
     * seleccion: devolver 0.  El wiring del editor puede mapear su seleccion
     * via un hook futuro. */
    if (from) *from = 0;
    if (to) *to = 0;
    return 0;
}

static const char *api_current_path(CoffeeHost *h) {
    if (!h) return NULL;
    if (h->backend.current_path) return h->backend.current_path(h->backend.ud);
    return NULL;
}

/* ---- Acciones del IDE (delegadas; stub si no hay hook) ---- */

static void api_open_file(CoffeeHost *h, const char *path) {
    if (h && h->backend.open_file)
        h->backend.open_file(h->backend.ud, path);
    else
        fprintf(stderr, "[ext-host] open_file('%s'): sin backend\n",
                path ? path : "");
}
static void api_save_file(CoffeeHost *h) {
    if (h && h->backend.save_file)
        h->backend.save_file(h->backend.ud);
    else
        fprintf(stderr, "[ext-host] save_file: sin backend\n");
}
static void api_new_tab(CoffeeHost *h) {
    if (h && h->backend.new_tab)
        h->backend.new_tab(h->backend.ud);
    else
        fprintf(stderr, "[ext-host] new_tab: sin backend\n");
}

/* ---- UI / feedback ---- */

static void api_set_status(CoffeeHost *h, const char *msg) {
    if (h && h->backend.set_status)
        h->backend.set_status(h->backend.ud, msg);
    else
        fprintf(stderr, "[ext-host] status: %s\n", msg ? msg : "");
}
static void api_show_message(CoffeeHost *h, const char *title, const char *body) {
    if (h && h->backend.show_message)
        h->backend.show_message(h->backend.ud, title, body);
    else
        fprintf(stderr, "[ext-host] message: %s -- %s\n", title ? title : "",
                body ? body : "");
}
static void api_log(CoffeeHost *h, CoffeeLogLevel level, const char *msg) {
    static const char *lv[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    int i = (level >= COFFEE_LOG_DEBUG && level <= COFFEE_LOG_ERROR) ? level : 1;
    fprintf(stderr, "[ext-host][%s] %s\n", lv[i], msg ? msg : "");
    /* tambien alimentar la pestana "Logs" del panel inferior, si hay backend */
    if (h && h->backend.log_line) h->backend.log_line(h->backend.ud, (int)i, msg);
}
static void api_output_append(CoffeeHost *h, const char *text) {
    if (h && h->backend.output_append)
        h->backend.output_append(h->backend.ud, text);
    else
        fputs(text ? text : "", stdout);
}
static void api_output_clear(CoffeeHost *h) {
    if (h && h->backend.output_clear) h->backend.output_clear(h->backend.ud);
}

/* ---- Canales del panel inferior (ABI v2) ---- */

static int api_register_output_channel(CoffeeHost *h, const char *id,
                                       const char *title) {
    if (!h || !id || !id[0]) return -1;
    if (h->backend.register_channel)
        return h->backend.register_channel(h->backend.ud, id, title);
    /* sin backend (headless): loguear y aceptar para no romper la activacion */
    fprintf(stderr, "[ext-host] register_output_channel('%s','%s'): sin backend\n",
            id, title ? title : "");
    return 0;
}
static void api_channel_append(CoffeeHost *h, const char *id, const char *text) {
    if (!h || !id) return;
    if (h->backend.channel_append)
        h->backend.channel_append(h->backend.ud, id, text);
    else
        fputs(text ? text : "", stdout);
}
static void api_channel_clear(CoffeeHost *h, const char *id) {
    if (!h || !id) return;
    if (h->backend.channel_clear) h->backend.channel_clear(h->backend.ud, id);
}

/* ---- Dibujo: vistas y decoraciones (STUB) ---- */

static int api_register_view(CoffeeHost *h, const char *id, CoffeeViewKind kind,
                             const char *title, CoffeePaintFn paint,
                             CoffeeViewInputFn input, void *userdata) {
    if (!h || !id || !paint) return -1;
    /* rechazar duplicados de id de vista */
    for (size_t i = 0; i < h->view_count; ++i)
        if (h->views[i].id && strcmp(h->views[i].id, id) == 0) return -2;
    if (!host_grow((void **)&h->views, &h->view_cap, h->view_count,
                   sizeof(HostView)))
        return -3;
    HostView *v = &h->views[h->view_count++];
    v->id = host_strdup(id);
    v->title = host_strdup(title);
    v->kind = kind;
    v->paint = paint;
    v->input = input;
    v->userdata = userdata;
    v->owner = h->registering;
    /* el indice de la vista recien anyadida sirve como id (>=0) */
    return (int)(h->view_count - 1);
}
static void api_remove_view(CoffeeHost *h, const char *id) {
    if (!h || !id) return;
    for (size_t i = 0; i < h->view_count;) {
        if (h->views[i].id && strcmp(h->views[i].id, id) == 0) {
            free(h->views[i].id);
            free(h->views[i].title);
            h->views[i] = h->views[--h->view_count]; /* swap-remove */
        } else {
            ++i;
        }
    }
    if (h->backend.request_repaint) h->backend.request_repaint(h->backend.ud);
}
static void api_request_repaint(CoffeeHost *h) {
    if (h && h->backend.request_repaint) h->backend.request_repaint(h->backend.ud);
}
static int api_set_line_background(CoffeeHost *h, size_t line, CoffeeColor bg) {
    (void)h;
    (void)line;
    (void)bg;
    return -1; /* no implementado aun */
}
static int api_set_gutter_marker(CoffeeHost *h, size_t line, const char *glyph,
                                 CoffeeColor color) {
    (void)h;
    (void)line;
    (void)glyph;
    (void)color;
    return -1;
}
static int api_set_inline_hint(CoffeeHost *h, size_t line, const char *text,
                               CoffeeColor color) {
    (void)h;
    (void)line;
    (void)text;
    (void)color;
    return -1;
}
static void api_clear_decorations(CoffeeHost *h) { (void)h; }

/* ---- Inter-extension: servicios y dependencias ---- */

static int api_register_service(CoffeeHost *h, const char *name, void *iface) {
    if (!h || !name || !iface) return -1;
    for (size_t i = 0; i < h->svc_count; ++i)
        if (h->svcs[i].name && strcmp(h->svcs[i].name, name) == 0) return -2;
    if (!host_grow((void **)&h->svcs, &h->svc_cap, h->svc_count,
                   sizeof(HostService)))
        return -3;
    HostService *s = &h->svcs[h->svc_count++];
    s->name = host_strdup(name);
    s->iface = iface;
    s->owner = h->registering;
    return 0;
}

static void *api_get_service(CoffeeHost *h, const char *name) {
    if (!h || !name) return NULL;
    for (size_t i = 0; i < h->svc_count; ++i)
        if (h->svcs[i].name && strcmp(h->svcs[i].name, name) == 0)
            return h->svcs[i].iface;
    return NULL;
}

static int api_has_extension(CoffeeHost *h, const char *id) {
    return ext_host_has(h, id);
}

static int api_run_command(CoffeeHost *h, const char *command_id) {
    return ext_host_run_command(h, command_id);
}

/* ---- Carga / descarga dinamica (delegan al host) ---- */

static int api_load_extension(CoffeeHost *h, const char *dir) {
    return ext_host_load(h, dir);
}
static int api_unload_extension(CoffeeHost *h, const char *id) {
    return ext_host_unload(h, id);
}
static int api_reload_extension(CoffeeHost *h, const char *id) {
    return ext_host_reload(h, id);
}

/* ---- Config / almacenamiento por-extension ---- */

static const char *api_get_config(CoffeeHost *h, const char *key) {
    if (!h || !key) return NULL;
    for (size_t i = 0; i < h->cfg_count; ++i)
        if (h->cfgs[i].key && strcmp(h->cfgs[i].key, key) == 0)
            return h->cfgs[i].value;
    return NULL;
}
static void api_set_config(CoffeeHost *h, const char *key, const char *value) {
    if (!h || !key) return;
    for (size_t i = 0; i < h->cfg_count; ++i) {
        if (h->cfgs[i].key && strcmp(h->cfgs[i].key, key) == 0) {
            free(h->cfgs[i].value);
            h->cfgs[i].value = host_strdup(value);
            return;
        }
    }
    if (!host_grow((void **)&h->cfgs, &h->cfg_cap, h->cfg_count,
                   sizeof(HostConfig)))
        return;
    HostConfig *c = &h->cfgs[h->cfg_count++];
    c->key = host_strdup(key);
    c->value = host_strdup(value);
}
static const char *api_ext_dir(CoffeeHost *h) {
    if (!h) return NULL;
    /* Devolvemos el directorio de la extension en curso (si lo hay), o
     * un valor por defecto.  El almacen de datos persistente llega mas adelante. */
    if (h->registering >= 0 && (size_t)h->registering < h->ext_count)
        return h->exts[h->registering].dir;
    return h->ext_dir_cache;
}

/* ---- Funciones nativas con nombre: STUB (lo usaria una ext con lenguaje embebido) */
static int api_register_native_fn(CoffeeHost *h, const char *lib,
                                  const char *name, void *fnptr) {
    (void)h;
    (void)fnptr;
    fprintf(stderr,
            "[ext-host] register_native_fn('%s:%s'): no implementado aun\n",
            lib ? lib : "", name ? name : "");
    return -1;
}

/* -- Rellena la vtable del CoffeeApi con TODAS las funciones (sin NULLs) ----- */
static void host_fill_api(CoffeeHost *h) {
    CoffeeApi *a = &h->api;
    a->abi_version = COFFEE_ABI_VERSION;

    a->register_command = api_register_command;
    a->subscribe_event = api_subscribe_event;
    a->add_menu_item = api_add_menu_item;
    a->bind_key = api_bind_key;

    a->buffer_length = api_buffer_length;
    a->buffer_get_text = api_buffer_get_text;
    a->buffer_insert = api_buffer_insert;
    a->buffer_replace = api_buffer_replace;
    a->cursor_pos = api_cursor_pos;
    a->set_cursor = api_set_cursor;
    a->selection = api_selection;
    a->current_path = api_current_path;

    a->open_file = api_open_file;
    a->save_file = api_save_file;
    a->new_tab = api_new_tab;

    a->set_status = api_set_status;
    a->show_message = api_show_message;
    a->log = api_log;
    a->output_append = api_output_append;
    a->output_clear = api_output_clear;

    a->register_view = api_register_view;
    a->remove_view = api_remove_view;
    a->request_repaint = api_request_repaint;
    a->set_line_background = api_set_line_background;
    a->set_gutter_marker = api_set_gutter_marker;
    a->set_inline_hint = api_set_inline_hint;
    a->clear_decorations = api_clear_decorations;

    a->register_service = api_register_service;
    a->get_service = api_get_service;
    a->has_extension = api_has_extension;
    a->run_command = api_run_command;

    a->load_extension = api_load_extension;
    a->unload_extension = api_unload_extension;
    a->reload_extension = api_reload_extension;

    a->get_config = api_get_config;
    a->set_config = api_set_config;
    a->ext_dir = api_ext_dir;

    a->register_native_fn = api_register_native_fn;

    a->register_output_channel = api_register_output_channel;
    a->channel_append = api_channel_append;
    a->channel_clear = api_channel_clear;
}

/* ===========================================================================
 *  Ciclo de vida del host
 * =========================================================================== */

CoffeeHost *ext_host_create(const CoffeeHostBackend *backend) {
    CoffeeHost *h = (CoffeeHost *)calloc(1, sizeof(CoffeeHost));
    if (!h) return NULL;
    if (backend) h->backend = *backend; /* copia por valor */
    h->registering = -1;
    host_fill_api(h);
    return h;
}

const CoffeeApi *ext_host_api(CoffeeHost *host) {
    return host ? &host->api : NULL;
}

void ext_host_set_buffer(CoffeeHost *host, Buffer *buffer) {
    if (host) host->backend.buffer = buffer;
}

/* Revierte TODO lo registrado por la extension @p owner (registro por-ext). */
static void host_revoke_owner(CoffeeHost *h, int owner) {
    /* comandos */
    for (size_t i = 0; i < h->cmd_count;) {
        if (h->cmds[i].owner == owner) {
            free(h->cmds[i].id);
            free(h->cmds[i].title);
            h->cmds[i] = h->cmds[--h->cmd_count]; /* swap-remove */
        } else {
            ++i;
        }
    }
    /* suscripciones a eventos */
    for (size_t i = 0; i < h->sub_count;) {
        if (h->subs[i].owner == owner)
            h->subs[i] = h->subs[--h->sub_count];
        else
            ++i;
    }
    /* servicios */
    for (size_t i = 0; i < h->svc_count;) {
        if (h->svcs[i].owner == owner) {
            free(h->svcs[i].name);
            h->svcs[i] = h->svcs[--h->svc_count];
        } else {
            ++i;
        }
    }
    /* vistas */
    for (size_t i = 0; i < h->view_count;) {
        if (h->views[i].owner == owner) {
            free(h->views[i].id);
            free(h->views[i].title);
            h->views[i] = h->views[--h->view_count];
        } else {
            ++i;
        }
    }
}

void ext_host_destroy(CoffeeHost *host) {
    if (!host) return;
    host->shutting_down = 1; /* las descargas de abajo no haran FreeLibrary */
    /* Emitir SHUTDOWN antes de descargar, para que las extensiones liberen. */
    ext_host_emit(host, COFFEE_EVENT_SHUTDOWN, NULL);
    /* Descargar todas las extensiones activas (en orden inverso de carga). */
    for (size_t i = host->ext_count; i-- > 0;) {
        if (host->exts[i].active && host->exts[i].id)
            ext_host_unload(host, host->exts[i].id);
    }
    /* Liberar lo que pudiera quedar (defensa: comandos/subs/svcs huerfanos). */
    for (size_t i = 0; i < host->cmd_count; ++i) {
        free(host->cmds[i].id);
        free(host->cmds[i].title);
    }
    for (size_t i = 0; i < host->svc_count; ++i) free(host->svcs[i].name);
    for (size_t i = 0; i < host->view_count; ++i) {
        free(host->views[i].id);
        free(host->views[i].title);
    }
    for (size_t i = 0; i < host->cfg_count; ++i) {
        free(host->cfgs[i].key);
        free(host->cfgs[i].value);
    }
    for (size_t i = 0; i < host->ext_count; ++i) {
        free(host->exts[i].id);
        free(host->exts[i].dir);
    }
    free(host->cmds);
    free(host->subs);
    free(host->svcs);
    free(host->views);
    free(host->cfgs);
    free(host->exts);
    free(host->ext_dir_cache);
    free(host);
}

/* ===========================================================================
 *  Dispatch de comandos y eventos
 * =========================================================================== */

int ext_host_run_command(CoffeeHost *host, const char *command_id) {
    if (!host || !command_id) return -1;
    for (size_t i = 0; i < host->cmd_count; ++i) {
        if (host->cmds[i].id && strcmp(host->cmds[i].id, command_id) == 0) {
            host->cmds[i].fn(host, host->cmds[i].userdata);
            return 0;
        }
    }
    return -2; /* comando no encontrado (p.ej. su extension se descargo) */
}

void ext_host_emit(CoffeeHost *host, CoffeeEventType event, const void *data) {
    if (!host) return;
    /* Copiar el conteo: un callback podria modificar la tabla; iteramos sobre
     * los indices presentes en el momento de la emision. */
    size_t n = host->sub_count;
    for (size_t i = 0; i < n && i < host->sub_count; ++i) {
        if (host->subs[i].ev == event)
            host->subs[i].fn(host, event, data, host->subs[i].userdata);
    }
}

int ext_host_has(CoffeeHost *host, const char *id) {
    if (!host || !id) return 0;
    for (size_t i = 0; i < host->ext_count; ++i)
        if (host->exts[i].active && host->exts[i].id &&
            strcmp(host->exts[i].id, id) == 0)
            return 1;
    return 0;
}

/* ===========================================================================
 *  Introspeccion
 * =========================================================================== */

size_t ext_host_count(CoffeeHost *host) { return host ? host->ext_count : 0; }

int ext_host_info(CoffeeHost *host, size_t idx, const char **id,
                  const char **name, const char **dir, int *active) {
    if (!host || idx >= host->ext_count) return 0;
    HostExtension *he = &host->exts[idx];
    if (id) *id = he->id;
    if (name) *name = he->id; /* hoy no hay campo "name" separado en el manifiesto */
    if (dir) *dir = he->dir;
    if (active) *active = he->active;
    return 1;
}

size_t ext_host_view_count(CoffeeHost *host) {
    return host ? host->view_count : 0;
}

int ext_host_view_at(CoffeeHost *host, size_t idx, CoffeeHostView *out) {
    if (!host || idx >= host->view_count || !out) return 0;
    HostView *v = &host->views[idx];
    out->id = v->id;
    out->title = v->title;
    out->kind = v->kind;
    out->paint = v->paint;
    out->input = v->input;
    out->userdata = v->userdata;
    return 1;
}

/* ===========================================================================
 *  Lector minimal del manifiesto coffee-extension.toml
 * ---------------------------------------------------------------------------
 *  No es un parser TOML completo: extrae solo las claves que el host usa.
 *    [extension]
 *    id           = "hello-c"
 *    entry        = "coffee_hello.dll"
 *    abi          = 1
 *    dependencies = ["a", "b"]
 * =========================================================================== */

/** Estructura del manifiesto leida (cadenas heap-alocadas). */
typedef struct {
    char *id;
    char *entry;
    unsigned abi;
    char **deps; /**< array de ids de dependencia */
    size_t dep_count;
} HostManifest;

static void manifest_free(HostManifest *m) {
    if (!m) return;
    free(m->id);
    free(m->entry);
    for (size_t i = 0; i < m->dep_count; ++i) free(m->deps[i]);
    free(m->deps);
    memset(m, 0, sizeof(*m));
}

/* Recorta espacios/comillas al inicio y final de @p s (in situ).  Devuelve s. */
static char *trim_value(char *s) {
    while (*s == ' ' || *s == '\t') ++s;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
                     s[n - 1] == '\n'))
        s[--n] = '\0';
    /* quitar comillas envolventes */
    if (n >= 2 && (s[0] == '"' || s[0] == '\'') && s[n - 1] == s[0]) {
        s[n - 1] = '\0';
        ++s;
    }
    return s;
}

/* Parsea una lista TOML simple ["a", "b", "c"] anyadiendo cada elemento como
 * dependencia.  @p list apunta al contenido entre corchetes (sin '[' ni ']'). */
static void manifest_parse_deps(HostManifest *m, char *list) {
    char *p = list;
    while (*p) {
        /* saltar separadores */
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            char *start = p;
            while (*p && *p != q) ++p;
            size_t len = (size_t)(p - start);
            if (len > 0) {
                char *dep = (char *)malloc(len + 1);
                if (dep) {
                    memcpy(dep, start, len);
                    dep[len] = '\0';
                    char **na = (char **)realloc(
                        m->deps, (m->dep_count + 1) * sizeof(char *));
                    if (na) {
                        m->deps = na;
                        m->deps[m->dep_count++] = dep;
                    } else {
                        free(dep);
                    }
                }
            }
            if (*p == q) ++p; /* consumir comilla de cierre */
        } else {
            ++p; /* avanzar ante caracteres inesperados */
        }
    }
}

/* Lee el manifiesto en @p dir/coffee-extension.toml.  0 = ok.
 * @p h (opcional, puede ser NULL) recibe el mensaje de error legible. */
static int manifest_read(CoffeeHost *h, const char *dir, HostManifest *out) {
    char path[1024];
    snprintf(path, sizeof(path), "%s%c%s", dir, COFFEE_PATH_SEP,
             "coffee-extension.toml");
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (h)
            host_set_error(h, "manifiesto coffee-extension.toml no encontrado "
                              "en %s", dir);
        else
            fprintf(stderr, "[ext-host] no se encontro manifiesto: %s\n", path);
        return -1;
    }
    memset(out, 0, sizeof(*out));
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        /* ignorar comentarios y lineas en blanco */
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\0' || *p == '\r' || *p == '\n') continue;
        if (*p == '[') continue; /* cabecera de tabla: no la necesitamos */
        /* clave = valor */
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim_value(p);
        char *val = eq + 1;
        if (strcmp(key, "id") == 0) {
            free(out->id);
            out->id = host_strdup(trim_value(val));
        } else if (strcmp(key, "entry") == 0) {
            free(out->entry);
            out->entry = host_strdup(trim_value(val));
        } else if (strcmp(key, "abi") == 0) {
            out->abi = (unsigned)strtoul(trim_value(val), NULL, 10);
        } else if (strcmp(key, "dependencies") == 0) {
            /* el valor puede ser "[...]"; recortar los corchetes */
            char *lb = strchr(val, '[');
            char *rb = strrchr(val, ']');
            if (lb && rb && rb > lb) {
                *rb = '\0';
                manifest_parse_deps(out, lb + 1);
            }
        }
    }
    fclose(f);
    if (!out->id || !out->entry) {
        if (h)
            host_set_error(h, "manifiesto incompleto en %s (falta 'id' o "
                              "'entry')", path);
        else
            fprintf(stderr,
                    "[ext-host] manifiesto incompleto (falta id o entry): %s\n",
                    path);
        manifest_free(out);
        return -2;
    }
    return 0;
}

/* ===========================================================================
 *  Carga / descarga de una extension
 * =========================================================================== */

/* Busca el indice de la extension cargada por id (-1 si no esta). */
static int host_find_ext(CoffeeHost *h, const char *id) {
    for (size_t i = 0; i < h->ext_count; ++i)
        if (h->exts[i].active && h->exts[i].id &&
            strcmp(h->exts[i].id, id) == 0)
            return (int)i;
    return -1;
}

int ext_host_load(CoffeeHost *host, const char *dir) {
    if (!host || !dir) return -1;

    host->last_error[0] = '\0'; /* limpiar el error previo */

    HostManifest m;
    if (manifest_read(host, dir, &m) != 0) return -2;

    /* ya cargada? */
    if (host_find_ext(host, m.id) >= 0) {
        host_set_error(host, "la extension '%s' ya esta cargada", m.id);
        manifest_free(&m);
        return -3;
    }

    /* abi compatible? */
    if (m.abi > COFFEE_ABI_VERSION) {
        host_set_error(host,
                       "ABI de la extension '%s' (%u) mayor al soportado (%u)",
                       m.id, m.abi, COFFEE_ABI_VERSION);
        manifest_free(&m);
        return -4;
    }

    /* construir la ruta de la DLL: <dir>/<entry> */
    char dllpath[1024];
    snprintf(dllpath, sizeof(dllpath), "%s%c%s", dir, COFFEE_PATH_SEP, m.entry);
    coffee_dll_t dll = coffee_dll_open(dllpath);
    if (!dll) {
        char oserr[256];
        coffee_dll_last_error(oserr, sizeof(oserr));
        host_set_error(host, "no se pudo cargar la DLL '%s': %s", m.entry,
                       oserr);
        manifest_free(&m);
        return -5;
    }

    CoffeeExtensionRegisterFn reg =
        (CoffeeExtensionRegisterFn)coffee_dll_sym(dll, COFFEE_EXTENSION_ENTRY);
    if (!reg) {
        host_set_error(host, "la DLL de '%s' no exporta %s", m.id,
                       COFFEE_EXTENSION_ENTRY);
        coffee_dll_close(dll);
        manifest_free(&m);
        return -6;
    }

    /* reservar el slot de la extension ANTES de registrar, para que el indice
     * "registering" sea valido y los registros se atribuyan a esta extension. */
    if (!host_grow((void **)&host->exts, &host->ext_cap, host->ext_count,
                   sizeof(HostExtension))) {
        host_set_error(host, "sin memoria para registrar la extension '%s'",
                       m.id);
        coffee_dll_close(dll);
        manifest_free(&m);
        return -7;
    }
    int idx = (int)host->ext_count++;
    HostExtension *he = &host->exts[idx];
    he->id = host_strdup(m.id);
    he->dir = host_strdup(dir);
    he->dll = dll;
    he->active = 1;

    /* invocar el registro de la extension con el indice activo */
    host->registering = idx;
    int rc = reg(host, &host->api);
    host->registering = -1;

    if (rc != 0) {
        host_set_error(host,
                       "coffee_extension_register de '%s' devolvio error (%d)",
                       m.id, rc);
        /* revertir lo que hubiera registrado + liberar el slot */
        host_revoke_owner(host, idx);
        coffee_dll_close(dll);
        free(he->id);
        free(he->dir);
        he->active = 0;
        he->id = NULL;
        he->dir = NULL;
        he->dll = NULL;
        host->ext_count--; /* solo valido por ser el ultimo slot */
        manifest_free(&m);
        return -8;
    }

    fprintf(stderr, "[ext-host] '%s' cargada desde %s\n", m.id, dir);
    manifest_free(&m);
    return 0;
}

int ext_host_unload(CoffeeHost *host, const char *id) {
    if (!host || !id) return -1;
    int idx = host_find_ext(host, id);
    if (idx < 0) {
        host_set_error(host, "la extension '%s' no esta cargada", id);
        return -2;
    }
    HostExtension *he = &host->exts[idx];

    /* invocar deactivate opcional ANTES de revocar/cerrar */
    CoffeeExtensionUnregisterFn deact =
        (CoffeeExtensionUnregisterFn)coffee_dll_sym(he->dll,
                                                    COFFEE_EXTENSION_DEACTIVATE);
    if (deact) deact(host);

    /* revertir TODO lo registrado por esta extension */
    host_revoke_owner(host, idx);

    /* loguear ANTES de liberar: @p id puede aliasar he->id (caso destroy), por
     * lo que dereferenciarlo tras el free seria un use-after-free. */
    fprintf(stderr, "[ext-host] '%s' descargada\n", he->id ? he->id : id);

    /* cerrar la DLL y marcar el slot como libre.  Al cerrar el IDE NO se
     * descarga (FreeLibrary): el proceso va a terminar y el SO reclama los
     * modulos; ademas descargar una DLL que enlaza un runtime con estado global
     * pesado (hilos, atexit) puede abortar en su DLL_PROCESS_DETACH. */
    if (!host->shutting_down)
        coffee_dll_close(he->dll);
    free(he->id);
    free(he->dir);
    he->id = NULL;
    he->dir = NULL;
    he->dll = NULL;
    he->active = 0;

    return 0;
}

int ext_host_reload(CoffeeHost *host, const char *id) {
    if (!host || !id) return -1;
    int idx = host_find_ext(host, id);
    if (idx < 0) {
        host_set_error(host, "la extension '%s' no esta cargada", id);
        return -2;
    }
    /* copiar el dir antes de descargar (unload libera la cadena) */
    char *dir = host_strdup(host->exts[idx].dir);
    int rc = ext_host_unload(host, id);
    if (rc == 0 && dir) rc = ext_host_load(host, dir);
    free(dir);
    return rc;
}

/* ===========================================================================
 *  Carga de un directorio con orden topologico por dependencias
 * =========================================================================== */

/** Entrada candidata: un subdirectorio con su manifiesto leido. */
typedef struct {
    char *dir;
    HostManifest m;
    int loaded; /**< 1 si ya se cargo (marca del orden topologico) */
} DirCandidate;

/* Emite un fallo de carga a la UI: panel de salida + barra de estado, ademas
 * de guardarlo como ultimo error.  Asi el usuario VE en el IDE que una
 * extension no cargo y por que, sin abrir una consola. */
static void host_report_failure(CoffeeHost *h, const char *id,
                                const char *reason) {
    char line[320];
    snprintf(line, sizeof(line), "extension '%s' fallo: %s\n",
             id ? id : "?", reason ? reason : "causa desconocida");
    if (h->backend.output_append)
        h->backend.output_append(h->backend.ud, line);
    if (h->backend.set_status) {
        char st[320];
        snprintf(st, sizeof(st), "extension '%s' fallo: %s", id ? id : "?",
                 reason ? reason : "causa desconocida");
        h->backend.set_status(h->backend.ud, st);
    }
    /* dejar tambien el mensaje accesible via ext_host_last_error */
    snprintf(h->last_error, sizeof(h->last_error), "extension '%s' fallo: %s",
             id ? id : "?", reason ? reason : "causa desconocida");
}

/* Busca el indice de un candidato por id (-1 si no existe). */
static int cand_find(DirCandidate *c, size_t n, const char *id) {
    for (size_t i = 0; i < n; ++i)
        if (c[i].m.id && strcmp(c[i].m.id, id) == 0) return (int)i;
    return -1;
}

/* Carga recursiva en orden topologico: primero las deps de @p i, luego @p i.
 * @p stack/@p depth detectan ciclos.  @p fail_count cuenta las extensiones que
 * no se pudieron cargar (para el resumen visible).  Cada fallo se reporta a la
 * UI via host_report_failure.  Devuelve 0 ok, <0 ciclo/dep ausente. */
static int cand_load_rec(CoffeeHost *host, DirCandidate *c, size_t n, int i,
                         int *stack, int depth, int *loaded_count,
                         int *fail_count) {
    if (c[i].loaded) return 0;
    /* deteccion de ciclo: i ya esta en la pila de recursion actual */
    for (int d = 0; d < depth; ++d) {
        if (stack[d] == i) {
            char reason[256];
            snprintf(reason, sizeof(reason),
                     "ciclo de dependencias detectado");
            host_report_failure(host, c[i].m.id, reason);
            (*fail_count)++;
            return -1;
        }
    }
    stack[depth] = i;
    /* cargar dependencias primero */
    for (size_t k = 0; k < c[i].m.dep_count; ++k) {
        int di = cand_find(c, n, c[i].m.deps[k]);
        if (di < 0) {
            char reason[256];
            snprintf(reason, sizeof(reason),
                     "falta la dependencia '%s'", c[i].m.deps[k]);
            host_report_failure(host, c[i].m.id, reason);
            (*fail_count)++;
            return -2;
        }
        int rc = cand_load_rec(host, c, n, di, stack, depth + 1, loaded_count,
                               fail_count);
        if (rc != 0) return rc;
    }
    /* cargar esta extension; si falla, mostrar la causa concreta en la UI */
    if (ext_host_load(host, c[i].dir) == 0) {
        (*loaded_count)++;
    } else {
        host_report_failure(host, c[i].m.id, ext_host_last_error(host));
        (*fail_count)++;
    }
    c[i].loaded = 1;
    return 0;
}

/* Lista los subdirectorios de @p root.  Rellena @p out con cadenas heap.
 * Devuelve el numero de subdirectorios, o <0 en error. */
static int list_subdirs(const char *root, char ***out) {
    char **dirs = NULL;
    size_t count = 0, cap = 0;
#if defined(_WIN32)
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s%c*", root, COFFEE_PATH_SEP);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        if (count >= cap) {
            cap = cap ? cap * 2 : 8;
            char **nd = (char **)realloc(dirs, cap * sizeof(char *));
            if (!nd) break;
            dirs = nd;
        }
        char full[1024];
        snprintf(full, sizeof(full), "%s%c%s", root, COFFEE_PATH_SEP,
                 fd.cFileName);
        dirs[count++] = host_strdup(full);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(root);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s%c%s", root, COFFEE_PATH_SEP, e->d_name);
        /* aceptar solo directorios; comprobar via manifiesto mas adelante */
        if (count >= cap) {
            cap = cap ? cap * 2 : 8;
            char **nd = (char **)realloc(dirs, cap * sizeof(char *));
            if (!nd) break;
            dirs = nd;
        }
        dirs[count++] = host_strdup(full);
    }
    closedir(d);
#endif
    *out = dirs;
    return (int)count;
}

int ext_host_load_dir(CoffeeHost *host, const char *extensions_root) {
    if (!host || !extensions_root) return -1;

    char **dirs = NULL;
    int ndirs = list_subdirs(extensions_root, &dirs);
    if (ndirs <= 0) {
        if (dirs) free(dirs);
        return ndirs < 0 ? -2 : 0; /* 0 extensiones si el dir esta vacio */
    }

    /* leer manifiesto de cada subdirectorio que lo tenga */
    DirCandidate *cands =
        (DirCandidate *)calloc((size_t)ndirs, sizeof(DirCandidate));
    size_t ncand = 0;
    for (int i = 0; i < ndirs; ++i) {
        HostManifest m;
        /* pasar NULL como host: un subdirectorio sin manifiesto no es un error
         * (puede no ser una extension); el fallo real se reporta al cargar. */
        if (manifest_read(NULL, dirs[i], &m) == 0) {
            cands[ncand].dir = host_strdup(dirs[i]);
            cands[ncand].m = m; /* toma posesion de las cadenas del manifiesto */
            cands[ncand].loaded = 0;
            ncand++;
        }
        free(dirs[i]);
    }
    free(dirs);

    int loaded = 0;
    int failed = 0;
    int result = 0;
    if (ncand > 0) {
        int *stack = (int *)malloc(ncand * sizeof(int));
        for (size_t i = 0; i < ncand; ++i) {
            int rc = cand_load_rec(host, cands, ncand, (int)i, stack, 0,
                                   &loaded, &failed);
            if (rc != 0) {
                result = rc; /* ciclo o dep ausente: reportar pero seguir */
            }
        }
        free(stack);
    }

    for (size_t i = 0; i < ncand; ++i) {
        free(cands[i].dir);
        manifest_free(&cands[i].m);
    }
    free(cands);

    /* Resumen VISIBLE: si alguna extension fallo, avisar en la barra de estado
     * para que el usuario sepa que mirar el panel de salida (donde ya estan los
     * detalles de cada fallo emitidos por host_report_failure). */
    if (failed > 0 && host->backend.set_status) {
        char st[160];
        snprintf(st, sizeof(st),
                 "%d extension(es) no se cargaron - ver salida", failed);
        host->backend.set_status(host->backend.ud, st);
    }

    return result < 0 ? result : loaded;
}
