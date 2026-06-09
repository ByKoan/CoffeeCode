/**
 * @file test_vec.c
 * @brief Pruebas unitarias del vector genérico (structs/vec) con ctests.
 */
#include "ctests.h"
#include "structs/vec.h"

/** push() añade al final y VEC_AT lee por índice; len cuenta los elementos. */
static void test_push_and_access(void) {
    Vec v;
    vec_init(&v, sizeof(int));
    for (int i = 0; i < 100; i++)
        EXPECT_TRUE(vec_push(&v, &i));
    EXPECT_EQ_INT((int)vec_len(&v), 100);
    EXPECT_EQ_INT(VEC_AT(&v, int, 0), 0);
    EXPECT_EQ_INT(VEC_AT(&v, int, 99), 99);
    vec_free(&v);
}

/** insert() abre hueco en una posición; remove() lo cierra desplazando. */
static void test_insert_remove(void) {
    Vec v;
    vec_init(&v, sizeof(int));
    for (int i = 0; i < 100; i++)
        vec_push(&v, &i);

    int x = -1;
    EXPECT_TRUE(vec_insert(&v, 0, &x));
    EXPECT_EQ_INT(VEC_AT(&v, int, 0), -1);
    EXPECT_EQ_INT(VEC_AT(&v, int, 1), 0);
    EXPECT_EQ_INT((int)vec_len(&v), 101);

    EXPECT_TRUE(vec_remove(&v, 0));
    EXPECT_EQ_INT(VEC_AT(&v, int, 0), 0);
    EXPECT_EQ_INT((int)vec_len(&v), 100);
    vec_free(&v);
}

/** remove_range borra [from,to); pop saca y devuelve el último. */
static void test_remove_range_and_pop(void) {
    Vec v;
    vec_init(&v, sizeof(int));
    for (int i = 0; i < 100; i++)
        vec_push(&v, &i);

    EXPECT_TRUE(vec_remove_range(&v, 10, 20));
    EXPECT_EQ_INT((int)vec_len(&v), 90);
    EXPECT_EQ_INT(VEC_AT(&v, int, 10), 20); /* el 20 ocupa ahora el hueco */

    int out = 0;
    EXPECT_TRUE(vec_pop(&v, &out));
    EXPECT_EQ_INT(out, 99);
    EXPECT_EQ_INT((int)vec_len(&v), 89);
    vec_free(&v);
}

/** resize crece poniendo a cero; push_slot construye en sitio; clear vacía. */
static void test_resize_slot_clear(void) {
    Vec v;
    vec_init(&v, sizeof(int));
    for (int i = 0; i < 89; i++)
        vec_push(&v, &i);

    EXPECT_TRUE(vec_resize(&v, 95));
    EXPECT_EQ_INT((int)vec_len(&v), 95);
    EXPECT_EQ_INT(VEC_AT(&v, int, 94),
                  0); /* posiciones nuevas inicializadas a 0 */

    int *slot = (int *)vec_push_slot(&v);
    EXPECT_NOT_NULL(slot);
    *slot = 777;
    EXPECT_EQ_INT(VEC_AT(&v, int, 95), 777);

    vec_clear(&v);
    EXPECT_EQ_INT((int)vec_len(&v), 0);
    vec_free(&v);
}

int main(void) {
    tt_suite("vec");
    tt_run("push y acceso por indice", test_push_and_access);
    tt_run("insert al inicio y remove", test_insert_remove);
    tt_run("remove_range y pop", test_remove_range_and_pop);
    tt_run("resize, push_slot y clear", test_resize_slot_clear);
    return tt_summary();
}
