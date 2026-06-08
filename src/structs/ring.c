/**
 * @file ring.c
 * @brief Implementación del buffer circular genérico (ver ring.h).
 */
#include "structs/ring.h"

#include <stdlib.h>
#include <string.h>

/** @brief Convierte un índice lógico en posición física dentro del buffer. */
static inline size_t ring_phys(const Ring *r, size_t logical)
{
    size_t p = r->head + logical;
    if (p >= r->cap)
        p -= r->cap; /* equivale a % cap pero sin división */
    return p;
}

/** @brief Puntero a la ranura física @p phys. */
static inline void *ring_slot(const Ring *r, size_t phys)
{
    return (char *)r->data + phys * r->elem;
}

int ring_init(Ring *r, size_t elem_size, size_t cap)
{
    if (cap == 0)
        return 0;
    r->data = malloc(cap * elem_size);
    if (!r->data)
        return 0;
    r->cap   = cap;
    r->elem  = elem_size;
    r->head  = 0;
    r->count = 0;
    return 1;
}

void ring_free(Ring *r)
{
    free(r->data);
    r->data  = NULL;
    r->cap   = 0;
    r->head  = 0;
    r->count = 0;
}

void ring_clear(Ring *r)
{
    r->head  = 0;
    r->count = 0;
}

int ring_push(Ring *r, const void *item)
{
    int evicted = 0;
    size_t pos;

    if (r->count == r->cap) {
        /* lleno: sobrescribir el más antiguo y avanzar head */
        pos = r->head;
        r->head = ring_phys(r, 1);
        evicted = 1;
    } else {
        pos = ring_phys(r, r->count);
        r->count++;
    }

    memcpy(ring_slot(r, pos), item, r->elem);
    return evicted ? 0 : 1;
}

int ring_pop_back(Ring *r, void *out)
{
    if (r->count == 0)
        return 0;
    r->count--;
    if (out)
        memcpy(out, ring_slot(r, ring_phys(r, r->count)), r->elem);
    return 1;
}

int ring_pop_front(Ring *r, void *out)
{
    if (r->count == 0)
        return 0;
    if (out)
        memcpy(out, ring_slot(r, r->head), r->elem);
    r->head = ring_phys(r, 1);
    r->count--;
    return 1;
}

void *ring_at(const Ring *r, size_t i)
{
    return ring_slot(r, ring_phys(r, i));
}

void *ring_back(const Ring *r)
{
    if (r->count == 0)
        return NULL;
    return ring_slot(r, ring_phys(r, r->count - 1));
}

void *ring_front(const Ring *r)
{
    if (r->count == 0)
        return NULL;
    return ring_slot(r, r->head);
}
