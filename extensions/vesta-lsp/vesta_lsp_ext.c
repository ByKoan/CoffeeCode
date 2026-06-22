/**
 * @file vesta_lsp_ext.c
 * @brief Extension de CoffeeCode: cliente del language server de Vesta (Vex).
 *
 * Arranca el servidor LSP de Vesta (vesta_lsp.exe) como subproceso del core,
 * sincroniza el documento `.vex` activo (didOpen / didChange con debounce /
 * didSave / didClose) y MUESTRA los diagnosticos de compilacion en vivo:
 *
 *   - Marcadores en el gutter de cada linea con error/aviso (x / !).
 *   - Fondo tenue de la linea afectada.
 *   - Una lista en el canal "Problemas" del panel inferior (con color ANSI).
 *
 * Ademas publica el servicio "coffee.svc.lsp" (svc_lsp.h) para que OTRAS
 * extensiones (p.ej. la extension `vesta`) consulten el estado del servidor o
 * lancen peticiones sin arrancar su propio language server.
 *
 * Convencion de lineas: las posiciones LSP (range.start.line) son 0-based y las
 * decoraciones del IDE (set_gutter_marker / set_line_background) tambien usan
 * lineas 0-based (ver render_ext.c: li = scroll_line + vi).  Por tanto NO hay
 * conversion: la linea del diagnostico se usa tal cual.
 *
 * Solo enlaza el header de la API del IDE (coffee_ext.h) y el SDK comun
 * (lsp_client.h / svc_lsp.h).  El transporte se cablea al facility de
 * subprocesos del core (proc_spawn / proc_write / proc_on_data / proc_on_exit).
 * Si el servidor no arranca, la extension lo reporta y queda inactiva sin
 * tumbar el IDE.
 */
#include "ext/coffee_ext.h"

#include "lsp_client.h"
#include "svc_lsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Ruta por defecto del servidor LSP de Vesta (compilado en el repo VestaVM).
 * El usuario la puede cambiar con el config "server_path" o la env var
 * VESTA_LSP_PATH. */
#define VESTA_LSP_DEFAULT_PATH "F:/C/VM/cmake-build-debug/vesta_lsp.exe"

/* Identificadores de los canales del panel inferior. */
#define VESTA_LSP_CHAN_LOG  "vesta-lsp"
#define VESTA_LSP_CHAN_PROB "vesta-problems"

/* Tics de inactividad antes de mandar el didChange acumulado (~250 ms a 60
 * FPS = 15 frames).  El tick corre una vez por frame en el hilo principal;
 * usamos un contador de frames en lugar de un reloj para no depender de
 * primitivas de tiempo no expuestas por la API. */
#define VESTA_LSP_DEBOUNCE_FRAMES 15

/* ------------------------------------------------------------------------- */
/* Estado global de la extension.                                            */
/* ------------------------------------------------------------------------- */

/** Diagnosticos guardados por-documento (para re-aplicarlos al cambiar tab). */
typedef struct VlDoc {
    char *uri;            /**< file-URI del documento (heap). */
    char *path;           /**< ruta nativa del documento (heap). */
    cJSON *diagnostics;   /**< copia (cJSON_Duplicate) del array, o NULL. */
    int open;             /**< 1 si ya se envio didOpen al servidor. */
    int pending_open;     /**< 1 si hay que enviar didOpen al estar listo. */
    int version;          /**< version del documento para didChange. */
    struct VlDoc *next;
} VlDoc;

typedef struct VlState {
    CoffeeHost *host;
    const CoffeeApi *api;

    CoffeeProc proc;      /**< subproceso del servidor (o NULL si no arranco). */
    LspClient *lsp;       /**< cliente LSP sobre el transporte del subproceso. */
    char *server_path;    /**< ruta resuelta del ejecutable (heap). */

    int server_alive;     /**< 1 mientras el subproceso vive. */
    int ready;            /**< 1 tras completar initialize + initialized. */

    VlDoc *docs;          /**< lista de documentos conocidos. */
    VlDoc *last_open;     /**< ultimo doc que el IDE notifico abrir/activar. */

    /* Debounce del didChange: el activo "sucio" + cuenta atras en frames. */
    int dirty;            /**< hay un cambio pendiente de enviar. */
    int dirty_frames;     /**< frames restantes antes de enviar. */

    CoffeeSvcLsp svc;     /**< vtable del servicio publicado. */
} VlState;

