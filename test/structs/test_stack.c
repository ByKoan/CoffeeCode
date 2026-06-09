/**
 * @file test_stack.c
 * @brief Pruebas unitarias de la pila generica (structs/stack) con ctests.
 */
#include "ctests.h"
#include "structs/stack.h"

/** Una pila recien inicializada esta vacia y peek devuelve NULL. */
static void test_init_empty(void) {
    Stack s;
    stack_init(&s, sizeof(int));
    EXPECT_TRUE(stack_empty(&s));
    EXPECT_NULL(stack_peek(&s));
    stack_free(&s);
}

/** push apila en orden LIFO; len cuenta y peek devuelve la cima. */
static void test_push_peek_len(void) {
    Stack s;
    stack_init(&s, sizeof(int));
    for (int i = 1; i <= 5; i++) /* apila 1..5 */
        EXPECT_TRUE(stack_push(&s, &i));
    EXPECT_EQ_INT((int)stack_len(&s), 5);
    EXPECT_EQ_INT(*(int *)stack_peek(&s), 5); /* la cima es el ultimo */
    stack_free(&s);
}

/** pop saca la cima en orden inverso (LIFO) y actualiza len/peek. */
static void test_pop_lifo(void) {
    Stack s;
    stack_init(&s, sizeof(int));
    for (int i = 1; i <= 5; i++)
        stack_push(&s, &i);

    int out = 0;
    EXPECT_TRUE(stack_pop(&s, &out));
    EXPECT_EQ_INT(out, 5);
    EXPECT_TRUE(stack_pop(&s, &out));
    EXPECT_EQ_INT(out, 4);
    EXPECT_EQ_INT((int)stack_len(&s), 3);
    EXPECT_EQ_INT(*(int *)stack_peek(&s), 3);
    stack_free(&s);
}

/** clear vacia la pila; pop sobre vacia devuelve 0. */
static void test_clear_and_pop_empty(void) {
    Stack s;
    stack_init(&s, sizeof(int));
    for (int i = 1; i <= 5; i++)
        stack_push(&s, &i);

    stack_clear(&s);
    EXPECT_TRUE(stack_empty(&s));

    int out = 0;
    EXPECT_FALSE(stack_pop(&s, &out)); /* nada que desapilar */
    stack_free(&s);
}

int main(void) {
    tt_suite("stack");
    tt_run("init deja la pila vacia", test_init_empty);
    tt_run("push, peek y len (LIFO)", test_push_peek_len);
    tt_run("pop en orden LIFO", test_pop_lifo);
    tt_run("clear y pop sobre vacia", test_clear_and_pop_empty);
    return tt_summary();
}
