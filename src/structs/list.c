/**
 * @file list.c
 * @brief Implementación de la lista doblemente enlazada genérica (ver list.h).
 *
 * @note Contenedor genérico basado en nodos. Como el vector, la lista no conoce
 * el tipo de los elementos: solo guarda @c l->elem (bytes por elemento, fijado
 * en
 * ::list_init) y copia bytes con @c memcpy. La particularidad está en CÓMO se
 * almacena el dato genérico: cada nodo se reserva con UNA sola llamada a @c
 * malloc que pide @c sizeof(ListNode) @c + @c elem bytes; el campo @c data es
 * un "array miembro flexible" (@c ListMaxAlign @c data[]) que apunta justo
 * detrás de los punteros prev/next, dentro del MISMO bloque. Ventajas frente a
 * guardar un
 * @c void* al dato: una asignación por nodo (no dos), mejor localidad de caché
 * y menos fragmentación. @c ListMaxAlign (unión de los tipos más exigentes)
 * garantiza que el dato queda correctamente alineado para cualquier tipo.
 *
 * Al ser doblemente enlazada (cada nodo conoce a su anterior y siguiente), dado
 * un nodo se puede insertar o borrar en O(1) sin recorrer la lista. El
 * recorrido sigue siendo O(n).
 */
#include "structs/list.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Reserva un nodo nuevo y copia @p item en su almacenamiento inline.
 *
 * Una única reserva de @c sizeof(ListNode)+elem bytes: cabecera (prev/next) +
 * el dato pegado detrás. El nodo nace aislado (prev/next a NULL); enlazarlo a
 * la lista es responsabilidad del llamante.
 * @param l Lista (aporta @c elem, el tamaño del dato).
 * @param item Puntero a @c elem bytes a copiar dentro del nodo (no nulo).
 * @return Nodo recién creado, o @c NULL si falló @c malloc.
 */
static ListNode *list_node_new(List *l, const void *item) {
    /* Bloque único: cabecera + hueco de 'elem' bytes para el dato inline. */
    ListNode *n = malloc(sizeof(ListNode) + l->elem);
    if (!n) return NULL;
    n->prev = NULL; /* nace desenlazado; el que llama lo conecta */
    n->next = NULL;
    /* Copiar el elemento al almacenamiento inline (n->data) sin conocer su
     * tipo. */
    memcpy(n->data, item, l->elem);
    return n;
}

/**
 * @brief Inicializa una lista vacía para elementos de @p elem_size bytes.
 * @param l Lista a inicializar (no nula).
 * @param elem_size Tamaño de cada elemento en bytes (> 0).
 * @return 1 siempre.
 */
int list_init(List *l, size_t elem_size) {
    l->head = NULL; /* lista vacía: sin cabeza ni cola */
    l->tail = NULL;
    l->len = 0;
    l->elem = elem_size; /* tamaño del dato que se copiará en cada nodo */
    return 1;
}

/**
 * @brief Elimina todos los nodos y deja la lista vacía (reutilizable).
 * @param l Lista (no nula).
 */
void list_clear(List *l) {
    ListNode *n = l->head;
    while (n) {
        /* Guardar el siguiente ANTES de liberar n (si no, se pierde el enlace).
         * Como el dato va inline en el mismo bloque, un solo free libera todo.
         */
        ListNode *next = n->next;
        free(n);
        n = next;
    }
    l->head = NULL;
    l->tail = NULL;
    l->len = 0;
}

/**
 * @brief Libera todos los nodos de la lista.
 * @param l Lista (no nula).
 */
void list_free(List *l) {
    /* No hay recursos extra por nodo (dato inline), así que vaciar == liberar.
     */
    list_clear(l);
}

/**
 * @brief Inserta una copia de @p item al final (nueva cola).
 * @param l Lista (no nula).
 * @param item Puntero a @c elem bytes (no nulo).
 * @return El nodo creado, o @c NULL si falló la asignación.
 */
ListNode *list_push_back(List *l, const void *item) {
    ListNode *n = list_node_new(l, item);
    if (!n) return NULL;
    n->prev = l->tail; /* el nuevo va detrás de la cola actual */
    if (l->tail)
        l->tail->next = n; /* enganchar la cola vieja con el nuevo nodo */
    else
        l->head = n; /* lista estaba vacía: el nuevo es también la cabeza */
    l->tail = n;     /* el nuevo pasa a ser la cola */
    l->len++;
    return n;
}

/**
 * @brief Inserta una copia de @p item al principio (nueva cabeza).
 * @param l Lista (no nula).
 * @param item Puntero a @c elem bytes (no nulo).
 * @return El nodo creado, o @c NULL si falló la asignación.
 */
