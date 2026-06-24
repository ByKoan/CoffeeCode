/**
 * @file hello.c
 * @brief Extension de ejemplo en C para CoffeeCode.
 *
 * Demuestra el ciclo completo de una extension nativa:
 *   - exporta @c coffee_extension_register (el punto de entrada que el host
 *     busca en la DLL);
 *   - comprueba la version del ABI;
 *   - registra el comando "hello.insert" que inserta "hola" en el buffer
 *     activo y actualiza la barra de estado;
 *   - se suscribe a COFFEE_EVENT_FILE_OPEN y loguea la ruta abierta;
 *   - publica un servicio trivial ("hello.service") para que otras extensiones
 *     lo consuman;
 *   - exporta @c coffee_extension_unregister (opcional) para limpieza.
 *
 * Solo enlaza el header @c coffee_ext.h: NO depende de SDL ni de internals del
 * IDE.  Se compila como DLL (.dll/.so).
 */
#include "ext/coffee_ext.h"

#include <stdio.h>

/**
 * @brief Contrato del servicio que esta extension publica.
 *
 * Otra extension lo obtiene con @c get_service("hello.service") y castea a
 * este struct.  Expone una funcion trivial de saludo.
 */
typedef struct HelloService {
    const char *(*greeting)(void); /**< devuelve un saludo constante */
} HelloService;

/** Implementacion del servicio: devuelve un saludo fijo. */
static const char *hello_greeting(void) { return "hola desde hello-c"; }

/** Instancia estatica del servicio (vive lo que dura la DLL cargada). */
static HelloService g_hello_service = {hello_greeting};

/**
 * @brief Callback del comando "hello.insert".
 *
 * Inserta "hola" en el cursor del buffer activo y actualiza la barra de estado.
 */
static void cmd_hello_insert(CoffeeHost *host, void *userdata) {
    const CoffeeApi *api = (const CoffeeApi *)userdata;
    if (!api) return;
    api->buffer_insert(host, "hola");
    api->set_status(host, "extension hello: insertado");
}

/**
 * @brief Callback suscrito a COFFEE_EVENT_FILE_OPEN.
 *
 * @p data es @c const char* con la ruta del archivo abierto.
 */
static void on_file_open(CoffeeHost *host, CoffeeEventType ev, const void *data,
                         void *userdata) {
    const CoffeeApi *api = (const CoffeeApi *)userdata;
    const char *path = (const char *)data;
    (void)ev;
    if (api) api->log(host, COFFEE_LOG_INFO, path ? path : "(sin ruta)");
}

/**
 * @brief Punto de entrada de la extension.
 *
 * El host lo invoca tras cargar la DLL.  Recibe el host y la vtable del IDE.
 * Devuelve 0 en exito (!=0 -> el host la descarta).
 */
COFFEE_EXTENSION_EXPORT int coffee_extension_register(CoffeeHost *host,
                                                      const CoffeeApi *api) {
    if (!api) return 1;
    /* rechazar si el host es mas nuevo de lo que entendemos */
    if (api->abi_version > COFFEE_ABI_VERSION) return 2;

    /* registrar el comando; pasamos la api como userdata para usarla luego */
    api->register_command(host, "hello.insert", "Insertar hola",
                          cmd_hello_insert, (void *)api);

    /* suscribirse al evento de apertura de archivo */
    api->subscribe_event(host, COFFEE_EVENT_FILE_OPEN, on_file_open,
                         (void *)api);

    /* publicar el servicio trivial */
    api->register_service(host, "hello.service", &g_hello_service);

    api->log(host, COFFEE_LOG_INFO, "extension hello-c activada");
    return 0;
}

/**
 * @brief Punto de salida opcional de la extension.
 *
 * El host ya revierte automaticamente comandos/eventos/servicios registrados;
 * aqui solo iria la liberacion de recursos propios (ninguno en este ejemplo).
 */
COFFEE_EXTENSION_EXPORT void coffee_extension_unregister(CoffeeHost *host) {
    (void)host;
    /* nada que liberar: el servicio es estatico y el host revoca el resto */
}
