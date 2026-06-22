/**
 * @file test_ext_common.c
 * @brief Pruebas headless del SDK comun de extensiones (jsonrpc + lsp_client).
 *
 * No usa SDL, ni el extension host, ni un proceso real: cablea el transporte a
 * un buffer en memoria (write_fn acumula lo "enviado") y alimenta a mano los
 * bytes "recibidos" del servidor con jsonrpc_feed()/lsp_feed().  Asi se valida
 * de forma determinista:
 *   - El framing JSON-RPC: una peticion se enmarca con Content-Length correcto;
 *     una respuesta partida en dos feed se ensambla y dispara una sola vez; dos
 *     mensajes en un feed disparan ambos; una notificacion llega a su handler.
 *   - El ciclo de vida LSP: initialize -> (respuesta) -> initialized + on_ready;
 *     didOpen produce la notificacion con uri/text; publishDiagnostics entrega
 *     uri + array al callback registrado.
 */
#include "ctests.h"
#include "jsonrpc.h"
#include "lsp_client.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Transporte de prueba: acumula en un buffer crecible lo que el cliente      */
/* "escribe".  Los tests inspeccionan ese buffer para comprobar el framing.   */
/* ------------------------------------------------------------------------- */

typedef struct {
    char *buf;   /**< Bytes acumulados (null-terminado para usar strstr). */
    size_t len;  /**< Bytes validos.                                      */
    size_t cap;  /**< Capacidad reservada.                                */
} WriteSink;

/** write_fn cableado al WriteSink: copia los bytes al buffer acumulador. */
static int sink_write(void *ud, const void *bytes, size_t len) {
    WriteSink *s = (WriteSink *)ud;
    if (s->len + len + 1 > s->cap) {
        size_t cap = s->cap ? s->cap : 256;
        while (cap < s->len + len + 1) cap *= 2;
        char *nb = (char *)realloc(s->buf, cap);
        if (!nb) return -1;
        s->buf = nb;
        s->cap = cap;
    }
    memcpy(s->buf + s->len, bytes, len);
    s->len += len;
    s->buf[s->len] = '\0';
    return (int)len;
}

/** Reinicia el contenido acumulado sin liberar la capacidad. */
static void sink_reset(WriteSink *s) {
    s->len = 0;
    if (s->buf) s->buf[0] = '\0';
}

static void sink_free(WriteSink *s) {
    free(s->buf);
    s->buf = NULL;
    s->len = 0;
    s->cap = 0;
}

/**
 * @brief Enmarca @p body (un JSON ya serializado) con cabecera Content-Length.
 *
 * Devuelve una cadena recien reservada (liberar con free) y, si @p out_len no
 * es NULL, su longitud total (cabecera + cuerpo).
 */
static char *frame_message(const char *body, size_t *out_len) {
    size_t body_len = strlen(body);
    char header[64];
    int hlen = snprintf(header, sizeof header, "Content-Length: %zu\r\n\r\n",
                        body_len);
    size_t total = (size_t)hlen + body_len;
    char *out = (char *)malloc(total + 1);
    memcpy(out, header, (size_t)hlen);
    memcpy(out + hlen, body, body_len);
    out[total] = '\0';
    if (out_len) *out_len = total;
    return out;
}

/* ------------------------------------------------------------------------- */
/* jsonrpc: framing de peticion                                              */
/* ------------------------------------------------------------------------- */

