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
#include "vex_semtokens.h"

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

    /* Resaltado semantico: paleta indexada por tokenType de la leyenda. */
    CoffeeColor *palette; /**< color por indice de la leyenda (heap), o NULL. */
    int *palette_set;     /**< 1 si el indice tiene color asignado (heap). */
    int palette_n;        /**< numero de entradas (= tamano de la leyenda). */

    /* Inline hints (ghost text): lineas donde pusimos un valor comptime, para
     * poder limpiarlas antes de re-aplicar tras un nuevo analisis. */
    size_t *hint_lines;   /**< lineas (0-based) con hint puesto (heap). */
    size_t hint_count;    /**< numero de hints activos. */
    size_t hint_cap;      /**< capacidad del array. */
} VlState;

/* Singleton: el core carga una sola instancia de la extension. */
static VlState g_state;

/* Declaracion adelantada: el handler de diagnosticos (definido antes en el
 * archivo) refresca el resaltado semantico, cuya rutina vive mas abajo. */
static void vl_request_semantic_tokens(VlState *st, VlDoc *doc);
static void vl_build_palette(VlState *st);

/* Indice de lineas del buffer activo (helpers definidos mas abajo).  Se usa
 * desde vl_apply_decorations para convertir columnas UTF-16 (las del LSP) a
 * codepoints (las que entiende el render). */
typedef struct VlLineIndex {
    const char *text; /**< texto completo (no propio). */
    size_t len;       /**< longitud en bytes. */
    size_t *starts;   /**< offset de inicio de cada linea (heap). */
    int n_lines;      /**< numero de lineas. */
} VlLineIndex;
static int vl_line_index_build(VlLineIndex *ix, const char *text, size_t len);
static void vl_line_index_free(VlLineIndex *ix);
static void vl_line_text(const VlLineIndex *ix, uint32_t line,
                         const char **out_ptr, size_t *out_len);

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

/**
 * @brief Compara dos rutas nativas de forma tolerante al formato.
 *
 * La misma ruta puede llegar distinta segun la fuente: el evento FILE_OPEN trae
 * la ruta tal cual (a veces con '/' y nombre corto tipo "DESMON~1"), mientras
 * que current_path() devuelve e->filepath (a veces con '\\' y nombre largo).
 * Comparamos normalizando '\\'->'/' y a minusculas para que casen; sin esto el
 * documento del evento y el activo no se reconocian como el mismo.
 */
static int vl_path_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    for (;; ++a, ++b) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca == '\\') ca = '/';
        if (cb == '\\') cb = '/';
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        if (ca == '\0') return 1;
    }
}

/** Busca el documento por ruta nativa (NULL si no existe). */
static VlDoc *vl_find_doc_by_path(VlState *st, const char *path) {
    if (!path) return NULL;
    for (VlDoc *d = st->docs; d; d = d->next)
        if (d->path && vl_path_eq(d->path, path)) return d;
    return NULL;
}

/** 1 si @p doc es el documento del buffer en pantalla (best-effort).
 *  Usa current_path() (tolerante al formato) y, si aun no esta poblado
 *  (apertura por CLI), recurre al ultimo documento notificado por el IDE. */
static int vl_doc_is_active(VlState *st, VlDoc *doc) {
    if (!doc) return 0;
    const char *active = st->api->current_path(st->host);
    if (active && active[0]) return vl_path_eq(active, doc->path);
    return st->last_open == doc;
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

    /* Indexar el buffer activo para convertir columnas UTF-16 (LSP) a
     * codepoints (las del render).  Solo hace falta para los subrayados de
     * rango; el gutter/fondo van por linea y no necesitan columnas. */
    char *text = vl_read_active_buffer(st);
    VlLineIndex ix;
    int have_ix = text && vl_line_index_build(&ix, text, strlen(text));

    cJSON *d = NULL;
    cJSON_ArrayForEach(d, diags) {
        cJSON *range = cJSON_GetObjectItemCaseSensitive(d, "range");
        cJSON *start = range
                           ? cJSON_GetObjectItemCaseSensitive(range, "start")
                           : NULL;
        cJSON *end = range ? cJSON_GetObjectItemCaseSensitive(range, "end")
                           : NULL;
        cJSON *jline = start
                           ? cJSON_GetObjectItemCaseSensitive(start, "line")
                           : NULL;
        if (!cJSON_IsNumber(jline)) continue;
        int line = (int)jline->valuedouble; /* 0-based (LSP == gutter) */
        if (line < 0) continue;

        cJSON *jsev = cJSON_GetObjectItemCaseSensitive(d, "severity");
        int sev = cJSON_IsNumber(jsev) ? (int)jsev->valuedouble : 1;

        /* gutter + fondo tenue en la linea de inicio (como antes). */
        const char *glyph = (sev == 1) ? "x" : "!";
        api->set_gutter_marker(st->host, (size_t)line, glyph,
                               vl_sev_color(sev));
        api->set_line_background(st->host, (size_t)line, vl_sev_bg(sev));

        /* subrayado ondulado del rango exacto (ABI v5).  Defensivo: si el host
         * no expone set_range_underline (extension cargada en un IDE mas viejo)
         * o no hay indice, nos quedamos con gutter+fondo. */
        if (!api->set_range_underline || !have_ix) continue;
        cJSON *jsc =
            start ? cJSON_GetObjectItemCaseSensitive(start, "character") : NULL;
        cJSON *jel = end ? cJSON_GetObjectItemCaseSensitive(end, "line") : NULL;
        cJSON *jec =
            end ? cJSON_GetObjectItemCaseSensitive(end, "character") : NULL;
        if (!cJSON_IsNumber(jsc) || !cJSON_IsNumber(jel) ||
            !cJSON_IsNumber(jec))
            continue;
        int eline = (int)jel->valuedouble;
        if (eline < line) continue; /* rango invertido: ignorar */
        uint32_t su16 = (jsc->valuedouble < 0) ? 0u : (uint32_t)jsc->valuedouble;
        uint32_t eu16 = (jec->valuedouble < 0) ? 0u : (uint32_t)jec->valuedouble;
        CoffeeColor col = vl_sev_color(sev);

        if (eline == line) {
            /* mismo renglon: [su16, eu16) -> codepoints. */
            const char *lp = NULL;
            size_t ll = 0;
            vl_line_text(&ix, (uint32_t)line, &lp, &ll);
            uint32_t s_cp = vex_utf16_units_to_codepoints(lp, ll, su16);
            uint32_t e_cp = vex_utf16_units_to_codepoints(lp, ll, eu16);
            if (e_cp <= s_cp) e_cp = s_cp + 1; /* rango de 0 ancho: marcar 1 col */
            api->set_range_underline(st->host, (size_t)line, s_cp, e_cp, col);
        } else {
            /* rango multilinea: subrayar cada renglon del rango (primera desde
             * su columna, intermedias enteras, ultima hasta su columna). */
            for (int ln = line; ln <= eline && ln < ix.n_lines; ++ln) {
                const char *lp = NULL;
                size_t ll = 0;
                vl_line_text(&ix, (uint32_t)ln, &lp, &ll);
                uint32_t line_cp =
                    vex_utf16_units_to_codepoints(lp, ll, 0xFFFFFFFFu);
                uint32_t s_cp =
                    (ln == line) ? vex_utf16_units_to_codepoints(lp, ll, su16)
                                 : 0u;
                uint32_t e_cp =
                    (ln == eline) ? vex_utf16_units_to_codepoints(lp, ll, eu16)
                                  : line_cp;
                if (e_cp <= s_cp) e_cp = s_cp + 1;
                api->set_range_underline(st->host, (size_t)ln, s_cp, e_cp, col);
            }
        }
    }

    if (have_ix) vl_line_index_free(&ix);
    free(text);
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
        /* Al volver a un .vex ya abierto, el resaltado del buffer anterior se
         * descarta (clear_tokens) y re-pedimos los tokens de este (su texto es
         * el que ahora esta activo para convertir columnas). */
        api->clear_tokens(st->host);
        vl_request_semantic_tokens(st, doc);
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

    /* Si es el documento activo, refrescar la vista (gutter/fondo + panel). */
    if (vl_doc_is_active(st, doc)) {
        st->api->clear_decorations(st->host);
        vl_apply_decorations(st, doc->diagnostics);
        vl_refresh_problems_channel(st, doc);
        /* publishDiagnostics llega tras cada (re)analisis del servidor: es el
         * momento natural para refrescar el resaltado semantico del activo, con
         * el mismo debounce que dispara el didChange (sin peticiones extra). */
        vl_request_semantic_tokens(st, doc);
    }
}