/* Singleton: el core carga una sola instancia de la extension. */
static VlState g_state;

/* ------------------------------------------------------------------------- */
/* Utilidades.                                                               */
/* ------------------------------------------------------------------------- */

/** strdup local (evita depender de extensiones POSIX del CRT). */
static char *vl_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/** 1 si @p path termina en ".vex" (case-insensitive en la extension). */
static int vl_is_vex(const char *path) {
    if (!path) return 0;
    size_t n = strlen(path);
    if (n < 4) return 0;
    const char *e = path + n - 4;
    return (e[0] == '.' &&
            (e[1] == 'v' || e[1] == 'V') &&
            (e[2] == 'e' || e[2] == 'E') &&
            (e[3] == 'x' || e[3] == 'X'));
}

/**
 * @brief Construye un file-URI a partir de una ruta nativa.
 *
 * Normaliza separadores ('\\' -> '/'), antepone "file:///" en Windows (rutas
 * con unidad tipo C:/...) o "file://" en POSIX (rutas absolutas que empiezan
 * por '/'), y percent-encodea los bytes que no son seguros en una URI.  El
 * resultado se reserva en heap (liberar con free), o NULL si falla.
 */
static char *vl_path_to_uri(const char *path) {
    if (!path || !path[0]) return NULL;

    /* Prefijo: en Windows una ruta con unidad "C:/..." -> "file:///C:/...".
     * En POSIX una ruta absoluta "/x" -> "file:///x".  En ambos casos basta
     * con "file:///" y dejar la ruta normalizada detras (la unidad o la barra
     * inicial encajan). */
    const char *prefix = "file:///";
    size_t plen = strlen(prefix);

    /* Reserva pesimista: cada byte puede expandir a "%XX" (3 chars). */
    size_t in_len = strlen(path);
    char *out = (char *)malloc(plen + in_len * 3 + 1);
    if (!out) return NULL;

    memcpy(out, prefix, plen);
    size_t o = plen;

    /* Si la ruta ya empieza por '/', no dupliquemos la barra del prefijo. */
    size_t start = 0;
    if (path[0] == '/' || path[0] == '\\') start = 1;

    static const char *hex = "0123456789ABCDEF";
    for (size_t i = start; i < in_len; ++i) {
        unsigned char ch = (unsigned char)path[i];
        if (ch == '\\') ch = '/'; /* normalizar separador */
        /* Caracteres seguros en una URI de archivo: alfanumericos, unos pocos
         * simbolos, la barra y los dos puntos de la unidad. */
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '/' || ch == ':' ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out[o++] = (char)ch;
        } else {
            out[o++] = '%';
            out[o++] = hex[(ch >> 4) & 0xF];
            out[o++] = hex[ch & 0xF];
        }
    }
    out[o] = '\0';
    return out;
}

/** Busca el documento por ruta nativa (NULL si no existe). */
static VlDoc *vl_find_doc_by_path(VlState *st, const char *path) {
    if (!path) return NULL;
    for (VlDoc *d = st->docs; d; d = d->next)
        if (d->path && strcmp(d->path, path) == 0) return d;
    return NULL;
}

/** Busca el documento por URI (NULL si no existe). */
static VlDoc *vl_find_doc_by_uri(VlState *st, const char *uri) {
    if (!uri) return NULL;
    for (VlDoc *d = st->docs; d; d = d->next)
        if (d->uri && strcmp(d->uri, uri) == 0) return d;
    return NULL;
}

/** Crea (o devuelve) el documento para @p path, generando su URI. */
static VlDoc *vl_get_or_make_doc(VlState *st, const char *path) {
    VlDoc *d = vl_find_doc_by_path(st, path);
    if (d) return d;
    d = (VlDoc *)calloc(1, sizeof(VlDoc));
    if (!d) return NULL;
    d->path = vl_strdup(path);
    d->uri = vl_path_to_uri(path);
    if (!d->path || !d->uri) {
        free(d->path);
        free(d->uri);
        free(d);
        return NULL;
    }
    d->version = 1;
    d->next = st->docs;
    st->docs = d;
    return d;
}

