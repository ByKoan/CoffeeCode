/**
 * @file vec.h
 * @brief Vector genérico (array dinámico) basado en @c void* y tamaño de elemento.
 *
 * Almacena elementos de cualquier tipo en un buffer contiguo que crece de forma
 * amortizada O(1) (duplicando capacidad). Es type-erased: el tamaño del elemento
 * se fija en @ref vec_init y todas las operaciones trabajan con copias de bytes.
 *
 * Acceso en caliente: usa el accesor @c static @c inline ::vec_at o la macro
 * tipada ::VEC_AT para evitar coste de llamada en bucles ajustados.
 *
 * @code
 * Vec v;
 * vec_init(&v, sizeof(int));
 * int x = 42;
 * vec_push(&v, &x);
 * int y = VEC_AT(&v, int, 0);   // 42, sin cast manual
 * vec_free(&v);
 * @endcode
 */
#pragma once

#include <stddef.h>

/**
 * @brief Vector dinámico genérico.
 *
 * Invariantes: @c len @c <= @c cap, y @c data apunta a @c cap*elem bytes (o es
 * @c NULL si @c cap==0). Los elementos válidos son @c [0, len).
 */
typedef struct {
    void   *data; /**< Buffer contiguo de @c cap*elem bytes (o @c NULL). */
    size_t  len;  /**< Número de elementos en uso. */
    size_t  cap;  /**< Capacidad actual, en elementos. */
    size_t  elem; /**< Tamaño de cada elemento, en bytes. */
} Vec;

/**
 * @brief Inicializa un vector vacío para elementos de @p elem_size bytes.
 * @param v Vector a inicializar (no nulo).
 * @param elem_size Tamaño de cada elemento en bytes (> 0).
 * @return 1 siempre (no reserva memoria hasta el primer @ref vec_push).
 */
int vec_init(Vec *v, size_t elem_size);

/**
 * @brief Inicializa un vector reservando capacidad inicial.
 * @param v Vector a inicializar (no nulo).
 * @param elem_size Tamaño de cada elemento en bytes (> 0).
 * @param cap Capacidad inicial en elementos.
 * @return 1 si se reservó memoria correctamente, 0 si falló @c malloc.
 */
int vec_init_cap(Vec *v, size_t elem_size, size_t cap);

/**
 * @brief Libera la memoria del vector y lo deja en estado vacío reutilizable.
 * @param v Vector (no nulo). Seguro llamar varias veces.
 */
void vec_free(Vec *v);

/**
 * @brief Garantiza capacidad para al menos @p cap elementos sin cambiar @c len.
 * @param v Vector (no nulo).
 * @param cap Capacidad mínima deseada en elementos.
 * @return 1 si hay capacidad suficiente, 0 si falló la reasignación.
 */
int vec_reserve(Vec *v, size_t cap);

/**
 * @brief Ajusta el número de elementos a @p n.
 *
 * Si crece, los elementos nuevos se inicializan a cero. Si decrece, se descartan.
 * @param v Vector (no nulo).
 * @param n Nuevo número de elementos.
 * @return 1 en éxito, 0 si falló la reasignación al crecer.
 */
int vec_resize(Vec *v, size_t n);

/**
 * @brief Vacía el vector (pone @c len a 0) conservando la capacidad.
 * @param v Vector (no nulo).
 */
void vec_clear(Vec *v);

/**
 * @brief Añade una copia de @p item al final.
 * @param v Vector (no nulo).
 * @param item Puntero a @c elem bytes a copiar (no nulo).
 * @return 1 en éxito, 0 si falló al crecer.
 */
int vec_push(Vec *v, const void *item);

/**
 * @brief Reserva un hueco al final y devuelve un puntero a él, sin copiar.
 *
 * Útil para construir el elemento en sitio y evitar una copia extra en rutas
 * calientes. El contenido del hueco es indeterminado.
 * @param v Vector (no nulo).
 * @return Puntero al nuevo elemento, o @c NULL si falló al crecer.
 * @warning El puntero se invalida en la siguiente operación que realoque.
 */
void *vec_push_slot(Vec *v);

/**
 * @brief Extrae el último elemento.
 * @param v Vector (no nulo).
 * @param out Destino de @c elem bytes, o @c NULL para descartar.
 * @return 1 si había elemento, 0 si el vector estaba vacío.
 */
int vec_pop(Vec *v, void *out);

/**
 * @brief Inserta una copia de @p item en la posición @p idx desplazando el resto.
 * @param v Vector (no nulo).
 * @param idx Posición de inserción en @c [0, len].
 * @param item Puntero a @c elem bytes (no nulo).
 * @return 1 en éxito, 0 si @p idx fuera de rango o falló al crecer.
 */
int vec_insert(Vec *v, size_t idx, const void *item);

/**
 * @brief Elimina el elemento en @p idx desplazando los posteriores.
 * @param v Vector (no nulo).
 * @param idx Posición a eliminar en @c [0, len).
 * @return 1 en éxito, 0 si @p idx fuera de rango.
 */
int vec_remove(Vec *v, size_t idx);

/**
 * @brief Elimina el rango @c [from, to) desplazando los posteriores.
 * @param v Vector (no nulo).
 * @param from Inicio del rango (incluido).
 * @param to Fin del rango (excluido).
 * @return 1 en éxito (incluido rango vacío), 0 si el rango es inválido.
 */
int vec_remove_range(Vec *v, size_t from, size_t to);

/**
 * @brief Sobrescribe el elemento en @p idx con una copia de @p item.
 * @param v Vector (no nulo).
 * @param idx Posición en @c [0, len).
 * @param item Puntero a @c elem bytes (no nulo).
 */
void vec_set(Vec *v, size_t idx, const void *item);

/**
 * @brief Devuelve un puntero al elemento @p i (sin comprobación de límites).
 * @param v Vector (no nulo).
 * @param i Índice en @c [0, len).
 * @return Puntero al elemento @p i.
 */
static inline void *vec_at(const Vec *v, size_t i)
{
    return (char *)v->data + i * v->elem;
}

/**
 * @brief Número de elementos en uso.
 */
static inline size_t vec_len(const Vec *v) { return v->len; }

/**
 * @brief Acceso tipado por valor: equivalente a @c ((T*)v->data)[i].
 * @param v Puntero a @ref Vec.
 * @param T Tipo del elemento.
 * @param i Índice.
 */
#define VEC_AT(v, T, i) (((T *)(v)->data)[i])