/* ------------------------------------------------------------------------- */
/* Resaltado semantico (semantic tokens del servidor -> set_tokens).         */
/* ------------------------------------------------------------------------- */

/**
 * @brief Color de la paleta de Vex para un nombre de tokenType de la leyenda.
 *
 * Paleta fija estilo editor oscuro, legible y consistente.  El mapeo es por
 * NOMBRE LSP (no por indice), porque la leyenda la decide el servidor; asi un
 * reordenamiento de la leyenda no descoloca los colores.  Los tipos de
 * "identificador comun" (variable/parameter/property/namespace/enumMember) se
 * dejan en el color de texto normal: devolvemos 0 y el llamante NO les empuja
 * tramo, de modo que esas zonas salen con el color por defecto del editor.
 *
 * @param name  Nombre LSP del tokenType (p.ej. "keyword").
 * @param[out] out  Recibe el color RGBA si procede colorear.
 * @return 1 si el tipo se colorea (out valido), 0 si se deja en texto normal.
 */
static int vl_palette_color_for(const char *name, CoffeeColor *out) {
    if (!name || !name[0] || !out) return 0;

    static const struct {
        const char *n;
        uint8_t r, g, b;
    } MAP[] = {
        /* Palabras clave / modificadores: malva. */
        {"keyword",       197, 134, 192},
        {"modifier",      197, 134, 192},
        /* Tipos / clases / structs / enums / interfaces / type params: turquesa. */
        {"type",           78, 201, 176},
        {"class",          78, 201, 176},
        {"struct",         78, 201, 176},
        {"enum",           78, 201, 176},
        {"interface",      78, 201, 176},
        {"typeParameter",  78, 201, 176},
        /* Funciones / metodos: amarillo suave. */
        {"function",      220, 220, 170},
        {"method",        220, 220, 170},
        /* Macros: azul claro. */
        {"macro",          86, 156, 214},
        /* Literales de texto: naranja terroso. */
        {"string",        206, 145, 120},
        /* Numeros: verde claro. */
        {"number",        181, 206, 168},
        /* Comentarios: verde apagado. */
        {"comment",       106, 153,  85},
        /* Operadores: gris claro. */
        {"operator",      212, 212, 212},
    };

    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; ++i) {
        if (strcmp(MAP[i].n, name) == 0) {
            out->r = MAP[i].r;
            out->g = MAP[i].g;
            out->b = MAP[i].b;
            out->a = 255;
            return 1;
        }
    }
    /* variable/parameter/property/namespace/enumMember y cualquier otro: texto
     * normal (sin tramo). */
    return 0;
}

/**
 * @brief Construye la paleta indexada (tokenType de la leyenda -> color).
 *
 * Lee la leyenda capturada por el cliente LSP y, por cada indice, resuelve su
 * color por nombre con vl_palette_color_for.  Idempotente: libera una paleta
 * previa.  Si la leyenda no esta disponible, deja la paleta vacia (el resaltado
 * semantico queda inactivo y el .vex sale plano).
 */
