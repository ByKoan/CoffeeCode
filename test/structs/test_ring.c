/**
 * @file test_ring.c
 * @brief Pruebas unitarias del buffer circular (structs/ring).
 *
 * Compilar y ejecutar:
 *   gcc -std=c11 -I include test/structs/test_ring.c src/structs/ring.c -o t && ./t
 */
#include "structs/ring.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    Ring r;
    ring_init(&r, sizeof(int), 3);
    assert(ring_empty(&r));

    int a = 1, b = 2, c = 3, d = 4;
    assert(ring_push(&r, &a) == 1);
    assert(ring_push(&r, &b) == 1);
    assert(ring_push(&r, &c) == 1);
    assert(ring_full(&r));
    assert(ring_push(&r, &d) == 0);          /* desbordó: descartó el más antiguo */

    /* contiene {2,3,4}; índice 0 = más antiguo */
    assert(*(int *)ring_at(&r, 0) == 2);
    assert(*(int *)ring_at(&r, 1) == 3);
    assert(*(int *)ring_at(&r, 2) == 4);
    assert(*(int *)ring_front(&r) == 2);
    assert(*(int *)ring_back(&r) == 4);

    int out = 0;
    assert(ring_pop_back(&r, &out) && out == 4);    /* {2,3} */
    assert(ring_pop_front(&r, &out) && out == 2);   /* {3} */
    assert(ring_len(&r) == 1 && *(int *)ring_back(&r) == 3);

    ring_clear(&r);
    assert(ring_empty(&r) && ring_pop_back(&r, &out) == 0);

    ring_free(&r);

    printf("test_ring: OK\n");
    return 0;
}
