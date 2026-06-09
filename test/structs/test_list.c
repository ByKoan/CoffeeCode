/**
 * @file test_list.c
 * @brief Pruebas unitarias de la lista doblemente enlazada (structs/list)
 *        con ctests.
 */
#include "ctests.h"
#include "structs/list.h"

/** push_back/push_front encadenan y list_len cuenta; recorrido en orden. */
static void test_push_and_traverse(void) {
    List l;
    list_init(&l, sizeof(int));
    for (int i = 1; i <= 3; i++)
        list_push_back(&l, &i); /* 1 2 3 */
    int z = 0;
    list_push_front(&l, &z); /* 0 1 2 3 */
    EXPECT_EQ_INT((int)list_len(&l), 4);

    /* recorrido en orden: 0 1 2 3 */
    int expect = 0;
    for (ListNode *n = list_first(&l); n; n = n->next)
        EXPECT_EQ_INT(LIST_DATA(n, int), expect++);
    EXPECT_EQ_INT(expect, 4);

    list_free(&l);
}

/** insert_after/insert_before colocan nodos junto a una referencia dada. */
static void test_insert_after_before(void) {
    List l;
    list_init(&l, sizeof(int));
    for (int i = 1; i <= 3; i++)
        list_push_back(&l, &i);
    int z = 0;
    list_push_front(&l, &z); /* 0 1 2 3 */

    /* insertar después de la cabeza (0) */
    int nine = 9;
    list_insert_after(&l, list_first(&l), &nine); /* 0 9 1 2 3 */
    EXPECT_EQ_INT(LIST_DATA(list_first(&l)->next, int), 9);

    /* insertar antes de la cola */
    int seven = 7;
    list_insert_before(&l, list_last(&l), &seven); /* 0 9 1 2 7 3 */
    EXPECT_EQ_INT(LIST_DATA(list_last(&l)->prev, int), 7);

    list_free(&l);
}

/** pop_front/pop_back extraen los extremos devolviendo su valor. */
static void test_pop_extremes(void) {
    List l;
    list_init(&l, sizeof(int));
    int vals[] = {0, 9, 1, 2, 7, 3}; /* 0 9 1 2 7 3 */
    for (int i = 0; i < 6; i++)
        list_push_back(&l, &vals[i]);

    int out = -1;
    EXPECT_TRUE(list_pop_front(&l, &out));
    EXPECT_EQ_INT(out, 0);
    EXPECT_TRUE(list_pop_back(&l, &out));
    EXPECT_EQ_INT(out, 3);
    EXPECT_EQ_INT((int)list_len(&l), 4); /* 9 1 2 7 */

    list_free(&l);
}

/** remove desenlaza un nodo interior; free vacía la lista por completo. */
static void test_remove_and_free(void) {
    List l;
    list_init(&l, sizeof(int));
    int vals[] = {9, 1, 2, 7}; /* 9 1 2 7 */
    for (int i = 0; i < 4; i++)
        list_push_back(&l, &vals[i]);

    /* eliminar un nodo interior (1) */
    list_remove(&l, list_first(&l)->next); /* 9 2 7 */
    EXPECT_EQ_INT((int)list_len(&l), 3);
    EXPECT_EQ_INT(LIST_DATA(list_first(&l), int), 9);
    EXPECT_EQ_INT(LIST_DATA(list_first(&l)->next, int), 2);

    list_free(&l);
    EXPECT_EQ_INT((int)list_len(&l), 0);
    EXPECT_NULL(list_first(&l));
}

int main(void) {
    tt_suite("list");
    tt_run("push back/front y recorrido en orden", test_push_and_traverse);
    tt_run("insert_after e insert_before", test_insert_after_before);
    tt_run("pop_front y pop_back", test_pop_extremes);
    tt_run("remove interior y free", test_remove_and_free);
    return tt_summary();
}