/** Una peticion se enmarca con Content-Length, lleva method e id. */
static void test_jsonrpc_request_framing(void) {
    WriteSink sink = {0};
    JsonRpc *rpc = jsonrpc_create(sink_write, &sink);
    EXPECT_NOT_NULL(rpc);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "k", "v");
    long id = jsonrpc_request(rpc, "foo", params, NULL, NULL);
    EXPECT_EQ_INT((int)id, 1);

    /* El buffer enviado debe tener cabecera + cuerpo bien formado. */
    EXPECT_CONTAINS(sink.buf, "Content-Length: ");
    EXPECT_CONTAINS(sink.buf, "\r\n\r\n");
    EXPECT_CONTAINS(sink.buf, "\"method\":\"foo\"");
    EXPECT_CONTAINS(sink.buf, "\"id\":1");
    EXPECT_CONTAINS(sink.buf, "\"jsonrpc\":\"2.0\"");
    EXPECT_CONTAINS(sink.buf, "\"k\":\"v\"");

    /* El Content-Length anunciado debe igualar el tamano real del cuerpo. */
    const char *sep = strstr(sink.buf, "\r\n\r\n");
    EXPECT_NOT_NULL(sep);
    const char *body = sep + 4;
    size_t real_body = sink.len - (size_t)(body - sink.buf);
    int announced = atoi(strstr(sink.buf, "Content-Length: ") +
                         strlen("Content-Length: "));
    EXPECT_EQ_INT(announced, (int)real_body);

    jsonrpc_destroy(rpc);
    sink_free(&sink);
}

/* ------------------------------------------------------------------------- */
/* jsonrpc: respuesta correlacionada por id                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    int calls;        /**< Veces que se invoco el callback.        */
    int got_value;    /**< Valor leido de result.value.            */
    int had_error;    /**< 1 si llego con error en vez de result.  */
} RespCapture;

static void capture_response(void *ud, cJSON *result, cJSON *error) {
    RespCapture *c = (RespCapture *)ud;
    c->calls++;
    if (error) {
        c->had_error = 1;
        return;
    }
    cJSON *v = cJSON_GetObjectItemCaseSensitive(result, "value");
    if (v && cJSON_IsNumber(v)) c->got_value = (int)v->valuedouble;
}

/** La respuesta con el id de la peticion dispara su callback con el result. */
static void test_jsonrpc_response_correlation(void) {
    WriteSink sink = {0};
    JsonRpc *rpc = jsonrpc_create(sink_write, &sink);

    RespCapture cap = {0};
    long id = jsonrpc_request(rpc, "foo", NULL, capture_response, &cap);
    EXPECT_EQ_INT((int)id, 1);

    /* Servidor responde con result.value = 42 para el id 1. */
    size_t flen = 0;
    char *framed = frame_message(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"value\":42}}", &flen);
    jsonrpc_feed(rpc, framed, flen);
    free(framed);

    EXPECT_EQ_INT(cap.calls, 1);
    EXPECT_EQ_INT(cap.got_value, 42);
    EXPECT_EQ_INT(cap.had_error, 0);

    jsonrpc_destroy(rpc);
    sink_free(&sink);
}

/** Una respuesta partida en dos feed se ensambla y dispara una sola vez. */
static void test_jsonrpc_response_split(void) {
    WriteSink sink = {0};
    JsonRpc *rpc = jsonrpc_create(sink_write, &sink);

    RespCapture cap = {0};
    jsonrpc_request(rpc, "foo", NULL, capture_response, &cap);

    size_t flen = 0;
    char *framed = frame_message(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"value\":7}}", &flen);

    /* Partir el mensaje por la mitad y feedear en dos trozos. */
    size_t half = flen / 2;
    jsonrpc_feed(rpc, framed, half);
    EXPECT_EQ_INT(cap.calls, 0); /* aun incompleto */
    jsonrpc_feed(rpc, framed + half, flen - half);
    EXPECT_EQ_INT(cap.calls, 1); /* ahora se dispara una vez */
    EXPECT_EQ_INT(cap.got_value, 7);

    free(framed);
    jsonrpc_destroy(rpc);
    sink_free(&sink);
}

