/**
 * @file ext_host.h
 * @brief Extension host de CoffeeCode: cargador de DLLs + respaldo del CoffeeApi.
 *
 * El host es el nucleo del sistema de extensiones.  Implementa:
 *   - El struct @c CoffeeApi (vtable estable) respaldado por el editor/buffer
 *     activo.  Las extensiones solo ven este contrato; nunca tocan structs
 *     internos del IDE.
 *   - Un @c CoffeeHost opaco que guarda las tablas de comandos, suscripciones a
 *     eventos y servicios, mas un REGISTRO POR-EXTENSION de todo lo que cada
 *     extension registro (para revertirlo limpiamente en el unload).
 *   - El cargador de DLLs (LoadLibrary/dlopen), la resolucion del simbolo de
 *     entrada @c coffee_extension_register y la descarga ordenada.
 *   - La carga de un directorio de extensiones con orden topologico por
 *     dependencias declaradas en el manifiesto @c coffee-extension.toml.
 *
 * Diseno clave: el host trabaja sobre un @c Buffer* (no sobre el @c Editor
 * completo).  Asi las operaciones de buffer funcionan sin SDL y el smoke test
 * headless puede ejercitar el ciclo completo load -> command -> manipular
 * buffer -> unload sin arrancar la UI.  Las acciones de UI (estado, panel de
 * salida, ruta del archivo, acciones del IDE) se delegan a callbacks opcionales
 * que el editor SDL rellena al cablear el host; en el modo headless quedan a
 * NULL y el host las trata como no-ops o stubs que loguean.
 */
#ifndef COFFEE_EXT_HOST_H
#define COFFEE_EXT_HOST_H

#include "buffer/buffer.h"
#include "ext/coffee_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hooks opcionales que el editor SDL provee al host para las acciones
 *        que NO son manipulacion pura de buffer.
 *
 * Todos los punteros pueden ser NULL: en ese caso el host trata la operacion
 * como un stub que se limita a loguear.  Esto permite que el smoke test
 * headless funcione con un @c CoffeeHostBackend que solo aporta el @c Buffer*.
 */
typedef struct CoffeeHostBackend {
    Buffer *buffer; /**< buffer activo respaldado por el host (obligatorio
                         para las operaciones de buffer; puede ser NULL si la
                         extension no lo usa). */
    void *ud;       /**< userdata del editor, pasado de vuelta a los hooks. */

    /* -- UI / feedback (opcionales) -- */
    void (*set_status)(void *ud, const char *msg);
    void (*show_message)(void *ud, const char *title, const char *body);
    void (*output_append)(void *ud, const char *text);
    void (*output_clear)(void *ud);

    /* -- Canales del panel inferior (opcionales) -- */
    int (*register_channel)(void *ud, const char *id, const char *title);
    void (*channel_append)(void *ud, const char *id, const char *text);
    void (*channel_clear)(void *ud, const char *id);
    /** Mensaje de log de una extension (alimenta la pestana "Logs"). */
    void (*log_line)(void *ud, int level, const char *msg);

    /* -- Editor / archivo (opcionales) -- */
    const char *(*current_path)(void *ud);
    void (*open_file)(void *ud, const char *path);
    void (*save_file)(void *ud);
    void (*new_tab)(void *ud);

    /* -- Repintado y decoraciones (opcionales) -- */
    void (*request_repaint)(void *ud);
} CoffeeHostBackend;

/**
 * @brief Crea un host respaldado por @p backend.
 *
 * El host COPIA el contenido de @p backend (no guarda el puntero), por lo que
 * el llamante puede pasar una variable de pila.  Devuelve NULL si falla la
 * reserva.
 */
CoffeeHost *ext_host_create(const CoffeeHostBackend *backend);

/** @brief Destruye el host: descarga todas las extensiones y libera recursos. */
void ext_host_destroy(CoffeeHost *host);

/** @brief Devuelve el CoffeeApi del host (la vtable que reciben las DLLs). */
const CoffeeApi *ext_host_api(CoffeeHost *host);

/**
 * @brief Devuelve el ultimo mensaje de error legible del host.
 *
 * Cada camino de fallo de @c ext_host_load / @c ext_host_load_dir /
 * @c ext_host_reload rellena un mensaje con la causa concreta (DLL que no
 * carga con el texto del SO, manifiesto ausente, simbolo de entrada ausente,
 * ABI incompatible, dependencia faltante o ciclo, fallo del registro).  El
 * IDE lo muestra en el panel de salida / barra de estado para que el usuario
 * VEA por que una extension no cargo.
 *
 * La cadena es propiedad del host y valida hasta la siguiente operacion de
 * carga (copiar si se necesita conservar).  Nunca devuelve NULL: si no hubo
 * error, devuelve una cadena vacia.
 */
