/**
 * @file str.c
 * @brief Implementación de la cadena dinámica segura (ver str.h).
 *
 * @note Patrón "string builder". Una cadena dinámica encapsula el trío
 * @c puntero + @c longitud + @c capacidad para construir texto sin
 * desbordamientos ni cálculos manuales de tamaño. A diferencia del vector
 * genérico, aquí el "elemento" es siempre un @c char, así que no hace falta
 * guardar @c elem. Dos invariantes guían todo el código:
 *   1. @c cap incluye espacio para el terminador, es decir @c cap @c >= @c
 * len+1 siempre que haya buffer.
 *   2. Tras cualquier operación, @c data[len]=='\0', de modo que ::str_cstr
 * puede pasarse directo a cualquier API de C (printf, fopen, ...). El
 * crecimiento es amortizado O(1) duplicando capacidad, igual que el vector.
 */
#include "structs/str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Capacidad mínima al crecer desde vacío (incluye terminador). */
#define STR_MIN_CAP 16

/** Cadena vacía estática para devolver desde ::str_cstr cuando no hay buffer.
 */
static const char STR_EMPTY[1] = {'\0'};

/**
 * @brief Garantiza capacidad para @p need bytes (terminador incluido).
 *
 * Mismo crecimiento amortizado que el vector: se duplica la capacidad hasta
 * cubrir @p need. El llamante siempre pide aquí el tamaño CON el '\0' contado,
 * por lo que tras crecer hay sitio garantizado para el terminador.
 * @param s Cadena (no nula).
 * @param need Bytes necesarios contando el terminador.
 * @return 1 en éxito, 0 si falló @c realloc (la cadena queda intacta).
 */
static int str_grow(Str *s, size_t need) {
    if (need <= s->cap) return 1; /* ya cabe (terminador incluido) */

    /* Arrancar en STR_MIN_CAP si no había buffer; si no, desde la cap actual.
     */
    size_t cap = s->cap ? s->cap : STR_MIN_CAP;
    while (cap < need)
        cap <<= 1; /* duplicar capacidad: crecimiento amortizado O(1) */

    /* realloc puede mover el buffer: data podría cambiar de dirección. */
    char *nd = realloc(s->data, cap);
    if (!nd) return 0; /* sin memoria: cadena original intacta */

    s->data = nd;
    s->cap = cap;
    return 1;
}

/**
 * @brief Inicializa una cadena vacía (no reserva memoria).
 * @param s Cadena a inicializar (no nula).
 * @return 1 siempre.
 */
int str_init(Str *s) {
    s->data = NULL; /* sin buffer: ::str_cstr devolverá STR_EMPTY */
    s->len = 0;
    s->cap = 0;
    return 1;
}

/**
 * @brief Inicializa una cadena reservando capacidad para @p cap caracteres.
 * @param s Cadena a inicializar (no nula).
 * @param cap Capacidad inicial en caracteres (sin contar el terminador).
 * @return 1 en éxito, 0 si falló la reserva.
 */
int str_init_cap(Str *s, size_t cap) {
    str_init(s);
    /* +1 para reservar también el hueco del terminador. */
    if (!str_grow(s, cap + 1)) return 0;
    s->data[0] = '\0'; /* establecer la invariante: cadena vacía válida */
    return 1;
}

/**
 * @brief Libera la memoria y deja la cadena vacía reutilizable.
 * @param s Cadena (no nula). Seguro llamarlo varias veces.
 */
void str_free(Str *s) {
    free(s->data);  /* free(NULL) es válido */
    s->data = NULL; /* evita doble free */
    s->len = 0;
    s->cap = 0;
}

/**
 * @brief Garantiza espacio para al menos @p min_len caracteres (+ terminador).
 * @param s Cadena (no nula).
 * @param min_len Longitud mínima en caracteres, sin contar el terminador.
 * @return 1 en éxito, 0 si falló la reasignación.
 */
int str_reserve(Str *s, size_t min_len) {
    if (!str_grow(s, min_len + 1)) return 0; /* +1: terminador */
    /* Mantener la invariante del '\0' aunque solo se haya reservado: si data
     * era NULL, ahora existe y debe terminar en '\0' en la posición len. */
    s->data[s->len] = '\0';
    return 1;
}

/**
 * @brief Vacía la cadena (longitud 0) conservando la capacidad.
 * @param s Cadena (no nula).
 */
void str_clear(Str *s) {
    s->len = 0;
    /* Restaurar '\0' al inicio si hay buffer, para que ::str_cstr dé "". */
    if (s->data) s->data[0] = '\0';
}

/**
 * @brief Añade un carácter al final.
 * @param s Cadena (no nula).
 * @param c Carácter a añadir.
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_push(Str *s, char c) {
    /* len+2 = carácter nuevo + terminador. */
    if (!str_grow(s, s->len + 2)) return 0;
    s->data[s->len++] = c;  /* escribir el carácter y avanzar len */
    s->data[s->len] = '\0'; /* re-cerrar la cadena */
    return 1;
}

/**
 * @brief Añade @p n bytes desde @p buf (pueden contener '\0' intermedios).
 * @param s Cadena (no nula).
 * @param buf Origen de los bytes (no nulo si @p n > 0).
 * @param n Número de bytes a copiar.
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_append_len(Str *s, const char *buf, size_t n) {
    if (n == 0) return 1; /* nada que añadir */
    /* len + n + 1 = contenido actual + nuevos bytes + terminador. */
    if (!str_grow(s, s->len + n + 1)) return 0;
    /* Copiar al final actual; memcpy basta (buffers distintos, sin solape). */
    memcpy(s->data + s->len, buf, n);
    s->len += n;
    s->data[s->len] = '\0'; /* re-cerrar tras el bloque añadido */
    return 1;
}

