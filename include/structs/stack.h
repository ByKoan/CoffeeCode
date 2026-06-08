/**
 * @file stack.h
 * @brief Pila genérica (LIFO) construida sobre ::Vec.
 *
 * Push/pop amortizados O(1). Reutiliza el vector dinámico como almacenamiento,
 * por lo que comparte su estrategia de crecimiento.
 *
 * @code
 * Stack s;
 * stack_init(&s, sizeof(int));
 * int x = 5; stack_push(&s, &x);
 * int top = *(int*)stack_peek(&s);   // 5
 * stack_pop(&s, &top);
 * stack_free(&s);
 * @endcode
 */
#pragma once

#include "structs/vec.h"

/**
 * @brief Pila LIFO genérica.
 */
typedef struct {
    Vec items; /**< Almacenamiento; la cima es el último elemento. */
} Stack;

/**
 * @brief Inicializa una pila vacía para elementos de @p elem_size bytes.
 * @return 1 siempre.
 */
int stack_init(Stack *s, size_t elem_size);

/**
 * @brief Libera la memoria de la pila.
 */
void stack_free(Stack *s);

/**
 * @brief Vacía la pila conservando la capacidad.
 */
void stack_clear(Stack *s);

/**
 * @brief Apila una copia de @p item.
 * @return 1 en éxito, 0 si falló al crecer.
 */
int stack_push(Stack *s, const void *item);

/**
 * @brief Desapila la cima.
 * @param out Destino de @c elem bytes, o @c NULL para descartar.
 * @return 1 si había elemento, 0 si la pila estaba vacía.
 */
int stack_pop(Stack *s, void *out);

/**
 * @brief Puntero a la cima sin desapilar.
 * @return Puntero al elemento superior, o @c NULL si la pila está vacía.
 */
void *stack_peek(const Stack *s);

/** @brief Número de elementos. */
static inline size_t stack_len(const Stack *s) { return vec_len(&s->items); }
/** @brief 1 si la pila está vacía. */
static inline int stack_empty(const Stack *s) { return s->items.len == 0; }
