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
 * VESTA_LSP_PATH.  NUNCA se hardcodea una ruta: se DESCUBRE buscando en env
 * vars, PATH e instalaciones estandar (ver vl_resolve_server_path). */
#if defined(_WIN32)
#define VL_EXE_NAME "vesta_lsp.exe"
#define VL_PATH_SEP ';'
#define VL_DIR_SEP '\\'
#else
#define VL_EXE_NAME "vesta_lsp"
#define VL_PATH_SEP ':'
#define VL_DIR_SEP '/'
#endif
/* Repo oficial de VestaVM para auto-instalar (override: config "vesta_repo" /
 * env VESTA_REPO).  Ramas: "release" (estable, default) y "feature" (ultimos
 * cambios; override: config "vesta_branch" / env VESTA_BRANCH). */
#define VL_DEFAULT_REPO "https://github.com/desmonHak/VM"
/* "feature" tiene los ultimos cambios Y punteros de submodulo validos.  La
 * rama "release" sera la estable en el futuro (cuando lo este, cambiar aqui).
 * Override: config "vesta_branch" / env VESTA_BRANCH. */
#define VL_DEFAULT_BRANCH "feature"

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
    /* Cache de los semantic tokens YA convertidos a columnas de codepoint del
     * ULTIMO analisis de este documento.  Permite re-pintar el resaltado al
     * instante al volver a la pestana (sin esperar la respuesta async del
     * servidor), igual que se cachea @c diagnostics.  Valido mientras el texto
     * no cambie; tras un didChange el servidor responde y se reemplaza. */
    VexSemTokenCp *sem_cps;        /**< tokens cacheados (heap), o NULL. */
    int sem_n;                     /**< numero de tokens en @c sem_cps. */
    /* Inline hints cacheados (resultados crudos del servidor) para re-aplicarlos
     * al instante al volver a la pestana, sin esperar otra respuesta async.  Hay
     * dos tipos coordinados: los valores comptime (al final de la linea) y los
     * nombres de parametros (en columna).  Se re-aplican AMBOS juntos. */
    cJSON *comptime_hints; /**< result de vesta/comptimeValues, o NULL. */
    cJSON *param_hints;    /**< result de vesta/paramHints, o NULL. */
    struct VlDoc *next;
} VlDoc;

typedef struct VlState {
    CoffeeHost *host;
    const CoffeeApi *api;

    CoffeeProc proc;      /**< subproceso del servidor (o NULL si no arranco). */
    CoffeeProc install_proc; /**< subproceso de auto-instalacion (async), o NULL. */
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

    /* Hover rico (popup con pestanas).  hover_gen invalida respuestas en vuelo
     * cuando el raton se mueve a otro simbolo; hover_uri es el doc del hover en
     * curso (para pedir IR/bytecode/JIT/AOT). */
    int hover_gen;
    char *hover_uri;
} VlState;

/* Singleton: el core carga una sola instancia de la extension. */
static VlState g_state;

/* Declaracion adelantada: el handler de diagnosticos (definido antes en el
 * archivo) refresca el resaltado semantico, cuya rutina vive mas abajo. */
static void vl_request_semantic_tokens(VlState *st, VlDoc *doc);
static void vl_build_palette(VlState *st);
/* Empuja al editor un lote de tokens (codepoints) ya convertidos; lo usa tanto
 * la respuesta del servidor como el re-pintado desde cache al cambiar de tab. */
static void vl_push_tokens(VlState *st, const VexSemTokenCp *toks, int n);
/* Re-aplica los inline hints (comptime + parametros) cacheados de un doc. */
static void vl_apply_inline_hints(VlState *st, VlDoc *doc);

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
         * descarta (clear_tokens).  Re-pintamos AL INSTANTE el resaltado
         * cacheado de ESTE documento (su ultimo analisis) para que no haya
         * parpadeo sin color, y ademas re-pedimos al servidor para refrescar
         * (su texto es el que ahora esta activo para convertir columnas). */
        api->clear_tokens(st->host);
        if (doc->sem_cps && doc->sem_n > 0) {
            vl_push_tokens(st, doc->sem_cps, doc->sem_n);
            api->request_repaint(st->host);
        }
        /* Re-pintar al instante los inline hints cacheados de este doc (valores
         * comptime + nombres de parametros), sin esperar la respuesta async. */
        vl_apply_inline_hints(st, doc);
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
        /* Secuencias de escape (\n, \xHH) y ANSI: dorado (estilo VS Code). */
        {"escapeSequence", 215, 186, 125},
        /* Delimitadores de interpolacion ${ }: magenta/rosa, destacan sobre
         * el naranja del texto del string y el color del identificador. */
        {"interpolation",  216, 132, 200},
        /* Registros de CPU en bloques asm: rojo coral (distinto de los 4
         * colores que reusan las categorias de instrucciones:
         * aritmeticas=function amarillo, logicas=macro azul, control=keyword
         * malva, movimiento=type turquesa). */
        {"register",       224, 108, 117},
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

    /* Cachear los tokens en el documento activo para re-pintarlos al instante
     * al volver a su pestana (sin esperar otra respuesta del servidor).  El
     * activo segun el IDE es st->last_open (vl_attend_path lo fija). */
    if (st->last_open) {
        free(st->last_open->sem_cps);
        st->last_open->sem_cps = NULL;
        st->last_open->sem_n = 0;
        if (m > 0) {
            VexSemTokenCp *keep =
                (VexSemTokenCp *)malloc((size_t)m * sizeof(VexSemTokenCp));
            if (keep) {
                memcpy(keep, cps, (size_t)m * sizeof(VexSemTokenCp));
                st->last_open->sem_cps = keep;
                st->last_open->sem_n = m;
            }
        }
    }

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
/* Color de los nombres de parametro (inlay): gris azulado tenue, distinto del
 * gris neutro de los valores comptime. */
static const CoffeeColor VL_PARAM_HINT_COLOR = {118, 132, 150, 255};

/* Aplica al editor TODOS los inline hints cacheados de @p doc: los valores
 * comptime (al final de la linea) y los nombres de parametros (en columna).
 * Limpia primero todo para no acumular.  Re-pinta al instante (lo llama tanto la
 * respuesta del servidor como el cambio de pestana). */