/**
 * @brief Añade una cadena C terminada en '\0'.
 * @param s Cadena (no nula).
 * @param cstr Cadena C a añadir (no nula).
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_append(Str *s, const char *cstr) {
    /* Reutiliza append_len midiendo la longitud con strlen. */
    return str_append_len(s, cstr, strlen(cstr));
}

/**
 * @brief Añade el contenido de otra cadena dinámica.
 * @param s Cadena destino (no nula).
 * @param o Cadena origen (no nula).
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_append_str(Str *s, const Str *o) {
    /* Si o aún no tiene buffer (data==NULL), tratarla como cadena vacía. */
    return str_append_len(s, o->data ? o->data : "", o->len);
}

/**
 * @brief Añade texto con formato estilo @c printf de forma segura.
 *
 * Usa la técnica de "doble pasada" de vsnprintf: la primera con destino NULL
 * solo CALCULA cuántos bytes haría falta (sin escribir), y con ese tamaño se
 * crece el buffer y se hace la segunda pasada que ya escribe. Así se formatea
 * sin riesgo de desbordamiento y sin buffers temporales fijos.
 * @param s Cadena destino (no nula).
 * @param fmt Cadena de formato estilo @c printf.
 * @return 1 en éxito, 0 si falló al crecer o el formato fue inválido.
 */
int str_appendf(Str *s, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    /* Una va_list se "consume" al recorrerla, así que duplicamos los
     * argumentos: ap para medir, ap2 para escribir. */
    va_copy(ap2, ap);

    /* Primera pasada: vsnprintf con buffer NULL y tamaño 0 devuelve el número
     * de bytes que escribiría (sin el '\0'), sin tocar memoria. */
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap); /* ap ya cumplió su función */

    if (need < 0) { /* error de codificación en el formato */
        va_end(ap2);
        return 0;
    }

    /* Asegurar sitio para los 'need' bytes + el terminador. */
    if (!str_grow(s, s->len + (size_t)need + 1)) {
        va_end(ap2);
        return 0;
    }

    /* Segunda pasada: escribir en el buffer ya dimensionado. El límite need+1
     * incluye el '\0' que vsnprintf coloca automáticamente. */
    vsnprintf(s->data + s->len, (size_t)need + 1, fmt, ap2);
    va_end(ap2);
    s->len += (size_t)need; /* el '\0' ya lo puso vsnprintf */
    return 1;
}

/**
 * @brief Reemplaza el contenido por la cadena C @p cstr.
 * @param s Cadena (no nula).
 * @param cstr Nuevo contenido (no nulo).
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_set(Str *s, const char *cstr) {
    s->len = 0;                 /* "vaciar" lógicamente (conserva capacidad) */
    return str_append(s, cstr); /* y reescribir desde cero */
}

/**
 * @brief Inserta @p n bytes de @p buf en la posición @p pos.
 * @param s Cadena (no nula).
 * @param pos Posición de inserción en @c [0, len].
 * @param buf Origen de los bytes (no nulo si @p n > 0).
 * @param n Número de bytes a insertar.
 * @return 1 en éxito, 0 si @p pos fuera de rango o falló al crecer.
 */
int str_insert(Str *s, size_t pos, const char *buf, size_t n) {
    if (pos > s->len) return 0; /* posición inválida */
    if (n == 0) return 1;       /* nada que insertar */
    if (!str_grow(s, s->len + n + 1)) return 0;

    /* Abrir hueco de n bytes en pos: mover la cola [pos, len] a la derecha.
     * Se copian (len - pos + 1) bytes para arrastrar también el '\0' final,
     * con lo que la cadena queda cerrada sin re-escribir el terminador. memmove
     * porque origen y destino se solapan dentro del mismo buffer. */
    memmove(s->data + pos + n, s->data + pos, s->len - pos + 1);
    /* Rellenar el hueco con los bytes nuevos. */
    memcpy(s->data + pos, buf, n);
    s->len += n;
    return 1;
}

/**
 * @brief Elimina el rango de caracteres @c [from, to).
 * @param s Cadena (no nula).
 * @param from Inicio del rango (incluido).
 * @param to Fin del rango (excluido).
 * @return 1 en éxito, 0 si el rango es inválido.
 */
int str_remove_range(Str *s, size_t from, size_t to) {
    if (from > to || to > s->len)
        return 0;             /* rango mal formado o fuera de límites */
    if (from == to) return 1; /* rango vacío */

    /* Traer la cola [to, len) sobre la posición from, tapando el rango borrado.
     * Se mueven (len - to) bytes; memmove por el solapamiento. */
    memmove(s->data + from, s->data + to, s->len - to);
    s->len -= (to - from);  /* descontar lo eliminado */
    s->data[s->len] = '\0'; /* re-cerrar la cadena en su nueva longitud */
    return 1;
}

/**
 * @brief Devuelve la cadena como @c const @c char* terminada en '\0'.
 * @param s Cadena (no nula).
 * @return Puntero válido null-terminado; nunca @c NULL (cadena vacía => "").
 */
const char *str_cstr(const Str *s) {
    /* Si nunca se reservó buffer, devolver la cadena vacía estática para que el
     * llamante siempre reciba un puntero usable. */
    return s->data ? s->data : STR_EMPTY;
}