/** Lee el texto completo del buffer activo (heap, NUL-terminado), o NULL. */
static char *vl_read_active_buffer(VlState *st) {
    const CoffeeApi *api = st->api;
    size_t len = api->buffer_length(st->host);
    char *text = (char *)malloc(len + 1);
    if (!text) return NULL;
    size_t got = api->buffer_get_text(st->host, 0, len, text, len + 1);
    text[got] = '\0';
    return text;
}

/* ------------------------------------------------------------------------- */
/* Transporte: cablea el cliente LSP al subproceso del core.                 */
/* ------------------------------------------------------------------------- */

/** write_fn del cliente LSP: vuelca los bytes al stdin del subproceso. */
static int vl_lsp_write(void *ud, const void *bytes, size_t len) {
    VlState *st = (VlState *)ud;
    if (!st || !st->proc) return -1;
    return st->api->proc_write(st->host, st->proc, bytes, len);
}

/** proc_on_data: alimenta el stdout del servidor al parser LSP. */
static void vl_on_proc_data(void *ud, const char *bytes, size_t len) {
    VlState *st = (VlState *)ud;
    if (st && st->lsp) lsp_feed(st->lsp, bytes, len);
}

/** proc_on_exit: marca el servidor caido y avisa por estado/canal. */
static void vl_on_proc_exit(void *ud, int exit_code) {
    VlState *st = (VlState *)ud;
    if (!st) return;
    st->server_alive = 0;
    st->ready = 0;
    char line[128];
    snprintf(line, sizeof line,
             "\x1b[31m[vesta-lsp] el servidor termino (codigo %d)\x1b[0m\n",
             exit_code);
    st->api->channel_append(st->host, VESTA_LSP_CHAN_LOG, line);
    st->api->set_status(st->host, "vesta-lsp: servidor detenido");
}

/* ------------------------------------------------------------------------- */
/* Diagnosticos: gutter + fondo de linea + canal "Problemas".                */
/* ------------------------------------------------------------------------- */

/** Color del marcador segun severidad LSP (1=Error, 2=Warning, otros=info). */
static CoffeeColor vl_sev_color(int sev) {
    CoffeeColor c;
    if (sev == 1) { /* Error: rojo */
        c.r = 220; c.g = 60; c.b = 60; c.a = 255;
    } else if (sev == 2) { /* Warning: amarillo */
        c.r = 210; c.g = 180; c.b = 40; c.a = 255;
    } else { /* Info/Hint: azul tenue */
        c.r = 90; c.g = 150; c.b = 220; c.a = 255;
    }
    return c;
}

/** Fondo tenue de la linea afectada (mismo tono, alfa bajo). */
static CoffeeColor vl_sev_bg(int sev) {
    CoffeeColor c = vl_sev_color(sev);
    c.a = 38; /* banda apenas visible para no tapar el texto */
    return c;
}

/** Codigo SGR ANSI por severidad para el canal "Problemas". */
static const char *vl_sev_ansi(int sev) {
    if (sev == 1) return "\x1b[31m"; /* rojo */
    if (sev == 2) return "\x1b[33m"; /* amarillo */
    return "\x1b[36m";               /* cyan */
}

/** Texto de severidad para la linea del canal. */
static const char *vl_sev_text(int sev) {
    if (sev == 1) return "error";
    if (sev == 2) return "aviso";
    if (sev == 3) return "info";
    return "hint";
}

/**
 * @brief Aplica las decoraciones (gutter + fondo) de un array de diagnosticos.
 *
 * Asume que el buffer activo es el documento de esos diagnosticos.  El llamante
 * ya limpio las decoraciones previas con clear_decorations().
 */
static void vl_apply_decorations(VlState *st, cJSON *diags) {
    const CoffeeApi *api = st->api;
    if (!diags) return;
    cJSON *d = NULL;
    cJSON_ArrayForEach(d, diags) {
        cJSON *range = cJSON_GetObjectItemCaseSensitive(d, "range");
        cJSON *start = range
                           ? cJSON_GetObjectItemCaseSensitive(range, "start")
                           : NULL;
        cJSON *jline = start
                           ? cJSON_GetObjectItemCaseSensitive(start, "line")
                           : NULL;
        if (!cJSON_IsNumber(jline)) continue;
        int line = (int)jline->valuedouble; /* 0-based (LSP == gutter) */
        if (line < 0) continue;

        cJSON *jsev = cJSON_GetObjectItemCaseSensitive(d, "severity");
        int sev = cJSON_IsNumber(jsev) ? (int)jsev->valuedouble : 1;

        const char *glyph = (sev == 1) ? "x" : "!";
        api->set_gutter_marker(st->host, (size_t)line, glyph,
                               vl_sev_color(sev));
        api->set_line_background(st->host, (size_t)line, vl_sev_bg(sev));
    }
}

