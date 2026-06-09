/**
 * @file test_hashmap.c
 * @brief Pruebas unitarias de la tabla hash (structs/hashmap) con ctests.
 */
#include "ctests.h"
#include "structs/hashmap.h"

/** Hash que colisiona siempre: peor caso para el sondeo lineal. */
static size_t hash_const(const void *k, size_t n) {
    (void)k;
    (void)n;
    return 0;
}

/** Inserción masiva, sobrescritura, borrado de pares e iteración. */
static void test_int_map(void) {
    HashMap m;
    hashmap_init(&m, sizeof(int), sizeof(int));

    /* inserción masiva: 1000 claves con valor i*10 */
    for (int i = 0; i < 1000; i++) {
        int v = i * 10;
        EXPECT_TRUE(hashmap_put(&m, &i, &v));
    }
    EXPECT_EQ_INT((int)hashmap_len(&m), 1000);
    for (int i = 0; i < 1000; i++) {
        int *p = hashmap_get(&m, &i);
        EXPECT_NOT_NULL(p);
        EXPECT_EQ_INT(*p, i * 10);
    }

    /* sobrescribir una clave impar (sobrevive al borrado de pares) */
    int k = 501, nv = -7;
    EXPECT_TRUE(hashmap_put(&m, &k, &nv));
    {
        int *p = hashmap_get(&m, &k);
        EXPECT_NOT_NULL(p);
        EXPECT_EQ_INT(*p, -7);
    }
    EXPECT_EQ_INT((int)hashmap_len(&m),
                  1000); /* sobrescribir no cambia tamaño */

    /* eliminar los pares */
    for (int i = 0; i < 1000; i += 2)
        EXPECT_TRUE(hashmap_remove(&m, &i));
    EXPECT_EQ_INT((int)hashmap_len(&m), 500);

    for (int i = 0; i < 1000; i++) {
        int *p = hashmap_get(&m, &i);
        if (i % 2 == 0) {
            EXPECT_NULL(p);
        } else if (i == 501) {
            EXPECT_NOT_NULL(p);
            EXPECT_EQ_INT(*p, -7);
        } else {
            EXPECT_NOT_NULL(p);
            EXPECT_EQ_INT(*p, i * 10);
        }
    }

    /* iteración: debe recorrer exactamente las 500 entradas */
    size_t it = 0;
    void *kk, *vv;
    int cnt = 0;
    while (hashmap_next(&m, &it, &kk, &vv))
        cnt++;
    EXPECT_EQ_INT(cnt, 500);

    hashmap_free(&m);
}

/** Peor caso del sondeo lineal: hash que colisiona siempre + borrados. */
static void test_collisions(void) {
    HashMap c;
    hashmap_init_ex(&c, sizeof(int), sizeof(int), 8, hash_const, NULL);

    for (int i = 0; i < 200; i++)
        EXPECT_TRUE(hashmap_put(&c, &i, &i));
    EXPECT_EQ_INT((int)hashmap_len(&c), 200);

    /* borrar impares mezclado, luego verificar que los pares siguen accesibles
       (estresa el desplazamiento hacia atras con cadenas de sondeo larguisimas)
     */
    for (int i = 1; i < 200; i += 2)
        EXPECT_TRUE(hashmap_remove(&c, &i));
    EXPECT_EQ_INT((int)hashmap_len(&c), 100);

    for (int i = 0; i < 200; i++) {
        int *p = hashmap_get(&c, &i);
        if (i % 2 == 0) {
            EXPECT_NOT_NULL(p);
            EXPECT_EQ_INT(*p, i);
        } else {
            EXPECT_NULL(p);
        }
    }

    hashmap_free(&c);
}

/** Claves tipo cadena: hash/igualdad por contenido, no por puntero. */
static void test_str_map(void) {
    HashMap s;
    hashmap_init_ex(&s, sizeof(char *), sizeof(int), 0, hashmap_str_hash,
                    hashmap_str_eq);

    const char *k1 = "alpha";
    int v1 = 1;
    const char *k2 = "beta";
    int v2 = 2;
    EXPECT_TRUE(hashmap_put(&s, &k1, &v1));
    EXPECT_TRUE(hashmap_put(&s, &k2, &v2));

    /* consultar con punteros distintos pero mismo contenido */
    const char *q1 = "alpha";
    const char *q2 = "beta";
    const char *miss = "gamma";
    {
        int *p1 = hashmap_get(&s, &q1);
        int *p2 = hashmap_get(&s, &q2);
        EXPECT_NOT_NULL(p1);
        EXPECT_EQ_INT(*p1, 1);
        EXPECT_NOT_NULL(p2);
        EXPECT_EQ_INT(*p2, 2);
    }
    EXPECT_NULL(hashmap_get(&s, &miss));

    EXPECT_TRUE(hashmap_remove(&s, &k1));
    EXPECT_NULL(hashmap_get(&s, &q1));
    EXPECT_EQ_INT((int)hashmap_len(&s), 1);

    hashmap_free(&s);
}

int main(void) {
    tt_suite("hashmap");
    tt_run("insercion masiva, sobrescritura, borrado e iteracion",
           test_int_map);
    tt_run("colisiones siempre (peor caso del sondeo lineal)", test_collisions);
    tt_run("claves tipo cadena por contenido", test_str_map);
    return tt_summary();
}
