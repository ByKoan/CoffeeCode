/**
 * @file lsp_client.c
 * @brief Implementacion de la capa LSP sobre el cliente JSON-RPC.
 *
 * Construye los objetos cJSON de cada peticion/notificacion LSP y los entrega
 * al JsonRpc subyacente.  Guarda el callback "on_ready" del initialize para
 * dispararlo tras enviar "initialized", y el callback de diagnosticos para el
 * handler de la notificacion publishDiagnostics.
 */
#include "lsp_client.h"

#include <stdlib.h>
#include <string.h>

/** Estado del cliente LSP. */
struct LspClient {
    JsonRpc *rpc;            /**< Cliente JSON-RPC subyacente.            */

    LspReadyFn on_ready;     /**< Callback tras initialize+initialized.   */
    void *ready_ud;          /**< Dato de usuario para @c on_ready.       */

    LspDiagnosticsFn on_diag;/**< Callback de publishDiagnostics.         */
    void *diag_ud;           /**< Dato de usuario para @c on_diag.        */

    char **legend;           /**< tokenTypes de semanticTokens (heap), o NULL. */
    int legend_n;            /**< Numero de entradas en @c legend.         */
};

/* --- Helpers de construccion de objetos LSP ------------------------------ */

/** Crea un objeto { "uri": <uri> } reutilizable como textDocument. */
static cJSON *make_text_document(const char *uri) {
    cJSON *td = cJSON_CreateObject();
    if (td) cJSON_AddStringToObject(td, "uri", uri ? uri : "");
    return td;
}

/** Crea un objeto Position { "line": l, "character": c } (0-based). */
static cJSON *make_position(int line, int col) {
    cJSON *pos = cJSON_CreateObject();
    if (pos) {
        cJSON_AddNumberToObject(pos, "line", line);
        cJSON_AddNumberToObject(pos, "character", col);
    }
    return pos;
}

/** Construye params { textDocument:{uri}, position:{line,character} }. */
static cJSON *make_doc_pos_params(const char *uri, int line, int col) {
    cJSON *params = cJSON_CreateObject();
    if (!params) return NULL;
    cJSON_AddItemToObject(params, "textDocument", make_text_document(uri));
    cJSON_AddItemToObject(params, "position", make_position(line, col));
    return params;
}

/* --- Callbacks internos -------------------------------------------------- */

/**
 * @brief Captura la leyenda de semantic tokens del result de initialize.
 *
 * Navega result.capabilities.semanticTokensProvider.legend.tokenTypes y, si es
 * un array de strings, lo copia a c->legend (cada nombre en heap).  Si el
 * servidor no anuncia leyenda, deja c->legend en NULL.  Idempotente: libera una
 * leyenda previa antes de sobreescribir.
 */
static void capture_semantic_legend(LspClient *c, cJSON *result) {
    if (!result) return;
    cJSON *caps = cJSON_GetObjectItemCaseSensitive(result, "capabilities");
    if (!caps) return;
    cJSON *prov =
        cJSON_GetObjectItemCaseSensitive(caps, "semanticTokensProvider");
    if (!prov) return;
    cJSON *legend = cJSON_GetObjectItemCaseSensitive(prov, "legend");
    if (!legend) return;
    cJSON *types = cJSON_GetObjectItemCaseSensitive(legend, "tokenTypes");
    if (!cJSON_IsArray(types)) return;

    int n = cJSON_GetArraySize(types);
    if (n <= 0) return;

    char **arr = (char **)calloc((size_t)n, sizeof(char *));
    if (!arr) return;

    int count = 0;
    cJSON *t = NULL;
    cJSON_ArrayForEach(t, types) {
        const char *s = cJSON_IsString(t) ? t->valuestring : "";
        size_t len = strlen(s) + 1;
        char *copy = (char *)malloc(len);
        if (copy) memcpy(copy, s, len);
        arr[count++] = copy; /* puede ser NULL si malloc fallo (se trata seguro) */
    }

    /* Liberar una leyenda anterior (re-initialize). */
    if (c->legend) {
        for (int i = 0; i < c->legend_n; ++i) free(c->legend[i]);
        free(c->legend);
    }
    c->legend = arr;
    c->legend_n = count;
}

/** Respuesta de "initialize": guarda la leyenda, envia "initialized" y dispara
 *  on_ready. */
