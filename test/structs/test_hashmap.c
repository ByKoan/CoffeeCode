/**
 * @file test_hashmap.c
 * @brief Pruebas unitarias de la tabla hash (structs/hashmap).
 *
 * Compilar y ejecutar:
 *   gcc -std=c11 -I include test/structs/test_hashmap.c \
 *       src/structs/hashmap.c -o t && ./t
 */
#include "structs/hashmap.h"
#include <assert.h>
#include <stdio.h>

/** Hash que colisiona siempre: peor caso para el sondeo lineal. */
static size_t hash_const(const void *k, size_t n) { (void)k; (void)n; return 0; }

static void test_int_map(void)
{
    HashMap m;
    hashmap_init(&m, sizeof(int), sizeof(int));

    for (int i = 0; i < 1000; i++) {
        int v = i * 10;
        assert(hashmap_put(&m, &i, &v));
    }
    assert(hashmap_len(&m) == 1000);
    for (int i = 0; i < 1000; i++) {
        int *p = hashmap_get(&m, &i);
        assert(p && *p == i * 10);
    }

    /* sobrescribir una clave impar (sobrevive al borrado de pares) */
    int k = 501, nv = -7;
    assert(hashmap_put(&m, &k, &nv));
    assert(*(int *)hashmap_get(&m, &k) == -7);
    assert(hashmap_len(&m) == 1000); /* sobrescribir no cambia el tamaño */

    /* eliminar los pares */
    for (int i = 0; i < 1000; i += 2)
        assert(hashmap_remove(&m, &i));
    assert(hashmap_len(&m) == 500);

    for (int i = 0; i < 1000; i++) {
        int *p = hashmap_get(&m, &i);
        if (i % 2 == 0)            assert(p == NULL);
        else if (i == 501)         assert(p && *p == -7);
        else                       assert(p && *p == i * 10);
    }

    /* iteración: debe recorrer exactamente las 500 entradas */
    size_t it = 0; void *kk, *vv; int cnt = 0;
    while (hashmap_next(&m, &it, &kk, &vv)) cnt++;
    assert(cnt == 500);

    hashmap_free(&m);
    printf("  int_map: OK\n");
}

static void test_collisions(void)
{
    HashMap c;
    hashmap_init_ex(&c, sizeof(int), sizeof(int), 8, hash_const, NULL);

    for (int i = 0; i < 200; i++)
        assert(hashmap_put(&c, &i, &i));
    assert(hashmap_len(&c) == 200);

    /* borrar impares mezclado, luego verificar que los pares siguen accesibles
       (estresa el desplazamiento hacia atrás con cadenas de sondeo larguísimas) */
    for (int i = 1; i < 200; i += 2)
        assert(hashmap_remove(&c, &i));
    assert(hashmap_len(&c) == 100);

    for (int i = 0; i < 200; i++) {
        int *p = hashmap_get(&c, &i);
        if (i % 2 == 0) assert(p && *p == i);
        else            assert(p == NULL);
    }

    hashmap_free(&c);
    printf("  collisions: OK\n");
}

static void test_str_map(void)
{
    HashMap s;
    hashmap_init_ex(&s, sizeof(char *), sizeof(int), 0,
                    hashmap_str_hash, hashmap_str_eq);

    const char *k1 = "alpha"; int v1 = 1;
    const char *k2 = "beta";  int v2 = 2;
    assert(hashmap_put(&s, &k1, &v1));
    assert(hashmap_put(&s, &k2, &v2));

    /* consultar con punteros distintos pero mismo contenido */
    const char *q1 = "alpha";
    const char *q2 = "beta";
    const char *miss = "gamma";
    assert(*(int *)hashmap_get(&s, &q1) == 1);
    assert(*(int *)hashmap_get(&s, &q2) == 2);
    assert(hashmap_get(&s, &miss) == NULL);

    assert(hashmap_remove(&s, &k1));
    assert(hashmap_get(&s, &q1) == NULL);
    assert(hashmap_len(&s) == 1);

    hashmap_free(&s);
    printf("  str_map: OK\n");
}

int main(void)
{
    test_int_map();
    test_collisions();
    test_str_map();
    printf("test_hashmap: OK\n");
    return 0;
}
