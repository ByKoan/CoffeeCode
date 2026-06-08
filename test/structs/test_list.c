/**
 * @file test_list.c
 * @brief Pruebas unitarias de la lista doblemente enlazada (structs/list).
 *
 * Compilar y ejecutar:
 *   gcc -std=c11 -I include test/structs/test_list.c src/structs/list.c -o t && ./t
 */
#include "structs/list.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    List l;
    list_init(&l, sizeof(int));
    for (int i = 1; i <= 3; i++) list_push_back(&l, &i);   /* 1 2 3 */
    int z = 0; list_push_front(&l, &z);                    /* 0 1 2 3 */
    assert(list_len(&l) == 4);

    /* recorrido en orden */
    int expect = 0;
    for (ListNode *n = list_first(&l); n; n = n->next)
        assert(LIST_DATA(n, int) == expect++);
    assert(expect == 4);

    /* insertar después de la cabeza (0) */
    int nine = 9;
    list_insert_after(&l, list_first(&l), &nine);          /* 0 9 1 2 3 */
    assert(LIST_DATA(list_first(&l)->next, int) == 9);

    /* insertar antes de la cola */
    int seven = 7;
    list_insert_before(&l, list_last(&l), &seven);         /* 0 9 1 2 7 3 */
    assert(LIST_DATA(list_last(&l)->prev, int) == 7);

    /* extraer ambos extremos */
    int out = -1;
    assert(list_pop_front(&l, &out) && out == 0);
    assert(list_pop_back(&l, &out) && out == 3);
    assert(list_len(&l) == 4);                             /* 9 1 2 7 */

    /* eliminar un nodo interior (1) */
    list_remove(&l, list_first(&l)->next);                 /* 9 2 7 */
    assert(list_len(&l) == 3);
    assert(LIST_DATA(list_first(&l), int) == 9);
    assert(LIST_DATA(list_first(&l)->next, int) == 2);

    list_free(&l);
    assert(list_len(&l) == 0 && list_first(&l) == NULL);

    printf("test_list: OK\n");
    return 0;
}
