/**
 * @file lsp_highlight.c
 * @brief Resaltado de sintaxis para VARIOS lenguajes a partir de los semantic
 *        tokens de un language server real (uno por lenguaje, reutilizado
 *        mientras la extension esta cargada).
 *
 * Estrategia (pensada para no saturar el LSP en cada tecla):
 *   1. Un solo servidor por lenguaje (tabla LANGS), lanzado de forma perezosa
 *      la primera vez que se abre un archivo de esa extension.
 *   2. didOpen/didChange se envian con el texto COMPLETO (full-sync, como
 *      soporta lsp_client.h), pero NUNCA en cada evento BUFFER_CHANGED: se
 *      acumulan y se disparan tras un debounce corto medido en frames via
 *      register_tick (evita re-pedir semanticTokens/full en cada pulsacion).
 *   3. semanticTokens/full se pide solo cuando el debounce vence; la
 *      respuesta se decodifica (deltas LSP -> linea/columna absolutas) y se
 *      empuja con set_tokens linea a linea.  clear_tokens() se llama antes de
 *      repintar para no dejar tramos obsoletos de un recuento anterior.
 *   4. Las peticiones solo se aplican si el archivo objetivo sigue siendo el
 *      buffer activo cuando llega la respuesta (evita pintar sobre el archivo
 *      equivocado si el usuario cambio de pestana mientras el LSP respondia).
 *
 * El resaltado sincrono embebido (lang_c.c) sigue cubriendo C/C++ como
 * fallback inmediato; esta extension, al usar set_tokens, tiene PRIORIDAD en
 * cuanto el LSP responde (ver coffee_ext.h, seccion "Resaltado de sintaxis").
 *
 * Universalidad: la lista de servidores NO esta en este .c -- se carga de
 * "servers.json" (junto al DLL, mismo mecanismo que lang-basic/languages.json)
 * en coffee_extension_register.  Quien tenga un lenguaje propio con su propio
 * LSP solo tiene que anyadir un objeto {id, exts, server_exe, args,
 * language_id} a ese JSON: cero cambios de codigo, cero recompilacion.  El
 * unico requisito real es que el binario del servidor hable el protocolo LSP
 * estandar (initialize + textDocument/semanticTokens/full), que es justo lo
 * que implementa cualquier LSP "de verdad", inventado o no.
 */
#include "ext/coffee_ext.h"
#include "lsp_client.h"
#include "svc_lsp.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBOUNCE_FRAMES 20 /* ~0.3s a 60fps: suficiente para no pedir tokens
                               en cada tecla, sin notarse como "lag" */
#define MAX_SPANS_LINE  256

/* -------------------------------------------------------------------------
 * Definicion de lenguaje/servidor, CARGADA de servers.json.  Los punteros
 * apuntan directamente a las cadenas del arbol cJSON (g_root se mantiene
 * vivo mientras la extension este cargada), sin copias.
 * ---------------------------------------------------------------------- */
typedef struct {
    const char *id;
    const char **exts; int n_exts;
    const char *server_exe;
    const char **argv; int argc;   /* argumentos, sin argv[0] */
    const char *language_id;
} LangDef;

static LangDef *g_defs = NULL;
static int g_n_defs = 0;
static cJSON *g_root = NULL;

/* -------------------------------------------------------------------------
 * Estado por lenguaje (una sesion LSP viva mientras haya archivos de ese
 * lenguaje abiertos).  Un slot por entrada de servers.json.
 * ---------------------------------------------------------------------- */
typedef struct {
    const LangDef *def;
    CoffeeProc proc;
    LspClient *lsp;
    int ready;       /* 1 tras initialize+initialized                    */
    int doc_open;    /* 1 si ya se hizo didOpen del archivo activo actual */
    int version;     /* version de documento (LSP)                       */
    char uri[1024];  /* uri del archivo actualmente sincronizado         */
    int dirty_frames;/* >0 mientras el debounce esta corriendo           */
} LangSession;

static LangSession *g_sessions = NULL; /* g_n_defs slots, 1:1 con g_defs */
static const CoffeeApi *g_api;
static CoffeeHost *g_host;