static void vl_apply_inline_hints(VlState *st, VlDoc *doc) {
    const CoffeeApi *api = st->api;
    if (!api->set_inline_hint) return;
    /* Limpiar lo anterior de un golpe (ABI v6). */
    if (api->clear_inline_hints) api->clear_inline_hints(st->host);
    if (!doc) { api->request_repaint(st->host); return; }

    /* (1) Valores comptime: ghost text al final de la linea. */
    if (doc->comptime_hints) {
        cJSON *values =
            cJSON_GetObjectItemCaseSensitive(doc->comptime_hints, "values");
        cJSON *v = NULL;
        cJSON_ArrayForEach(v, values) {
            cJSON *bk = cJSON_GetObjectItemCaseSensitive(v, "builtin_kind");
            cJSON *jl = cJSON_GetObjectItemCaseSensitive(v, "line");
            cJSON *vs = cJSON_GetObjectItemCaseSensitive(v, "value_str");
            if (!cJSON_IsString(bk) || bk->valuestring[0] == '\0') continue;
            if (!cJSON_IsNumber(jl) || jl->valuedouble < 1.0) continue;
            if (!cJSON_IsString(vs)) continue;
            size_t line0 = (size_t)(jl->valuedouble - 1.0);
            /* ": " para tipos inferidos, nada para static_assert (OK/FALLA),
             * "= " para valores. */
            const char *prefix = "= ";
            if (strcmp(bk->valuestring, "type") == 0)
                prefix = ": ";
            else if (strcmp(bk->valuestring, "static_assert") == 0)
                prefix = "";
            char buf[180];
            snprintf(buf, sizeof(buf), "%s%s", prefix, vs->valuestring);
            api->set_inline_hint(st->host, line0, buf, VL_HINT_COLOR);
        }
    }

    /* (2) Parameter hints: nombre del parametro ANTES de cada argumento. */
    if (doc->param_hints && api->set_inline_hint_at) {
        cJSON *hints =
            cJSON_GetObjectItemCaseSensitive(doc->param_hints, "hints");
        if (cJSON_IsArray(hints)) {
            /* El servidor da la columna en UTF-16: la convertimos a codepoints
             * con el texto del buffer activo (que es el de este doc). */
            char *text = vl_read_active_buffer(st);
            VlLineIndex ix;
            int have_ix = text && vl_line_index_build(&ix, text, strlen(text));
            cJSON *h = NULL;
            cJSON_ArrayForEach(h, hints) {
                cJSON *jl = cJSON_GetObjectItemCaseSensitive(h, "line");
                cJSON *jc = cJSON_GetObjectItemCaseSensitive(h, "character");
                cJSON *jlab = cJSON_GetObjectItemCaseSensitive(h, "label");
                if (!cJSON_IsNumber(jl) || !cJSON_IsNumber(jc) ||
                    !cJSON_IsString(jlab))
                    continue;
                uint32_t line0 =
                    (jl->valuedouble < 0) ? 0u : (uint32_t)jl->valuedouble;
                uint32_t u16 =
                    (jc->valuedouble < 0) ? 0u : (uint32_t)jc->valuedouble;
                uint32_t cp = u16;
                if (have_ix) {
                    const char *lp = NULL;
                    size_t ll = 0;
                    vl_line_text(&ix, line0, &lp, &ll);
                    cp = vex_utf16_units_to_codepoints(lp, ll, u16);
                }
                api->set_inline_hint_at(st->host, (size_t)line0, cp,
                                        jlab->valuestring, VL_PARAM_HINT_COLOR);
            }
            if (have_ix) vl_line_index_free(&ix);
            free(text);
        }
    }
    api->request_repaint(st->host);
}

/* Respuesta de vesta/comptimeValues: cachea y re-aplica todos los hints. */
static void vl_on_comptime_hints(void *ud, cJSON *result, cJSON *error) {
    VlState *st = (VlState *)ud;
    if (!st || error || !result) return;
    VlDoc *doc = st->last_open;
    if (doc) {
        if (doc->comptime_hints) cJSON_Delete(doc->comptime_hints);
        doc->comptime_hints = cJSON_Duplicate(result, 1);
    }
    vl_apply_inline_hints(st, doc);
}

/* Respuesta de vesta/paramHints: cachea y re-aplica todos los hints. */
static void vl_on_param_hints(void *ud, cJSON *result, cJSON *error) {
    VlState *st = (VlState *)ud;
    if (!st || error || !result) return;
    VlDoc *doc = st->last_open;
    if (doc) {
        if (doc->param_hints) cJSON_Delete(doc->param_hints);
        doc->param_hints = cJSON_Duplicate(result, 1);
    }
    vl_apply_inline_hints(st, doc);
}