/**
 * @brief Refresca el canal "Problemas" con todos los diagnosticos de @p doc.
 *
 * Limpia el canal y vuelca una linea por diagnostico con el formato
 * "ruta:linea:col: severidad: mensaje" (1-based en el texto para que coincida
 * con lo que el usuario ve numerado en el editor).
 */
static void vl_refresh_problems_channel(VlState *st, VlDoc *doc) {
    const CoffeeApi *api = st->api;
    api->channel_clear(st->host, VESTA_LSP_CHAN_PROB);

    if (!doc || !doc->diagnostics ||
        cJSON_GetArraySize(doc->diagnostics) == 0) {
        api->channel_append(st->host, VESTA_LSP_CHAN_PROB,
                            "\x1b[32m(sin problemas)\x1b[0m\n");
        api->set_status(st->host, "vesta-lsp: sin problemas");
        return;
    }

    int count = 0;
    cJSON *d = NULL;
    cJSON_ArrayForEach(d, doc->diagnostics) {
        cJSON *range = cJSON_GetObjectItemCaseSensitive(d, "range");
        cJSON *start = range
                           ? cJSON_GetObjectItemCaseSensitive(range, "start")
                           : NULL;
        int line = 0, col = 0;
        if (start) {
            cJSON *jl = cJSON_GetObjectItemCaseSensitive(start, "line");
            cJSON *jc = cJSON_GetObjectItemCaseSensitive(start, "character");
            if (cJSON_IsNumber(jl)) line = (int)jl->valuedouble;
            if (cJSON_IsNumber(jc)) col = (int)jc->valuedouble;
        }
        cJSON *jsev = cJSON_GetObjectItemCaseSensitive(d, "severity");
        int sev = cJSON_IsNumber(jsev) ? (int)jsev->valuedouble : 1;
        cJSON *jmsg = cJSON_GetObjectItemCaseSensitive(d, "message");
        const char *msg = cJSON_IsString(jmsg) ? jmsg->valuestring : "";

        /* Construir la linea: ruta:linea:col: severidad: mensaje (1-based). */
        char buf[1280];
        snprintf(buf, sizeof buf, "%s%s:%d:%d: %s: %s\x1b[0m\n",
                 vl_sev_ansi(sev), doc->path ? doc->path : "?",
                 line + 1, col + 1, vl_sev_text(sev), msg);
        api->channel_append(st->host, VESTA_LSP_CHAN_PROB, buf);
        count++;
    }

    char status[96];
    snprintf(status, sizeof status, "vesta-lsp: %d problema%s", count,
             count == 1 ? "" : "s");
    api->set_status(st->host, status);
}

/** Re-aplica las decoraciones guardadas del documento ACTIVO (tras cambiar tab). */
static void vl_reapply_active(VlState *st) {
    const CoffeeApi *api = st->api;
    /* current_path() es valido solo hasta la siguiente llamada al CoffeeApi:
     * resolvemos el doc ANTES de cualquier otra llamada (clear_decorations). */
    const char *path = api->current_path(st->host);
    int is_vex = (path && vl_is_vex(path));
    VlDoc *doc = is_vex ? vl_find_doc_by_path(st, path) : NULL;

    if (!is_vex) {
        /* Documento no-.vex activo: el panel queda como estaba. */
        return;
    }
    api->clear_decorations(st->host);
    if (doc) {
        vl_apply_decorations(st, doc->diagnostics);
        vl_refresh_problems_channel(st, doc);
    } else {
        api->channel_clear(st->host, VESTA_LSP_CHAN_PROB);
    }
}

/**
 * @brief Handler de textDocument/publishDiagnostics.
 *
 * Guarda una copia de los diagnosticos por-uri y, si corresponden al buffer
 * activo, re-pinta el gutter/fondo + el canal "Problemas".
 */
