/**
 * @file vec.c
 * @brief Implementación del vector genérico (ver vec.h).
 *
 * @note Patrón de "contenedor genérico" en C. C no tiene plantillas
 * (templates), así que para guardar elementos de cualquier tipo en un mismo
 * contenedor se usa "type erasure" (borrado de tipo): el vector no sabe qué
 * tipo guarda, solo conoce el tamaño en bytes de cada elemento (@c v->elem,
 * fijado en ::vec_init). La memoria se maneja como un bloque plano de bytes (@c
 * void* / @c char*) y la posición del elemento i se calcula con aritmética de
 * punteros: @c base @c + @c i*elem. Las copias se hacen con @c memcpy/@c
 * memmove sobre @c elem bytes, sin conocer el tipo. El precio de la genericidad
 * es que el llamante siempre pasa/recibe los elementos por puntero (@c const @c
 * void* al copiar dentro, @c void* al copiar fuera).
 */
#include "structs/vec.h"

#include <stdlib.h>
#include <string.h>

/** Capacidad inicial al crecer desde vacío (potencia de 2). */
#define VEC_MIN_CAP 8

/**
 * @brief Crece la capacidad a al menos @p need elementos (duplicando).
 *
 * Estrategia de "crecimiento amortizado": en vez de realojar en cada inserción
 * (lo que daría O(n) por push y O(n^2) al construir el vector), se duplica la
 * capacidad cada vez que se llena. Como una secuencia de N inserciones provoca
 * solo log2(N) realojos y la suma de las copias (1+2+4+...+N) es < 2N, el coste
 * total se reparte y cada push sale a O(1) "amortizado".
 *
 * @param v Vector (no nulo).
 * @param need Número mínimo de elementos que debe caber tras crecer.
 * @return 1 en éxito, 0 si falló @c realloc (el vector queda intacto).
 */
static int vec_grow(Vec *v, size_t need) {
    /* Ya cabe: no tocar memoria. Esto hace barato llamar a vec_grow "por si
     * acaso" antes de cada inserción. */
    if (need <= v->cap) return 1;

    /* Punto de partida del duplicado: si aún no hay buffer (cap==0) arrancamos
     * en VEC_MIN_CAP para no hacer realojos diminutos al principio. */
    size_t cap = v->cap ? v->cap : VEC_MIN_CAP;
    while (cap < need)
        cap <<= 1; /* duplicar (cap *= 2); crecimiento amortizado O(1) */

    /* realloc reserva un bloque NUEVO de cap*elem bytes, copia el contenido
     * viejo y libera el antiguo. OJO: puede devolver una dirección DISTINTA,
     * así que todo puntero al interior del vector (p. ej. de vec_at o
     * vec_push_slot) queda invalidado tras crecer. cap*elem son bytes porque el
     * contenedor razona en bytes, no en elementos tipados. */
    void *nd = realloc(v->data, cap * v->elem);
    if (!nd) return 0; /* sin memoria: no se pierde el buffer original */

    v->data = nd; /* publicar el nuevo buffer (posiblemente reubicado) */
    v->cap = cap; /* y la nueva capacidad en elementos */
    return 1;
}

/**
 * @brief Inicializa un vector vacío para elementos de @p elem_size bytes.
 * @param v Vector a inicializar (no nulo).
 * @param elem_size Tamaño de cada elemento en bytes (> 0).
 * @return 1 siempre (no reserva memoria hasta la primera inserción).
 */
int vec_init(Vec *v, size_t elem_size) {
    v->data = NULL; /* aún sin buffer: se reserva perezosamente */
    v->len = 0;     /* sin elementos */
    v->cap = 0;     /* sin capacidad */
    v->elem =
        elem_size; /* clave del "type erasure": tamaño fijo del elemento */
    return 1;
}

/**
 * @brief Inicializa un vector y le reserva capacidad inicial.
 * @param v Vector a inicializar (no nulo).
 * @param elem_size Tamaño de cada elemento en bytes (> 0).
 * @param cap Capacidad inicial en elementos (0 = no reservar).
 * @return 1 en éxito, 0 si falló la reserva.
 */
