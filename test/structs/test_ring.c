/**
 * @file test_ring.c
 * @brief Pruebas unitarias del buffer circular (structs/ring) con ctests.
 */
#include "ctests.h"
#include "structs/ring.h"

/** Tras init el buffer esta vacio; push llena hasta cap. */
static void test_init_and_fill(void) {
    Ring r;
    ring_init(&r, sizeof(int), 3);
    EXPECT_TRUE(ring_empty(&r));

    int a = 1, b = 2, c = 3;
    EXPECT_EQ_INT(ring_push(&r, &a), 1);
    EXPECT_EQ_INT(ring_push(&r, &b), 1);
    EXPECT_EQ_INT(ring_push(&r, &c), 1);
    EXPECT_TRUE(ring_full(&r));
    EXPECT_EQ_INT((int)ring_len(&r), 3);
    ring_free(&r);
}

/** Con el buffer lleno, push descarta el mas antiguo (devuelve 0). */
static void test_overflow_overwrites_oldest(void) {
    Ring r;
    ring_init(&r, sizeof(int), 3);
    int a = 1, b = 2, c = 3, d = 4;
    ring_push(&r, &a);
    ring_push(&r, &b);
    ring_push(&r, &c);

    EXPECT_EQ_INT(ring_push(&r, &d), 0); /* desborda: descarta el 1 */

    /* contiene {2,3,4}; indice 0 = mas antiguo */
    EXPECT_EQ_INT(*(int *)ring_at(&r, 0), 2);
    EXPECT_EQ_INT(*(int *)ring_at(&r, 1), 3);
    EXPECT_EQ_INT(*(int *)ring_at(&r, 2), 4);
    EXPECT_EQ_INT(*(int *)ring_front(&r), 2);
    EXPECT_EQ_INT(*(int *)ring_back(&r), 4);
    ring_free(&r);
}

/** pop_back saca el mas reciente; pop_front el mas antiguo. */
static void test_pop_back_and_front(void) {
    Ring r;
    ring_init(&r, sizeof(int), 3);
    int a = 1, b = 2, c = 3, d = 4;
    ring_push(&r, &a);
    ring_push(&r, &b);
    ring_push(&r, &c);
    ring_push(&r, &d); /* contiene {2,3,4} */

    int out = 0;
    EXPECT_TRUE(ring_pop_back(&r, &out)); /* {2,3} */
    EXPECT_EQ_INT(out, 4);
    EXPECT_TRUE(ring_pop_front(&r, &out)); /* {3} */
    EXPECT_EQ_INT(out, 2);
    EXPECT_EQ_INT((int)ring_len(&r), 1);
    EXPECT_EQ_INT(*(int *)ring_back(&r), 3);
    ring_free(&r);
}

/** clear vacia el buffer; pop sobre vacio devuelve 0. */
static void test_clear_and_empty(void) {
    Ring r;
    ring_init(&r, sizeof(int), 3);
    int a = 1, b = 2;
    ring_push(&r, &a);
    ring_push(&r, &b);

    ring_clear(&r);
    EXPECT_TRUE(ring_empty(&r));
    EXPECT_EQ_INT((int)ring_len(&r), 0);

    int out = 0;
    EXPECT_EQ_INT(ring_pop_back(&r, &out), 0); /* vacio: nada que sacar */
    ring_free(&r);
}

int main(void) {
    tt_suite("ring");
    tt_run("init y llenado", test_init_and_fill);
    tt_run("desborde sobrescribe el mas antiguo",
           test_overflow_overwrites_oldest);
    tt_run("pop_back y pop_front", test_pop_back_and_front);
    tt_run("clear y vacio", test_clear_and_empty);
    return tt_summary();
}