/* -------------------------------------------------------------------------
 * Carga de servers.json (una vez, en el registro de la extension)
 * ---------------------------------------------------------------------- */
static const char **json_str_array(cJSON *arr, int *out_n) {
    int n = (arr && cJSON_IsArray(arr)) ? cJSON_GetArraySize(arr) : 0;
    const char **out = (const char **)malloc(sizeof(char *) * (size_t)(n + 1));
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        out[i] = cJSON_IsString(it) ? it->valuestring : "";
    }
    out[n] = NULL;
    if (out_n) *out_n = n;
    return out;
}

static int load_servers(CoffeeHost *host, const CoffeeApi *api) {
    const char *dir = api->ext_dir(host);
    if (!dir) {
        api->log(host, COFFEE_LOG_ERROR, "lsp-highlight: ext_dir() vacio");
        return 0;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/servers.json", dir);

    FILE *f = fopen(path, "rb");
    if (!f) {
        char msg[1100];
        snprintf(msg, sizeof msg, "lsp-highlight: no se pudo abrir '%s'", path);
        api->log(host, COFFEE_LOG_ERROR, msg);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    g_root = cJSON_Parse(buf);
    free(buf);
    if (!g_root || !cJSON_IsArray(g_root)) {
        api->log(host, COFFEE_LOG_ERROR, "lsp-highlight: servers.json invalido");
        if (g_root) cJSON_Delete(g_root);
        g_root = NULL;
        return 0;
    }

    g_n_defs = cJSON_GetArraySize(g_root);
    g_defs = (LangDef *)calloc((size_t)g_n_defs, sizeof(LangDef));
    g_sessions = (LangSession *)calloc((size_t)g_n_defs, sizeof(LangSession));
    if (!g_defs || !g_sessions) { g_n_defs = 0; return 0; }

    for (int i = 0; i < g_n_defs; i++) {
        cJSON *o = cJSON_GetArrayItem(g_root, i);
        LangDef *L = &g_defs[i];

        cJSON *id = cJSON_GetObjectItemCaseSensitive(o, "id");
        L->id = cJSON_IsString(id) ? id->valuestring : "?";

        L->exts = json_str_array(cJSON_GetObjectItemCaseSensitive(o, "exts"),
                                 &L->n_exts);
        L->argv = json_str_array(cJSON_GetObjectItemCaseSensitive(o, "args"),
                                 &L->argc);

        cJSON *exe = cJSON_GetObjectItemCaseSensitive(o, "server_exe");
        L->server_exe = cJSON_IsString(exe) ? exe->valuestring : "";

        cJSON *lid = cJSON_GetObjectItemCaseSensitive(o, "language_id");
        L->language_id = cJSON_IsString(lid) ? lid->valuestring : L->id;
    }
    return 1;
}

static void free_servers(void) {
    for (int i = 0; i < g_n_defs; i++) {
        free((void *)g_defs[i].exts);
        free((void *)g_defs[i].argv);
    }
    free(g_defs); g_defs = NULL;
    free(g_sessions); g_sessions = NULL;
    g_n_defs = 0;
    if (g_root) cJSON_Delete(g_root);
    g_root = NULL;
}

/* -------------------------------------------------------------------------
 * Utilidades
 * ---------------------------------------------------------------------- */
static const LangDef *lang_for_path(const char *path) {
    if (!path) return NULL;
    const char *dot = strrchr(path, '.');
    if (!dot) return NULL;
    for (int i = 0; i < g_n_defs; i++)
        for (int e = 0; e < g_defs[i].n_exts; e++)
            if (strcmp(dot, g_defs[i].exts[e]) == 0) return &g_defs[i];
    return NULL;
}

static LangSession *session_for(const LangDef *def) {
    if (!def) return NULL;
    return &g_sessions[def - g_defs]; /* 1 slot por indice, sin busqueda */
}

static void path_to_uri(const char *path, char *out, size_t cap) {
    /* Conversion simple file:// -- suficiente para rutas absolutas sin
     * espacios/caracteres especiales; produccion real deberia % -escapar. */
    snprintf(out, cap, "file://%s%s", path[0] == '/' ? "" : "/", path);
}

/* Mapea el NOMBRE (LSP) del tipo de semantic token a un color RGBA.  Cada
 * lenguaje puede tener su propia leyenda; por eso se resuelve por NOMBRE
 * (via lsp_semantic_legend), no por indice fijo. */
static CoffeeColor color_for_type(const char *name) {
    if (!name) return (CoffeeColor){212, 212, 212, 255};
    if (!strcmp(name, "keyword") || !strcmp(name, "modifier"))
        return (CoffeeColor){197, 134, 192, 255};
    if (!strcmp(name, "type") || !strcmp(name, "class") ||
        !strcmp(name, "struct") || !strcmp(name, "enum") ||
        !strcmp(name, "interface") || !strcmp(name, "typeParameter"))
        return (CoffeeColor){78, 201, 176, 255};
    if (!strcmp(name, "string")) return (CoffeeColor){206, 145, 120, 255};
    if (!strcmp(name, "number")) return (CoffeeColor){181, 206, 168, 255};
    if (!strcmp(name, "comment")) return (CoffeeColor){106, 153, 85, 255};
    if (!strcmp(name, "function") || !strcmp(name, "method"))
        return (CoffeeColor){220, 220, 170, 255};
    if (!strcmp(name, "macro") || !strcmp(name, "namespace"))
        return (CoffeeColor){78, 201, 176, 255};
    if (!strcmp(name, "variable") || !strcmp(name, "parameter") ||
        !strcmp(name, "property"))
        return (CoffeeColor){156, 220, 254, 255};
    return (CoffeeColor){212, 212, 212, 255};
}

/* -------------------------------------------------------------------------
 * Transporte: LspClient <-> proc_write / proc_on_data del core
 * ---------------------------------------------------------------------- */
static int lsp_write_cb(void *ud, const void *bytes, size_t len) {
    LangSession *s = (LangSession *)ud;
    return g_api->proc_write(g_host, s->proc, bytes, len);
}
static void lsp_data_cb(void *ud, const char *bytes, size_t len) {
    LangSession *s = (LangSession *)ud;
    lsp_feed(s->lsp, bytes, len);
}
static void lsp_exit_cb(void *ud, int exit_code) {
    LangSession *s = (LangSession *)ud;
    char msg[256];
    snprintf(msg, sizeof msg, "lsp-highlight: %s termino (exit=%d)",
             s->def->id, exit_code);
    g_api->log(g_host, COFFEE_LOG_WARN, msg);
    s->ready = 0;
    s->doc_open = 0;
    s->lsp = NULL; /* el proc ya no es valido; se relanzara en el proximo uso */
}

/* -------------------------------------------------------------------------
 * Peticion de semantic tokens + push como spans
 * ---------------------------------------------------------------------- */
static void on_semantic_tokens(void *ud, cJSON *result, cJSON *error) {
    LangSession *s = (LangSession *)ud;
    if (error || !result) return;
    /* Si el usuario ya cambio de archivo, no pintar sobre el buffer activo
     * actual con datos de otro archivo. */
    const char *cur = g_api->current_path(g_host);
    char cur_uri[1024];
    if (cur) path_to_uri(cur, cur_uri, sizeof cur_uri);
    if (!cur || strcmp(cur_uri, s->uri) != 0) return;

    cJSON *data = cJSON_GetObjectItemCaseSensitive(result, "data");
    if (!cJSON_IsArray(data)) return;

    int n_legend = 0;
    const char *const *legend = lsp_semantic_legend(s->lsp, &n_legend);

    g_api->clear_tokens(g_host); /* fuera los tramos del recuento anterior */

    CoffeeSpan buf[MAX_SPANS_LINE];
    int bufn = 0, cur_line = -1;
    int line = 0, col = 0;
    int n = cJSON_GetArraySize(data) / 5;

    for (int i = 0; i < n; i++) {
        int dl = (int)cJSON_GetArrayItem(data, i * 5 + 0)->valuedouble;
        int dc = (int)cJSON_GetArrayItem(data, i * 5 + 1)->valuedouble;
        int len = (int)cJSON_GetArrayItem(data, i * 5 + 2)->valuedouble;
        int ttype = (int)cJSON_GetArrayItem(data, i * 5 + 3)->valuedouble;
        if (dl > 0) { line += dl; col = dc; } else { col += dc; }

        if (cur_line != line) {
            if (cur_line >= 0 && bufn > 0)
                g_api->set_tokens(g_host, (uint32_t)cur_line, buf, bufn);
            cur_line = line;
            bufn = 0;
        }
        if (bufn < MAX_SPANS_LINE) {
            const char *tname =
                (ttype >= 0 && ttype < n_legend) ? legend[ttype] : NULL;
            buf[bufn].start_col = (uint32_t)col;
            buf[bufn].len = (uint32_t)len;
            buf[bufn].color = color_for_type(tname);
            bufn++;
        }
    }
    if (cur_line >= 0 && bufn > 0)
        g_api->set_tokens(g_host, (uint32_t)cur_line, buf, bufn);
}

static void request_tokens(LangSession *s) {
    if (!s->ready || !s->doc_open) return;
    lsp_semantic_tokens_full(s->lsp, s->uri, on_semantic_tokens, s);
}

/* Envia el texto COMPLETO del buffer activo (didOpen la 1a vez, didChange
 * despues) y encadena la peticion de semantic tokens. */
static void sync_and_request(LangSession *s, const char *path) {
    size_t n_chars = g_api->buffer_length(g_host);
    size_t cap = n_chars * 4 + 16; /* margen para UTF-8 */
    char *text = (char *)malloc(cap);
    if (!text) return;
    size_t written = g_api->buffer_get_text(g_host, 0, n_chars, text, cap);
    text[written] = '\0';

    path_to_uri(path, s->uri, sizeof s->uri);
    if (!s->doc_open) {
        s->version = 1;
        lsp_did_open(s->lsp, s->uri, s->def->language_id, s->version, text);
        s->doc_open = 1;
    } else {
        s->version++;
        lsp_did_change(s->lsp, s->uri, s->version, text);
    }
    free(text);
    request_tokens(s);
}

static void on_lsp_ready(void *ud) {
    LangSession *s = (LangSession *)ud;
    s->ready = 1;
    const char *path = g_api->current_path(g_host);
    if (path && lang_for_path(path) == s->def) sync_and_request(s, path);
}

static LangSession *ensure_session(const LangDef *def) {
    LangSession *s = session_for(def);
    if (!s) return NULL;
    if (s->lsp) return s; /* ya lanzada (viva o reiniciando) */

    s->proc = g_api->proc_spawn(g_host, def->server_exe, def->argv, def->argc);
    if (!s->proc) {
        char msg[256];
        snprintf(msg, sizeof msg,
                 "lsp-highlight: no se pudo lanzar '%s' (%s no esta en PATH?)",
                 def->id, def->server_exe);
        g_api->log(g_host, COFFEE_LOG_WARN, msg);
        return NULL;
    }
    s->lsp = lsp_create(lsp_write_cb, s);
    g_api->proc_on_data(g_host, s->proc, lsp_data_cb, s);
    g_api->proc_on_exit(g_host, s->proc, lsp_exit_cb, s);

    const char *root = g_api->workspace_root(g_host);
    char root_uri[1024] = {0};
    if (root) path_to_uri(root, root_uri, sizeof root_uri);
    lsp_initialize(s->lsp, root[0] ? root_uri : NULL, on_lsp_ready, s);
    return s;
}

/* -------------------------------------------------------------------------
 * Eventos del IDE
 * ---------------------------------------------------------------------- */
static void handle_active_file(const char *path) {
    const LangDef *def = lang_for_path(path);
    if (!def) return;
    LangSession *s = ensure_session(def);
    if (!s) return;
    if (s->ready) sync_and_request(s, path);
    /* si aun no esta ready, on_lsp_ready hace la 1a sincronizacion */
}

static void on_file_open(CoffeeHost *h, CoffeeEventType ev, const void *data,
                         void *ud) {
    (void)h; (void)ev; (void)ud;
    handle_active_file((const char *)data);
}

static void on_tab_switch(CoffeeHost *h, CoffeeEventType ev, const void *data,
                          void *ud) {
    (void)h; (void)ev; (void)data; (void)ud;
    const char *path = g_api->current_path(g_host);
    if (path) handle_active_file(path);
}

/* BUFFER_CHANGED llega en CADA edicion: NO se reenvia al LSP aqui.  Solo se
 * arma el debounce; register_tick_cb es quien de verdad sincroniza. */
static void on_buffer_changed(CoffeeHost *h, CoffeeEventType ev,
                              const void *data, void *ud) {
    (void)h; (void)ev; (void)data; (void)ud;
    const char *path = g_api->current_path(g_host);
    const LangDef *def = lang_for_path(path);
    if (!def) return;
    LangSession *s = session_for(def);
    if (s) s->dirty_frames = DEBOUNCE_FRAMES;
}

static void on_tick(void *ud) {
    (void)ud;
    for (int i = 0; i < g_n_defs; i++) {
        LangSession *s = &g_sessions[i];
        if (s->def && s->dirty_frames > 0) {
            if (--s->dirty_frames == 0) {
                const char *path = g_api->current_path(g_host);
                if (path && lang_for_path(path) == s->def && s->ready)
                    sync_and_request(s, path);
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Servicio compartido (coffee.svc.lsp): permite que OTRA extension (p.ej. un
 * panel de "problems" o go-to-definition) reutilice estas mismas conexiones
 * LSP en vez de lanzar sus propios servidores.
 * ---------------------------------------------------------------------- */
static int svc_is_ready(void *self) {
    LangSession *s = (LangSession *)self;
    return s && s->ready;
}
static int svc_request(void *self, const char *method, const char *params_json,
                       char **out_json) {
    (void)self; (void)method; (void)params_json;
    if (out_json) *out_json = NULL;
    return -1; /* generico no implementado en esta v0.1; ver lsp_client.h
                  para anyadir jsonrpc_request(...) directo si hace falta */
}
static const char *svc_server_path(void *self) {
    LangSession *s = (LangSession *)self;
    return s && s->def ? s->def->server_exe : NULL;
}
static CoffeeSvcLsp g_svc = {COFFEE_SVC_LSP_VERSION, svc_is_ready, svc_request,
                             svc_server_path, NULL};

/* -------------------------------------------------------------------------
 * Entrada / salida de la extension
 * ---------------------------------------------------------------------- */
COFFEE_EXTENSION_EXPORT
int coffee_extension_register(CoffeeHost *host, const CoffeeApi *api) {
    g_host = host;
    g_api = api;

    if (!api->register_highlighter || !api->set_tokens || !api->proc_spawn ||
        !api->register_tick) {
        api->log(host, COFFEE_LOG_ERROR,
                 "lsp-highlight: host sin ABI v4 (resaltado) o v3 (procesos)");
        return 1;
    }
    if (!load_servers(host, api)) return 1; /* servers.json ausente/invalido */

    api->subscribe_event(host, COFFEE_EVENT_FILE_OPEN, on_file_open, NULL);
    api->subscribe_event(host, COFFEE_EVENT_TAB_SWITCH, on_tab_switch, NULL);
    api->subscribe_event(host, COFFEE_EVENT_BUFFER_CHANGED, on_buffer_changed,
                         NULL);
    api->register_tick(host, on_tick, NULL);

    /* Publica un servicio por sesion activa bajo un nombre unico por lenguaje
     * (coffee.svc.lsp es el nombre "generico"; aqui basta registrar el del
     * primer lenguaje activo para el caso comun de un solo lenguaje). */
    if (g_n_defs > 0) {
        g_svc.self = &g_sessions[0];
        api->register_service(host, COFFEE_SVC_LSP_NAME, &g_svc);
    }

    char msg[128];
    snprintf(msg, sizeof msg,
             "lsp-highlight: listo (%d servidores desde servers.json)",
             g_n_defs);
    api->log(host, COFFEE_LOG_INFO, msg);
    return 0;
}

COFFEE_EXTENSION_EXPORT
void coffee_extension_unregister(CoffeeHost *host) {
    for (int i = 0; i < g_n_defs; i++) {
        LangSession *s = &g_sessions[i];
        if (s->lsp) lsp_shutdown(s->lsp);
        if (s->lsp) lsp_destroy(s->lsp);
        if (s->proc) g_api->proc_kill(host, s->proc);
    }
    free_servers();
}
