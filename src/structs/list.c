/**
 * @file list.c
 * @brief Implementación de la lista doblemente enlazada genérica (ver list.h).
 */
#include "structs/list.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Reserva un nodo nuevo y copia @p item en su almacenamiento inline.
 * @return Nodo aislado (prev/next a @c NULL), o @c NULL si falló @c malloc.
 */
static ListNode *list_node_new(List *l, const void *item)
{
    ListNode *n = malloc(sizeof(ListNode) + l->elem);
    if (!n)
        return NULL;
    n->prev = NULL;
    n->next = NULL;
    memcpy(n->data, item, l->elem);
    return n;
}

int list_init(List *l, size_t elem_size)
{
    l->head = NULL;
    l->tail = NULL;
    l->len  = 0;
    l->elem = elem_size;
    return 1;
}

void list_clear(List *l)
{
    ListNode *n = l->head;
    while (n) {
        ListNode *next = n->next;
        free(n);
        n = next;
    }
    l->head = NULL;
    l->tail = NULL;
    l->len  = 0;
}

void list_free(List *l)
{
    list_clear(l);
}

ListNode *list_push_back(List *l, const void *item)
{
    ListNode *n = list_node_new(l, item);
    if (!n)
        return NULL;
    n->prev = l->tail;
    if (l->tail)
        l->tail->next = n;
    else
        l->head = n;
    l->tail = n;
    l->len++;
    return n;
}

ListNode *list_push_front(List *l, const void *item)
{
    ListNode *n = list_node_new(l, item);
    if (!n)
        return NULL;
    n->next = l->head;
    if (l->head)
        l->head->prev = n;
    else
        l->tail = n;
    l->head = n;
    l->len++;
    return n;
}

ListNode *list_insert_before(List *l, ListNode *ref, const void *item)
{
    if (ref == l->head)
        return list_push_front(l, item);

    ListNode *n = list_node_new(l, item);
    if (!n)
        return NULL;
    n->prev = ref->prev;
    n->next = ref;
    ref->prev->next = n; /* ref->prev no es NULL: ref no es la cabeza */
    ref->prev = n;
    l->len++;
    return n;
}

ListNode *list_insert_after(List *l, ListNode *ref, const void *item)
{
    if (ref == l->tail)
        return list_push_back(l, item);

    ListNode *n = list_node_new(l, item);
    if (!n)
        return NULL;
    n->next = ref->next;
    n->prev = ref;
    ref->next->prev = n; /* ref->next no es NULL: ref no es la cola */
    ref->next = n;
    l->len++;
    return n;
}

void list_remove(List *l, ListNode *node)
{
    if (node->prev)
        node->prev->next = node->next;
    else
        l->head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        l->tail = node->prev;

    free(node);
    l->len--;
}

int list_pop_front(List *l, void *out)
{
    if (!l->head)
        return 0;
    if (out)
        memcpy(out, l->head->data, l->elem);
    list_remove(l, l->head);
    return 1;
}

int list_pop_back(List *l, void *out)
{
    if (!l->tail)
        return 0;
    if (out)
        memcpy(out, l->tail->data, l->elem);
    list_remove(l, l->tail);
    return 1;
}