int vec_init_cap(Vec *v, size_t elem_size, size_t cap) {
    vec_init(v, elem_size);
    /* Solo reservamos si se pidió capacidad; con cap==0 nos quedamos vacíos. */
    return cap ? vec_reserve(v, cap) : 1;
}

/**
 * @brief Libera el buffer y deja el vector en estado vacío reutilizable.
 * @param v Vector (no nulo). Seguro llamarlo varias veces (free(NULL) es
 * válido).
 */
void vec_free(Vec *v) {
    free(v->data);  /* libera el bloque de bytes (NULL es no-op) */
    v->data = NULL; /* evita doble free y deja el vector listo para reusar */
    v->len = 0;
    v->cap = 0;
}

/**
 * @brief Garantiza capacidad para al menos @p cap elementos sin cambiar @c len.
 * @param v Vector (no nulo).
 * @param cap Capacidad mínima deseada en elementos.
 * @return 1 si hay capacidad suficiente, 0 si falló la reasignación.
 */
int vec_reserve(Vec *v, size_t cap) {
    /* Simple fachada de vec_grow: reservar es "asegurar capacidad". */
    return vec_grow(v, cap);
}

/**
 * @brief Ajusta el número de elementos a @p n.
 *
 * Si crece, los elementos nuevos se ponen a cero; si decrece, se descartan
 * (sin tocar memoria, solo baja @c len).
 * @param v Vector (no nulo).
 * @param n Nuevo número de elementos.
 * @return 1 en éxito, 0 si falló la reasignación al crecer.
 */
int vec_resize(Vec *v, size_t n) {
    if (n > v->len) {
        /* Asegurar sitio para los n elementos antes de tocar nada. */
        if (!vec_grow(v, n)) return 0;
        /* Poner a cero la zona recién añadida [len, n). En bytes:
         * inicio = data + len*elem ; tamaño = (n-len)*elem. */
        memset((char *)v->data + v->len * v->elem, 0, (n - v->len) * v->elem);
    }
    /* Al encoger no hay que liberar nada: basta reducir len; la capacidad
     * sobrante se conserva para futuras inserciones. */
    v->len = n;
    return 1;
}

/**
 * @brief Vacía el vector (pone @c len a 0) conservando la capacidad/buffer.
 * @param v Vector (no nulo).
 */
void vec_clear(Vec *v) {
    /* No se libera memoria: solo "olvidamos" los elementos. Reusar el buffer
     * evita realojar si el vector se vuelve a llenar. */
    v->len = 0;
}

/**
 * @brief Añade una copia de @p item al final.
 * @param v Vector (no nulo).
 * @param item Puntero a @c elem bytes a copiar (no nulo).
 * @return 1 en éxito, 0 si falló al crecer.
 */
int vec_push(Vec *v, const void *item) {
    /* Asegurar hueco para un elemento más (puede realojar y mover data). */
    if (!vec_grow(v, v->len + 1)) return 0;
    /* Copiar elem bytes desde item al hueco final: data + len*elem. */
    memcpy((char *)v->data + v->len * v->elem, item, v->elem);
    v->len++; /* ya hay un elemento más en uso */
    return 1;
}

/**
 * @brief Reserva un hueco al final y devuelve un puntero a él, sin copiar.
 *
 * Permite construir el elemento "en sitio" evitando una copia. El contenido
 * del hueco es indeterminado y el puntero se invalida en la siguiente operación
 * que realoque (p. ej. otro push que dispare un crecimiento).
 * @param v Vector (no nulo).
 * @return Puntero al nuevo elemento, o @c NULL si falló al crecer.
 */
void *vec_push_slot(Vec *v) {
    if (!vec_grow(v, v->len + 1)) return NULL;
    /* Dirección del hueco final ANTES de incrementar len. */
    void *slot = (char *)v->data + v->len * v->elem;
    v->len++;
    return slot;
}

