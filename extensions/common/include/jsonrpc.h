/**
 * @file jsonrpc.h
 * @brief Cliente JSON-RPC 2.0 sobre un transporte de bytes, agnostico del IDE.
 *
 * Implementa el framing estandar de Language Server Protocol
 * ("Content-Length: N\r\n\r\n<json de N bytes>") sobre cualquier transporte de
 * bytes: el llamante provee una funcion de escritura (que cableara al
 * subproceso via proc_write) y alimenta los bytes que llegan del servidor (via
 * proc_on_data) con jsonrpc_feed().  El SDK no conoce SDL ni el core del IDE.
 *
 * Correlacion de peticiones: cada jsonrpc_request() asigna un id monotono y
 * guarda el callback; cuando llega una respuesta con ese id se invoca y se
 * libera.  Las notificaciones entrantes (sin id) se entregan al handler
 * registrado por metodo con jsonrpc_on_notification().
 *
 * No usa hilos: todo el dispatch ocurre en el hilo que llama a jsonrpc_feed().
 */
#ifndef COFFEE_EXT_JSONRPC_H
#define COFFEE_EXT_JSONRPC_H

#include "cJSON.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cliente JSON-RPC opaco. */
typedef struct JsonRpc JsonRpc;

/**
 * @brief Callback de respuesta a una peticion previamente enviada.
 *
 * Exactamente uno de @p result / @p error es no NULL.  Los nodos cJSON
 * pertenecen al JsonRpc y se liberan al volver del callback: si necesitas
 * conservar algo, duplicalo con cJSON_Duplicate().
 *
 * @param ud     Dato de usuario pasado a jsonrpc_request().
 * @param result Campo "result" de la respuesta, o NULL si hubo error.
 * @param error  Campo "error" de la respuesta, o NULL si fue exitosa.
 */
typedef void (*JsonRpcResponseFn)(void *ud, cJSON *result, cJSON *error);

/**
 * @brief Callback de notificacion entrante del servidor (sin id).
 *
 * @param ud     Dato de usuario registrado con jsonrpc_on_notification().
 * @param params Campo "params" de la notificacion (puede ser NULL).  Propiedad
 *               del JsonRpc; valido solo durante la llamada.
 */
typedef void (*JsonRpcNotifyFn)(void *ud, cJSON *params);

/**
 * @brief Crea un cliente JSON-RPC.
 *
 * @param write_fn Funcion que envia bytes crudos al transporte (p.ej. el stdin
 *                 del subproceso).  Debe devolver el numero de bytes escritos o
 *                 un valor < 0 en error.  No puede ser NULL.
 * @param write_ud Dato de usuario opaco que se pasa tal cual a @p write_fn.
 * @return Cliente nuevo, o NULL si falta memoria o @p write_fn es NULL.
 */
JsonRpc *jsonrpc_create(int (*write_fn)(void *ud, const void *bytes, size_t len),
                        void *write_ud);

/**
 * @brief Destruye el cliente y libera todos sus recursos.
 *
 * Los callbacks de peticiones pendientes se descartan sin invocar.  Acepta NULL.
 */
void jsonrpc_destroy(JsonRpc *rpc);

/**
 * @brief Alimenta bytes crudos recibidos del transporte (stdout del servidor).
 *
 * Maneja mensajes partidos en varios feed (acumula en un buffer interno),
 * varios mensajes en un solo feed, y cabeceras extra antes de la separacion
 * "\r\n\r\n".  Por cada mensaje JSON completo despacha la respuesta o
 * notificacion correspondiente.  Es robusto ante basura: si la cabecera no
 * trae Content-Length valido descarta hasta resincronizar.
 *
 * @param rpc   Cliente.
 * @param bytes Bytes recibidos (no necesita estar null-terminado).
 * @param len   Numero de bytes en @p bytes.
 */
void jsonrpc_feed(JsonRpc *rpc, const char *bytes, size_t len);

/**
 * @brief Envia una peticion con id y registra su callback de respuesta.
 *
 * Toma propiedad de @p params: lo serializa, lo enmarca con Content-Length y lo
 * envia por write_fn, y luego lo libera.  Pasa NULL si la peticion no lleva
 * params.
 *
 * @param rpc         Cliente.
 * @param method      Nombre del metodo JSON-RPC (p.ej. "initialize").
 * @param params      Params de la peticion (se consume), o NULL.
 * @param on_response Callback al recibir la respuesta correlacionada, o NULL.
 * @param ud          Dato de usuario para @p on_response.
 * @return El id asignado a la peticion (> 0), o -1 en error.
 */
long jsonrpc_request(JsonRpc *rpc, const char *method, cJSON *params,
                     JsonRpcResponseFn on_response, void *ud);

/**
 * @brief Envia una notificacion (sin id, sin respuesta esperada).
 *
 * Toma propiedad de @p params (lo consume).  Pasa NULL si no lleva params.
 *
 * @param rpc    Cliente.
 * @param method Nombre del metodo de la notificacion.
 * @param params Params (se consume), o NULL.
 */
void jsonrpc_notify(JsonRpc *rpc, const char *method, cJSON *params);

/**
 * @brief Registra un handler para una notificacion entrante por nombre.
 *
 * Si ya habia un handler para @p method se reemplaza.  El handler se invoca
 * cada vez que llega una notificacion con ese metodo.
 *
 * @param rpc    Cliente.
 * @param method Nombre del metodo a escuchar.
 * @param cb     Callback (NULL elimina el handler).
 * @param ud     Dato de usuario para @p cb.
 */
void jsonrpc_on_notification(JsonRpc *rpc, const char *method,
                             JsonRpcNotifyFn cb, void *ud);

#ifdef __cplusplus
}
#endif

#endif /* COFFEE_EXT_JSONRPC_H */
