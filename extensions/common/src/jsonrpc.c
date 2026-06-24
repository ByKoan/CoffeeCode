/**
 * @file jsonrpc.c
 * @brief Implementacion del cliente JSON-RPC 2.0 con framing LSP.
 *
 * Mantiene un buffer de entrada en el que se acumulan los bytes recibidos de
 * jsonrpc_feed() hasta poder extraer mensajes completos
 * ("Content-Length: N\r\n\r\n<N bytes>").  Las peticiones pendientes se guardan
 * en una lista enlazada (id -> callback) y se correlacionan al llegar la
 * respuesta.  Los handlers de notificaciones se guardan en otra lista
 * (metodo -> callback).
 */
#include "jsonrpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Duplica una cadena con malloc (strdup no es C11 estandar, lo evitamos). */
static char *_strdup_local(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* --- Entradas de las listas internas ------------------------------------- */

/** Peticion en vuelo: id asignado y callback a invocar con su respuesta. */
typedef struct PendingReq {
    long id;                   /**< Id JSON-RPC de la peticion.            */
    JsonRpcResponseFn cb;      /**< Callback de respuesta (puede ser NULL).*/
    void *ud;                  /**< Dato de usuario para @c cb.            */
    struct PendingReq *next;   /**< Siguiente en la lista.                 */
} PendingReq;

/** Handler de notificacion entrante registrado por nombre de metodo. */
typedef struct NotifyHandler {
    char *method;              /**< Nombre del metodo escuchado.           */
    JsonRpcNotifyFn cb;        /**< Callback (no NULL mientras este vivo). */
    void *ud;                  /**< Dato de usuario para @c cb.            */
    struct NotifyHandler *next;/**< Siguiente en la lista.                 */
} NotifyHandler;

/** Estado del cliente JSON-RPC. */
struct JsonRpc {
    int (*write_fn)(void *ud, const void *bytes, size_t len); /**< Transporte. */
    void *write_ud;            /**< Dato de usuario del transporte.        */

    char *in_buf;              /**< Buffer de acumulacion de entrada.      */
    size_t in_len;             /**< Bytes validos en @c in_buf.            */
    size_t in_cap;             /**< Capacidad reservada de @c in_buf.      */

    long next_id;              /**< Siguiente id a asignar (monotono).     */
    PendingReq *pending;       /**< Peticiones en vuelo.                   */
    NotifyHandler *handlers;   /**< Handlers de notificaciones.            */
};

/* --- Buffer de entrada --------------------------------------------------- */

/** Asegura que @c in_buf tiene capacidad para @p extra bytes adicionales. */
static int in_buf_reserve(JsonRpc *rpc, size_t extra) {
    if (rpc->in_len + extra <= rpc->in_cap) return 1;
    size_t cap = rpc->in_cap ? rpc->in_cap : 256;
    while (cap < rpc->in_len + extra) cap *= 2;
    char *nb = (char *)realloc(rpc->in_buf, cap);
    if (!nb) return 0;
    rpc->in_buf = nb;
    rpc->in_cap = cap;
    return 1;
}

/** Descarta @p n bytes del frente del buffer de entrada (compactando). */
static void in_buf_consume(JsonRpc *rpc, size_t n) {
    if (n >= rpc->in_len) {
        rpc->in_len = 0;
        return;
    }
    memmove(rpc->in_buf, rpc->in_buf + n, rpc->in_len - n);
    rpc->in_len -= n;
}

/* --- Listas internas ----------------------------------------------------- */

/** Extrae y devuelve la peticion pendiente con @p id (NULL si no existe). */
static PendingReq *pending_take(JsonRpc *rpc, long id) {
    PendingReq **pp = &rpc->pending;
    while (*pp) {
        if ((*pp)->id == id) {
            PendingReq *p = *pp;
            *pp = p->next;
            return p;
        }
        pp = &(*pp)->next;
    }
    return NULL;
}

/** Busca el handler de notificacion registrado para @p method. */
static NotifyHandler *handler_find(JsonRpc *rpc, const char *method) {
    for (NotifyHandler *h = rpc->handlers; h; h = h->next)
        if (strcmp(h->method, method) == 0) return h;
    return NULL;
}

/* --- Envio --------------------------------------------------------------- */

/** Serializa @p msg, lo enmarca con Content-Length y lo envia.  Consume @p msg. */
static void send_framed(JsonRpc *rpc, cJSON *msg) {
    if (!msg) return;
    char *body = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    if (!body) return;

    size_t body_len = strlen(body);
    /* Cabecera "Content-Length: <num>\r\n\r\n" cabe holgada en 64 bytes. */
    char header[64];
    int hlen = snprintf(header, sizeof header,
                        "Content-Length: %zu\r\n\r\n", body_len);
    if (hlen > 0)
        rpc->write_fn(rpc->write_ud, header, (size_t)hlen);
    rpc->write_fn(rpc->write_ud, body, body_len);
    free(body);
}

/** Construye el sobre JSON-RPC base ({"jsonrpc":"2.0","method":...}). */
static cJSON *make_envelope(const char *method, cJSON *params) {
    cJSON *msg = cJSON_CreateObject();
    if (!msg) {
        cJSON_Delete(params);
        return NULL;
    }
    cJSON_AddStringToObject(msg, "jsonrpc", "2.0");
    cJSON_AddStringToObject(msg, "method", method);
    if (params) cJSON_AddItemToObject(msg, "params", params);
    return msg;
}

/* --- API publica --------------------------------------------------------- */

JsonRpc *jsonrpc_create(int (*write_fn)(void *ud, const void *bytes, size_t len),
                        void *write_ud) {
    if (!write_fn) return NULL;
    JsonRpc *rpc = (JsonRpc *)calloc(1, sizeof *rpc);
    if (!rpc) return NULL;
    rpc->write_fn = write_fn;
    rpc->write_ud = write_ud;
    rpc->next_id = 1;
    return rpc;
}

void jsonrpc_destroy(JsonRpc *rpc) {
    if (!rpc) return;
    PendingReq *p = rpc->pending;
    while (p) {
        PendingReq *n = p->next;
        free(p);
        p = n;
    }
    NotifyHandler *h = rpc->handlers;
    while (h) {
        NotifyHandler *n = h->next;
        free(h->method);
        free(h);
        h = n;
    }
    free(rpc->in_buf);
    free(rpc);
}

long jsonrpc_request(JsonRpc *rpc, const char *method, cJSON *params,
                     JsonRpcResponseFn on_response, void *ud) {
    if (!rpc || !method) {
        cJSON_Delete(params);
        return -1;
    }
    long id = rpc->next_id++;

    /* Registrar la peticion pendiente antes de enviar. */
    PendingReq *req = (PendingReq *)calloc(1, sizeof *req);
    if (!req) {
        cJSON_Delete(params);
        return -1;
    }
    req->id = id;
    req->cb = on_response;
    req->ud = ud;
    req->next = rpc->pending;
    rpc->pending = req;

    cJSON *msg = make_envelope(method, params);
    if (!msg) {
        pending_take(rpc, id);
        free(req);
        return -1;
    }
    cJSON_AddNumberToObject(msg, "id", (double)id);
    send_framed(rpc, msg);
    return id;
}

void jsonrpc_notify(JsonRpc *rpc, const char *method, cJSON *params) {
    if (!rpc || !method) {
        cJSON_Delete(params);
        return;
    }
    send_framed(rpc, make_envelope(method, params));
}

void jsonrpc_on_notification(JsonRpc *rpc, const char *method,
                             JsonRpcNotifyFn cb, void *ud) {
    if (!rpc || !method) return;
    NotifyHandler *h = handler_find(rpc, method);
    if (!cb) {
        /* Eliminar el handler existente, si lo hay. */
        if (!h) return;
        NotifyHandler **pp = &rpc->handlers;
        while (*pp && *pp != h) pp = &(*pp)->next;
        if (*pp) {
            *pp = h->next;
            free(h->method);
            free(h);
        }
        return;
    }
    if (h) {
        h->cb = cb;
        h->ud = ud;
        return;
    }
    h = (NotifyHandler *)calloc(1, sizeof *h);
    if (!h) return;
    h->method = _strdup_local(method);
    if (!h->method) {
        free(h);
        return;
    }
    h->cb = cb;
    h->ud = ud;
    h->next = rpc->handlers;
    rpc->handlers = h;
}

/* --- Despacho de mensajes entrantes -------------------------------------- */

/** Despacha un objeto JSON-RPC ya parseado (respuesta o notificacion). */
static void dispatch_message(JsonRpc *rpc, cJSON *msg) {
    cJSON *id = cJSON_GetObjectItemCaseSensitive(msg, "id");
    cJSON *method = cJSON_GetObjectItemCaseSensitive(msg, "method");

    if (id && cJSON_IsNumber(id) && !method) {
        /* Respuesta a una de nuestras peticiones. */
        PendingReq *req = pending_take(rpc, (long)id->valuedouble);
        if (req) {
            if (req->cb) {
                cJSON *result = cJSON_GetObjectItemCaseSensitive(msg, "result");
                cJSON *error = cJSON_GetObjectItemCaseSensitive(msg, "error");
                req->cb(req->ud, result, error);
            }
            free(req);
        }
        return;
    }

    if (method && cJSON_IsString(method)) {
        if (id) {
            /* Peticion del servidor al cliente: respondemos "no soportado"
             * de forma limpia para no dejar al servidor esperando. */
            cJSON *resp = cJSON_CreateObject();
            if (resp) {
                cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
                cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
                cJSON *err = cJSON_AddObjectToObject(resp, "error");
                if (err) {
                    cJSON_AddNumberToObject(err, "code", -32601);
                    cJSON_AddStringToObject(err, "message", "Method not found");
                }
                send_framed(rpc, resp);
            }
            return;
        }
        /* Notificacion entrante: entregar al handler registrado. */
        NotifyHandler *h = handler_find(rpc, method->valuestring);
        if (h && h->cb) {
            cJSON *params = cJSON_GetObjectItemCaseSensitive(msg, "params");
            h->cb(h->ud, params);
        }
    }
}

/**
 * @brief Intenta extraer un mensaje completo del frente del buffer.
 *
 * @return 1 si proceso un mensaje (y lo consumio), 0 si necesita mas bytes.
 *         En caso de cabecera invalida resincroniza descartando bytes y
 *         devuelve 1 para reintentar.
 */
static int try_extract_one(JsonRpc *rpc) {
    if (rpc->in_len == 0) return 0;

    /* Buscar el fin de cabeceras "\r\n\r\n". */
    const char *buf = rpc->in_buf;
    size_t sep = (size_t)-1;
    for (size_t i = 0; i + 3 < rpc->in_len; ++i) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
            buf[i + 3] == '\n') {
            sep = i;
            break;
        }
    }
    if (sep == (size_t)-1) {
        /* Sin fin de cabeceras todavia.  Si el buffer crece sin limite sin
         * un "\r\n", probablemente es basura: descartar para resincronizar. */
        if (rpc->in_len > 65536) {
            in_buf_consume(rpc, rpc->in_len);
        }
        return 0;
    }

    /* Parsear Content-Length dentro de la region de cabeceras [0, sep). */
    size_t content_len = 0;
    int have_len = 0;
    const char *needle = "content-length:";
    size_t hdr_end = sep;
    for (size_t i = 0; i < hdr_end;) {
        /* Localizar el final de esta linea de cabecera: el "\r\n" siguiente, o
         * el fin de la region de cabeceras si esta linea es la ultima (no lleva
         * "\r\n" propio porque el separador "\r\n\r\n" empieza justo despues). */
        size_t line_end = i;
        while (line_end < hdr_end &&
               !(line_end + 1 < hdr_end && buf[line_end] == '\r' &&
                 buf[line_end + 1] == '\n'))
            line_end++;
        /* Comparar el prefijo de la linea con "content-length:" (case-insens). */
        size_t line_len = line_end - i;
        size_t nlen = strlen(needle);
        if (line_len >= nlen) {
            int match = 1;
            for (size_t k = 0; k < nlen; ++k) {
                char ch = buf[i + k];
                if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
                if (ch != needle[k]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                /* Saltar espacios y leer el numero. */
                size_t p = i + nlen;
                while (p < line_end && (buf[p] == ' ' || buf[p] == '\t')) p++;
                content_len = 0;
                have_len = 0;
                while (p < line_end && buf[p] >= '0' && buf[p] <= '9') {
                    content_len = content_len * 10 + (size_t)(buf[p] - '0');
                    have_len = 1;
                    p++;
                }
            }
        }
        i = line_end + 2; /* saltar el "\r\n" */
    }

    size_t body_start = sep + 4;
    if (!have_len) {
        /* Cabecera sin Content-Length valido: descartar hasta el cuerpo y
         * reintentar resincronizacion. */
        in_buf_consume(rpc, body_start);
        return 1;
    }

    if (rpc->in_len < body_start + content_len) {
        /* Cuerpo aun incompleto: esperar mas bytes. */
        return 0;
    }

    /* Parsear el cuerpo JSON de exactamente content_len bytes. */
    cJSON *msg = cJSON_ParseWithLength(buf + body_start, content_len);
    /* Consumir el mensaje completo del buffer ANTES de despachar (los
     * callbacks podrian volver a feedear). */
    in_buf_consume(rpc, body_start + content_len);
    if (msg) {
        dispatch_message(rpc, msg);
        cJSON_Delete(msg);
    }
    return 1;
}

void jsonrpc_feed(JsonRpc *rpc, const char *bytes, size_t len) {
    if (!rpc || !bytes || len == 0) return;
    if (!in_buf_reserve(rpc, len)) return;
    memcpy(rpc->in_buf + rpc->in_len, bytes, len);
    rpc->in_len += len;

    /* Extraer todos los mensajes completos disponibles. */
    while (try_extract_one(rpc)) {
        /* try_extract_one consume; el bucle termina cuando faltan bytes. */
    }
}