/**
 * @brief Extrae el último elemento.
 * @param v Vector (no nulo).
 * @param out Destino de @c elem bytes, o @c NULL para descartar.
 * @return 1 si había elemento, 0 si el vector estaba vacío.
 */
int vec_pop(Vec *v, void *out) {
    if (v->len == 0) return 0; /* nada que extraer */
    v->len--; /* "quitar" el último: ahora apunta al elemento a sacar */
    /* Copiar fuera solo si el llamante quiere el valor; si no, se descarta. */
    if (out) memcpy(out, (char *)v->data + v->len * v->elem, v->elem);
    return 1;
}

/**
 * @brief Inserta una copia de @p item en @p idx desplazando el resto a la
 * derecha.
 * @param v Vector (no nulo).
 * @param idx Posición de inserción en @c [0, len] (len = añadir al final).
 * @param item Puntero a @c elem bytes (no nulo).
 * @return 1 en éxito, 0 si @p idx fuera de rango o falló al crecer.
 */
int vec_insert(Vec *v, size_t idx, const void *item) {
    if (idx > v->len)
        return 0; /* posición inválida (idx==len sí vale: al final) */
    if (!vec_grow(v, v->len + 1)) return 0;

    /* OJO: cachear base DESPUÉS de vec_grow, porque el realloc pudo mover data.
     */
    char *base = (char *)v->data;
    /* Abrir hueco: mover el bloque [idx, len) un elemento a la derecha. memmove
     * (no memcpy) porque origen y destino se solapan. Bytes movidos =
     * (len-idx)*elem. */
    memmove(base + (idx + 1) * v->elem, base + idx * v->elem,
            (v->len - idx) * v->elem);
    /* Escribir el elemento nuevo en el hueco recién abierto. */
    memcpy(base + idx * v->elem, item, v->elem);
    v->len++;
    return 1;
}

/**
 * @brief Elimina el elemento en @p idx desplazando los posteriores a la
 * izquierda.
 * @param v Vector (no nulo).
 * @param idx Posición a eliminar en @c [0, len).
 * @return 1 en éxito, 0 si @p idx fuera de rango.
 */
int vec_remove(Vec *v, size_t idx) {
    if (idx >= v->len) return 0; /* fuera de rango */
    char *base = (char *)v->data;
    /* Tapar el hueco: traer [idx+1, len) un elemento a la izquierda. Se mueven
     * (len-idx-1) elementos; memmove por el solapamiento. */
    memmove(base + idx * v->elem, base + (idx + 1) * v->elem,
            (v->len - idx - 1) * v->elem);
    v->len--;
    return 1;
}

/**
 * @brief Elimina el rango @c [from, to) desplazando los posteriores.
 * @param v Vector (no nulo).
 * @param from Inicio del rango (incluido).
 * @param to Fin del rango (excluido).
 * @return 1 en éxito (incluido rango vacío), 0 si el rango es inválido.
 */
int vec_remove_range(Vec *v, size_t from, size_t to) {
    if (from > to || to > v->len)
        return 0;             /* rango mal formado o fuera de límites */
    if (from == to) return 1; /* rango vacío: nada que hacer */

    char *base = (char *)v->data;
    /* Traer la cola [to, len) sobre el inicio del rango borrado [from, ...).
     * Se mueven (len-to) elementos; el resto (los del rango) quedan
     * "olvidados". */
    memmove(base + from * v->elem, base + to * v->elem,
            (v->len - to) * v->elem);
    v->len -= (to - from); /* descontar los elementos eliminados */
    return 1;
}

/**
 * @brief Sobrescribe el elemento en @p idx con una copia de @p item.
 * @param v Vector (no nulo).
 * @param idx Posición en @c [0, len) (sin comprobación de límites).
 * @param item Puntero a @c elem bytes (no nulo).
 */
void vec_set(Vec *v, size_t idx, const void *item) {
    /* Copia directa sobre la celda idx: data + idx*elem. */
    memcpy((char *)v->data + idx * v->elem, item, v->elem);
}
