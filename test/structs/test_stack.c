/**
 * @file test_stack.c
 * @brief Pruebas unitarias de la pila genérica (structs/stack).
 *
 * Compilar y ejecutar:
 *   gcc -std=c11 -I include test/structs/test_stack.c \
 *       src/structs/stack.c src/structs/vec.c -o t && ./t
 */
#include "structs/stack.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    Stack s;
    stack_init(&s, sizeof(int));
    assert(stack_empty(&s) && stack_peek(&s) == NULL);

    for (int i = 1; i <= 5; i++) assert(stack_push(&s, &i));   /* 1..5 */
    assert(stack_len(&s) == 5);
    assert(*(int *)stack_peek(&s) == 5);                       /* LIFO */

    int out = 0;
    assert(stack_pop(&s, &out) && out == 5);
    assert(stack_pop(&s, &out) && out == 4);
    assert(stack_len(&s) == 3 && *(int *)stack_peek(&s) == 3);

    stack_clear(&s);
    assert(stack_empty(&s) && stack_pop(&s, &out) == 0);

    stack_free(&s);

    printf("test_stack: OK\n");
    return 0;
}