static void on_initialize_response(void *ud, cJSON *result, cJSON *error) {
    (void)error;
    LspClient *c = (LspClient *)ud;
    /* Capturar la leyenda de semantic tokens antes de marcar listo, para que
     * on_ready ya pueda construir su mapa indice -> color. */
    capture_semantic_legend(c, result);
    /* Notificar "initialized" (params objeto vacio, como exige el protocolo). */
    jsonrpc_notify(c->rpc, "initialized", cJSON_CreateObject());
    if (c->on_ready) c->on_ready(c->ready_ud);
}

/** Handler de "textDocument/publishDiagnostics": extrae uri + array. */
static void on_publish_diagnostics(void *ud, cJSON *params) {
    LspClient *c = (LspClient *)ud;
    if (!c->on_diag || !params) return;
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(params, "uri");
    cJSON *diags = cJSON_GetObjectItemCaseSensitive(params, "diagnostics");
    const char *uri_s = (uri && cJSON_IsString(uri)) ? uri->valuestring : "";
    c->on_diag(c->diag_ud, uri_s, diags);
}

/* --- API publica --------------------------------------------------------- */

LspClient *lsp_create(int (*write_fn)(void *ud, const void *bytes, size_t len),
                      void *write_ud) {
    LspClient *c = (LspClient *)calloc(1, sizeof *c);
    if (!c) return NULL;
    c->rpc = jsonrpc_create(write_fn, write_ud);
    if (!c->rpc) {
        free(c);
        return NULL;
    }
    return c;
}

void lsp_feed(LspClient *c, const char *bytes, size_t len) {
    if (c) jsonrpc_feed(c->rpc, bytes, len);
}

void lsp_destroy(LspClient *c) {
    if (!c) return;
    jsonrpc_destroy(c->rpc);
    if (c->legend) {
        for (int i = 0; i < c->legend_n; ++i) free(c->legend[i]);
        free(c->legend);
    }
    free(c);
}

const char *const *lsp_semantic_legend(LspClient *c, int *out_n) {
    if (out_n) *out_n = c ? c->legend_n : 0;
    return c ? (const char *const *)c->legend : NULL;
}

void lsp_initialize(LspClient *c, const char *root_uri, LspReadyFn on_ready,
                    void *ud) {
    if (!c) return;
    c->on_ready = on_ready;
    c->ready_ud = ud;

    /* Registrar el handler de diagnosticos desde el arranque. */
    jsonrpc_on_notification(c->rpc, "textDocument/publishDiagnostics",
                            on_publish_diagnostics, c);

    cJSON *params = cJSON_CreateObject();
    if (!params) return;
    /* processId null: el servidor no debe vigilar a un proceso padre. */
    cJSON_AddNullToObject(params, "processId");
    if (root_uri)
        cJSON_AddStringToObject(params, "rootUri", root_uri);
    else
        cJSON_AddNullToObject(params, "rootUri");

    /* Capacidades minimas del cliente: full text sync + las features que
     * exponemos.  Un servidor las usa para decidir que ofrecer. */
    cJSON *caps = cJSON_AddObjectToObject(params, "capabilities");
    if (caps) {
        cJSON *text = cJSON_AddObjectToObject(caps, "textDocument");
        if (text) {
            cJSON *sync = cJSON_AddObjectToObject(text, "synchronization");
            if (sync) {
                cJSON_AddBoolToObject(sync, "didSave", 1);
                cJSON_AddBoolToObject(sync, "dynamicRegistration", 0);
            }
            cJSON_AddObjectToObject(text, "hover");
            cJSON_AddObjectToObject(text, "definition");
            cJSON_AddObjectToObject(text, "references");
            cJSON_AddObjectToObject(text, "completion");
            cJSON_AddObjectToObject(text, "semanticTokens");
            cJSON *pub = cJSON_AddObjectToObject(text, "publishDiagnostics");
            if (pub) cJSON_AddBoolToObject(pub, "relatedInformation", 1);
        }
    }

    jsonrpc_request(c->rpc, "initialize", params, on_initialize_response, c);
}

void lsp_shutdown(LspClient *c) {
    if (!c) return;
    jsonrpc_request(c->rpc, "shutdown", NULL, NULL, NULL);
    jsonrpc_notify(c->rpc, "exit", NULL);
}