const char *ext_host_last_error(CoffeeHost *host);

/**
 * @brief Actualiza el buffer activo respaldado por el host.
 *
 * El editor SDL la llama al cambiar de pestana para que las operaciones de
 * buffer del CoffeeApi apunten al buffer correcto.
 */
void ext_host_set_buffer(CoffeeHost *host, Buffer *buffer);

/**
 * @brief Carga UNA extension desde un directorio con su manifiesto.
 *
 * @p dir debe contener @c coffee-extension.toml (con @c id, @c entry, @c abi y
 * opcionalmente @c dependencies) y la DLL referida por @c entry.  El host
 * parsea el manifiesto, carga la DLL, comprueba la ABI, resuelve el simbolo de
 * entrada y lo invoca con (host, api).  Devuelve 0 en exito, !=0 en fallo.
 */
int ext_host_load(CoffeeHost *host, const char *dir);

/**
 * @brief Descarga una extension por id.
 *
 * Invoca @c coffee_extension_unregister (si existe), revierte TODO lo que la
 * extension registro via el registro por-extension (comandos, eventos,
 * servicios) y libera la DLL.  Devuelve 0 en exito, !=0 si no existe.
 */
int ext_host_unload(CoffeeHost *host, const char *id);

/** @brief Recarga (unload + load) una extension por id.  0 = ok. */
int ext_host_reload(CoffeeHost *host, const char *id);

/**
 * @brief Escanea @p extensions_root, lee los manifiestos de cada subdirectorio,
 *        ordena topologicamente por dependencias y carga en orden.
 *
 * Una dependencia se carga ANTES que quien la declara.  Un ciclo de
 * dependencias es un error (se reporta y se aborta la carga del conjunto).
 * Devuelve el numero de extensiones cargadas con exito (>=0), o <0 en error
 * grave (p.ej. ciclo).
 */
int ext_host_load_dir(CoffeeHost *host, const char *extensions_root);

/** @brief Invoca un comando registrado por id.  0 = invocado, !=0 = no existe. */
int ext_host_run_command(CoffeeHost *host, const char *command_id);

/** @brief Notifica a los suscriptores de @p event con @p data. */
void ext_host_emit(CoffeeHost *host, CoffeeEventType event, const void *data);

/** @brief 1 si la extension @p id esta cargada y activa, 0 si no. */
int ext_host_has(CoffeeHost *host, const char *id);

/* ===========================================================================
 *  Introspeccion: listar las extensiones cargadas para el panel de
 *  extensiones del IDE (el "marketplace de cargadas").
 * =========================================================================== */

/** @brief Numero de slots de extension del host (incluye slots inactivos). */
size_t ext_host_count(CoffeeHost *host);

/**
 * @brief Devuelve los datos de la extension del slot @p idx.
 *
 * Pensada para el panel de extensiones: rellena los punteros de salida (los que
 * no sean NULL) con el id, nombre legible, directorio y estado de la extension
 * en el slot @p idx.  Las cadenas devueltas son propiedad del host y validas
 * mientras la extension siga cargada (copiar si se necesitan luego).
 *
 * @param host   Host.
 * @param idx    Indice de slot en [0, ext_host_count).
 * @param[out] id     Id del manifiesto (o NULL si el slot esta libre).
 * @param[out] name   Nombre legible (hoy = id; reservado para un campo futuro).
 * @param[out] dir    Directorio de la extension.
 * @param[out] active 1 si la extension esta activa, 0 si el slot esta libre.
 * @return 1 si @p idx es un slot valido, 0 si esta fuera de rango.
 */
int ext_host_info(CoffeeHost *host, size_t idx, const char **id,
                  const char **name, const char **dir, int *active);

/* ===========================================================================
 *  Vistas registradas: el render del IDE las dibuja.
 * =========================================================================== */

/** @brief Datos de una vista registrada por una extension (solo lectura). */
typedef struct CoffeeHostView {
    const char *id;            /**< id de la vista */
    const char *title;         /**< titulo legible */
    CoffeeViewKind kind;       /**< sidebar/panel/overlay/statusbar */
    CoffeePaintFn paint;       /**< callback de pintado */
    CoffeeViewInputFn input;   /**< callback de input (puede ser NULL) */
    void *userdata;            /**< userdata de los callbacks */
} CoffeeHostView;

/** @brief Numero de vistas registradas y vivas. */
size_t ext_host_view_count(CoffeeHost *host);

/**
 * @brief Copia los datos de la vista @p idx en @p out.
 * @return 1 si @p idx es valido y la vista esta viva, 0 si no.
 */
int ext_host_view_at(CoffeeHost *host, size_t idx, CoffeeHostView *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* COFFEE_EXT_HOST_H */
