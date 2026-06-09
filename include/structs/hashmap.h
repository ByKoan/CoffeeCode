/**
 * @file hashmap.h
 * @brief Tabla hash genérica de direccionamiento abierto (linear probing).
 *
 * Claves y valores de tamaño fijo arbitrario (definidos en ::hashmap_init).
 * Características de rendimiento:
 *  - Capacidad potencia de 2: el índice es @c hash & (cap-1) (sin módulo).
 *  - Linear probing: muy buena localidad de caché.
 *  - Borrado por desplazamiento hacia atrás (sin "tombstones"): las cadenas de
 *    sondeo se mantienen compactas y no se degradan con el tiempo.
 *  - Hash FNV-1a por defecto; se puede inyectar hash/igualdad propios (p. ej.
 *    para claves tipo cadena con ::hashmap_str_hash / ::hashmap_str_eq).
 *
 * @code
 * HashMap m;
 * hashmap_init(&m, sizeof(int), sizeof(int));
 * int k = 7, v = 100;
 * hashmap_put(&m, &k, &v);
 * int *got = hashmap_get(&m, &k);   // got != NULL, *got == 100
 * hashmap_free(&m);
 * @endcode
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Función de hash sobre @p key_size bytes de la clave.
 */
typedef size_t (*HashFn)(const void *key, size_t key_size);

/**
 * @brief Igualdad de claves: devuelve 1 si @p a y @p b son iguales.
 */
typedef int (*EqFn)(const void *a, const void *b, size_t key_size);

/**
 * @brief Tabla hash genérica.
 *
 * Disposición SoA (arrays paralelos) por ranura para sondeo cache-friendly.
 */
typedef struct {
    void *keys;      /**< @c cap*key_size bytes con las claves. */
    void *vals;      /**< @c cap*val_size bytes con los valores. */
    size_t *hashes;  /**< Hash cacheado de cada ranura ocupada. */
    uint8_t *state;  /**< 0 = vacía, 1 = ocupada. */
    size_t cap;      /**< Número de ranuras (potencia de 2). */
    size_t count;    /**< Ranuras ocupadas. */
    size_t key_size; /**< Tamaño de clave en bytes. */
    size_t val_size; /**< Tamaño de valor en bytes. */
    HashFn hash;     /**< Función de hash. */
    EqFn eq;         /**< Función de igualdad. */
} HashMap;

/**
 * @brief Inicializa una tabla con hash FNV-1a y comparación por bytes.
 * @param m Tabla (no nula).
 * @param key_size Tamaño de clave en bytes (> 0).
 * @param val_size Tamaño de valor en bytes (> 0).
 * @return 1 en éxito, 0 si falló la reserva inicial.
 */
int hashmap_init(HashMap *m, size_t key_size, size_t val_size);

/**
 * @brief Inicializa una tabla con capacidad y funciones personalizadas.
 * @param initial_cap Capacidad inicial sugerida (se redondea a potencia de 2).
 * @param hash Función de hash, o @c NULL para FNV-1a por defecto.
 * @param eq Función de igualdad, o @c NULL para comparación por bytes.
 * @return 1 en éxito, 0 si falló la reserva.
 */
int hashmap_init_ex(HashMap *m, size_t key_size, size_t val_size,
                    size_t initial_cap, HashFn hash, EqFn eq);

/**
 * @brief Libera toda la memoria de la tabla.
 */
void hashmap_free(HashMap *m);

/**
 * @brief Vacía la tabla conservando la capacidad.
 */
void hashmap_clear(HashMap *m);

/**
 * @brief Inserta o sobrescribe el valor asociado a @p key.
 * @param m Tabla (no nula).
 * @param key Puntero a @c key_size bytes (no nulo).
 * @param val Puntero a @c val_size bytes (no nulo).
 * @return 1 en éxito, 0 si falló al crecer.
 */
int hashmap_put(HashMap *m, const void *key, const void *val);

/**
 * @brief Devuelve un puntero al valor asociado a @p key.
 * @return Puntero mutable al valor, o @c NULL si la clave no existe.
 * @warning El puntero se invalida tras cualquier inserción o borrado.
 */
void *hashmap_get(const HashMap *m, const void *key);

/**
 * @brief Indica si @p key está presente.
 * @return 1 si existe, 0 si no.
 */
int hashmap_has(const HashMap *m, const void *key);

/**
 * @brief Elimina la entrada de @p key.
 * @return 1 si se eliminó, 0 si la clave no existía.
 */
int hashmap_remove(HashMap *m, const void *key);

/**
 * @brief Número de entradas.
 */
static inline size_t hashmap_len(const HashMap *m) {
    return m->count;
}

/**
 * @brief Itera sobre las entradas ocupadas.
 *
 * @param m Tabla (no nula).
 * @param iter Cursor; inicialízalo a 0 antes del primer uso.
 * @param key_out Recibe puntero a la clave (puede ser @c NULL).
 * @param val_out Recibe puntero al valor (puede ser @c NULL).
 * @return 1 si devolvió una entrada, 0 al terminar.
 *
 * @code
 * size_t it = 0; void *k, *v;
 * while (hashmap_next(&m, &it, &k, &v)) { ... }
 * @endcode
 */
int hashmap_next(const HashMap *m, size_t *iter, void **key_out,
                 void **val_out);

/**
 * @brief Hash FNV-1a para claves tipo @c const @c char* (clave = puntero a
 * cadena).
 *
 * Pásala a ::hashmap_init_ex cuando la clave almacenada sea un @c char*.
 */
size_t hashmap_str_hash(const void *key, size_t key_size);

/**
 * @brief Igualdad para claves tipo @c const @c char* (compara con @c strcmp).
 */
int hashmap_str_eq(const void *a, const void *b, size_t key_size);
