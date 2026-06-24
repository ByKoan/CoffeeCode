/**
 * @file svc_lsp.h
 * @brief Contrato del servicio LSP compartido entre extensiones.
 *
 * Define la vtable que una extension proveedora (p.ej. una extension de
 * lenguaje con su propio language server) publica con
 * register_service() y que cualquier consumidora obtiene con get_service().
 * Permite que otras extensiones consulten el language server (estado, request
 * generica) sin arrancar su propio servidor.
 *
 * Es solo el contrato: la implementacion vive en la extension proveedora.  Toda
 * peticion va por JSON serializado (cadenas) para no acoplar consumidor y
 * proveedor a una libreria JSON concreta a traves de la frontera del servicio.
 */
#ifndef COFFEE_EXT_SVC_LSP_H
#define COFFEE_EXT_SVC_LSP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Nombre con el que se registra/obtiene este servicio. */
#define COFFEE_SVC_LSP_NAME "coffee.svc.lsp"

/** Version del contrato; el consumidor debe comprobar version >= la que espera. */
#define COFFEE_SVC_LSP_VERSION 1u

/**
 * @brief Vtable del servicio LSP compartido.
 *
 * El primer campo es la version del contrato.  @c self es el puntero opaco al
 * estado del proveedor y se pasa como primer argumento de cada metodo.
 */
typedef struct CoffeeSvcLsp {
    /** Version de este contrato (== COFFEE_SVC_LSP_VERSION del proveedor). */
    unsigned int version;

    /**
     * @brief Indica si el language server ya termino su handshake (initialize).
     * @param self Estado opaco del proveedor.
     * @return 1 si esta listo para atender peticiones, 0 en caso contrario.
     */
    int (*is_ready)(void *self);

    /**
     * @brief Lanza una peticion LSP (estandar o custom) de forma sincrona-logica.
     *
     * Los parametros y el resultado viajan como JSON serializado para no
     * acoplar a una libreria JSON concreta.  El proveedor reserva la cadena de
     * salida con malloc(); el llamante la libera con free().
     *
     * @param self        Estado opaco del proveedor.
     * @param method      Metodo LSP, p.ej. "textDocument/hover" o un metodo
     *                    custom del proveedor.
     * @param params_json Params como JSON serializado (puede ser NULL).
     * @param out_json    [out] Recibe el "result" como JSON recien reservado, o
     *                    NULL si no hay resultado.  Liberar con free().
     * @return 0 en exito, valor < 0 en error (servidor no listo, fallo, etc.).
     */
    int (*request)(void *self, const char *method, const char *params_json,
                   char **out_json);

    /**
     * @brief Ruta absoluta del ejecutable del language server en uso.
     * @param self Estado opaco del proveedor.
     * @return Cadena propiedad del proveedor (no liberar), o NULL si se desconoce.
     */
    const char *(*server_path)(void *self);

    /** Puntero opaco al estado del proveedor (primer argumento de los metodos). */
    void *self;
} CoffeeSvcLsp;

#ifdef __cplusplus
}
#endif

#endif /* COFFEE_EXT_SVC_LSP_H */
