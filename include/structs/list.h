/**
 * @file list.h
 * @brief Lista doblemente enlazada genérica con los datos almacenados en el
 * nodo.
 *
 * Cada nodo guarda su elemento "inline" (una sola asignación por nodo, mejor
 * localidad y menos @c malloc). El tamaño del elemento se fija en ::list_init.
 * Inserción/borrado en O(1) dado un nodo; recorrido O(n).
 *
 * @code
 * List l;
 * list_init(&l, sizeof(int));
 * int x = 1; list_push_back(&l, &x);
 * for (ListNode *n = list_first(&l); n; n = n->next)
 *     printf("%d\n", LIST_DATA(n, int));
 * list_free(&l);
 * @endcode
 */
#pragma once

#include <stddef.h>

/**
 * @brief Nodo de la lista. El dato va inmediatamente después, max-alineado.
 */
typedef struct ListNode {
    struct ListNode *prev; /**< Nodo anterior, o @c NULL si es la cabeza. */
    struct ListNode *next; /**< Nodo siguiente, o @c NULL si es la cola. */
    max_align_t data[];    /**< Almacenamiento del elemento (array flexible). */
} ListNode;

/**
 * @brief Lista doblemente enlazada.
 */
typedef struct {
    ListNode *head; /**< Primer nodo, o @c NULL si vacía. */
    ListNode *tail; /**< Último nodo, o @c NULL si vacía. */
    size_t len;     /**< Número de nodos. */
    size_t elem;    /**< Tamaño de cada elemento en bytes. */
} List;

/**
 * @brief Inicializa una lista vacía para elementos de @p elem_size bytes.
 * @param l Lista a inicializar (no nula).
 * @param elem_size Tamaño de cada elemento en bytes (> 0).
 * @return 1 siempre.
 */
int list_init(List *l, size_t elem_size);

/**
 * @brief Libera todos los nodos y deja la lista vacía.
 * @param l Lista (no nula).
 */
void list_free(List *l);

/**
 * @brief Elimina todos los nodos (equivalente a ::list_free, reutilizable).
 * @param l Lista (no nula).
 */
void list_clear(List *l);

/**
 * @brief Inserta una copia de @p item al final.
 * @param l Lista (no nula).
 * @param item Puntero a @c elem bytes a copiar (no nulo).
 * @return El nodo creado, o @c NULL si falló la asignación.
 */
ListNode *list_push_back(List *l, const void *item);

/**
 * @brief Inserta una copia de @p item al principio.
 * @param l Lista (no nula).
 * @param item Puntero a @c elem bytes a copiar (no nulo).
 * @return El nodo creado, o @c NULL si falló la asignación.
 */
ListNode *list_push_front(List *l, const void *item);

/**
 * @brief Inserta una copia de @p item justo antes de @p ref.
 * @param l Lista (no nula).
 * @param ref Nodo de referencia (no nulo, perteneciente a @p l).
 * @param item Puntero a @c elem bytes a copiar (no nulo).
 * @return El nodo creado, o @c NULL si falló la asignación.
 */
ListNode *list_insert_before(List *l, ListNode *ref, const void *item);

/**
 * @brief Inserta una copia de @p item justo después de @p ref.
 * @param l Lista (no nula).
 * @param ref Nodo de referencia (no nulo, perteneciente a @p l).
 * @param item Puntero a @c elem bytes a copiar (no nulo).
 * @return El nodo creado, o @c NULL si falló la asignación.
 */
ListNode *list_insert_after(List *l, ListNode *ref, const void *item);

/**
 * @brief Extrae el primer elemento.
 * @param l Lista (no nula).
 * @param out Destino de @c elem bytes, o @c NULL para descartar.
 * @return 1 si había elemento, 0 si la lista estaba vacía.
 */
int list_pop_front(List *l, void *out);

/**
 * @brief Extrae el último elemento.
 * @param l Lista (no nula).
 * @param out Destino de @c elem bytes, o @c NULL para descartar.
 * @return 1 si había elemento, 0 si la lista estaba vacía.
 */
int list_pop_back(List *l, void *out);

/**
 * @brief Desenlaza y libera @p node de la lista.
 * @param node Nodo a eliminar (no nulo, perteneciente a @p l).
 */
void list_remove(List *l, ListNode *node);

/** @brief Primer nodo (o @c NULL si vacía). */
static inline ListNode *list_first(const List *l) {
    return l->head;
}
/** @brief Último nodo (o @c NULL si vacía). */
static inline ListNode *list_last(const List *l) {
    return l->tail;
}
/** @brief Número de elementos. */
static inline size_t list_len(const List *l) {
    return l->len;
}

/**
 * @brief Puntero al dato de un nodo.
 */
static inline void *list_data(ListNode *n) {
    return (void *)n->data;
}

/**
 * @brief Acceso tipado por valor al dato de un nodo.
 *
 * Pasa por ::list_data (que devuelve @c void*) para no romper las reglas de
 * aliasing estricto al castear el almacenamiento del nodo.
 */
#define LIST_DATA(n, T) (*(T *)list_data(n))