static void vl_build_palette(VlState *st) {
    /* Liberar paleta anterior (re-initialize). */
    free(st->palette);
    free(st->palette_set);
    st->palette = NULL;
    st->palette_set = NULL;
    st->palette_n = 0;

    int n = 0;
    const char *const *legend = lsp_semantic_legend(st->lsp, &n);
    if (!legend || n <= 0) return;

    st->palette = (CoffeeColor *)calloc((size_t)n, sizeof(CoffeeColor));
    st->palette_set = (int *)calloc((size_t)n, sizeof(int));
    if (!st->palette || !st->palette_set) {
        free(st->palette);
        free(st->palette_set);
        st->palette = NULL;
        st->palette_set = NULL;
        return;
    }
    st->palette_n = n;
    for (int i = 0; i < n; ++i) {
        CoffeeColor c;
        if (legend[i] && vl_palette_color_for(legend[i], &c)) {
            st->palette[i] = c;
            st->palette_set[i] = 1;
        }
    }
}

/**
 * @brief Indice de inicios de linea (offsets en bytes) del texto del buffer.
 *
 * Permite resolver el texto de una linea N por (offset[N], offset[N+1]) para la
 * conversion UTF-16 -> codepoints.  Las lineas se separan por '\n'; un '\r'
 * final se excluye del rango (no afecta el conteo de columnas).
 */
/* struct VlLineIndex: definida arriba (junto a los prototipos adelantados). */

/** Construye el indice de lineas. Devuelve 1 si ok, 0 si fallo de memoria. */
static int vl_line_index_build(VlLineIndex *ix, const char *text, size_t len) {
    ix->text = text;
    ix->len = len;
    ix->starts = NULL;
    ix->n_lines = 0;

    /* Contar lineas (numero de '\n' + 1). */
    int lines = 1;
    for (size_t i = 0; i < len; ++i)
        if (text[i] == '\n') lines++;

    ix->starts = (size_t *)malloc((size_t)lines * sizeof(size_t));
    if (!ix->starts) return 0;
    ix->n_lines = lines;

    int li = 0;
    ix->starts[li++] = 0;
    for (size_t i = 0; i < len && li < lines; ++i)
        if (text[i] == '\n') ix->starts[li++] = i + 1;
    return 1;
}

static void vl_line_index_free(VlLineIndex *ix) {
    free(ix->starts);
    ix->starts = NULL;
    ix->n_lines = 0;
}

/** Devuelve (ptr, len_bytes) del texto de la linea @p line (sin '\n' ni '\r'). */
static void vl_line_text(const VlLineIndex *ix, uint32_t line,
                         const char **out_ptr, size_t *out_len) {
    *out_ptr = NULL;
    *out_len = 0;
    if ((int)line >= ix->n_lines) return;
    size_t start = ix->starts[line];
    size_t end = ((int)line + 1 < ix->n_lines) ? ix->starts[line + 1] : ix->len;
    /* Excluir el '\n' terminador y un posible '\r' previo. */
    if (end > start && ix->text[end - 1] == '\n') end--;
    if (end > start && ix->text[end - 1] == '\r') end--;
    *out_ptr = ix->text + start;
    *out_len = end - start;
}

/**
 * @brief Empuja al editor un lote de tokens de codepoint agrupados por linea.
 *
 * Asume que @p toks viene ORDENADO por linea (lo esta: el decode acumula deltas
 * monotonos).  Recorre por tramos de la misma linea, construye un array de
 * CoffeeSpan y lo entrega con set_tokens(line, ...).  Las lineas sin token no se
 * tocan (quedan en color por defecto).  El llamante ya hizo clear_tokens.
 */
static void vl_push_tokens(VlState *st, const VexSemTokenCp *toks, int n) {
    const CoffeeApi *api = st->api;
    /* Buffer reutilizable de spans por linea (crece segun haga falta). */
    CoffeeSpan stack_spans[256];
    CoffeeSpan *spans = stack_spans;
    int spans_cap = (int)(sizeof stack_spans / sizeof stack_spans[0]);
    CoffeeSpan *heap_spans = NULL;

    int i = 0;
    while (i < n) {
        uint32_t line = toks[i].line;
        int j = i;
        int cnt = 0;
        /* Contar cuantos tokens consecutivos van en esta misma linea. */
        while (j < n && toks[j].line == line) {
            j++;
            cnt++;
        }
        /* Asegurar capacidad. */
        if (cnt > spans_cap) {
            CoffeeSpan *nb =
                (CoffeeSpan *)realloc(heap_spans, (size_t)cnt * sizeof(CoffeeSpan));
            if (!nb) {
                /* Sin memoria: saltar esta linea (degradacion suave). */
                i = j;
                continue;
            }
            heap_spans = nb;
            spans = heap_spans;
            spans_cap = cnt;
        }
        int w = 0;
        for (int k = i; k < j; ++k) {
            if (toks[k].len_cp == 0) continue; /* tramo vacio: omitir. */
            spans[w].start_col = toks[k].start_cp;
            spans[w].len = toks[k].len_cp;
            spans[w].color = st->palette[toks[k].type];
            w++;
        }
        if (w > 0) api->set_tokens(st->host, line, spans, w);
        i = j;
    }
    free(heap_spans);
}

