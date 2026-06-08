/**
 * @file hashmap.c
 * @brief Implementación de la tabla hash de direccionamiento abierto (ver hashmap.h).
 *
 * Linear probing con capacidad potencia de 2 y borrado por desplazamiento hacia
 * atrás (algoritmo R de Knuth adaptado), que evita "tombstones" y mantiene las
 * cadenas de sondeo compactas. Factor de carga máximo 3/4.
 */
#include "structs/hashmap.h"

#include <stdlib.h>
#include <string.h>

#define HM_MIN_CAP 8        /**< Capacidad mínima (potencia de 2). */
#define HM_NOT_FOUND ((size_t)-1)

/** @brief Redondea @p n hacia arriba a potencia de 2 (mínimo HM_MIN_CAP). */
static size_t next_pow2(size_t n)
{
    size_t c = HM_MIN_CAP;
    while (c < n)
        c <<= 1;
    return c;
}

/** @brief Hash FNV-1a por defecto sobre @p n bytes de la clave. */
static size_t fnv1a(const void *key, size_t n)
{
    const unsigned char *p = (const unsigned char *)key;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return (size_t)h;
}

/** @brief Igualdad por bytes por defecto. */
static int eq_bytes(const void *a, const void *b, size_t n)
{
    return memcmp(a, b, n) == 0;
}

/** @brief Puntero a la clave de la ranura @p i. */
static inline void *hm_key(const HashMap *m, size_t i)
{
    return (char *)m->keys + i * m->key_size;
}

/** @brief Puntero al valor de la ranura @p i. */
static inline void *hm_val(const HashMap *m, size_t i)
{
    return (char *)m->vals + i * m->val_size;
}

/** @brief Reserva los arrays para @p cap ranuras. @return 1 en éxito. */
static int hm_alloc(HashMap *m, size_t cap)
{
    m->keys   = malloc(cap * m->key_size);
    m->vals   = malloc(cap * m->val_size);
    m->hashes = malloc(cap * sizeof(size_t));
    m->state  = calloc(cap, 1);
    if (!m->keys || !m->vals || !m->hashes || !m->state) {
        free(m->keys);
        free(m->vals);
        free(m->hashes);
        free(m->state);
        m->keys = m->vals = NULL;
        m->hashes = NULL;
        m->state = NULL;
        return 0;
    }
    m->cap = cap;
    return 1;
}

int hashmap_init(HashMap *m, size_t key_size, size_t val_size)
{
    return hashmap_init_ex(m, key_size, val_size, HM_MIN_CAP, NULL, NULL);
}

int hashmap_init_ex(HashMap *m, size_t key_size, size_t val_size,
                    size_t initial_cap, HashFn hash, EqFn eq)
{
    m->keys = m->vals = NULL;
    m->hashes = NULL;
    m->state = NULL;
    m->cap = 0;
    m->count = 0;
    m->key_size = key_size;
    m->val_size = val_size;
    m->hash = hash ? hash : fnv1a;
    m->eq = eq ? eq : eq_bytes;
    return hm_alloc(m, next_pow2(initial_cap ? initial_cap : HM_MIN_CAP));
}

void hashmap_free(HashMap *m)
{
    free(m->keys);
    free(m->vals);
    free(m->hashes);
    free(m->state);
    m->keys = m->vals = NULL;
    m->hashes = NULL;
    m->state = NULL;
    m->cap = 0;
    m->count = 0;
}

void hashmap_clear(HashMap *m)
{
    if (m->state)
        memset(m->state, 0, m->cap);
    m->count = 0;
}

/**
 * @brief Coloca una entrada (clave única) en una tabla con sitio; no comprueba
 *        duplicados ni hace crecer. Usado por put (tras find) y por el rehash.
 */
static void hm_place(HashMap *m, size_t h, const void *key, const void *val)
{
    size_t mask = m->cap - 1;
    size_t i = h & mask;
    while (m->state[i])
        i = (i + 1) & mask;

    m->state[i] = 1;
    m->hashes[i] = h;
    memcpy(hm_key(m, i), key, m->key_size);
    memcpy(hm_val(m, i), val, m->val_size);
    m->count++;
}

