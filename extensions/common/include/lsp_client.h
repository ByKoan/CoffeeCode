/**
 * @file lsp_client.h
 * @brief Capa fina de Language Server Protocol sobre el cliente JSON-RPC.
 *
 * Envuelve un JsonRpc para hablar LSP sin que cada extension reimplemente el
 * ciclo de vida (initialize/initialized/shutdown), la sincronizacion de
 * documentos (didOpen/didChange/didSave/didClose, en modo full-sync) ni las
 * peticiones estandar (hover, definition, references, completion,
 * semanticTokens).  Incluye soporte para peticiones custom del ecosistema
 * Vesta (lsp_vesta_request) y para la notificacion publishDiagnostics.
 *
 * El transporte se cablea igual que en jsonrpc.h: el llamante pasa una funcion
 * de escritura (proc_write del core) y alimenta los bytes entrantes con
 * lsp_feed() (proc_on_data del core).  Las posiciones LSP usan linea y caracter
 * 0-based.
 */
#ifndef COFFEE_EXT_LSP_CLIENT_H
#define COFFEE_EXT_LSP_CLIENT_H

#include "cJSON.h"
#include "jsonrpc.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cliente LSP opaco (envuelve un JsonRpc propio). */
typedef struct LspClient LspClient;

/** Callback generico que entrega el "result" de una peticion LSP. */
typedef void (*LspResultFn)(void *ud, cJSON *result, cJSON *error);

/** Callback de "server -> ready" tras completar initialize + initialized. */
typedef void (*LspReadyFn)(void *ud);

/**
 * @brief Callback de diagnosticos (textDocument/publishDiagnostics).
 *
 * @param ud          Dato de usuario registrado con lsp_on_diagnostics().
 * @param uri         URI del documento afectado.
 * @param diagnostics Array cJSON "diagnostics".  Propiedad del cliente; valido
 *                    solo durante la llamada (duplica si lo conservas).
 */
typedef void (*LspDiagnosticsFn)(void *ud, const char *uri,
                                 cJSON *diagnostics);

/**
 * @brief Crea un cliente LSP (y su JsonRpc interno).
 *
 * @param write_fn Funcion de escritura del transporte (ver jsonrpc_create).
 * @param write_ud Dato de usuario para @p write_fn.
 * @return Cliente nuevo, o NULL en error.
 */
LspClient *lsp_create(int (*write_fn)(void *ud, const void *bytes, size_t len),
                      void *write_ud);

/** Alimenta bytes del stdout del servidor (delega en jsonrpc_feed). */
void lsp_feed(LspClient *c, const char *bytes, size_t len);

/** Destruye el cliente LSP y su JsonRpc.  Acepta NULL. */
void lsp_destroy(LspClient *c);

/* --- Ciclo de vida ------------------------------------------------------- */

/**
 * @brief Envia "initialize" y, al recibir respuesta, "initialized" + on_ready.
 *
 * @param root_uri URI raiz del workspace (file://...), o NULL.
 * @param on_ready Callback al quedar el servidor listo, o NULL.
 * @param ud       Dato de usuario para @p on_ready.
 */
void lsp_initialize(LspClient *c, const char *root_uri, LspReadyFn on_ready,
                    void *ud);

/** Envia "shutdown" seguido de la notificacion "exit". */
void lsp_shutdown(LspClient *c);

/* --- Sincronizacion de documentos (full sync) ---------------------------- */

/** textDocument/didOpen con uri, languageId, version y texto completo. */
void lsp_did_open(LspClient *c, const char *uri, const char *language_id,
                  int version, const char *text);

/** textDocument/didChange en modo full-sync (un solo cambio con todo el texto). */
void lsp_did_change(LspClient *c, const char *uri, int version,
                    const char *text);

/** textDocument/didSave del documento @p uri. */
void lsp_did_save(LspClient *c, const char *uri);

/** textDocument/didClose del documento @p uri. */
void lsp_did_close(LspClient *c, const char *uri);

/* --- Peticiones estandar ------------------------------------------------- */

/** textDocument/hover en (line, character) 0-based. */
void lsp_hover(LspClient *c, const char *uri, int line, int col,
               LspResultFn cb, void *ud);

/** textDocument/definition en (line, character) 0-based. */
void lsp_definition(LspClient *c, const char *uri, int line, int col,
                    LspResultFn cb, void *ud);

/** textDocument/references; @p include_decl mapea a context.includeDeclaration. */
void lsp_references(LspClient *c, const char *uri, int line, int col,
                    int include_decl, LspResultFn cb, void *ud);

/** textDocument/completion en (line, character) 0-based. */
void lsp_completion(LspClient *c, const char *uri, int line, int col,
                    LspResultFn cb, void *ud);

/** textDocument/semanticTokens/full del documento @p uri. */
void lsp_semantic_tokens_full(LspClient *c, const char *uri, LspResultFn cb,
                              void *ud);

/* --- Peticiones custom del ecosistema Vesta ------------------------------ */

/**
 * @brief Envia una peticion LSP custom (p.ej. "vesta/bytecode").
 *
 * @param method Nombre del metodo custom.
 * @param params Params (se consume; ver jsonrpc_request), o NULL.
 * @param cb     Callback de resultado, o NULL.
 * @param ud     Dato de usuario para @p cb.
 */
void lsp_vesta_request(LspClient *c, const char *method, cJSON *params,
                       LspResultFn cb, void *ud);

/* --- Diagnosticos -------------------------------------------------------- */

/**
 * @brief Registra el handler de textDocument/publishDiagnostics.
 *
 * @param cb Callback que recibe uri + array de diagnosticos, o NULL.
 * @param ud Dato de usuario para @p cb.
 */
void lsp_on_diagnostics(LspClient *c, LspDiagnosticsFn cb, void *ud);

#ifdef __cplusplus
}
#endif

#endif /* COFFEE_EXT_LSP_CLIENT_H */