static void vl_on_diagnostics(void *ud, const char *uri, cJSON *diagnostics) {
    VlState *st = (VlState *)ud;
    if (!st || !uri) return;

    VlDoc *doc = vl_find_doc_by_uri(st, uri);
    if (!doc) {
        /* Diagnosticos de un documento que aun no registramos: lo ignoramos
         * (solo gestionamos los que abrimos nosotros). */
        return;
    }

    /* Reemplazar la copia guardada. */
    if (doc->diagnostics) {
        cJSON_Delete(doc->diagnostics);
        doc->diagnostics = NULL;
    }
    if (diagnostics) doc->diagnostics = cJSON_Duplicate(diagnostics, 1);

    /* Si es el documento activo, refrescar la vista. */
    const char *active = st->api->current_path(st->host);
    if (active && doc->path && strcmp(active, doc->path) == 0) {
        st->api->clear_decorations(st->host);
        vl_apply_decorations(st, doc->diagnostics);
        vl_refresh_problems_channel(st, doc);
    }
}

/* ------------------------------------------------------------------------- */
/* Sincronizacion de documentos.                                             */
/* ------------------------------------------------------------------------- */

/**
 * @brief Envia didOpen del documento ACTIVO si coincide con @p doc.
 *
 * Solo el buffer activo es legible (buffer_get_text opera sobre el activo).  Si
 * @p doc es el documento del buffer en pantalla, lee su texto y manda didOpen;
 * en caso contrario marca @p doc como pendiente para abrirlo cuando vuelva a
 * estar activo.  Requiere que el servidor este listo.
 */
static void vl_send_did_open(VlState *st, VlDoc *doc) {
    if (!st->ready || !st->lsp || !doc || doc->open) return;
    const char *active = st->api->current_path(st->host);
    /* Es el documento del buffer en pantalla cuando current_path lo confirma o,
     * mientras current_path aun no esta poblado (apertura via CLI), cuando es el
     * ultimo documento que el IDE nos notifico abrir (st->last_open). */
    int is_active = (active && doc->path && strcmp(active, doc->path) == 0) ||
                    (!active && st->last_open == doc);
    if (is_active) {
        char *text = vl_read_active_buffer(st);
        lsp_did_open(st->lsp, doc->uri, "vex", doc->version, text ? text : "");
        doc->open = 1;
        doc->pending_open = 0;
        free(text);
    } else {
        /* No es el buffer en pantalla: abrir cuando vuelva a ser el activo. */
        doc->pending_open = 1;
    }
}

/** on_ready del LSP: marca listo y abre los documentos .vex pendientes. */
static void vl_on_ready(void *ud) {
    VlState *st = (VlState *)ud;
    if (!st) return;
    st->ready = 1;
    st->api->channel_append(st->host, VESTA_LSP_CHAN_LOG,
                            "\x1b[32m[vesta-lsp] servidor listo (initialize ok)"
                            "\x1b[0m\n");
    st->api->set_status(st->host, "vesta-lsp: servidor listo");

    /* Abrir los documentos ya conocidos (registrados antes de estar listo). */
    for (VlDoc *d = st->docs; d; d = d->next)
        if (!d->open) vl_send_did_open(st, d);

    /* Y, por si el activo aun no se registro, intentarlo ahora. */
    const char *path = st->api->current_path(st->host);
    if (path && vl_is_vex(path)) {
        VlDoc *doc = vl_get_or_make_doc(st, path);
        if (doc) vl_send_did_open(st, doc);
    }
}

/**
 * @brief Atiende un documento .vex que pasa a ser el activo.
 *
 * Registra el documento (aunque el servidor no este listo todavia) y, si lo
 * esta, manda didOpen o re-aplica sus diagnosticos guardados.  @p path puede
 * venir del evento FILE_OPEN (su data) o de current_path().
 */
static void vl_attend_path(VlState *st, const char *path) {
    if (!path || !vl_is_vex(path)) return;
    VlDoc *doc = vl_get_or_make_doc(st, path);
    if (!doc) return;
    st->last_open = doc; /* el activo segun el IDE (aunque current_path lagee) */
    if (!st->ready) {
        /* Aun sin servidor: el doc queda registrado; on_ready lo abrira. */
        return;
    }
    if (!doc->open) {
        vl_send_did_open(st, doc);
    } else {
        /* Ya abierto: re-aplicar sus diagnosticos guardados a la vista. */
        vl_reapply_active(st);
    }
}