/** @brief Reasigna a @p newcap ranuras y reinserta las entradas. */
static int hm_grow(HashMap *m, size_t newcap)
{
    HashMap nm = *m;
    nm.keys = NULL;
    nm.vals = NULL;
    nm.hashes = NULL;
    nm.state = NULL;
    nm.cap = 0;
    nm.count = 0;
    if (!hm_alloc(&nm, newcap))
        return 0;

    for (size_t i = 0; i < m->cap; i++)
        if (m->state[i])
            hm_place(&nm, m->hashes[i], hm_key(m, i), hm_val(m, i));

    free(m->keys);
    free(m->vals);
    free(m->hashes);
    free(m->state);
    m->keys = nm.keys;
    m->vals = nm.vals;
    m->hashes = nm.hashes;
    m->state = nm.state;
    m->cap = nm.cap;
    m->count = nm.count;
    return 1;
}

/** @brief Busca la ranura de @p key. @return índice o HM_NOT_FOUND. */
static size_t hm_find(const HashMap *m, const void *key, size_t h)
{
    size_t mask = m->cap - 1;
    size_t i = h & mask;
    while (m->state[i]) {
        if (m->hashes[i] == h && m->eq(hm_key(m, i), key, m->key_size))
            return i;
        i = (i + 1) & mask;
    }
    return HM_NOT_FOUND;
}

int hashmap_put(HashMap *m, const void *key, const void *val)
{
    size_t h = m->hash(key, m->key_size);

    size_t existing = hm_find(m, key, h);
    if (existing != HM_NOT_FOUND) {
        memcpy(hm_val(m, existing), val, m->val_size);
        return 1;
    }

    /* crecer si superaríamos el factor de carga 3/4 */
    if ((m->count + 1) * 4 > m->cap * 3) {
        if (!hm_grow(m, m->cap * 2))
            return 0;
    }
    hm_place(m, h, key, val);
    return 1;
}

void *hashmap_get(const HashMap *m, const void *key)
{
    size_t h = m->hash(key, m->key_size);
    size_t i = hm_find(m, key, h);
    return i == HM_NOT_FOUND ? NULL : hm_val(m, i);
}

int hashmap_has(const HashMap *m, const void *key)
{
    size_t h = m->hash(key, m->key_size);
    return hm_find(m, key, h) != HM_NOT_FOUND;
}

/** @brief ¿Está @p home cíclicamente en el intervalo @c (i, j]? */
static int hm_in_range(size_t i, size_t home, size_t j)
{
    if (i < j)
        return home > i && home <= j;
    return home > i || home <= j;
}

int hashmap_remove(HashMap *m, const void *key)
{
    size_t h = m->hash(key, m->key_size);
    size_t s = hm_find(m, key, h);
    if (s == HM_NOT_FOUND)
        return 0;

    size_t mask = m->cap - 1;
    m->state[s] = 0;
    m->count--;

    /* desplazamiento hacia atrás: rellenar el hueco con entradas posteriores
       cuyo "home" no quede antes del hueco, manteniendo las cadenas válidas. */
    size_t i = s, j = s;
    for (;;) {
        j = (j + 1) & mask;
        if (!m->state[j])
            break;
        size_t home = m->hashes[j] & mask;
        if (hm_in_range(i, home, j))
            continue; /* la entrada j está bien colocada respecto al hueco */

        memcpy(hm_key(m, i), hm_key(m, j), m->key_size);
        memcpy(hm_val(m, i), hm_val(m, j), m->val_size);
        m->hashes[i] = m->hashes[j];
        m->state[i] = 1;
        m->state[j] = 0;
        i = j; /* el hueco se mueve a j */
    }
    return 1;
}

int hashmap_next(const HashMap *m, size_t *iter, void **key_out, void **val_out)
{
    for (size_t i = *iter; i < m->cap; i++) {
        if (m->state[i]) {
            if (key_out)
                *key_out = hm_key(m, i);
            if (val_out)
                *val_out = hm_val(m, i);
            *iter = i + 1;
            return 1;
        }
    }
    return 0;
}

size_t hashmap_str_hash(const void *key, size_t key_size)
{
    (void)key_size;
    const char *s = *(const char *const *)key; /* la clave es un char* */
    uint64_t h = 1469598103934665603ULL;
    for (; *s; ++s) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return (size_t)h;
}

int hashmap_str_eq(const void *a, const void *b, size_t key_size)
{
    (void)key_size;
    return strcmp(*(const char *const *)a, *(const char *const *)b) == 0;
}