/** Dos mensajes en un solo feed disparan ambos callbacks. */
static void test_jsonrpc_two_in_one_feed(void) {
    WriteSink sink = {0};
    JsonRpc *rpc = jsonrpc_create(sink_write, &sink);

    RespCapture cap1 = {0}, cap2 = {0};
    long id1 = jsonrpc_request(rpc, "a", NULL, capture_response, &cap1);
    long id2 = jsonrpc_request(rpc, "b", NULL, capture_response, &cap2);
    EXPECT_EQ_INT((int)id1, 1);
    EXPECT_EQ_INT((int)id2, 2);

    size_t l1 = 0, l2 = 0;
    char *m1 = frame_message(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"value\":11}}", &l1);
    char *m2 = frame_message(
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"value\":22}}", &l2);

    /* Concatenar los dos mensajes en un unico buffer y feedear de golpe. */
    char *both = (char *)malloc(l1 + l2 + 1);
    memcpy(both, m1, l1);
    memcpy(both + l1, m2, l2);
    jsonrpc_feed(rpc, both, l1 + l2);

    EXPECT_EQ_INT(cap1.calls, 1);
    EXPECT_EQ_INT(cap1.got_value, 11);
    EXPECT_EQ_INT(cap2.calls, 1);
    EXPECT_EQ_INT(cap2.got_value, 22);

    free(m1);
    free(m2);
    free(both);
    jsonrpc_destroy(rpc);
    sink_free(&sink);
}

/* ------------------------------------------------------------------------- */
/* jsonrpc: notificacion entrante                                            */
/* ------------------------------------------------------------------------- */

typedef struct {
    int calls;
    char method_seen[64];
} NotifCapture;

static void capture_notification(void *ud, cJSON *params) {
    NotifCapture *c = (NotifCapture *)ud;
    c->calls++;
    cJSON *m = cJSON_GetObjectItemCaseSensitive(params, "msg");
    if (m && cJSON_IsString(m)) {
        strncpy(c->method_seen, m->valuestring, sizeof c->method_seen - 1);
        c->method_seen[sizeof c->method_seen - 1] = '\0';
    }
}

/** Una notificacion entrante (sin id) llega al handler registrado por metodo. */
static void test_jsonrpc_notification(void) {
    WriteSink sink = {0};
    JsonRpc *rpc = jsonrpc_create(sink_write, &sink);

    NotifCapture cap = {0};
    jsonrpc_on_notification(rpc, "window/logMessage", capture_notification,
                            &cap);

    size_t flen = 0;
    char *framed = frame_message(
        "{\"jsonrpc\":\"2.0\",\"method\":\"window/logMessage\","
        "\"params\":{\"msg\":\"hola\"}}",
        &flen);
    jsonrpc_feed(rpc, framed, flen);
    free(framed);

    EXPECT_EQ_INT(cap.calls, 1);
    EXPECT_EQ_STR(cap.method_seen, "hola");

    /* Una notificacion de otro metodo no debe disparar este handler. */
    char *other = frame_message(
        "{\"jsonrpc\":\"2.0\",\"method\":\"otro\",\"params\":{}}", &flen);
    jsonrpc_feed(rpc, other, flen);
    free(other);
    EXPECT_EQ_INT(cap.calls, 1); /* sigue en 1 */

    jsonrpc_destroy(rpc);
    sink_free(&sink);
}

/* ------------------------------------------------------------------------- */
/* lsp_client: ciclo de vida                                                 */
/* ------------------------------------------------------------------------- */

typedef struct {
    int ready_calls;
} ReadyCapture;

static void capture_ready(void *ud) {
    ((ReadyCapture *)ud)->ready_calls++;
}

