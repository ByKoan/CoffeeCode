/**
 * @file test_vec.c
 * @brief Pruebas unitarias del vector genérico (structs/vec).
 *
 * Compilar y ejecutar:
 *   gcc -std=c11 -I include test/structs/test_vec.c src/structs/vec.c -o t && ./t
 */
#include "structs/vec.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    Vec v;
    vec_init(&v, sizeof(int));
    for (int i = 0; i < 100; i++) assert(vec_push(&v, &i));
    assert(vec_len(&v) == 100);
    assert(VEC_AT(&v, int, 0) == 0 && VEC_AT(&v, int, 99) == 99);

    /* insertar al principio */
    int x = -1;
    assert(vec_insert(&v, 0, &x));
    assert(VEC_AT(&v, int, 0) == -1 && VEC_AT(&v, int, 1) == 0);
    assert(vec_len(&v) == 101);

    /* eliminar índice 0 */
    assert(vec_remove(&v, 0));
    assert(VEC_AT(&v, int, 0) == 0 && vec_len(&v) == 100);

    /* eliminar rango [10,20) */
    assert(vec_remove_range(&v, 10, 20));
    assert(vec_len(&v) == 90);
    assert(VEC_AT(&v, int, 10) == 20);

    /* pop */
    int out = 0;
    assert(vec_pop(&v, &out) && out == 99 && vec_len(&v) == 89);

    /* resize crece a cero */
    assert(vec_resize(&v, 95));
    assert(vec_len(&v) == 95 && VEC_AT(&v, int, 94) == 0);

    /* push_slot: construir en sitio */
    int *slot = (int *)vec_push_slot(&v);
    *slot = 777;
    assert(VEC_AT(&v, int, 95) == 777);

    vec_clear(&v);
    assert(vec_len(&v) == 0);
    vec_free(&v);

    printf("test_vec: OK\n");
    return 0;
}