/** Atiende el documento ACTIVO (si es .vex). */
static void vl_open_active(VlState *st) {
    vl_attend_path(st, st->api->current_path(st->host));
}

/** Programa el envio de un didChange tras el periodo de debounce. */
static void vl_mark_dirty(VlState *st) {
    st->dirty = 1;
    st->dirty_frames = VESTA_LSP_DEBOUNCE_FRAMES;
}

/** Envia ahora el didChange con el texto completo del buffer activo. */
static void vl_flush_change(VlState *st) {
    st->dirty = 0;
    if (!st->ready || !st->lsp) return;
    const char *path = st->api->current_path(st->host);
    if (!path || !vl_is_vex(path)) return;
    VlDoc *doc = vl_get_or_make_doc(st, path);
    if (!doc) return;
    if (!doc->open) {
        /* Cambio antes del didOpen: abrir con el texto actual. */
        vl_open_active(st);
        return;
    }
    char *text = vl_read_active_buffer(st);
    doc->version++;
    lsp_did_change(st->lsp, doc->uri, doc->version, text ? text : "");
    free(text);
}

/* ------------------------------------------------------------------------- */
/* Eventos del IDE.                                                          */
/* ------------------------------------------------------------------------- */

static void vl_on_event(CoffeeHost *host, CoffeeEventType ev, const void *data,
                        void *ud) {
    (void)host;
    VlState *st = (VlState *)ud;
    if (!st) return;

    switch (ev) {
    case COFFEE_EVENT_FILE_OPEN: {
        /* FILE_OPEN entrega la ruta en data; current_path() puede no estar aun
         * poblado en este instante.  Registramos el doc (lo abrira on_ready si
         * el servidor no esta listo todavia) y re-aplicamos su vista. */
        const char *path = (const char *)data;
        if (!path) path = st->api->current_path(st->host);
        vl_attend_path(st, path);
        vl_reapply_active(st);
        break;
    }

    case COFFEE_EVENT_TAB_SWITCH:
        /* Cambio de pestana: abrir si es .vex nuevo, o re-aplicar sus
         * diagnosticos guardados; si no es .vex, limpiar la vista. */
        vl_open_active(st);
        vl_reapply_active(st);
        break;

    case COFFEE_EVENT_BUFFER_CHANGED:
        /* Cambio en el buffer: programar didChange con debounce. */
        if (st->server_alive) {
            const char *path = st->api->current_path(st->host);
            if (path && vl_is_vex(path)) vl_mark_dirty(st);
        }
        break;

    case COFFEE_EVENT_FILE_SAVE: {
        const char *path = (const char *)data;
        if (!path) path = st->api->current_path(st->host);
        if (st->server_alive && path && vl_is_vex(path)) {
            VlDoc *doc = vl_find_doc_by_path(st, path);
            if (doc && doc->open) lsp_did_save(st->lsp, doc->uri);
        }
        break;
    }

    case COFFEE_EVENT_SHUTDOWN:
        /* Cierre del IDE: apagar el servidor limpiamente. */
        if (st->lsp && st->server_alive) lsp_shutdown(st->lsp);
        if (st->proc) {
            st->api->proc_kill(st->host, st->proc);
            st->proc = NULL;
        }
        st->server_alive = 0;
        st->ready = 0;
        break;

    default:
        break;
    }
}

/** Tick por frame: cuenta atras del debounce y envia el didChange acumulado. */
static void vl_on_tick(void *ud) {
    VlState *st = (VlState *)ud;
    if (!st || !st->dirty) return;
    if (st->dirty_frames > 0) {
        st->dirty_frames--;
        return;
    }
    vl_flush_change(st);
}

/* ------------------------------------------------------------------------- */
/* Servicio "coffee.svc.lsp".                                                */
/* ------------------------------------------------------------------------- */

static int svc_is_ready(void *self) {
    VlState *st = (VlState *)self;
    return (st && st->ready) ? 1 : 0;
}

static const char *svc_server_path(void *self) {
    VlState *st = (VlState *)self;
    return st ? st->server_path : NULL;
}

/** Resultado de una peticion sincrona-logica del servicio. */
typedef struct VlReqCap {
    char *out;   /**< JSON serializado del result (heap, free por el llamante). */
    int done;    /**< 1 cuando llego la respuesta. */
} VlReqCap;