/** initialize produce el request con rootUri; su respuesta envia initialized. */
static void test_lsp_lifecycle(void) {
    WriteSink sink = {0};
    LspClient *c = lsp_create(sink_write, &sink);
    EXPECT_NOT_NULL(c);

    ReadyCapture cap = {0};
    lsp_initialize(c, "file:///workspace", capture_ready, &cap);

    /* El request initialize debe llevar el rootUri correcto. */
    EXPECT_CONTAINS(sink.buf, "\"method\":\"initialize\"");
    EXPECT_CONTAINS(sink.buf, "\"rootUri\":\"file:///workspace\"");
    EXPECT_EQ_INT(cap.ready_calls, 0); /* aun no listo */

    /* Servidor responde a initialize (id 1).  El cliente debe emitir
     * "initialized" y disparar on_ready. */
    sink_reset(&sink);
    size_t flen = 0;
    char *framed = frame_message(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"capabilities\":{}}}",
        &flen);
    lsp_feed(c, framed, flen);
    free(framed);

    EXPECT_CONTAINS(sink.buf, "\"method\":\"initialized\"");
    EXPECT_EQ_INT(cap.ready_calls, 1);

    lsp_destroy(c);
    sink_free(&sink);
}

/** didOpen produce la notificacion con uri, languageId, version y text. */
static void test_lsp_did_open(void) {
    WriteSink sink = {0};
    LspClient *c = lsp_create(sink_write, &sink);

    lsp_did_open(c, "file:///a.vex", "vesta", 1, "let x = 1");

    EXPECT_CONTAINS(sink.buf, "\"method\":\"textDocument/didOpen\"");
    EXPECT_CONTAINS(sink.buf, "\"uri\":\"file:///a.vex\"");
    EXPECT_CONTAINS(sink.buf, "\"languageId\":\"vesta\"");
    EXPECT_CONTAINS(sink.buf, "\"version\":1");
    EXPECT_CONTAINS(sink.buf, "\"text\":\"let x = 1\"");

    lsp_destroy(c);
    sink_free(&sink);
}

/* ------------------------------------------------------------------------- */
/* lsp_client: diagnosticos                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    int calls;
    char uri[128];
    int n_diags;
} DiagCapture;

static void capture_diagnostics(void *ud, const char *uri, cJSON *diagnostics) {
    DiagCapture *c = (DiagCapture *)ud;
    c->calls++;
    strncpy(c->uri, uri ? uri : "", sizeof c->uri - 1);
    c->uri[sizeof c->uri - 1] = '\0';
    c->n_diags = diagnostics ? cJSON_GetArraySize(diagnostics) : -1;
}

/** publishDiagnostics entrega el uri y el array de diagnosticos al callback. */
static void test_lsp_diagnostics(void) {
    WriteSink sink = {0};
    LspClient *c = lsp_create(sink_write, &sink);

    DiagCapture cap = {0};
    lsp_on_diagnostics(c, capture_diagnostics, &cap);

    size_t flen = 0;
    char *framed = frame_message(
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
        "\"params\":{\"uri\":\"file:///b.vex\",\"diagnostics\":["
        "{\"message\":\"err1\"},{\"message\":\"err2\"}]}}",
        &flen);
    lsp_feed(c, framed, flen);
    free(framed);

    EXPECT_EQ_INT(cap.calls, 1);
    EXPECT_EQ_STR(cap.uri, "file:///b.vex");
    EXPECT_EQ_INT(cap.n_diags, 2);

    lsp_destroy(c);
    sink_free(&sink);
}

int main(void) {
    tt_suite("ext_common");
    tt_run("jsonrpc: framing de peticion (Content-Length, method, id)",
           test_jsonrpc_request_framing);
    tt_run("jsonrpc: respuesta correlacionada por id dispara el callback",
           test_jsonrpc_response_correlation);
    tt_run("jsonrpc: respuesta partida en dos feed se ensambla una vez",
           test_jsonrpc_response_split);
    tt_run("jsonrpc: dos mensajes en un feed disparan ambos",
           test_jsonrpc_two_in_one_feed);
    tt_run("jsonrpc: notificacion entrante llega a su handler",
           test_jsonrpc_notification);
    tt_run("lsp: initialize -> respuesta -> initialized + on_ready",
           test_lsp_lifecycle);
    tt_run("lsp: didOpen produce la notificacion con uri/text",
           test_lsp_did_open);
    tt_run("lsp: publishDiagnostics entrega uri + array", test_lsp_diagnostics);
    return tt_summary();
}