/** Resultado de semanticTokens/full: decodifica y empuja al editor. */
static void vl_on_semantic_tokens(void *ud, cJSON *result, cJSON *error) {
    VlState *st = (VlState *)ud;
    if (!st || error || !result) return;
    if (!st->palette || st->palette_n <= 0) return; /* sin leyenda: nada. */

    /* El resultado puede ser null (sin tokens) o { data: [...] }. */
    cJSON *jdata = cJSON_GetObjectItemCaseSensitive(result, "data");
    if (!cJSON_IsArray(jdata)) return;

    int count = cJSON_GetArraySize(jdata);
    if (count <= 0) {
        /* Documento sin tokens: limpiar lo que hubiera. */
        st->api->clear_tokens(st->host);
        st->api->request_repaint(st->host);
        return;
    }

    /* Copiar el array plano de uint32 (cJSON guarda numbers como double). */
    uint32_t *data = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
    if (!data) return;
    int di = 0;
    cJSON *e = NULL;
    cJSON_ArrayForEach(e, jdata) {
        double v = cJSON_IsNumber(e) ? e->valuedouble : 0.0;
        if (v < 0) v = 0;
        data[di++] = (uint32_t)v;
    }

    /* Descodificar a tokens absolutos (en unidades UTF-16). */
    int n_tok = vex_semtokens_decode(data, (size_t)count, NULL, 0);
    if (n_tok <= 0) {
        free(data);
        return;
    }
    VexSemToken *toks = (VexSemToken *)malloc((size_t)n_tok * sizeof(VexSemToken));
    if (!toks) {
        free(data);
        return;
    }
    vex_semtokens_decode(data, (size_t)count, toks, n_tok);
    free(data);

    /* Indexar las lineas del buffer activo para convertir columnas. */
    char *text = vl_read_active_buffer(st);
    VlLineIndex ix;
    int have_ix = text && vl_line_index_build(&ix, text, strlen(text));

    /* Convertir cada token a columnas de codepoint, descartando los que apuntan
     * a un tipo sin color (identificador comun -> texto normal). */
    VexSemTokenCp *cps =
        (VexSemTokenCp *)malloc((size_t)n_tok * sizeof(VexSemTokenCp));
    if (!cps) {
        free(toks);
        if (have_ix) vl_line_index_free(&ix);
        free(text);
        return;
    }
    int m = 0;
    for (int i = 0; i < n_tok; ++i) {
        if (toks[i].type >= (uint32_t)st->palette_n ||
            !st->palette_set[toks[i].type])
            continue; /* tipo sin color asignado: dejar texto normal. */
        const char *lp = NULL;
        size_t ll = 0;
        if (have_ix) vl_line_text(&ix, toks[i].line, &lp, &ll);
        vex_semtoken_to_cp(&toks[i], lp, ll, &cps[m]);
        m++;
    }

    /* Reemplazar el resaltado previo y empujar el nuevo por linea. */
    st->api->clear_tokens(st->host);
    if (m > 0) vl_push_tokens(st, cps, m);
    st->api->request_repaint(st->host);

    free(cps);
    free(toks);
    if (have_ix) vl_line_index_free(&ix);
    free(text);
}

/**
 * @brief Pide los semantic tokens del documento ACTIVO si es @p doc.
 *
 * Solo el buffer activo es legible, asi que solo pedimos tokens del doc que esta
 * en pantalla (su texto es el que usaremos para convertir columnas).  Requiere
 * servidor listo, leyenda capturada y doc abierto.
 */
/* Color tenue (ghost) de los inline hints de valores comptime. */
static const CoffeeColor VL_HINT_COLOR = {130, 130, 130, 255};

/* Limpia los inline hints que pusimos antes (texto vacio = quitar). */
static void vl_clear_inline_hints(VlState *st) {
    if (!st->api->set_inline_hint) return;
    CoffeeColor z = {0, 0, 0, 0};
    for (size_t i = 0; i < st->hint_count; ++i)
        st->api->set_inline_hint(st->host, st->hint_lines[i], "", z);
    st->hint_count = 0;
}

/* Recuerda una linea con hint para poder limpiarla en el proximo refresco. */
static void vl_remember_hint_line(VlState *st, size_t line) {
    if (st->hint_count >= st->hint_cap) {
        size_t nc = st->hint_cap ? st->hint_cap * 2 : 16;
        size_t *nb = (size_t *)realloc(st->hint_lines, nc * sizeof(size_t));
        if (!nb) return;
        st->hint_lines = nb;
        st->hint_cap = nc;
    }
    st->hint_lines[st->hint_count++] = line;
}

/* Respuesta de vesta/comptimeValues: muestra los valores de los builtins
 * (sizeof<T>, kind<T>, ...) como ghost text en su linea. */
static void vl_on_comptime_hints(void *ud, cJSON *result, cJSON *error) {
    VlState *st = (VlState *)ud;
    if (!st || error || !result) return;
    if (!st->api->set_inline_hint) return;
    vl_clear_inline_hints(st);
    cJSON *values = cJSON_GetObjectItemCaseSensitive(result, "values");
    if (!cJSON_IsArray(values)) return;
    cJSON *v = NULL;
    cJSON_ArrayForEach(v, values) {
        cJSON *bk = cJSON_GetObjectItemCaseSensitive(v, "builtin_kind");
        cJSON *jl = cJSON_GetObjectItemCaseSensitive(v, "line");
        cJSON *vs = cJSON_GetObjectItemCaseSensitive(v, "value_str");
        if (!cJSON_IsString(bk) || bk->valuestring[0] == '\0') continue;
        if (!cJSON_IsNumber(jl) || jl->valuedouble < 1.0) continue;
        if (!cJSON_IsString(vs)) continue;
        size_t line0 = (size_t)(jl->valuedouble - 1.0); /* 1-based -> 0-based */
        char buf[160];
        snprintf(buf, sizeof(buf), "= %s", vs->valuestring);
        st->api->set_inline_hint(st->host, line0, buf, VL_HINT_COLOR);
        vl_remember_hint_line(st, line0);
    }
    st->api->request_repaint(st->host);
}

/* Pide al servidor los valores comptime del .vex activo y los muestra inline. */
static void vl_refresh_inline_hints(VlState *st) {
    if (!st->ready || !st->lsp || !st->api->set_inline_hint) return;
    const char *path = st->api->current_path(st->host);
    if (!path || !vl_is_vex(path)) return;
    VlDoc *doc = vl_find_doc_by_path(st, path);
    if (!doc || !doc->open) return;
    cJSON *params = cJSON_CreateObject();
    if (!params) return;
    cJSON_AddStringToObject(params, "uri", doc->uri);
    lsp_vesta_request(st->lsp, "vesta/comptimeValues", params,
                      vl_on_comptime_hints, st);
}