/* Pide al servidor los valores comptime Y los parameter hints del .vex activo. */
static void vl_refresh_inline_hints(VlState *st) {
    if (!st->ready || !st->lsp || !st->api->set_inline_hint) return;
    const char *path = st->api->current_path(st->host);
    if (!path || !vl_is_vex(path)) return;
    VlDoc *doc = vl_find_doc_by_path(st, path);
    if (!doc || !doc->open) return;
    cJSON *p1 = cJSON_CreateObject();
    if (p1) {
        cJSON_AddStringToObject(p1, "uri", doc->uri);
        lsp_vesta_request(st->lsp, "vesta/comptimeValues", p1,
                          vl_on_comptime_hints, st);
    }
    cJSON *p2 = cJSON_CreateObject();
    if (p2) {
        cJSON_AddStringToObject(p2, "uri", doc->uri);
        lsp_vesta_request(st->lsp, "vesta/paramHints", p2, vl_on_param_hints,
                          st);
    }
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

    /* Vista correlada "Godbolt" (solo-LSP): si el server trae asm_lines
     * [{addr,text,line}] + source [{line,text}], renderizamos el bloque
     * fuente y luego el asm con un prefijo de linea por instruccion, para
     * ver de un vistazo que instruccion(es) genera cada linea .vex.  Si no
     * vienen (server viejo / no compilable), caemos al texto plano. */
    cJSON *asm_lines = cJSON_GetObjectItemCaseSensitive(result, "asm_lines");
    if (cJSON_IsArray(asm_lines) && cJSON_GetArraySize(asm_lines) > 0) {
        cJSON *src = cJSON_GetObjectItemCaseSensitive(result, "source");
        if (cJSON_IsArray(src) && cJSON_GetArraySize(src) > 0) {
            insp_emit(st, INSP_KEY "--- fuente ---" INSP_RST "\n");
            cJSON *s = NULL;
            cJSON_ArrayForEach(s, src) {
                int ln = insp_num(s, "line", 0);
                const char *tx = insp_str(s, "text", "");
                char buf[1024];
                snprintf(buf, sizeof buf,
                         INSP_DIM "L%-4d" INSP_RST " %s\n", ln, tx);
                insp_emit(st, buf);
            }
            insp_emit(st, "\n");
        }
        insp_emit(st, INSP_KEY
                  "--- nativo (linea | offset | instruccion) ---" INSP_RST
                  "\n");
        cJSON *a = NULL;
        int prev_line = -1;
        cJSON_ArrayForEach(a, asm_lines) {
            int ln = insp_num(a, "line", 0);
            const char *addr = insp_str(a, "addr", "");
            const char *tx = insp_str(a, "text", "");
            char head[32];
            /* Mostrar el numero de linea solo cuando cambia (agrupa la rafaga
             * de instrs de una misma linea); las repeticiones van en blanco.
             * Linea 0 = prologo/epilogo/sintetico -> punto medio gris. */
            if (ln == 0)
                snprintf(head, sizeof head, INSP_DIM "   . " INSP_RST);
            else if (ln != prev_line)
                snprintf(head, sizeof head, INSP_KEY "L%-4d" INSP_RST, ln);
            else
                snprintf(head, sizeof head, "     ");
            prev_line = ln;
            char buf[1280];
            snprintf(buf, sizeof buf, "%s " INSP_DIM "+%s" INSP_RST "  %s\n",
                     head, addr, tx);
            insp_emit(st, buf);
        }
        /* Stack frame (debug-info): slots, offset, tamano y que valor/var. */
        cJSON *frame = cJSON_GetObjectItemCaseSensitive(result, "frame");
        if (cJSON_IsArray(frame) && cJSON_GetArraySize(frame) > 0) {
            insp_emit(st, "\n" INSP_KEY
                          "--- stack frame (offset | tam | clase | valor) ---"
                          INSP_RST "\n");
            cJSON *f = NULL;
            cJSON_ArrayForEach(f, frame) {
                char buf[512];
                snprintf(buf, sizeof buf,
                         INSP_DIM "%-10s" INSP_RST " sz=%-3d %-8s %s\n",
                         insp_str(f, "label", ""), insp_num(f, "size", 0),
                         insp_str(f, "kind", ""), insp_str(f, "name", ""));
                insp_emit(st, buf);
            }
        }
        return;
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
/* Hover rico (popup con pestanas doc/IR/bytecode/JIT/AOT).                   */
/* ------------------------------------------------------------------------- */

/* Contexto de una peticion del hover: identifica la generacion (para descartar
 * respuestas obsoletas si el raton ya se movio) y la pestana a rellenar. */
typedef struct {
    VlState *st;
    int gen;
    int tab; /* -1 = symbolInfo; 0..4 = pestana a rellenar */
} HoverReq;

static HoverReq *hover_req_new(VlState *st, int tab) {
    HoverReq *r = (HoverReq *)malloc(sizeof(HoverReq));
    if (r) {
        r->st = st;
        r->gen = st->hover_gen;
        r->tab = tab;
    }
    return r;
}

/* Respuesta de una pestana on-demand (ir/bytecode/jitAsm/aotAsm): rellena su
 * contenido si el hover sigue vigente. */
static void vl_on_hover_tab(void *ud, cJSON *result, cJSON *error) {
    HoverReq *rq = (HoverReq *)ud;
    if (!rq) return;
    VlState *st = rq->st;
    if (st && rq->gen == st->hover_gen && st->api->set_hover_tab) {
        const char *text = NULL, *err = NULL, *reason = NULL;
        int bytes = -1, instr = -1, nrelocs = -1, fail = 0;
        if (result && !error) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(result, "text");
            cJSON *je = cJSON_GetObjectItemCaseSensitive(result, "error");
            cJSON *ju = cJSON_GetObjectItemCaseSensitive(result, "unsupported");
            cJSON *ji = cJSON_GetObjectItemCaseSensitive(result, "incompatible");
            cJSON *jr = cJSON_GetObjectItemCaseSensitive(result, "reason");
            cJSON *jb = cJSON_GetObjectItemCaseSensitive(result, "bytes");
            cJSON *jn = cJSON_GetObjectItemCaseSensitive(result, "instructions");
            cJSON *jx = cJSON_GetObjectItemCaseSensitive(result, "relocs");
            if (cJSON_IsString(t)) text = t->valuestring;
            if (cJSON_IsString(je)) err = je->valuestring;
            if (cJSON_IsTrue(ju) || cJSON_IsTrue(ji)) fail = 1;
            if (cJSON_IsString(jr)) reason = jr->valuestring;
            if (cJSON_IsNumber(jb)) bytes = (int)jb->valuedouble;
            if (cJSON_IsNumber(jn)) instr = (int)jn->valuedouble;
            if (cJSON_IsArray(jx)) nrelocs = cJSON_GetArraySize(jx);
        }
        /* Vista "Godbolt" correlada (solo-LSP): si el server trae asm_lines
         * [{addr,text,line}] (+ source [{line,text}]) y la funcion SI se
         * compilo, codificamos el contenido con el sentinela 0x1D para que
         * render_hover_popup lo maquete con dos secciones (fuente + asm) y
         * cross-highlight por linea al pasar el raton.  Encoding por fila
         * (espejo del 0x1F del diff):
         *   byte0=0x1D byte1='\n'
         *   'H'\x1f<texto cabecera>\n        (stats: bytes/instrucciones)
         *   'S'\x1f<linea>\x1f<texto>\n      (linea fuente .vex)
         *   'A'\x1f<linea>\x1f<addr>\x1f<texto>\n  (instruccion nativa)
         * Si no hay asm_lines (server viejo / no compilable) caemos al
         * volcado plano de abajo. */
        cJSON *jal = result ? cJSON_GetObjectItemCaseSensitive(result,
                                                               "asm_lines")
                            : NULL;
        if (!err && !fail && cJSON_IsArray(jal) &&
            cJSON_GetArraySize(jal) > 0) {
            cJSON *jsrc =
                cJSON_GetObjectItemCaseSensitive(result, "source");
            /* Legenda de argumentos -> registros (que registro lleva cada
             * argumento), para la cabecera H. */
            char argbuf[512];
            int ao = 0;
            cJSON *jargs = cJSON_GetObjectItemCaseSensitive(result, "args");
            if (cJSON_IsArray(jargs)) {
                cJSON *ja = NULL;
                cJSON_ArrayForEach(ja, jargs) {
                    const char *an = insp_str(ja, "name", "?");
                    const char *ar = insp_str(ja, "reg", "?");
                    ao += snprintf(argbuf + ao, sizeof(argbuf) - ao,
                                   "%s%s=%s", ao ? ", " : "", an, ar);
                    if (ao > (int)sizeof(argbuf) - 32) break;
                }
            }
            argbuf[ao] = 0;
            size_t cap = 64 + (size_t)ao + 16;
            cJSON *it = NULL;
            if (cJSON_IsArray(jsrc))
                cJSON_ArrayForEach(it, jsrc) cap +=
                    strlen(insp_str(it, "text", "")) + 24;
            cJSON_ArrayForEach(it, jal) cap +=
                strlen(insp_str(it, "text", "")) +
                strlen(insp_str(it, "addr", "")) + 32;
            cJSON *jframe =
                cJSON_GetObjectItemCaseSensitive(result, "frame");
            if (cJSON_IsArray(jframe))
                cJSON_ArrayForEach(it, jframe) cap +=
                    strlen(insp_str(it, "label", "")) +
                    strlen(insp_str(it, "name", "")) +
                    strlen(insp_str(it, "kind", "")) + 40;
            cJSON *jirbl =
                cJSON_GetObjectItemCaseSensitive(result, "ir_by_line");
            if (cJSON_IsObject(jirbl)) {
                cJSON *ln = NULL;
                cJSON_ArrayForEach(ln, jirbl) {
                    cJSON *op = NULL;
                    cJSON_ArrayForEach(op, ln)
                        cap += (cJSON_IsString(op) ? strlen(op->valuestring)
                                                   : 0) +
                               24;
                }
            }
            cJSON *jirid =
                cJSON_GetObjectItemCaseSensitive(result, "ir_by_id");
            if (cJSON_IsObject(jirid)) {
                cJSON *en = NULL;
                cJSON_ArrayForEach(en, jirid)
                    cap += (cJSON_IsString(en) ? strlen(en->valuestring) : 0) +
                           24;
            }
            cJSON *jbn =
                cJSON_GetObjectItemCaseSensitive(result, "block_names");
            if (cJSON_IsArray(jbn)) {
                cJSON *bnm = NULL;
                cJSON_ArrayForEach(bnm, jbn)
                    cap += (cJSON_IsString(bnm) ? strlen(bnm->valuestring) : 0) +
                           24;
            }
            char *gb = (char *)malloc(cap);
            if (gb) {
                int o = 0;
                gb[o++] = 0x1D;
                gb[o++] = '\n';
                if (bytes >= 0 || instr >= 0 || ao > 0)
                    o += snprintf(gb + o, cap - o,
                                  "H\x1f%d bytes, %d instrucciones%s%s\n",
                                  bytes, instr, ao ? "  |  args: " : "",
                                  argbuf);
                if (cJSON_IsArray(jsrc)) {
                    cJSON_ArrayForEach(it, jsrc) {
                        o += snprintf(gb + o, cap - o, "S\x1f%d\x1f%s\n",
                                      insp_num(it, "line", 0),
                                      insp_str(it, "text", ""));
                    }
                }
                cJSON_ArrayForEach(it, jal) {
                    cJSON *jiid =
                        cJSON_GetObjectItemCaseSensitive(it, "ir_id");
                    unsigned iid =
                        cJSON_IsNumber(jiid) ? (unsigned)jiid->valuedouble
                                             : 0xFFFFFFFFu;
                    o += snprintf(gb + o, cap - o, "A\x1f%d\x1f%s\x1f%u\x1f%s\n",
                                  insp_num(it, "line", 0),
                                  insp_str(it, "addr", ""), iid,
                                  insp_str(it, "text", ""));
                }
                /* Filas del stack frame (debug-info): el render las pinta en
                 * una banda inferior.  Formato:
                 *   F\x1f<label>\x1f<kind>\x1f<size>\x1f<name>\n */
                if (cJSON_IsArray(jframe)) {
                    cJSON_ArrayForEach(it, jframe) {
                        o += snprintf(gb + o, cap - o,
                                      "F\x1f%s\x1f%s\x1f%d\x1f%s\n",
                                      insp_str(it, "label", ""),
                                      insp_str(it, "kind", ""),
                                      insp_num(it, "size", 0),
                                      insp_str(it, "name", ""));
                    }
                }
                /* Filas de correlacion IR<->linea: I\x1f<linea>\x1f<op IR> */
                if (cJSON_IsObject(jirbl)) {
                    cJSON *ln = NULL;
                    cJSON_ArrayForEach(ln, jirbl) {
                        cJSON *op = NULL;
                        cJSON_ArrayForEach(op, ln) {
                            if (cJSON_IsString(op))
                                o += snprintf(gb + o, cap - o, "I\x1f%s\x1f%s\n",
                                              ln->string, op->valuestring);
                        }
                    }
                }
                /* Mapa op-IR exacta: J\x1f<ir_id>\x1f<op IR> */
                if (cJSON_IsObject(jirid)) {
                    cJSON *en = NULL;
                    cJSON_ArrayForEach(en, jirid) {
                        if (cJSON_IsString(en))
                            o += snprintf(gb + o, cap - o, "J\x1f%s\x1f%s\n",
                                          en->string, en->valuestring);
                    }
                }
                /* Nombres de bloque: B\x1f<indice>\x1f<nombre> (etiquetas que
                 * dividen el contenido en el asm nativo). */
                if (cJSON_IsArray(jbn)) {
                    int bi = 0;
                    cJSON *bnm = NULL;
                    cJSON_ArrayForEach(bnm, jbn) {
                        if (cJSON_IsString(bnm))
                            o += snprintf(gb + o, cap - o, "B\x1f%d\x1f%s\n", bi,
                                          bnm->valuestring);
                        ++bi;
                    }
                }
                st->api->set_hover_tab(st->host, rq->tab, gb);
                free(gb);
            }
            free(rq);
            return;
        }
        /* Construir el contenido: cabecera con stats o el motivo de por que no
         * hay codigo (asi las pestanas JIT/AOT informan en vez de "asm pelado"
         * o "error"). */
        size_t cap = (text ? strlen(text) : 0) + 640;
        char *buf = (char *)malloc(cap);
        if (buf) {
            int off = 0;
            if (err) {
                off += snprintf(buf + off, cap - off, "// error: %s\n", err);
            } else if (fail) {
                off += snprintf(buf + off, cap - off,
                                "// No disponible para esta funcion:\n// %s\n",
                                reason ? reason : "operacion no soportada");
            } else {
                if (bytes >= 0 || instr >= 0 || nrelocs >= 0) {
                    off += snprintf(buf + off, cap - off, "//");
                    if (bytes >= 0)
                        off += snprintf(buf + off, cap - off, " %d bytes", bytes);
                    if (instr >= 0)
                        off += snprintf(buf + off, cap - off,
                                        ", %d instrucciones", instr);
                    if (nrelocs >= 0)
                        off += snprintf(buf + off, cap - off,
                                        ", %d reubicaciones", nrelocs);
                    off += snprintf(buf + off, cap - off, "\n\n");
                }
                if (text)
                    off += snprintf(buf + off, cap - off, "%s", text);
            }
            if (off == 0) snprintf(buf, cap, "(sin datos)");
            st->api->set_hover_tab(st->host, rq->tab, buf);
            free(buf);
        }
    }
    free(rq);
}

/* Respuesta de vesta/irDiff (filas alineadas): rellena DOS pestanas desde una
 * sola respuesta -- "IR Δ" unificada (indice base) e "IR ⇄" lado a lado
 * (indice base+1).  La lado-a-lado lleva un sentinela 0x1E al inicio y campos
 * separados por 0x1F que el render maqueta en dos columnas. */
static void vl_on_ir_diff(void *ud, cJSON *result, cJSON *error) {
    HoverReq *rq = (HoverReq *)ud;
    if (!rq) return;
    VlState *st = rq->st;
    int base = rq->tab;
    if (st && rq->gen == st->hover_gen && st->api->set_hover_tab) {
        cJSON *rows =
            result ? cJSON_GetObjectItemCaseSensitive(result, "rows") : NULL;
        cJSON *err =
            result ? cJSON_GetObjectItemCaseSensitive(result, "error") : NULL;
        if (error || cJSON_IsString(err) || !cJSON_IsArray(rows)) {
            const char *m = cJSON_IsString(err) ? err->valuestring : "(sin diff)";
            st->api->set_hover_tab(st->host, base, m);
        } else {
            /* Dimensionar los buffers. */
            size_t uni_cap = 2, side_cap = 8;
            cJSON *row = NULL;
            cJSON_ArrayForEach(row, rows) {
                cJSON *jl = cJSON_GetObjectItemCaseSensitive(row, "l");
                cJSON *jr = cJSON_GetObjectItemCaseSensitive(row, "r");
                size_t ll = cJSON_IsString(jl) ? strlen(jl->valuestring) : 0;
                size_t rl = cJSON_IsString(jr) ? strlen(jr->valuestring) : 0;
                uni_cap += ll + rl + 8;
                side_cap += ll + rl + 12;
            }
            char *uni = (char *)malloc(uni_cap);
            char *side = (char *)malloc(side_cap);
            if (uni && side) {
                int uo = 0, so = 0;
                side[so++] = 0x1E; /* sentinela: contenido lado-a-lado */
                side[so++] = '\n';
                cJSON_ArrayForEach(row, rows) {
                    cJSON *jk = cJSON_GetObjectItemCaseSensitive(row, "k");
                    cJSON *jl = cJSON_GetObjectItemCaseSensitive(row, "l");
                    cJSON *jr = cJSON_GetObjectItemCaseSensitive(row, "r");
                    const char *k = cJSON_IsString(jk) ? jk->valuestring : "same";
                    const char *l = cJSON_IsString(jl) ? jl->valuestring : "";
                    const char *rr = cJSON_IsString(jr) ? jr->valuestring : "";
                    char lm = ' ', rm = ' ';
                    if (strcmp(k, "del") == 0) lm = '-';
                    else if (strcmp(k, "add") == 0) rm = '+';
                    else if (strcmp(k, "chg") == 0) { lm = '-'; rm = '+'; }
                    /* unificada */
                    if (strcmp(k, "same") == 0)
                        uo += snprintf(uni + uo, uni_cap - uo, "  %s\n", l);
                    else if (strcmp(k, "del") == 0)
                        uo += snprintf(uni + uo, uni_cap - uo, "- %s\n", l);
                    else if (strcmp(k, "add") == 0)
                        uo += snprintf(uni + uo, uni_cap - uo, "+ %s\n", rr);
                    else
                        uo += snprintf(uni + uo, uni_cap - uo, "- %s\n+ %s\n", l,
                                       rr);
                    /* lado a lado: lm \x1f l \x1f rm \x1f r */
                    so += snprintf(side + so, side_cap - so,
                                   "%c\x1f%s\x1f%c\x1f%s\n", lm, l, rm, rr);
                }
                /* Empaquetar ambas sub-vistas en UN contenido 0x1C para la
                 * pestana IR unica: 0x1C + <lado a lado> + 0x1C + <unificado>.
                 * El render dibuja el selector y elige la sub-vista activa. */
                size_t comb_cap = (size_t)so + (size_t)uo + 4;
                char *comb = (char *)malloc(comb_cap);
                if (comb) {
                    int co = 0;
                    comb[co++] = 0x1C;
                    memcpy(comb + co, side, so);
                    co += so;
                    comb[co++] = 0x1C;
                    memcpy(comb + co, uni, uo);
                    co += uo;
                    comb[co] = 0;
                    st->api->set_hover_tab(st->host, base, comb);
                    free(comb);
                }
            }
            free(uni);
            free(side);
        }
    }
    free(rq);
}

/* Respuesta de vesta/symbolInfo: abre el popup con las pestanas y dispara las
 * peticiones del resto de pestanas (IR/bytecode/JIT/AOT). */
static void vl_on_symbol_info(void *ud, cJSON *result, cJSON *error) {
    HoverReq *rq = (HoverReq *)ud;
    if (!rq) return;
    VlState *st = rq->st;
    int gen = rq->gen;
    free(rq);
    if (!st || gen != st->hover_gen || !st->api->show_hover) return;
    cJSON *found =
        result ? cJSON_GetObjectItemCaseSensitive(result, "found") : NULL;
    if (error || !cJSON_IsBool(found) || !cJSON_IsTrue(found)) {
        if (st->api->hide_hover) st->api->hide_hover(st->host);
        return;
    }
    cJSON *jn = cJSON_GetObjectItemCaseSensitive(result, "name");
    cJSON *js = cJSON_GetObjectItemCaseSensitive(result, "signature");
    cJSON *jd = cJSON_GetObjectItemCaseSensitive(result, "doc");
    cJSON *jk = cJSON_GetObjectItemCaseSensitive(result, "kind");
    cJSON *jc = cJSON_GetObjectItemCaseSensitive(result, "callable");
    const char *name = cJSON_IsString(jn) ? jn->valuestring : "";
    const char *sig = cJSON_IsString(js) ? js->valuestring : "";
    const char *doc = cJSON_IsString(jd) ? jd->valuestring : "";
    const char *kind = cJSON_IsString(jk) ? jk->valuestring : "";
    int callable = cJSON_IsBool(jc) && cJSON_IsTrue(jc);

    /* Pestana Doc: nombre + categoria + firma + comentarios.  Solo se muestra
     * cuando HAY doc real (comentarios) o firma -- si no, no se anyade la
     * pestana (no ensuciar con un "Doc" vacio). */
    int has_doc = (doc && doc[0]) || (sig && sig[0]);
    /* La pestana Doc se renderiza como Markdown/HTML: cabecera para el nombre,
     * firma en `codigo`, y la doc tal cual (puede traer markdown del usuario). */
    char docbuf[4096];
    int off = 0;
    off += snprintf(docbuf + off, sizeof(docbuf) - off, "## %s  (%s)\n", name,
                    kind);
    if (sig && sig[0])
        off += snprintf(docbuf + off, sizeof(docbuf) - off, "\n`%s`\n", sig);
    if (doc && doc[0])
        off += snprintf(docbuf + off, sizeof(docbuf) - off, "\n%s\n", doc);

    if (callable) {
        /* Pestanas: [Doc?] + IR + Bytecode + JIT + AOT.  El indice base de IR
         * depende de si hay pestana Doc. */
        const char *uri = st->hover_uri;
        int base;
        /* Una sola pestana "IR" con selector interno (lado a lado / unificado);
         * comprime el espacio del popup. */
        if (has_doc) {
            const char *tabs[] = {"Doc", "IR", "Bytecode", "JIT", "AOT"};
            st->api->show_hover(st->host, tabs, 5);
            st->api->set_hover_tab(st->host, 0, docbuf);
            base = 1;
        } else {
            const char *tabs[] = {"IR", "Bytecode", "JIT", "AOT"};
            st->api->show_hover(st->host, tabs, 4);
            base = 0;
        }
        if (uri && st->lsp) {
            cJSON *p;
            /* irDiff rellena la UNICA pestana IR (base) con un contenido 0x1C
             * que empaqueta ambas sub-vistas (lado a lado + unificado); el
             * render dibuja el selector. */
            p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "uri", uri);
            cJSON_AddStringToObject(p, "function", name);
            lsp_vesta_request(st->lsp, "vesta/irDiff", p, vl_on_ir_diff,
                              hover_req_new(st, base + 0));
            p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "uri", uri);
            cJSON_AddStringToObject(p, "function", name); /* bytecode SOLO de la fn */
            lsp_vesta_request(st->lsp, "vesta/bytecode", p, vl_on_hover_tab,
                              hover_req_new(st, base + 1));
            p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "uri", uri);
            cJSON_AddStringToObject(p, "function", name);
            lsp_vesta_request(st->lsp, "vesta/jitAsm", p, vl_on_hover_tab,
                              hover_req_new(st, base + 2));
            p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "uri", uri);
            cJSON_AddStringToObject(p, "function", name);
            cJSON_AddStringToObject(p, "tier", "bare");
            lsp_vesta_request(st->lsp, "vesta/aotAsm", p, vl_on_hover_tab,
                              hover_req_new(st, base + 3));
        }
    } else if (has_doc) {
        const char *tabs[] = {"Info"};
        st->api->show_hover(st->host, tabs, 1);
        st->api->set_hover_tab(st->host, 0, docbuf);
    } else {
        /* Identificador sin nada que mostrar: no abrir popup. */
        if (st->api->hide_hover) st->api->hide_hover(st->host);
    }
}

