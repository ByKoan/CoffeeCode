/**
 * @file ext_host.c
 * @brief Implementacion del extension host de CoffeeCode (incremento E1).
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- Carga dinamica por plataforma ----------------------------------------- */
#if defined(_WIN32)
#include <windows.h>
typedef HMODULE coffee_dll_t; /**< handle de modulo nativo */
#define COFFEE_DLL_EXT ".dll"
static coffee_dll_t coffee_dll_open(const char *path) {
    return LoadLibraryA(path);
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

/* ===========================================================================
 *  Estructuras internas del host
 * =========================================================================== */

/** @brief Un comando registrado por una extension. */
typedef struct {
    char *id;             /**< id unico ("vesta.run") */
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

    /** Indice de la extension que se esta registrando ahora mismo (para
     *  atribuir lo que registre).  -1 cuando no hay registro en curso. */
    int registering;

    char *ext_dir_cache; /**< directorio de datos privado (ultimo consultado) */
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

/* add_menu_item / bind_key: STUB en E1 (loguean; el wiring de menus/atajos del
 * IDE llega en E1.b/E4).  Devuelven 0 para no romper la activacion. */
static int api_add_menu_item(CoffeeHost *h, const char *menu_path,
                             const char *command_id) {
    (void)h;
    fprintf(stderr, "[ext-host] add_menu_item('%s','%s'): stub (E1)\n",
            menu_path ? menu_path : "", command_id ? command_id : "");
    return 0;
}
static int api_bind_key(CoffeeHost *h, const char *keychord,
                        const char *command_id) {
    (void)h;
    fprintf(stderr, "[ext-host] bind_key('%s','%s'): stub (E1)\n",
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
        fprintf(stderr, "[ext-host] open_file('%s'): sin backend (E1)\n",
                path ? path : "");
}
static void api_save_file(CoffeeHost *h) {
    if (h && h->backend.save_file)
        h->backend.save_file(h->backend.ud);
    else
        fprintf(stderr, "[ext-host] save_file: sin backend (E1)\n");
}
static void api_new_tab(CoffeeHost *h) {
    if (h && h->backend.new_tab)
        h->backend.new_tab(h->backend.ud);
    else
        fprintf(stderr, "[ext-host] new_tab: sin backend (E1)\n");
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
    (void)h;
    static const char *lv[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    int i = (level >= COFFEE_LOG_DEBUG && level <= COFFEE_LOG_ERROR) ? level : 1;
    fprintf(stderr, "[ext-host][%s] %s\n", lv[i], msg ? msg : "");
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

/* ---- Dibujo: vistas y decoraciones (STUB en E1; llega en E1.b) ---- */

static int api_register_view(CoffeeHost *h, const char *id, CoffeeViewKind kind,
                             const char *title, CoffeePaintFn paint,
                             CoffeeViewInputFn input, void *userdata) {
    (void)h;
    (void)kind;
    (void)title;
    (void)paint;
    (void)input;
    (void)userdata;
    fprintf(stderr, "[ext-host] register_view('%s'): no implementado en E1\n",
            id ? id : "");
    return -1;
}
static void api_remove_view(CoffeeHost *h, const char *id) {
    (void)h;
    (void)id;
}
static void api_request_repaint(CoffeeHost *h) {
    if (h && h->backend.request_repaint) h->backend.request_repaint(h->backend.ud);
}
static int api_set_line_background(CoffeeHost *h, size_t line, CoffeeColor bg) {
    (void)h;
    (void)line;
    (void)bg;
    return -1; /* no implementado en E1 */
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
    /* En E1 devolvemos el directorio de la extension en curso (si lo hay), o
     * un valor por defecto.  El almacen de datos persistente llega en E4. */
    if (h->registering >= 0 && (size_t)h->registering < h->ext_count)
        return h->exts[h->registering].dir;
    return h->ext_dir_cache;
}

/* ---- Puente para EXTENSIONES EN VEX (STUB en E1; lo usa la ext vesta en E3) */
static int api_register_native_fn(CoffeeHost *h, const char *lib,
                                  const char *name, void *fnptr) {
    (void)h;
    (void)fnptr;
    fprintf(stderr,
            "[ext-host] register_native_fn('%s:%s'): no implementado en E1\n",
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
}

void ext_host_destroy(CoffeeHost *host) {
    if (!host) return;
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

/* Lee el manifiesto en @p dir/coffee-extension.toml.  0 = ok. */
static int manifest_read(const char *dir, HostManifest *out) {
    char path[1024];
    snprintf(path, sizeof(path), "%s%c%s", dir, COFFEE_PATH_SEP,
             "coffee-extension.toml");
    FILE *f = fopen(path, "rb");
    if (!f) {
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

    HostManifest m;
    if (manifest_read(dir, &m) != 0) return -2;

    /* ya cargada? */
    if (host_find_ext(host, m.id) >= 0) {
        fprintf(stderr, "[ext-host] '%s' ya esta cargada\n", m.id);
        manifest_free(&m);
        return -3;
    }

    /* abi compatible? */
    if (m.abi > COFFEE_ABI_VERSION) {
        fprintf(stderr,
                "[ext-host] '%s' requiere ABI %u > %u (soportado); se ignora\n",
                m.id, m.abi, COFFEE_ABI_VERSION);
        manifest_free(&m);
        return -4;
    }

    /* construir la ruta de la DLL: <dir>/<entry> */
    char dllpath[1024];
    snprintf(dllpath, sizeof(dllpath), "%s%c%s", dir, COFFEE_PATH_SEP, m.entry);
    coffee_dll_t dll = coffee_dll_open(dllpath);
    if (!dll) {
        fprintf(stderr, "[ext-host] no se pudo cargar la DLL: %s\n", dllpath);
        manifest_free(&m);
        return -5;
    }

    CoffeeExtensionRegisterFn reg =
        (CoffeeExtensionRegisterFn)coffee_dll_sym(dll, COFFEE_EXTENSION_ENTRY);
    if (!reg) {
        fprintf(stderr, "[ext-host] '%s' no exporta %s\n", m.id,
                COFFEE_EXTENSION_ENTRY);
        coffee_dll_close(dll);
        manifest_free(&m);
        return -6;
    }

    /* reservar el slot de la extension ANTES de registrar, para que el indice
     * "registering" sea valido y los registros se atribuyan a esta extension. */
    if (!host_grow((void **)&host->exts, &host->ext_cap, host->ext_count,
                   sizeof(HostExtension))) {
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
        fprintf(stderr, "[ext-host] '%s' fallo su registro (rc=%d); descargo\n",
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
    if (idx < 0) return -2;
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

    /* cerrar la DLL y marcar el slot como libre */
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
    if (idx < 0) return -2;
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

/* Busca el indice de un candidato por id (-1 si no existe). */
static int cand_find(DirCandidate *c, size_t n, const char *id) {
    for (size_t i = 0; i < n; ++i)
        if (c[i].m.id && strcmp(c[i].m.id, id) == 0) return (int)i;
    return -1;
}

/* Carga recursiva en orden topologico: primero las deps de @p i, luego @p i.
 * @p stack/@p depth detectan ciclos.  Devuelve 0 ok, <0 ciclo/error. */
static int cand_load_rec(CoffeeHost *host, DirCandidate *c, size_t n, int i,
                         int *stack, int depth, int *loaded_count) {
    if (c[i].loaded) return 0;
    /* deteccion de ciclo: i ya esta en la pila de recursion actual */
    for (int d = 0; d < depth; ++d) {
        if (stack[d] == i) {
            fprintf(stderr,
                    "[ext-host] ciclo de dependencias detectado en '%s'\n",
                    c[i].m.id ? c[i].m.id : "?");
            return -1;
        }
    }
    stack[depth] = i;
    /* cargar dependencias primero */
    for (size_t k = 0; k < c[i].m.dep_count; ++k) {
        int di = cand_find(c, n, c[i].m.deps[k]);
        if (di < 0) {
            fprintf(stderr,
                    "[ext-host] '%s' depende de '%s' que no esta presente\n",
                    c[i].m.id, c[i].m.deps[k]);
            return -2;
        }
        int rc =
            cand_load_rec(host, c, n, di, stack, depth + 1, loaded_count);
        if (rc != 0) return rc;
    }
    /* cargar esta extension */
    if (ext_host_load(host, c[i].dir) == 0) (*loaded_count)++;
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
        if (manifest_read(dirs[i], &m) == 0) {
            cands[ncand].dir = host_strdup(dirs[i]);
            cands[ncand].m = m; /* toma posesion de las cadenas del manifiesto */
            cands[ncand].loaded = 0;
            ncand++;
        }
        free(dirs[i]);
    }
    free(dirs);

    int loaded = 0;
    int result = 0;
    if (ncand > 0) {
        int *stack = (int *)malloc(ncand * sizeof(int));
        for (size_t i = 0; i < ncand; ++i) {
            int rc = cand_load_rec(host, cands, ncand, (int)i, stack, 0,
                                   &loaded);
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

    return result < 0 ? result : loaded;
}
