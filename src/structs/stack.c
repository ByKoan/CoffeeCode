/**
 * @file stack.c
 * @brief Implementación de la pila genérica sobre ::Vec (ver stack.h).
 *
 * @note Una pila LIFO (Last In, First Out) no necesita estructura propia: es un
 * vector dinámico (::Vec) al que solo se accede por UN extremo, el final. La
 * "cima" es siempre el último elemento del vector. Por eso casi todas las
 * funciones son adaptadores de una línea sobre la API de Vec:
 *   - push  -> añadir al final del vector  (vec_push)
 *   - pop   -> quitar del final del vector  (vec_pop)
 *   - peek  -> mirar el último sin quitarlo (vec_at en len-1)
 * Como el crecimiento es responsabilidad del Vec (duplica capacidad al
 * llenarse), push/pop quedan amortizados O(1) y la pila hereda esa estrategia
 * tal cual.
 */
#include "structs/stack.h"

/**
 * @copydoc stack_init
 *
 * Delega en @c vec_init: la pila es exactamente un Vec de elementos de
 * @p elem_size bytes. Empieza vacía y sin reservar.
 */
int stack_init(Stack *s, size_t elem_size) {
    return vec_init(&s->items, elem_size); /* la pila ES un vector vacío */
}

/**
 * @copydoc stack_free
 *
 * Libera el vector subyacente; tras esto la pila no debe usarse sin
 * reinicializar.
 */
void stack_free(Stack *s) {
    vec_free(&s->items); /* libera el almacenamiento del Vec */
}

/**
 * @copydoc stack_clear
 *
 * Pone la longitud a 0 conservando la capacidad reservada (no libera memoria),
 * de modo que reutilizar la pila no vuelve a pagar reservas.
 */
void stack_clear(Stack *s) {
    vec_clear(&s->items); /* len = 0, capacidad intacta */
}

/**
 * @copydoc stack_push
 *
 * Apilar = añadir al final del vector. El nuevo elemento queda como cima.
 * Puede fallar (devolver 0) si el Vec necesitaba crecer y falló la reserva.
 */
int stack_push(Stack *s, const void *item) {
    return vec_push(&s->items, item); /* el final del Vec es la cima */
}

/**
 * @copydoc stack_pop
 *
 * Desapilar = quitar el último del vector. @c vec_pop copia ese elemento a
 * @p out (si no es NULL) y reduce la longitud. Devuelve 0 si estaba vacía.
 */
int stack_pop(Stack *s, void *out) {
    return vec_pop(&s->items, out); /* saca el último = la cima */
}

/**
 * @copydoc stack_peek
 *
 * Mira la cima sin desapilar. La cima es el elemento en el índice @c len-1.
 *
 * Paso a paso:
 *   1. Si la pila está vacía no hay cima -> NULL.
 *   2. Si no, devuelve el puntero al último elemento del vector.
 *
 * @return Puntero a la cima, o NULL si está vacía.
 */
void *stack_peek(const Stack *s) {
    if (s->items.len == 0) return NULL;         /* pila vacía: no hay cima */
    return vec_at(&s->items, s->items.len - 1); /* último elemento = cima */
}