static void svc_req_cb(void *ud, cJSON *result, cJSON *error) {
    VlReqCap *cap = (VlReqCap *)ud;
    cJSON *node = error ? error : result;
    if (node) cap->out = cJSON_PrintUnformatted(node);
    cap->done = 1;
}

/**
 * @brief request del servicio: lanza una peticion LSP/vesta y espera el result.
 *
 * El cliente LSP no bloquea (el dispatch ocurre cuando llegan bytes via
 * proc_on_data, en el hilo principal).  Esta llamada NO puede bombear el
 * subproceso por si misma, asi que entrega el result solo si el servidor ya
 * respondio antes de volver (best-effort).  Para uso asincrono real, el
 * consumidor deberia preferir su propio cliente; documentado en svc_lsp.h.
 */
static int svc_request(void *self, const char *method, const char *params_json,
                       char **out_json) {
    VlState *st = (VlState *)self;
    if (out_json) *out_json = NULL;
    if (!st || !st->ready || !st->lsp || !method) return -1;

    cJSON *params = NULL;
    if (params_json && params_json[0]) {
        params = cJSON_Parse(params_json);
        if (!params) return -2;
    }

    VlReqCap cap = {0};
    lsp_vesta_request(st->lsp, method, params, svc_req_cb, &cap);

    /* best-effort: si el servidor ya tenia la respuesta encolada, el dispatch
     * la habra entregado dentro de lsp_vesta_request -> lsp_feed previos. */
    if (cap.done && out_json) {
        *out_json = cap.out;
        return 0;
    }
    free(cap.out);
    return cap.done ? 0 : -3;
}

/* ------------------------------------------------------------------------- */
/* Resolucion de la ruta del servidor.                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief Resuelve la ruta del ejecutable del servidor LSP.
 *
 * Orden: config "server_path" -> env var VESTA_LSP_PATH -> default.  Si la ruta
 * efectiva no estaba en el config, la persiste para que el usuario la vea y la
 * pueda editar.  Devuelve una cadena en heap (free por el llamante), o NULL.
 */
static char *vl_resolve_server_path(VlState *st) {
    const char *cfg = st->api->get_config(st->host, "server_path");
    if (cfg && cfg[0]) return vl_strdup(cfg);

    const char *env = getenv("VESTA_LSP_PATH");
    if (env && env[0]) {
        st->api->set_config(st->host, "server_path", env);
        return vl_strdup(env);
    }

    st->api->set_config(st->host, "server_path", VESTA_LSP_DEFAULT_PATH);
    return vl_strdup(VESTA_LSP_DEFAULT_PATH);
}

/* ------------------------------------------------------------------------- */
/* Registro / desregistro de la extension.                                   */
/* ------------------------------------------------------------------------- */

