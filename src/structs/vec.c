/**
 * @file vec.c
 * @brief Implementación del vector genérico (ver vec.h).
 */
#include "structs/vec.h"

#include <stdlib.h>
#include <string.h>

/** Capacidad inicial al crecer desde vacío (potencia de 2). */
#define VEC_MIN_CAP 8

/**
 * @brief Crece la capacidad a al menos @p need elementos (duplicando).
 * @return 1 en éxito, 0 si falló @c realloc.
 */
static int vec_grow(Vec *v, size_t need)
{
    if (need <= v->cap)
        return 1;

    size_t cap = v->cap ? v->cap : VEC_MIN_CAP;
    while (cap < need)
        cap <<= 1; /* duplicar; crecimiento amortizado O(1) */

    void *nd = realloc(v->data, cap * v->elem);
    if (!nd)
        return 0;

    v->data = nd;
    v->cap  = cap;
    return 1;
}

int vec_init(Vec *v, size_t elem_size)
{
    v->data = NULL;
    v->len  = 0;
    v->cap  = 0;
    v->elem = elem_size;
    return 1;
}

int vec_init_cap(Vec *v, size_t elem_size, size_t cap)
{
    vec_init(v, elem_size);
    return cap ? vec_reserve(v, cap) : 1;
}

void vec_free(Vec *v)
{
    free(v->data);
    v->data = NULL;
    v->len  = 0;
    v->cap  = 0;
}

int vec_reserve(Vec *v, size_t cap)
{
    return vec_grow(v, cap);
}

int vec_resize(Vec *v, size_t n)
{
    if (n > v->len) {
        if (!vec_grow(v, n))
            return 0;
        /* poner a cero los elementos nuevos */
        memset((char *)v->data + v->len * v->elem, 0, (n - v->len) * v->elem);
    }
    v->len = n;
    return 1;
}

void vec_clear(Vec *v)
{
    v->len = 0;
}

int vec_push(Vec *v, const void *item)
{
    if (!vec_grow(v, v->len + 1))
        return 0;
    memcpy((char *)v->data + v->len * v->elem, item, v->elem);
    v->len++;
    return 1;
}

void *vec_push_slot(Vec *v)
{
    if (!vec_grow(v, v->len + 1))
        return NULL;
    void *slot = (char *)v->data + v->len * v->elem;
    v->len++;
    return slot;
}

int vec_pop(Vec *v, void *out)
{
    if (v->len == 0)
        return 0;
    v->len--;
    if (out)
        memcpy(out, (char *)v->data + v->len * v->elem, v->elem);
    return 1;
}

int vec_insert(Vec *v, size_t idx, const void *item)
{
    if (idx > v->len)
        return 0;
    if (!vec_grow(v, v->len + 1))
        return 0;

    char *base = (char *)v->data;
    /* desplazar [idx, len) un hueco a la derecha */
    memmove(base + (idx + 1) * v->elem,
            base + idx * v->elem,
            (v->len - idx) * v->elem);
    memcpy(base + idx * v->elem, item, v->elem);
    v->len++;
    return 1;
}

int vec_remove(Vec *v, size_t idx)
{
    if (idx >= v->len)
        return 0;
    char *base = (char *)v->data;
    memmove(base + idx * v->elem,
            base + (idx + 1) * v->elem,
            (v->len - idx - 1) * v->elem);
    v->len--;
    return 1;
}

int vec_remove_range(Vec *v, size_t from, size_t to)
{
    if (from > to || to > v->len)
        return 0;
    if (from == to)
        return 1;

    char *base = (char *)v->data;
    memmove(base + from * v->elem,
            base + to * v->elem,
            (v->len - to) * v->elem);
    v->len -= (to - from);
    return 1;
}

void vec_set(Vec *v, size_t idx, const void *item)
{
    memcpy((char *)v->data + idx * v->elem, item, v->elem);
}