ListNode *list_push_front(List *l, const void *item) {
    ListNode *n = list_node_new(l, item);
    if (!n) return NULL;
    n->next = l->head; /* el nuevo va delante de la cabeza actual */
    if (l->head)
        l->head->prev = n; /* enganchar la cabeza vieja con el nuevo nodo */
    else
        l->tail = n; /* lista estaba vacía: el nuevo es también la cola */
    l->head = n;     /* el nuevo pasa a ser la cabeza */
    l->len++;
    return n;
}

/**
 * @brief Inserta una copia de @p item justo antes de @p ref.
 * @param l Lista (no nula).
 * @param ref Nodo de referencia (no nulo, perteneciente a @p l).
 * @param item Puntero a @c elem bytes (no nulo).
 * @return El nodo creado, o @c NULL si falló la asignación.
 */
ListNode *list_insert_before(List *l, ListNode *ref, const void *item) {
    /* Caso límite: insertar antes de la cabeza es exactamente push_front (y
     * allí se actualiza l->head, que aquí no tocaríamos). */
    if (ref == l->head) return list_push_front(l, item);

    ListNode *n = list_node_new(l, item);
    if (!n) return NULL;
    /* Coser el nodo entre ref->prev y ref (cuatro punteros a recablear). */
    n->prev = ref->prev;
    n->next = ref;
    ref->prev->next = n; /* ref->prev no es NULL: ref no es la cabeza */
    ref->prev = n;
    l->len++;
    return n;
}

/**
 * @brief Inserta una copia de @p item justo después de @p ref.
 * @param l Lista (no nula).
 * @param ref Nodo de referencia (no nulo, perteneciente a @p l).
 * @param item Puntero a @c elem bytes (no nulo).
 * @return El nodo creado, o @c NULL si falló la asignación.
 */
ListNode *list_insert_after(List *l, ListNode *ref, const void *item) {
    /* Caso límite: insertar tras la cola es exactamente push_back (actualiza
     * tail). */
    if (ref == l->tail) return list_push_back(l, item);

    ListNode *n = list_node_new(l, item);
    if (!n) return NULL;
    /* Coser el nodo entre ref y ref->next. */
    n->next = ref->next;
    n->prev = ref;
    ref->next->prev = n; /* ref->next no es NULL: ref no es la cola */
    ref->next = n;
    l->len++;
    return n;
}

/**
 * @brief Desenlaza y libera @p node de la lista.
 *
 * Operación O(1): al ser doblemente enlazada, el nodo conoce a sus vecinos, así
 * que se "puentean" mutuamente sin recorrer la lista. Los extremos (prev/next a
 * NULL) actualizan head/tail según corresponda.
 * @param l Lista (no nula).
 * @param node Nodo a eliminar (no nulo, perteneciente a @p l).
 */
void list_remove(List *l, ListNode *node) {
    /* Reenlazar el lado izquierdo: el anterior salta directo al siguiente. Si
     * no hay anterior, node era la cabeza, así que la cabeza pasa a ser
     * node->next. */
    if (node->prev)
        node->prev->next = node->next;
    else
        l->head = node->next;

    /* Simétrico para el lado derecho: el siguiente apunta atrás al anterior, o
     * la cola pasa a ser node->prev si node era la cola. */
    if (node->next)
        node->next->prev = node->prev;
    else
        l->tail = node->prev;

    free(node); /* un solo free: dato inline incluido */
    l->len--;
}

/**
 * @brief Extrae el primer elemento.
 * @param l Lista (no nula).
 * @param out Destino de @c elem bytes, o @c NULL para descartar.
 * @return 1 si había elemento, 0 si la lista estaba vacía.
 */
int list_pop_front(List *l, void *out) {
    if (!l->head) return 0; /* lista vacía */
    /* Copiar el dato fuera ANTES de liberar el nodo (si lo quieren). */
    if (out) memcpy(out, l->head->data, l->elem);
    list_remove(l, l->head);
    return 1;
}

/**
 * @brief Extrae el último elemento.
 * @param l Lista (no nula).
 * @param out Destino de @c elem bytes, o @c NULL para descartar.
 * @return 1 si había elemento, 0 si la lista estaba vacía.
 */
int list_pop_back(List *l, void *out) {
    if (!l->tail) return 0; /* lista vacía */
    /* Copiar el dato fuera ANTES de liberar el nodo (si lo quieren). */
    if (out) memcpy(out, l->tail->data, l->elem);
    list_remove(l, l->tail);
    return 1;
}