COFFEE_EXTENSION_EXPORT int coffee_extension_register(CoffeeHost *host,
                                                      const CoffeeApi *api) {
    if (!api) return 1;
    /* rechazar si el host habla un ABI mas nuevo del que entendemos */
    if (api->abi_version > COFFEE_ABI_VERSION) return 2;
    /* requerimos el facility de subprocesos (ABI v3) para el servidor LSP */
    if (!api->proc_spawn || !api->proc_on_data || !api->register_tick) return 3;

    memset(&g_state, 0, sizeof g_state);
    g_state.host = host;
    g_state.api = api;

    /* Canales del panel inferior: log de estado + lista de problemas. */
    api->register_output_channel(host, VESTA_LSP_CHAN_LOG, "Vesta LSP");
    api->register_output_channel(host, VESTA_LSP_CHAN_PROB, "Problemas");

    /* Resolver la ruta del servidor (config / env / default). */
    g_state.server_path = vl_resolve_server_path(&g_state);
    if (!g_state.server_path) {
        api->channel_append(host, VESTA_LSP_CHAN_LOG,
                            "\x1b[31m[vesta-lsp] sin memoria al resolver la ruta"
                            "\x1b[0m\n");
        api->log(host, COFFEE_LOG_ERROR, "vesta-lsp: sin memoria");
        return 0; /* inactiva pero no tumba el IDE */
    }

    {
        char line[640];
        snprintf(line, sizeof line, "[vesta-lsp] servidor: %s\n",
                 g_state.server_path);
        api->channel_append(host, VESTA_LSP_CHAN_LOG, line);
    }

    /* Lanzar el subproceso del servidor (sin argumentos extra). */
    g_state.proc = api->proc_spawn(host, g_state.server_path, NULL, 0);
    if (!g_state.proc) {
        api->channel_append(host, VESTA_LSP_CHAN_LOG,
                            "\x1b[31m[vesta-lsp] no se pudo arrancar el "
                            "servidor; configura 'server_path' o define "
                            "VESTA_LSP_PATH\x1b[0m\n");
        api->log(host, COFFEE_LOG_ERROR,
                 "vesta-lsp: no se encontro vesta_lsp.exe");
        api->set_status(host, "vesta-lsp: servidor no disponible");
        /* Extension cargada pero inactiva: el resto del IDE sigue funcionando. */
        return 0;
    }
    g_state.server_alive = 1;

    /* Crear el cliente LSP sobre el transporte del subproceso. */
    g_state.lsp = lsp_create(vl_lsp_write, &g_state);
    if (!g_state.lsp) {
        api->channel_append(host, VESTA_LSP_CHAN_LOG,
                            "\x1b[31m[vesta-lsp] sin memoria para el cliente LSP"
                            "\x1b[0m\n");
        api->proc_kill(host, g_state.proc);
        g_state.proc = NULL;
        g_state.server_alive = 0;
        return 0;
    }

    /* Cablear datos del servidor -> parser LSP, fin del proceso y diagnosticos. */
    api->proc_on_data(host, g_state.proc, vl_on_proc_data, &g_state);
    api->proc_on_exit(host, g_state.proc, vl_on_proc_exit, &g_state);
    lsp_on_diagnostics(g_state.lsp, vl_on_diagnostics, &g_state);

    /* rootUri: la carpeta del proyecto, o la del archivo activo. */
    char *root_uri = NULL;
    const char *root = api->workspace_root ? api->workspace_root(host) : NULL;
    if (root && root[0]) {
        root_uri = vl_path_to_uri(root);
    } else {
        const char *active = api->current_path(host);
        if (active && active[0]) root_uri = vl_path_to_uri(active);
    }
    lsp_initialize(g_state.lsp, root_uri, vl_on_ready, &g_state);
    free(root_uri);

    /* Suscribir eventos del editor + tick para el debounce. */
    api->subscribe_event(host, COFFEE_EVENT_FILE_OPEN, vl_on_event, &g_state);
    api->subscribe_event(host, COFFEE_EVENT_TAB_SWITCH, vl_on_event, &g_state);
    api->subscribe_event(host, COFFEE_EVENT_BUFFER_CHANGED, vl_on_event,
                         &g_state);
    api->subscribe_event(host, COFFEE_EVENT_FILE_SAVE, vl_on_event, &g_state);
    api->subscribe_event(host, COFFEE_EVENT_SHUTDOWN, vl_on_event, &g_state);
    api->register_tick(host, vl_on_tick, &g_state);

    /* Publicar el servicio LSP para otras extensiones. */
    g_state.svc.version = COFFEE_SVC_LSP_VERSION;
    g_state.svc.is_ready = svc_is_ready;
    g_state.svc.request = svc_request;
    g_state.svc.server_path = svc_server_path;
    g_state.svc.self = &g_state;
    api->register_service(host, COFFEE_SVC_LSP_NAME, &g_state.svc);

    api->log(host, COFFEE_LOG_INFO, "extension vesta-lsp activada");
    return 0;
}

COFFEE_EXTENSION_EXPORT void coffee_extension_unregister(CoffeeHost *host) {
    (void)host;
    VlState *st = &g_state;

    /* Cerrar el servidor si sigue vivo (idempotente con el evento SHUTDOWN). */
    if (st->lsp && st->server_alive) lsp_shutdown(st->lsp);
    if (st->proc) {
        st->api->proc_kill(st->host, st->proc);
        st->proc = NULL;
    }
    if (st->lsp) {
        lsp_destroy(st->lsp);
        st->lsp = NULL;
    }

    /* Liberar los documentos y sus diagnosticos. */
    VlDoc *d = st->docs;
    while (d) {
        VlDoc *nx = d->next;
        if (d->diagnostics) cJSON_Delete(d->diagnostics);
        free(d->uri);
        free(d->path);
        free(d);
        d = nx;
    }
    st->docs = NULL;

    free(st->server_path);
    st->server_path = NULL;
    st->server_alive = 0;
    st->ready = 0;
}
