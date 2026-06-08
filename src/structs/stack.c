/**
 * @file stack.c
 * @brief Implementación de la pila genérica sobre ::Vec (ver stack.h).
 */
#include "structs/stack.h"

int stack_init(Stack *s, size_t elem_size)
{
    return vec_init(&s->items, elem_size);
}

void stack_free(Stack *s)
{
    vec_free(&s->items);
}

void stack_clear(Stack *s)
{
    vec_clear(&s->items);
}

int stack_push(Stack *s, const void *item)
{
    return vec_push(&s->items, item);
}

int stack_pop(Stack *s, void *out)
{
    return vec_pop(&s->items, out);
}

void *stack_peek(const Stack *s)
{
    if (s->items.len == 0)
        return NULL;
    return vec_at(&s->items, s->items.len - 1);
}