static void vl_request_semantic_tokens(VlState *st, VlDoc *doc) {
    if (!st->ready || !st->lsp || !doc || !doc->open) return;
    if (!st->palette || st->palette_n <= 0) return;
    if (!vl_doc_is_active(st, doc)) return;
    lsp_semantic_tokens_full(st->lsp, doc->uri, vl_on_semantic_tokens, st);
    vl_refresh_inline_hints(st); /* + valores comptime como ghost text */
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
    if (vl_doc_is_active(st, doc)) {
        char *text = vl_read_active_buffer(st);
        lsp_did_open(st->lsp, doc->uri, "vex", doc->version, text ? text : "");
        doc->open = 1;
        doc->pending_open = 0;
        free(text);
        /* Pedir el resaltado inicial sin esperar a una edicion (el servidor
         * responde con los semantic tokens del texto recien abierto). */
        vl_request_semantic_tokens(st, doc);
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

    /* Construir la paleta de resaltado desde la leyenda anunciada por el
     * servidor en su respuesta de initialize (indice de tokenType -> color). */
    vl_build_palette(st);

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
/* Inspector del ecosistema: comandos que invocan los metodos "vesta/*" sobre */
/* el .vex activo y vuelcan el resultado formateado al panel inferior.        */
/* ------------------------------------------------------------------------- */

/* Identificador y titulo del canal del inspector en el panel inferior. */
#define VESTA_INSP_CHAN "vesta-inspector"

/* Codigos SGR ANSI usados por el formateo del inspector. */
#define INSP_HEAD   "\x1b[1;36m" /* cabecera: cyan en negrita */
#define INSP_KEY    "\x1b[36m"   /* clave/etiqueta: cyan */
#define INSP_WARN   "\x1b[33m"   /* aviso (unsupported/incompatible): amarillo */
#define INSP_ERR    "\x1b[31m"   /* error: rojo */
#define INSP_OK     "\x1b[32m"   /* ok / afirmativo: verde */
#define INSP_DIM    "\x1b[90m"   /* secundario: gris */
#define INSP_RST    "\x1b[0m"    /* reset */

/**
 * @brief Contexto que viaja con cada peticion del inspector hasta su callback.
 *
 * Como @c lsp_vesta_request no propaga el nombre del metodo a su callback,
 * adjuntamos aqui el codigo del metodo (para elegir el formateo) y el estado de
 * la extension.  Se reserva por peticion y se libera al recibir la respuesta.
 */
typedef enum {
    INSP_M_BYTECODE = 0,
    INSP_M_IR,
    INSP_M_JITASM,
    INSP_M_AOTASM,
    INSP_M_AOTCOMPAT,
    INSP_M_COMPLEXITY,
    INSP_M_DIAGRAM,
    INSP_M_FUNCTIONS,
    INSP_M_MACROS,
    INSP_M_COMPTIME
} InspMethod;

typedef struct {
    VlState   *st;     /**< estado de la extension. */
    InspMethod method; /**< que metodo se pidio (selecciona el formateo). */
    char      *label;  /**< etiqueta legible para la cabecera (heap). */
} InspReq;

/** Numero (cJSON) -> entero, con valor por defecto si no es numero. */
static int insp_num(const cJSON *o, const char *key, int dflt) {
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(j) ? (int)j->valuedouble : dflt;
}

/** String (cJSON) -> const char*, con valor por defecto si no es string. */
static const char *insp_str(const cJSON *o, const char *key, const char *dflt) {
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(j) ? j->valuestring : dflt;
}

/** Escribe @p text en el canal del inspector (atajo). */
static void insp_emit(VlState *st, const char *text) {
    st->api->channel_append(st->host, VESTA_INSP_CHAN, text);
}

/** Cabecera de seccion: limpia el canal y escribe el titulo + el archivo. */
static void insp_header(VlState *st, const char *title, VlDoc *doc) {
    st->api->channel_clear(st->host, VESTA_INSP_CHAN);
    char buf[1024];
    snprintf(buf, sizeof buf, INSP_HEAD "== %s ==" INSP_RST "\n", title);
    insp_emit(st, buf);
    if (doc && doc->path) {
        snprintf(buf, sizeof buf, INSP_DIM "%s" INSP_RST "\n\n", doc->path);
        insp_emit(st, buf);
    }
}

/** Vuelca un bloque de texto tal cual al canal (asegurando salto final). */
static void insp_emit_text_block(VlState *st, const char *text) {
    if (!text || !text[0]) {
        insp_emit(st, INSP_DIM "(vacio)" INSP_RST "\n");
        return;
    }
    insp_emit(st, text);
    size_t n = strlen(text);
    if (text[n - 1] != '\n') insp_emit(st, "\n");
}

/* --- Formateadores por metodo (cada uno recibe el "result" del servidor). - */

/** jitAsm / aotAsm: texto del codigo nativo + cabecera de funcion + avisos. */
static void insp_fmt_text_like(VlState *st, InspMethod m, cJSON *result) {
    /* jitAsm puede traer { unsupported, reason }; aotAsm { incompatible, reason }. */
    if (m == INSP_M_JITASM) {
        cJSON *uns = cJSON_GetObjectItemCaseSensitive(result, "unsupported");
        if (uns && (cJSON_IsTrue(uns) || cJSON_IsBool(uns))) {
            char buf[1024];
            snprintf(buf, sizeof buf,
                     INSP_WARN "no se pudo compilar a nativo (JIT): %s" INSP_RST
                               "\n",
                     insp_str(result, "reason", "(sin motivo)"));
            insp_emit(st, buf);
            return;
        }
    } else if (m == INSP_M_AOTASM) {
        cJSON *inc = cJSON_GetObjectItemCaseSensitive(result, "incompatible");
        if (inc && (cJSON_IsTrue(inc) || cJSON_IsBool(inc))) {
            char buf[1024];
            snprintf(buf, sizeof buf,
                     INSP_WARN "incompatible con AOT: %s" INSP_RST "\n",
                     insp_str(result, "reason", "(sin motivo)"));
            insp_emit(st, buf);
            return;
        }
    }

    /* Cabecera de funcion / tamano si el server las trae (jit/aot asm). */
    const char *fn = insp_str(result, "function", NULL);
    if (fn) {
        char buf[512];
        snprintf(buf, sizeof buf, INSP_KEY "funcion:" INSP_RST " %s", fn);
        insp_emit(st, buf);
        cJSON *bytes = cJSON_GetObjectItemCaseSensitive(result, "bytes");
        if (cJSON_IsNumber(bytes)) {
            snprintf(buf, sizeof buf, INSP_DIM "  (%d bytes)" INSP_RST,
                     (int)bytes->valuedouble);
            insp_emit(st, buf);
        }
        insp_emit(st, "\n\n");
    }

    insp_emit_text_block(st, insp_str(result, "text", NULL));
}

/** complexity: una linea por funcion (parcial / total / confianza). */
static void insp_fmt_complexity(VlState *st, cJSON *result) {
    cJSON *fns = cJSON_GetObjectItemCaseSensitive(result, "functions");
    if (!cJSON_IsArray(fns) || cJSON_GetArraySize(fns) == 0) {
        insp_emit(st, INSP_DIM "(sin informacion de complejidad)" INSP_RST "\n");
        return;
    }
    cJSON *f = NULL;
    cJSON_ArrayForEach(f, fns) {
        const char *name = insp_str(f, "name", "?");
        const char *partial = insp_str(f, "partial", "?");
        const char *total = insp_str(f, "total", "?");
        const char *conf = insp_str(f, "confidence", NULL);
        char buf[1024];
        if (conf)
            snprintf(buf, sizeof buf,
                     INSP_KEY "%s" INSP_RST ": parcial=%s  total=%s  " INSP_DIM
                              "[%s]" INSP_RST "\n",
                     name, partial, total, conf);
        else
            snprintf(buf, sizeof buf,
                     INSP_KEY "%s" INSP_RST ": parcial=%s  total=%s\n", name,
                     partial, total);
        insp_emit(st, buf);
    }
}

/** aotCompat: tier + compatible + issues + funciones ok. */
static void insp_fmt_aotcompat(VlState *st, cJSON *result) {
    char buf[1280];
    const char *tier = insp_str(result, "tier", "?");
    cJSON *jcomp = cJSON_GetObjectItemCaseSensitive(result, "compatible");
    int compatible = jcomp ? cJSON_IsTrue(jcomp) : 0;

    snprintf(buf, sizeof buf, INSP_KEY "tier:" INSP_RST " %s\n", tier);
    insp_emit(st, buf);
    snprintf(buf, sizeof buf, INSP_KEY "compatible:" INSP_RST " %s%s" INSP_RST
                                       "\n",
             compatible ? INSP_OK : INSP_ERR, compatible ? "si" : "no");
    insp_emit(st, buf);

    cJSON *issues = cJSON_GetObjectItemCaseSensitive(result, "issues");
    int n_issues = cJSON_IsArray(issues) ? cJSON_GetArraySize(issues) : 0;
    if (n_issues > 0) {
        snprintf(buf, sizeof buf, "\n" INSP_WARN "problemas (%d):" INSP_RST "\n",
                 n_issues);
        insp_emit(st, buf);
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, issues) {
            const char *fn = insp_str(it, "fn_name", "?");
            int line = insp_num(it, "source_line", 0);
            const char *op = insp_str(it, "op", "?");
            const char *reason = insp_str(it, "reason", "");
            snprintf(buf, sizeof buf,
                     INSP_WARN "  %s:%d" INSP_RST "  %s -> %s\n", fn, line, op,
                     reason);
            insp_emit(st, buf);
        }
    } else {
        insp_emit(st, "\n" INSP_OK "sin problemas" INSP_RST "\n");
    }

    cJSON *ok = cJSON_GetObjectItemCaseSensitive(result, "ok_functions");
    if (cJSON_IsArray(ok)) {
        snprintf(buf, sizeof buf, "\n" INSP_KEY "funciones ok (%d):" INSP_RST
                                  "\n",
                 cJSON_GetArraySize(ok));
        insp_emit(st, buf);
        cJSON *e = NULL;
        cJSON_ArrayForEach(e, ok) {
            if (cJSON_IsString(e)) {
                snprintf(buf, sizeof buf, "  %s\n", e->valuestring);
                insp_emit(st, buf);
            }
        }
    } else if (cJSON_IsNumber(ok)) {
        snprintf(buf, sizeof buf, "\n" INSP_KEY "funciones ok:" INSP_RST
                                  " %d\n",
                 (int)ok->valuedouble);
        insp_emit(st, buf);
    }
}

/** functions: "nombre  (linea N)" por funcion. */
static void insp_fmt_functions(VlState *st, cJSON *result) {
    cJSON *fns = cJSON_GetObjectItemCaseSensitive(result, "functions");
    if (!cJSON_IsArray(fns) || cJSON_GetArraySize(fns) == 0) {
        insp_emit(st, INSP_DIM "(sin funciones)" INSP_RST "\n");
        return;
    }
    cJSON *f = NULL;
    cJSON_ArrayForEach(f, fns) {
        const char *name = insp_str(f, "name", "?");
        int line = insp_num(f, "line", 0);
        char buf[768];
        snprintf(buf, sizeof buf, INSP_KEY "%s" INSP_RST "  " INSP_DIM
                                           "(linea %d)" INSP_RST "\n",
                 name, line);
        insp_emit(st, buf);
    }
}

/** macroExpand: por expansion el nombre + sitio + el codigo generado; skipped. */
static void insp_fmt_macros(VlState *st, cJSON *result) {
    char buf[1536];
    cJSON *exps = cJSON_GetObjectItemCaseSensitive(result, "expansions");
    int n_exp = cJSON_IsArray(exps) ? cJSON_GetArraySize(exps) : 0;
    if (n_exp > 0) {
        snprintf(buf, sizeof buf, INSP_HEAD "expansiones (%d):" INSP_RST "\n",
                 n_exp);
        insp_emit(st, buf);
        cJSON *e = NULL;
        cJSON_ArrayForEach(e, exps) {
            const char *name = insp_str(e, "macro_name", "?");
            const char *loc = insp_str(e, "call_site_loc", "");
            snprintf(buf, sizeof buf, "\n" INSP_KEY "%s" INSP_RST " @ %s\n",
                     name, loc);
            insp_emit(st, buf);
            const char *args = insp_str(e, "args", NULL);
            if (args && args[0]) {
                snprintf(buf, sizeof buf, INSP_DIM "  args: %s" INSP_RST "\n",
                         args);
                insp_emit(st, buf);
            }
            insp_emit_text_block(st, insp_str(e, "generated_code", NULL));
        }
    } else {
        insp_emit(st, INSP_DIM "(sin expansiones)" INSP_RST "\n");
    }

    cJSON *skip = cJSON_GetObjectItemCaseSensitive(result, "skipped");
    int n_skip = cJSON_IsArray(skip) ? cJSON_GetArraySize(skip) : 0;
    if (n_skip > 0) {
        snprintf(buf, sizeof buf, "\n" INSP_WARN "omitidas (%d):" INSP_RST "\n",
                 n_skip);
        insp_emit(st, buf);
        cJSON *s = NULL;
        cJSON_ArrayForEach(s, skip) {
            const char *name = insp_str(s, "macro_name", "?");
            const char *reason = insp_str(s, "reason", "");
            snprintf(buf, sizeof buf, INSP_WARN "  %s" INSP_RST " -> %s\n", name,
                     reason);
            insp_emit(st, buf);
        }
    }
}

/** comptimeValues: "nombre [tipo] = valor" por entrada. */
static void insp_fmt_comptime(VlState *st, cJSON *result) {
    cJSON *vals = cJSON_GetObjectItemCaseSensitive(result, "values");
    if (!cJSON_IsArray(vals) || cJSON_GetArraySize(vals) == 0) {
        insp_emit(st, INSP_DIM "(sin valores comptime)" INSP_RST "\n");
        return;
    }
    cJSON *v = NULL;
    cJSON_ArrayForEach(v, vals) {
        const char *name = insp_str(v, "name", "?");
        const char *type = insp_str(v, "type_kind", NULL);
        const char *scope = insp_str(v, "scope", NULL);
        const char *value = insp_str(v, "value_str", "?");
        char buf[1280];
        if (type)
            snprintf(buf, sizeof buf,
                     INSP_KEY "%s" INSP_RST " " INSP_DIM "[%s]" INSP_RST
                              " = %s%s%s\n",
                     name, type, value, scope ? INSP_DIM "  " : "",
                     scope ? scope : "");
        else
            snprintf(buf, sizeof buf, INSP_KEY "%s" INSP_RST " = %s\n", name,
                     value);
        insp_emit(st, buf);
    }
}

/** Callback unico del inspector: despacha al formateador segun el metodo. */
static void insp_on_result(void *ud, cJSON *result, cJSON *error) {
    InspReq *req = (InspReq *)ud;
    if (!req) return;
    VlState *st = req->st;

    if (error) {
        const char *msg = insp_str(error, "message", NULL);
        char buf[1024];
        snprintf(buf, sizeof buf,
                 "\n" INSP_ERR "el servidor devolvio un error: %s" INSP_RST "\n",
                 msg ? msg : "(sin detalle)");
        insp_emit(st, buf);
    } else if (!result) {
        insp_emit(st, "\n" INSP_WARN "respuesta vacia del servidor" INSP_RST
                      "\n");
    } else {
        switch (req->method) {
        case INSP_M_BYTECODE:
        case INSP_M_IR:
        case INSP_M_DIAGRAM:
            insp_emit_text_block(st, insp_str(result, "text", NULL));
            break;
        case INSP_M_JITASM:
        case INSP_M_AOTASM:
            insp_fmt_text_like(st, req->method, result);
            break;
        case INSP_M_AOTCOMPAT:
            insp_fmt_aotcompat(st, result);
            break;
        case INSP_M_COMPLEXITY:
            insp_fmt_complexity(st, result);
            break;
        case INSP_M_FUNCTIONS:
            insp_fmt_functions(st, result);
            break;
        case INSP_M_MACROS:
            insp_fmt_macros(st, result);
            break;
        case INSP_M_COMPTIME:
            insp_fmt_comptime(st, result);
            break;
        default:
            break;
        }
    }

    st->api->request_repaint(st->host);
    free(req->label);
    free(req);
}

/**
 * @brief Resuelve el documento .vex ACTIVO para una peticion del inspector.
 *
 * Devuelve el VlDoc del buffer en pantalla si es .vex y el servidor esta listo;
 * en caso contrario escribe el motivo en el canal y devuelve NULL.
 */
static VlDoc *insp_active_doc_or_warn(VlState *st, const char *title) {
    const char *path = st->api->current_path(st->host);
    if (!path || !vl_is_vex(path)) {
        insp_header(st, title, NULL);
        insp_emit(st, INSP_WARN "el archivo activo no es un .vex" INSP_RST "\n");
        st->api->set_status(st->host, "vesta-inspector: el activo no es .vex");
        return NULL;
    }
    if (!st->ready || !st->lsp) {
        insp_header(st, title, NULL);
        insp_emit(st, INSP_WARN "el servidor LSP de Vesta no esta listo todavia"
                                INSP_RST "\n");
        st->api->set_status(st->host, "vesta-inspector: servidor no listo");
        return NULL;
    }
    VlDoc *doc = vl_get_or_make_doc(st, path);
    if (!doc || !doc->uri) {
        insp_header(st, title, NULL);
        insp_emit(st, INSP_ERR "no se pudo resolver el documento activo" INSP_RST
                               "\n");
        return NULL;
    }
    return doc;
}

/**
 * @brief Lanza una peticion "vesta/*" del inspector sobre el .vex activo.
 *
 * Construye params { uri } y le anyade los extras de @p extra (que se consume).
 * Escribe la cabecera, un aviso "consultando..." y dispara la peticion con un
 * contexto que selecciona el formateo en la respuesta.
 */
static void insp_run(VlState *st, const char *title, const char *method,
                     InspMethod which, cJSON *extra) {
    VlDoc *doc = insp_active_doc_or_warn(st, title);
    if (!doc) {
        if (extra) cJSON_Delete(extra);
        return;
    }

    cJSON *params = cJSON_CreateObject();
    if (!params) {
        if (extra) cJSON_Delete(extra);
        return;
    }
    cJSON_AddStringToObject(params, "uri", doc->uri);
    /* Volcar los pares extra (phase, kind, format, ...) en params. */
    if (extra) {
        cJSON *e = extra->child;
        while (e) {
            cJSON *nx = e->next;
            cJSON_DetachItemViaPointer(extra, e);
            cJSON_AddItemToObject(params, e->string, e);
            e = nx;
        }
        cJSON_Delete(extra);
    }

    InspReq *req = (InspReq *)calloc(1, sizeof(InspReq));
    if (!req) {
        cJSON_Delete(params);
        return;
    }
    req->st = st;
    req->method = which;
    req->label = vl_strdup(title);

    insp_header(st, title, doc);
    insp_emit(st, INSP_DIM "consultando al servidor..." INSP_RST "\n\n");
    st->api->set_status(st->host, "vesta-inspector: consultando...");

    /* lsp_vesta_request consume params. */
    lsp_vesta_request(st->lsp, method, params, insp_on_result, req);
}

/* --- Comandos (uno por vista). --------------------------------------------- */

static void insp_cmd_bytecode(CoffeeHost *h, void *ud) {
    (void)h;
    insp_run((VlState *)ud, "Bytecode .vel", "vesta/bytecode", INSP_M_BYTECODE,
             NULL);
}

static void insp_cmd_ir(CoffeeHost *h, void *ud) {
    (void)h;
    cJSON *extra = cJSON_CreateObject();
    if (extra) cJSON_AddStringToObject(extra, "phase", "post");
    insp_run((VlState *)ud, "IR (post-opt)", "vesta/ir", INSP_M_IR, extra);
}

static void insp_cmd_jitasm(CoffeeHost *h, void *ud) {
    (void)h;
    insp_run((VlState *)ud, "Codigo nativo del JIT", "vesta/jitAsm",
             INSP_M_JITASM, NULL);
}

static void insp_cmd_aotasm(CoffeeHost *h, void *ud) {
    (void)h;
    insp_run((VlState *)ud, "Codigo nativo del AOT", "vesta/aotAsm",
             INSP_M_AOTASM, NULL);
}

static void insp_cmd_aotcompat(CoffeeHost *h, void *ud) {
    (void)h;
    insp_run((VlState *)ud, "Compatibilidad AOT", "vesta/aotCompat",
             INSP_M_AOTCOMPAT, NULL);
}

static void insp_cmd_complexity(CoffeeHost *h, void *ud) {
    (void)h;
    insp_run((VlState *)ud, "Complejidad (Big-O)", "vesta/complexity",
             INSP_M_COMPLEXITY, NULL);
}

static void insp_cmd_diagram(CoffeeHost *h, void *ud) {
    (void)h;
    cJSON *extra = cJSON_CreateObject();
    if (extra) {
        cJSON_AddStringToObject(extra, "kind", "ir-post");
        cJSON_AddStringToObject(extra, "format", "mermaid");
    }
    insp_run((VlState *)ud, "Diagrama (IR post, mermaid)", "vesta/diagram",
             INSP_M_DIAGRAM, extra);
}

static void insp_cmd_functions(CoffeeHost *h, void *ud) {
    (void)h;
    insp_run((VlState *)ud, "Funciones", "vesta/functions", INSP_M_FUNCTIONS,
             NULL);
}

static void insp_cmd_macros(CoffeeHost *h, void *ud) {
    (void)h;
    insp_run((VlState *)ud, "Expansion de macros", "vesta/macroExpand",
             INSP_M_MACROS, NULL);
}

static void insp_cmd_comptime(CoffeeHost *h, void *ud) {
    (void)h;
    insp_run((VlState *)ud, "Valores comptime", "vesta/comptimeValues",
             INSP_M_COMPTIME, NULL);
}

/** Registra el canal, los comandos, sus entradas de menu y un atajo. */
static void insp_register_commands(VlState *st) {
    CoffeeHost *h = st->host;
    const CoffeeApi *api = st->api;

    api->register_output_channel(h, VESTA_INSP_CHAN, "Vesta Inspector");

    struct {
        const char *id;
        const char *title;
        CoffeeCommandFn fn;
    } CMDS[] = {
        {"vesta.inspect.bytecode",   "Vesta: ver bytecode .vel",
         insp_cmd_bytecode},
        {"vesta.inspect.ir",         "Vesta: ver IR (post-opt)", insp_cmd_ir},
        {"vesta.inspect.jitasm",     "Vesta: ver codigo nativo del JIT",
         insp_cmd_jitasm},
        {"vesta.inspect.aotasm",     "Vesta: ver codigo nativo del AOT",
         insp_cmd_aotasm},
        {"vesta.inspect.aotcompat",  "Vesta: compatibilidad AOT",
         insp_cmd_aotcompat},
        {"vesta.inspect.complexity", "Vesta: complejidad (Big-O)",
         insp_cmd_complexity},
        {"vesta.inspect.diagram",    "Vesta: ver diagrama", insp_cmd_diagram},
        {"vesta.inspect.functions",  "Vesta: funciones", insp_cmd_functions},
        {"vesta.inspect.macros",     "Vesta: expansion de macros",
         insp_cmd_macros},
        {"vesta.inspect.comptime",   "Vesta: valores comptime",
         insp_cmd_comptime},
    };

    for (size_t i = 0; i < sizeof CMDS / sizeof CMDS[0]; ++i) {
        api->register_command(h, CMDS[i].id, CMDS[i].title, CMDS[i].fn, st);
        api->add_menu_item(h, "Vesta/Inspector", CMDS[i].id);
    }

    /* Un atajo de conveniencia para la vista mas usada (bytecode). */
    api->bind_key(h, "Ctrl+Shift+B", "vesta.inspect.bytecode");
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

    /* Registrar el inspector del ecosistema: comandos que consultan los metodos
     * "vesta/*" del servidor y vuelcan el resultado formateado al panel. */
    insp_register_commands(&g_state);

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

    free(st->palette);
    free(st->palette_set);
    st->palette = NULL;
    st->palette_set = NULL;
    st->palette_n = 0;

    free(st->server_path);
    st->server_path = NULL;
    st->server_alive = 0;
    st->ready = 0;
}