void lsp_did_open(LspClient *c, const char *uri, const char *language_id,
                  int version, const char *text) {
    if (!c) return;
    cJSON *params = cJSON_CreateObject();
    if (!params) return;
    cJSON *td = cJSON_AddObjectToObject(params, "textDocument");
    if (td) {
        cJSON_AddStringToObject(td, "uri", uri ? uri : "");
        cJSON_AddStringToObject(td, "languageId",
                                language_id ? language_id : "");
        cJSON_AddNumberToObject(td, "version", version);
        cJSON_AddStringToObject(td, "text", text ? text : "");
    }
    jsonrpc_notify(c->rpc, "textDocument/didOpen", params);
}

void lsp_did_change(LspClient *c, const char *uri, int version,
                    const char *text) {
    if (!c) return;
    cJSON *params = cJSON_CreateObject();
    if (!params) return;
    cJSON *td = cJSON_AddObjectToObject(params, "textDocument");
    if (td) {
        cJSON_AddStringToObject(td, "uri", uri ? uri : "");
        cJSON_AddNumberToObject(td, "version", version);
    }
    /* Full sync: un unico contentChange con todo el texto (sin range). */
    cJSON *changes = cJSON_AddArrayToObject(params, "contentChanges");
    if (changes) {
        cJSON *ch = cJSON_CreateObject();
        if (ch) {
            cJSON_AddStringToObject(ch, "text", text ? text : "");
            cJSON_AddItemToArray(changes, ch);
        }
    }
    jsonrpc_notify(c->rpc, "textDocument/didChange", params);
}

void lsp_did_save(LspClient *c, const char *uri) {
    if (!c) return;
    cJSON *params = cJSON_CreateObject();
    if (!params) return;
    cJSON_AddItemToObject(params, "textDocument", make_text_document(uri));
    jsonrpc_notify(c->rpc, "textDocument/didSave", params);
}

void lsp_did_close(LspClient *c, const char *uri) {
    if (!c) return;
    cJSON *params = cJSON_CreateObject();
    if (!params) return;
    cJSON_AddItemToObject(params, "textDocument", make_text_document(uri));
    jsonrpc_notify(c->rpc, "textDocument/didClose", params);
}

void lsp_hover(LspClient *c, const char *uri, int line, int col,
               LspResultFn cb, void *ud) {
    if (!c) return;
    jsonrpc_request(c->rpc, "textDocument/hover",
                    make_doc_pos_params(uri, line, col), cb, ud);
}

void lsp_definition(LspClient *c, const char *uri, int line, int col,
                    LspResultFn cb, void *ud) {
    if (!c) return;
    jsonrpc_request(c->rpc, "textDocument/definition",
                    make_doc_pos_params(uri, line, col), cb, ud);
}

void lsp_references(LspClient *c, const char *uri, int line, int col,
                    int include_decl, LspResultFn cb, void *ud) {
    if (!c) return;
    cJSON *params = make_doc_pos_params(uri, line, col);
    if (params) {
        cJSON *ctx = cJSON_AddObjectToObject(params, "context");
        if (ctx)
            cJSON_AddBoolToObject(ctx, "includeDeclaration",
                                  include_decl ? 1 : 0);
    }
    jsonrpc_request(c->rpc, "textDocument/references", params, cb, ud);
}

void lsp_completion(LspClient *c, const char *uri, int line, int col,
                    LspResultFn cb, void *ud) {
    if (!c) return;
    jsonrpc_request(c->rpc, "textDocument/completion",
                    make_doc_pos_params(uri, line, col), cb, ud);
}

void lsp_semantic_tokens_full(LspClient *c, const char *uri, LspResultFn cb,
                              void *ud) {
    if (!c) return;
    cJSON *params = cJSON_CreateObject();
    if (params)
        cJSON_AddItemToObject(params, "textDocument", make_text_document(uri));
    jsonrpc_request(c->rpc, "textDocument/semanticTokens/full", params, cb, ud);
}

void lsp_on_diagnostics(LspClient *c, LspDiagnosticsFn cb, void *ud) {
    if (!c) return;
    c->on_diag = cb;
    c->diag_ud = ud;
    /* Asegurar el registro del handler aunque se llame antes de initialize. */
    jsonrpc_on_notification(c->rpc, "textDocument/publishDiagnostics",
                            on_publish_diagnostics, c);
}
