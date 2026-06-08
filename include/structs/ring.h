/**
 * @file ring.h
 * @brief Buffer circular genérico de capacidad fija (FIFO con acceso indexado).
 *
 * Reserva @c cap elementos una sola vez. Al insertar con el buffer lleno se
 * sobrescribe el elemento más antiguo (útil para historiales acotados como
 * undo/redo). Todas las operaciones son O(1).
 *
 * El índice lógico 0 es siempre el elemento más antiguo y @c count-1 el más
 * reciente, independientemente de la posición física interna.
 *
 * @code
 * Ring r;
 * ring_init(&r, sizeof(int), 3);
 * int a=1,b=2,c=3,d=4;
 * ring_push(&r,&a); ring_push(&r,&b); ring_push(&r,&c);
 * ring_push(&r,&d);              // descarta el 1; contiene {2,3,4}
 * int oldest = *(int*)ring_at(&r,0);   // 2
 * @endcode
 */
#pragma once

#include <stddef.h>

/**
 * @brief Buffer circular de capacidad fija.
 */
typedef struct {
    void *data;   /**< Buffer de @c cap*elem bytes. */
    size_t cap;   /**< Capacidad fija en elementos. */
    size_t elem;  /**< Tamaño de cada elemento en bytes. */
    size_t head;  /**< Índice físico del elemento más antiguo. */
    size_t count; /**< Número de elementos válidos (<= cap). */
} Ring;

/**
 * @brief Inicializa el buffer reservando @p cap elementos.
 * @param r Buffer (no nulo).
 * @param elem_size Tamaño de cada elemento en bytes (> 0).
 * @param cap Capacidad fija en elementos (> 0).
 * @return 1 en éxito, 0 si falló @c malloc o @p cap es 0.
 */
int ring_init(Ring *r, size_t elem_size, size_t cap);

/**
 * @brief Libera la memoria del buffer.
 */
void ring_free(Ring *r);

/**
 * @brief Vacía el buffer (count a 0) conservando la capacidad.
 */
void ring_clear(Ring *r);

/**
 * @brief Inserta una copia de @p item como elemento más reciente.
 *
 * Si el buffer está lleno, descarta el más antiguo para hacer sitio.
 * @param r Buffer (no nulo).
 * @param item Puntero a @c elem bytes (no nulo).
 * @return 1 si no se descartó nada, 0 si se sobrescribió el más antiguo.
 */
int ring_push(Ring *r, const void *item);

/**
 * @brief Extrae el elemento más reciente.
 * @param out Destino de @c elem bytes, o @c NULL para descartar.
 * @return 1 si había elemento, 0 si estaba vacío.
 */
int ring_pop_back(Ring *r, void *out);

/**
 * @brief Extrae el elemento más antiguo.
 * @param out Destino de @c elem bytes, o @c NULL para descartar.
 * @return 1 si había elemento, 0 si estaba vacío.
 */
int ring_pop_front(Ring *r, void *out);

/**
 * @brief Puntero al elemento en el índice lógico @p i (0 = más antiguo).
 * @param r Buffer (no nulo).
 * @param i Índice en @c [0, count).
 * @return Puntero al elemento.
 */
void *ring_at(const Ring *r, size_t i);

/** @brief Elemento más reciente, o @c NULL si vacío. */
void *ring_back(const Ring *r);
/** @brief Elemento más antiguo, o @c NULL si vacío. */
void *ring_front(const Ring *r);

/** @brief Número de elementos válidos. */
static inline size_t ring_len(const Ring *r) {
    return r->count;
}
/** @brief Capacidad fija. */
static inline size_t ring_cap(const Ring *r) {
    return r->cap;
}
/** @brief 1 si está lleno. */
static inline int ring_full(const Ring *r) {
    return r->count == r->cap;
}
/** @brief 1 si está vacío. */
static inline int ring_empty(const Ring *r) {
    return r->count == 0;
}
