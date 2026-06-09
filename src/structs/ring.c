/**
 * @file ring.c
 * @brief Implementación del buffer circular genérico (ver ring.h).
 *
 * @note Por qué un buffer circular. Un anillo guarda hasta @c cap elementos en
 * un bloque de memoria reservado UNA sola vez. En lugar de mover datos al
 * insertar/extraer por los extremos, lo que se mueven son dos índices (@c head
 * y la posición de cola, derivada de @c count) que "giran" sobre el mismo
 * bloque: cuando un índice llega al final vuelve al principio. Así, encolar
 * (push) y desencolar (pop) por cualquiera de los dos extremos cuestan O(1) sin
 * reasignar ni copiar el resto. El precio es que la capacidad es fija: cuando
 * se llena, un nuevo push SOBRESCRIBE el elemento más antiguo (semántica de
 * historial acotado, justo lo que necesita el undo/redo del editor).
 *
 * @note Disposición física vs. lógica. El "índice lógico" 0 siempre es el
 * elemento más antiguo y @c count-1 el más reciente, pero físicamente el más
 * antiguo está en la ranura @c head, que puede ser cualquiera. La traducción
 * lógico→físico es @c (head + logico) mod cap. Para evitar la división se usa
 * una resta condicional (ver ::ring_phys).
 */
#include "structs/ring.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Convierte un índice lógico en posición física dentro del buffer.
 *
 * El elemento lógico 0 vive en la ranura @c head; el lógico @p logical vive en
 * @c head+logical, pero "envuelto" al principio del bloque si se sale del
 * final.
 *
 * Paso a paso:
 *   1. Suma cruda @c head + logical (puede caer fuera de @c [0, cap)).
 *   2. Si se pasó del final, resta @c cap una vez para volver al principio.
 *
 * @note La resta condicional equivale a @c % cap pero sin instrucción de
 * división. Es correcta porque tanto @c head como @p logical están en
 * @c [0, cap), así que la suma nunca supera @c 2*cap-2: una única resta basta.
 *
 * @param r Buffer (no nulo).
 * @param logical Índice lógico (0 = más antiguo).
 * @return Posición física (ranura) en @c [0, cap).
 */
static inline size_t ring_phys(const Ring *r, size_t logical) {
    size_t p = r->head + logical; /* posición sin envolver */
    if (p >= r->cap) p -= r->cap; /* envuelve: equivale a % cap sin división */
    return p;
}

/**
 * @brief Puntero a la ranura física @p phys.
 *
 * Aritmética de bytes: cada ranura ocupa @c elem bytes, así que la ranura
 * @p phys empieza en @c data + phys*elem. Se castea a @c char* para que el
 * desplazamiento sea en bytes (no en elementos).
 *
 * @param r Buffer (no nulo).
 * @param phys Ranura física en @c [0, cap).
 * @return Puntero al primer byte de esa ranura.
 */
static inline void *ring_slot(const Ring *r, size_t phys) {
    return (char *)r->data + phys * r->elem;
}

/**
 * @copydoc ring_init
 *
 * Reserva el bloque de @c cap*elem_size bytes y deja el anillo vacío con
 * @c head = 0. No se inicializa el contenido: las ranuras solo se leen tras
 * haberse escrito (count las protege).
 */
int ring_init(Ring *r, size_t elem_size, size_t cap) {
    if (cap == 0) return 0;            /* capacidad 0 no tiene sentido: fallo */
    r->data = malloc(cap * elem_size); /* bloque único para todo el anillo */
    if (!r->data) return 0;            /* sin memoria: fallo */
    r->cap = cap;                      /* capacidad fija */
    r->elem = elem_size;               /* tamaño de cada elemento */
    r->head = 0;  /* el más antiguo arranca en la ranura 0 */
    r->count = 0; /* aún no hay elementos */
    return 1;
}

/**
 * @copydoc ring_free
 *
 * Libera el bloque y deja la estructura en un estado "vacío seguro" (punteros a
 * NULL, contadores a 0) por si se reusa o se vuelve a liberar.
 */
void ring_free(Ring *r) {
    free(r->data);  /* libera el bloque reservado en init */
    r->data = NULL; /* evita doble free / uso colgante */
    r->cap = 0;
    r->head = 0;
    r->count = 0;
}

/**
 * @copydoc ring_clear
 *
 * No toca la memoria ni el contenido: basta con olvidar los elementos poniendo
 * @c count a 0 y recolocar @c head al principio. La capacidad se conserva.
 */
void ring_clear(Ring *r) {
    r->head = 0;  /* el más antiguo vuelve a la ranura 0 */
    r->count = 0; /* sin elementos válidos */
}