/* Dispara el hover: pide symbolInfo del simbolo bajo (line,col). */
static void vl_request_hover(VlState *st, uint32_t line, uint32_t col) {
    if (!st->ready || !st->lsp || !st->api->show_hover) return;
    const char *path = st->api->current_path(st->host);
    if (!path || !vl_is_vex(path)) return;
    VlDoc *doc = vl_find_doc_by_path(st, path);
    if (!doc || !doc->open) return;
    st->hover_gen++; /* invalidar respuestas en vuelo del hover anterior */
    free(st->hover_uri);
    st->hover_uri = vl_strdup(doc->uri);
    cJSON *p = cJSON_CreateObject();
    if (!p) return;
    cJSON_AddStringToObject(p, "uri", doc->uri);
    cJSON_AddNumberToObject(p, "line", (double)line);
    cJSON_AddNumberToObject(p, "character", (double)col);
    lsp_vesta_request(st->lsp, "vesta/symbolInfo", p, vl_on_symbol_info,
                      hover_req_new(st, -1));
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

    case COFFEE_EVENT_TEXT_HOVER: {
        /* El raton se detuvo sobre un identificador: pedir su info y abrir el
         * popup de hover con las pestanas (doc/IR/bytecode/JIT/AOT). */
        const CoffeeHoverPos *pos = (const CoffeeHoverPos *)data;
        if (pos && st->server_alive)
            vl_request_hover(st, pos->line, pos->col);
        break;
    }

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
/** @brief true si @p p existe y es un fichero legible. */
static int vl_file_exists(const char *p) {
    if (!p || !p[0]) return 0;
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/** @brief Une @p dir [+ @p sub] + @p exe; devuelve heap si el fichero existe,
 *  o NULL.  @p sub puede ser NULL (p.ej. "<dir>/<exe>"). */
static char *vl_try_join(const char *dir, const char *sub, const char *exe) {
    if (!dir || !dir[0]) return NULL;
    char buf[1024];
    if (sub && sub[0])
        snprintf(buf, sizeof buf, "%s%c%s%c%s", dir, VL_DIR_SEP, sub,
                 VL_DIR_SEP, exe);
    else
        snprintf(buf, sizeof buf, "%s%c%s", dir, VL_DIR_SEP, exe);
    return vl_file_exists(buf) ? vl_strdup(buf) : NULL;
}

/** @brief Busca @p exe en cada entrada de la variable PATH. */
static char *vl_search_path_env(const char *exe) {
    const char *path = getenv("PATH");
    if (!path) return NULL;
    const char *p = path;
    char dir[1024];
    while (*p) {
        const char *sep = strchr(p, VL_PATH_SEP);
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        if (len > 0 && len < sizeof dir) {
            memcpy(dir, p, len);
            dir[len] = 0;
            char *r = vl_try_join(dir, NULL, exe);
            if (r) return r;
        }
        if (!sep) break;
        p = sep + 1;
    }
    return NULL;
}

/**
 * @brief Resuelve la ruta del ejecutable del servidor LSP por DESCUBRIMIENTO.
 *
 * Orden (nunca hardcodea una ruta):
 *   1. config "server_path" explicito (si el fichero existe).
 *   2. env VESTA_LSP_PATH (fichero directo o carpeta que lo contenga).
 *   3. env VESTA_HOME [+ /bin].
 *   4. PATH (cada entrada).
 *   5. instalaciones estandar del SO:
 *      Windows: %ProgramFiles%\VestaVM[\bin], C:\Program Files\VestaVM[\bin],
 *               %LOCALAPPDATA%\VestaVM, %APPDATA%\VestaVM, %USERPROFILE%\VestaVM.
 *      POSIX:   /usr/local/bin, /usr/local/share/vesta, ~/.local/share/vesta.
 * Persiste la ruta hallada en el config.  Devuelve heap (free por el llamante)
 * o NULL si no se encontro (el caller informa + puede ofrecer auto-descarga).
 */
static char *vl_resolve_server_path(VlState *st) {
    char *r = NULL;
    const char *exe = VL_EXE_NAME;

    /* 1. config explicito. */
    const char *cfg = st->api->get_config(st->host, "server_path");
    if (cfg && cfg[0] && vl_file_exists(cfg)) return vl_strdup(cfg);

    /* 2. VESTA_LSP_PATH: fichero directo o carpeta. */
    const char *env = getenv("VESTA_LSP_PATH");
    if (env && env[0]) {
        if (vl_file_exists(env))
            r = vl_strdup(env);
        else
            r = vl_try_join(env, NULL, exe);
        if (r) goto found;
    }

    /* 3. VESTA_HOME [+ bin]. */
    const char *vh = getenv("VESTA_HOME");
    if (vh && ((r = vl_try_join(vh, NULL, exe)) ||
               (r = vl_try_join(vh, "bin", exe))))
        goto found;

    /* 4. PATH. */
    if ((r = vl_search_path_env(exe))) goto found;

    /* 5. instalaciones estandar. */
#if defined(_WIN32)
    const char *pf = getenv("ProgramFiles");
    if (pf && ((r = vl_try_join(pf, "VestaVM", exe)) ||
               (r = vl_try_join(pf, "VestaVM\\bin", exe))))
        goto found;
    if ((r = vl_try_join("C:\\Program Files\\VestaVM", NULL, exe)) ||
        (r = vl_try_join("C:\\Program Files\\VestaVM", "bin", exe)))
        goto found;
    const char *la = getenv("LOCALAPPDATA");
    if (la && ((r = vl_try_join(la, "VestaVM", exe)) ||
               (r = vl_try_join(la, "VestaVM\\bin", exe)) ||
               (r = vl_try_join(la, "VestaVM\\build", exe)) ||
               (r = vl_try_join(la, "VestaVM\\src\\build", exe))))
        goto found;
    const char *ad = getenv("APPDATA");
    if (ad && (r = vl_try_join(ad, "VestaVM", exe))) goto found;
    const char *up = getenv("USERPROFILE");
    if (up && (r = vl_try_join(up, "VestaVM", exe))) goto found;
#else
    if ((r = vl_try_join("/usr/local/bin", NULL, exe)) ||
        (r = vl_try_join("/usr/local/share/vesta", NULL, exe)) ||
        (r = vl_try_join("/usr/local/share/vesta/bin", NULL, exe)))
        goto found;
    const char *home = getenv("HOME");
    if (home && ((r = vl_try_join(home, ".local/share/vesta", exe)) ||
                 (r = vl_try_join(home, ".local/share/vesta/bin", exe)) ||
                 (r = vl_try_join(home, ".local/share/vesta/build", exe)) ||
                 (r = vl_try_join(home, ".local/share/vesta/src/build", exe))))
        goto found;
#endif
    return NULL; /* no encontrado */

found:
    st->api->set_config(st->host, "server_path", r);
    return r;
}

/** @brief Carpeta de instalacion estandar para VestaVM (auto-instalacion). */
static void vl_install_dir(char *out, size_t n) {
#if defined(_WIN32)
    const char *la = getenv("LOCALAPPDATA");
    snprintf(out, n, "%s%cVestaVM", (la && la[0]) ? la : ".", VL_DIR_SEP);
#else
    const char *home = getenv("HOME");
    snprintf(out, n, "%s/.local/share/vesta", (home && home[0]) ? home : ".");
#endif
}

/* Forward-decl: arranca el servidor LSP (definido junto a register).  Lo llama
 * tanto register (cuando ya hay ruta) como el callback de fin de instalacion. */
static int vl_start_lsp_server(VlState *st);

/** @brief stdout/stderr de la auto-instalacion -> terminal del IDE. */
static void vl_install_on_data(void *ud, const char *bytes, size_t len) {
    VlState *st = (VlState *)ud;
    if (!st || !bytes || !len) return;
    char buf[4096];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, bytes, n);
    buf[n] = 0;
    st->api->channel_append(st->host, VESTA_LSP_CHAN_LOG, buf);
}

/** @brief Fin de la auto-instalacion: re-resolver la ruta y arrancar el LSP. */
static void vl_install_on_exit(void *ud, int code) {
    VlState *st = (VlState *)ud;
    if (!st) return;
    st->install_proc = NULL;
    char line[160];
    snprintf(line, sizeof line,
             "[vesta-lsp] auto-instalacion terminada (codigo %d)\n", code);
    st->api->channel_append(st->host, VESTA_LSP_CHAN_LOG, line);
    if (!st->server_path) st->server_path = vl_resolve_server_path(st);
    if (st->server_path) {
        vl_start_lsp_server(st);
    } else {
        st->api->channel_append(
            st->host, VESTA_LSP_CHAN_LOG,
            "\x1b[31m[vesta-lsp] tras instalar sigue sin encontrarse "
            "vesta_lsp (falta git/cmake/MinGW en el PATH?  revisa la salida "
            "de arriba)\x1b[0m\n");
        st->api->set_status(st->host, "vesta-lsp: instalacion fallida");
    }
}

/**
 * @brief Lanza la auto-instalacion de VestaVM desde GitHub de forma ASINCRONA.
 *
 * No congela el IDE: spawnea un subproceso (powershell / sh) cuya salida se
 * vuelca en vivo a la terminal del IDE (canal "Vesta LSP"); al terminar,
 * @c vl_install_on_exit re-resuelve la ruta y arranca el servidor.
 *
 * Flujo por defecto (override: config/env download_url, vesta_repo,
 * vesta_branch):
 *   1. si hay RELEASE en GitHub -> descarga el .zip y lo extrae;
 *   2. si NO hay -> git clone --depth 1 --branch <rama> + cmake build.
 * El build usa el generador "MinGW Makefiles" (VestaVM se compila con MinGW,
 * NO con Visual Studio).
 *
 * @return 1 si lanzo el subproceso (instalacion en curso), 0 si no pudo.
 */
static int vl_start_autoinstall(VlState *st) {
    char dir[1024];
    vl_install_dir(dir, sizeof dir);
    const char *url = st->api->get_config(st->host, "download_url");
    if (!url || !url[0]) url = getenv("VESTA_LSP_DOWNLOAD_URL");
    const char *repo = st->api->get_config(st->host, "vesta_repo");
    if (!repo || !repo[0]) repo = getenv("VESTA_REPO");
    if (!repo || !repo[0]) repo = VL_DEFAULT_REPO;
    const char *branch = st->api->get_config(st->host, "vesta_branch");
    if (!branch || !branch[0]) branch = getenv("VESTA_BRANCH");
    if (!branch || !branch[0]) branch = VL_DEFAULT_BRANCH;

    char script[8192];
    const char *exe;
    const char *argv[8];
    int argc;

#if defined(_WIN32)
    /* Script PowerShell con comillas SIMPLES (sin dobles internas) para que
     * win_build_cmdline pueda pasarlo como UN argumento (-Command). */
    if (url && url[0]) {
        snprintf(script, sizeof script,
                 "$ErrorActionPreference='Stop'; $d='%s'; "
                 "New-Item -ItemType Directory -Force $d | Out-Null; "
                 "$z=Join-Path $d 'vesta.zip'; "
                 "Write-Output ('[install] descargando '+'%s'); "
                 "Invoke-WebRequest -Uri '%s' -OutFile $z; "
                 "Expand-Archive -Force $z $d; Write-Output '[install] listo'",
                 dir, url, url);
    } else {
        snprintf(
            script, sizeof script,
            "$ErrorActionPreference='Stop'; $d='%s'; $repo='%s'; $br='%s'; "
            "New-Item -ItemType Directory -Force $d | Out-Null; "
            "Write-Output ('[install] destino: '+$d); $ok=$false; "
            "try { $api=($repo -replace 'github.com','api.github.com/repos')"
            "+'/releases/latest'; "
            "Write-Output ('[install] buscando release: '+$api); "
            "$rel=Invoke-RestMethod -Headers @{'User-Agent'='vesta-lsp'} $api; "
            "$a=$rel.assets | Where-Object { $_.name -like '*.zip' } | "
            "Select-Object -First 1; "
            "if ($a) { Write-Output ('[install] descargando '+$a.name); "
            "$z=Join-Path $d 'vesta.zip'; "
            "Invoke-WebRequest $a.browser_download_url -OutFile $z; "
            "Expand-Archive -Force $z $d; $ok=$true } "
            "else { Write-Output '[install] release sin .zip' } } "
            "catch { Write-Output ('[install] sin release: '"
            "+$_.Exception.Message) } "
            "if (-not $ok) { Write-Output '[install] clonando + compilando "
            "(MinGW)...'; $s=Join-Path $d 'src'; $b=Join-Path $d 'build'; "
            "if (Test-Path $s) { Remove-Item -Recurse -Force $s } "
            "if (Test-Path $b) { Remove-Item -Recurse -Force $b } "
            "foreach ($m in 'C:\\TDM-GCC-64\\bin','C:\\mingw64\\bin',"
            "'C:\\msys64\\mingw64\\bin','C:\\ProgramData\\mingw64\\mingw64\\bin')"
            " { if (Test-Path (Join-Path $m 'gcc.exe')) "
            "{ $env:PATH=$m+';'+$env:PATH; break } } "
            /* Forzar colores ANSI aunque stdout sea un pipe (el canal del IDE
             * los renderiza): cmake (CLICOLOR_FORCE), gcc via cmake
             * (CMAKE_COLOR_DIAGNOSTICS) y git (color.ui=always). */
            "$env:CLICOLOR_FORCE='1'; $env:CMAKE_COLOR_DIAGNOSTICS='ON'; "
            "git -c color.ui=always clone --depth 1 --recurse-submodules "
            "--branch $br $repo $s; "
            "cmake -S $s -B $b -G 'MinGW Makefiles' "
            "-DCMAKE_BUILD_TYPE=Release; "
            "cmake --build $b --target vesta_lsp } "
            "Write-Output '[install] terminado'",
            dir, repo, branch);
    }
    exe = "powershell.exe";
    argv[0] = "-NoProfile";
    argv[1] = "-ExecutionPolicy";
    argv[2] = "Bypass";
    argv[3] = "-Command";
    argv[4] = script;
    argc = 5;
#else
    if (url && url[0])
        snprintf(script, sizeof script,
                 "set -e; d='%s'; mkdir -p \"$d\"; "
                 "echo '[install] descargando %s'; "
                 "curl -fL '%s' -o \"$d/vesta.zip\"; "
                 "unzip -o \"$d/vesta.zip\" -d \"$d\"; echo '[install] listo'",
                 dir, url, url);
    else
        snprintf(script, sizeof script,
                 "set -e; export CLICOLOR_FORCE=1 CMAKE_COLOR_DIAGNOSTICS=ON; "
                 "d='%s'; repo='%s'; br='%s'; mkdir -p \"$d\"; "
                 "rm -rf \"$d/src\" \"$d/build\"; "
                 "echo '[install] clonando + compilando...'; "
                 "git -c color.ui=always clone --depth 1 --recurse-submodules "
                 "--branch \"$br\" \"$repo\" \"$d/src\"; "
                 "cmake -S \"$d/src\" -B \"$d/build\" "
                 "-DCMAKE_BUILD_TYPE=Release; "
                 "cmake --build \"$d/build\" --target vesta_lsp; "
                 "echo '[install] terminado'",
                 dir, repo, branch);
    exe = "sh";
    argv[0] = "-c";
    argv[1] = script;
    argc = 2;
#endif

    st->api->channel_append(
        st->host, VESTA_LSP_CHAN_LOG,
        "[vesta-lsp] instalando VestaVM en SEGUNDO PLANO desde GitHub "
        "(el IDE sigue usable; sigue el progreso aqui)...\n");
    st->install_proc = st->api->proc_spawn(st->host, exe, argv, argc);
    if (!st->install_proc) {
        st->api->channel_append(
            st->host, VESTA_LSP_CHAN_LOG,
            "\x1b[31m[vesta-lsp] no se pudo lanzar la auto-instalacion "
            "(falta powershell/sh?)\x1b[0m\n");
        return 0;
    }
    st->api->proc_on_data(st->host, st->install_proc, vl_install_on_data, st);
    st->api->proc_on_exit(st->host, st->install_proc, vl_install_on_exit, st);
    return 1;
}

/**
 * @brief Arranca el servidor LSP (subproceso + cliente + eventos + servicio).
 *
 * Requiere @c st->server_path ya resuelto.  Lo llaman register (cuando la ruta
 * se descubrio al cargar) y @c vl_install_on_exit (tras auto-instalar).  Hace
 * el setup completo (eventos, tick, servicio, comandos del inspector) UNA vez,
 * cuando el servidor esta disponible.  Devuelve 1 si arranco, 0 si no.
 */
static int vl_start_lsp_server(VlState *st) {
    CoffeeHost *host = st->host;
    const CoffeeApi *api = st->api;
    if (!st->server_path) return 0;

    {
        char line[640];
        snprintf(line, sizeof line, "[vesta-lsp] servidor: %s\n",
                 st->server_path);
        api->channel_append(host, VESTA_LSP_CHAN_LOG, line);
    }

    st->proc = api->proc_spawn(host, st->server_path, NULL, 0);
    if (!st->proc) {
        api->channel_append(host, VESTA_LSP_CHAN_LOG,
                            "\x1b[31m[vesta-lsp] no se pudo arrancar el "
                            "servidor\x1b[0m\n");
        api->log(host, COFFEE_LOG_ERROR, "vesta-lsp: proc_spawn fallo");
        api->set_status(host, "vesta-lsp: servidor no disponible");
        return 0;
    }
    st->server_alive = 1;

    st->lsp = lsp_create(vl_lsp_write, st);
    if (!st->lsp) {
        api->channel_append(host, VESTA_LSP_CHAN_LOG,
                            "\x1b[31m[vesta-lsp] sin memoria para el cliente LSP"
                            "\x1b[0m\n");
        api->proc_kill(host, st->proc);
        st->proc = NULL;
        st->server_alive = 0;
        return 0;
    }

    api->proc_on_data(host, st->proc, vl_on_proc_data, st);
    api->proc_on_exit(host, st->proc, vl_on_proc_exit, st);
    lsp_on_diagnostics(st->lsp, vl_on_diagnostics, st);

    char *root_uri = NULL;
    const char *root = api->workspace_root ? api->workspace_root(host) : NULL;
    if (root && root[0]) {
        root_uri = vl_path_to_uri(root);
    } else {
        const char *active = api->current_path(host);
        if (active && active[0]) root_uri = vl_path_to_uri(active);
    }
    lsp_initialize(st->lsp, root_uri, vl_on_ready, st);
    free(root_uri);

    api->subscribe_event(host, COFFEE_EVENT_FILE_OPEN, vl_on_event, st);
    api->subscribe_event(host, COFFEE_EVENT_TAB_SWITCH, vl_on_event, st);
    api->subscribe_event(host, COFFEE_EVENT_BUFFER_CHANGED, vl_on_event, st);
    api->subscribe_event(host, COFFEE_EVENT_FILE_SAVE, vl_on_event, st);
    api->subscribe_event(host, COFFEE_EVENT_TEXT_HOVER, vl_on_event, st);
    api->subscribe_event(host, COFFEE_EVENT_SHUTDOWN, vl_on_event, st);
    api->register_tick(host, vl_on_tick, st);

    st->svc.version = COFFEE_SVC_LSP_VERSION;
    st->svc.is_ready = svc_is_ready;
    st->svc.request = svc_request;
    st->svc.server_path = svc_server_path;
    st->svc.self = st;
    api->register_service(host, COFFEE_SVC_LSP_NAME, &st->svc);

    insp_register_commands(st);
    api->log(host, COFFEE_LOG_INFO, "vesta-lsp: servidor LSP arrancado");
    return 1;
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

    /* Resolver la ruta del servidor por descubrimiento (config/env/PATH/
     * instalaciones estandar).  Si falla, intentar auto-instalar desde GitHub
     * (si hay URL configurada) y reintentar. */
    /* Descubrir la ruta del servidor.  Si esta -> arrancar ya.  Si NO ->
     * auto-instalar EN SEGUNDO PLANO (sin congelar el IDE); al terminar, el
     * callback de fin arranca el servidor.  El editor queda usable mientras. */
    g_state.server_path = vl_resolve_server_path(&g_state);
    if (g_state.server_path) {
        vl_start_lsp_server(&g_state);
    } else {
        api->channel_append(
            host, VESTA_LSP_CHAN_LOG,
            "\x1b[33m[vesta-lsp] 'vesta_lsp' no encontrado (busque en "
            "VESTA_LSP_PATH, VESTA_HOME, PATH, instalaciones estandar).\x1b[0m\n"
            "[vesta-lsp] intentando auto-instalar desde GitHub...\n");
        if (!vl_start_autoinstall(&g_state)) {
            api->channel_append(
                host, VESTA_LSP_CHAN_LOG,
                "\x1b[31m[vesta-lsp] no se pudo iniciar la auto-instalacion.  "
                "Instala VestaVM, define VESTA_LSP_PATH, o configura "
                "'download_url'/'vesta_repo'.\x1b[0m\n");
            api->log(host, COFFEE_LOG_ERROR, "vesta-lsp: sin servidor");
            api->set_status(host, "vesta-lsp: servidor no disponible");
        }
    }

    api->log(host, COFFEE_LOG_INFO, "extension vesta-lsp activada");
    return 0; /* la extension queda cargada; el IDE sigue usable */
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
    /* Abortar la auto-instalacion si seguia en curso. */
    if (st->install_proc) {
        st->api->proc_kill(st->host, st->install_proc);
        st->install_proc = NULL;
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
        if (d->comptime_hints) cJSON_Delete(d->comptime_hints);
        if (d->param_hints) cJSON_Delete(d->param_hints);
        free(d->sem_cps);
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
    free(st->hover_uri);
    st->hover_uri = NULL;

    free(st->server_path);
    st->server_path = NULL;
    st->server_alive = 0;
    st->ready = 0;
}