/**
 * @copydoc ring_push
 *
 * Inserta por la "cola" (extremo reciente). Dos casos:
 *
 *   - Buffer NO lleno: la cola está en la ranura lógica @c count. Se escribe
 * ahí y se incrementa @c count. El más antiguo (@c head) no se toca.
 *   - Buffer lleno: no hay sitio nuevo, así que se reutiliza la ranura del más
 *     antiguo (@c head): se escribe encima y se avanza @c head un paso
 * (envolviendo con ::ring_phys). El elemento que estaba ahí queda DESCARTADO
 * (evicted) y
 *     @c count no cambia (sigue lleno). Esto convierte al anillo en un
 * historial deslizante de los últimos @c cap elementos.
 *
 * @return 1 si no se descartó nada; 0 si se sobrescribió el más antiguo.
 */
int ring_push(Ring *r, const void *item) {
    int evicted = 0; /* ¿se descartó el más antiguo? */
    size_t pos;      /* ranura física donde escribir */

    if (r->count == r->cap) {
        /* lleno: sobrescribir el más antiguo y avanzar head */
        pos = r->head;             /* la ranura del más antiguo se recicla */
        r->head = ring_phys(r, 1); /* el nuevo "más antiguo" es el siguiente */
        evicted = 1;               /* hemos perdido un elemento */
    } else {
        pos = ring_phys(r, r->count); /* primera ranura libre = cola lógica */
        r->count++;                   /* un elemento más */
    }

    memcpy(ring_slot(r, pos), item,
           r->elem); /* copia el elemento dentro del anillo */
    return evicted ? 0 : 1;
}

/**
 * @copydoc ring_pop_back
 *
 * Extrae por la "cola" (LIFO sobre el extremo reciente). El más reciente está
 * en la ranura lógica @c count-1.
 *
 * Paso a paso:
 *   1. Si está vacío, no hay nada que sacar.
 *   2. Decrementa @c count: con eso la ranura del antiguo último deja de ser
 *      válida (no hace falta borrar su contenido).
 *   3. Si se pidió, copia ese elemento a @p out antes de "olvidarlo".
 *
 * @return 1 si había elemento, 0 si estaba vacío.
 */
int ring_pop_back(Ring *r, void *out) {
    if (r->count == 0) return 0; /* vacío: nada que extraer */
    r->count--;                  /* el último deja de ser válido */
    /* tras decrementar, count apunta a la ranura recién liberada (el ex-último)
     */
    if (out) memcpy(out, ring_slot(r, ring_phys(r, r->count)), r->elem);
    return 1;
}

/**
 * @copydoc ring_pop_front
 *
 * Extrae por la "cabeza" (FIFO: saca el más antiguo, que está en @c head).
 *
 * Paso a paso:
 *   1. Si está vacío, no hay nada que sacar.
 *   2. Si se pidió, copia el elemento de @c head a @p out.
 *   3. Avanza @c head al siguiente (envolviendo) y decrementa @c count. El que
 *      era el segundo más antiguo pasa a ser ahora el más antiguo (lógico 0).
 *
 * @return 1 si había elemento, 0 si estaba vacío.
 */
int ring_pop_front(Ring *r, void *out) {
    if (r->count == 0) return 0; /* vacío: nada que extraer */
    if (out)
        memcpy(out, ring_slot(r, r->head), r->elem); /* copia el más antiguo */
    r->head = ring_phys(r, 1); /* el más antiguo pasa a ser el siguiente */
    r->count--;                /* un elemento menos */
    return 1;
}

/**
 * @copydoc ring_at
 *
 * Acceso indexado en O(1): traduce el índice lógico @p i a ranura física y
 * devuelve su puntero. No comprueba límites (el llamante garantiza @c i<count).
 */
void *ring_at(const Ring *r, size_t i) {
    return ring_slot(r, ring_phys(r, i)); /* lógico i -> físico -> puntero */
}

/**
 * @copydoc ring_back
 *
 * El más reciente es el lógico @c count-1. Devuelve NULL si el anillo está
 * vacío (no habría índice válido).
 */
void *ring_back(const Ring *r) {
    if (r->count == 0) return NULL; /* vacío: no hay "último" */
    return ring_slot(r, ring_phys(r, r->count - 1));
}

/**
 * @copydoc ring_front
 *
 * El más antiguo es siempre la ranura @c head. Devuelve NULL si está vacío.
 */
void *ring_front(const Ring *r) {
    if (r->count == 0) return NULL; /* vacío: no hay "primero" */
    return ring_slot(r, r->head);
}
