/**
 * @file str.h
 * @brief Cadena dinámica segura: crece sola y mantiene siempre el terminador @c
 * '\0'.
 *
 * Encapsula el patrón @c malloc/realloc + longitud para manejar texto sin
 * desbordamientos ni cálculos manuales de tamaño. Tras cualquier operación,
 * @c data[len] es @c '\0', por lo que ::str_cstr es seguro para APIs de C.
 *
 * @code
 * Str s;
 * str_init(&s);
 * str_append(&s, "hola");
 * str_push(&s, ' ');
 * str_appendf(&s, "%d", 42);
 * puts(str_cstr(&s));        // "hola 42"
 * str_free(&s);
 * @endcode
 */
#pragma once

#include <stdarg.h>
#include <stddef.h>

/**
 * @brief Cadena dinámica.
 *
 * Invariante: si @c cap @c > @c 0 entonces @c data[len]=='\0' y @c cap @c >= @c
 * len+1.
 */
typedef struct {
    /** Buffer de caracteres terminado en @c '\0' (o @c NULL si vacío sin
     * reservar). */
    char *data;
    size_t len; /**< Longitud en bytes, sin contar el terminador. */
    size_t cap; /**< Capacidad del buffer en bytes (incluye el terminador). */
} Str;

/**
 * @brief Inicializa una cadena vacía (no reserva memoria).
 * @param s Cadena a inicializar (no nula).
 * @return 1 siempre.
 */
int str_init(Str *s);

/**
 * @brief Inicializa una cadena reservando capacidad para @p cap caracteres.
 * @param s Cadena a inicializar (no nula).
 * @param cap Capacidad inicial en caracteres (sin contar el terminador).
 * @return 1 en éxito, 0 si falló la reserva.
 */
int str_init_cap(Str *s, size_t cap);

/**
 * @brief Libera la memoria y deja la cadena vacía reutilizable.
 * @param s Cadena (no nula). Seguro llamarlo varias veces.
 */
void str_free(Str *s);

/**
 * @brief Garantiza espacio para al menos @p min_len caracteres (+ terminador).
 * @param s Cadena (no nula).
 * @param min_len Longitud mínima en caracteres, sin contar el terminador.
 * @return 1 en éxito, 0 si falló la reasignación.
 */
int str_reserve(Str *s, size_t min_len);

/**
 * @brief Vacía la cadena (longitud 0) conservando la capacidad.
 * @param s Cadena (no nula).
 */
void str_clear(Str *s);

/**
 * @brief Añade un carácter al final.
 * @param s Cadena (no nula).
 * @param c Carácter a añadir.
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_push(Str *s, char c);

/**
 * @brief Añade una cadena C terminada en @c '\0'.
 * @param s Cadena (no nula).
 * @param cstr Cadena C a añadir (no nula).
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_append(Str *s, const char *cstr);

/**
 * @brief Añade @p n bytes desde @p buf (pueden contener @c '\0' intermedios).
 * @param s Cadena (no nula).
 * @param buf Origen de los bytes (no nulo si @p n > 0).
 * @param n Número de bytes a copiar.
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_append_len(Str *s, const char *buf, size_t n);

/**
 * @brief Añade el contenido de otra cadena dinámica.
 * @param s Cadena destino (no nula).
 * @param o Cadena origen (no nula).
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_append_str(Str *s, const Str *o);

/**
 * @brief Añade texto con formato @c printf de forma segura (sin desbordar).
 * @param s Cadena destino.
 * @param fmt Formato estilo @c printf.
 * @return 1 en éxito, 0 si falló al crecer o el formato fue inválido.
 */
int str_appendf(Str *s, const char *fmt, ...);

/**
 * @brief Reemplaza el contenido por la cadena C @p cstr.
 * @param s Cadena (no nula).
 * @param cstr Nuevo contenido (no nulo).
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_set(Str *s, const char *cstr);

/**
 * @brief Inserta @p n bytes de @p buf en la posición @p pos.
 * @param s Cadena (no nula).
 * @param pos Posición en @c [0, len].
 * @param buf Origen de los bytes (no nulo si @p n > 0).
 * @param n Número de bytes a insertar.
 * @return 1 en éxito, 0 si @p pos fuera de rango o falló al crecer.
 */
int str_insert(Str *s, size_t pos, const char *buf, size_t n);

/**
 * @brief Elimina el rango de caracteres @c [from, to).
 * @param s Cadena (no nula).
 * @param from Inicio del rango (incluido).
 * @param to Fin del rango (excluido).
 * @return 1 en éxito, 0 si el rango es inválido.
 */
int str_remove_range(Str *s, size_t from, size_t to);

/**
 * @brief Devuelve la cadena como @c const @c char* terminada en @c '\0'.
 * @param s Cadena (no nula).
 * @return Puntero válido y null-terminado; nunca @c NULL (cadena vacía => "").
 */
const char *str_cstr(const Str *s);

/**
 * @brief Longitud de la cadena en bytes (sin el terminador).
 * @param s Cadena (no nula).
 * @return Longitud actual en bytes.
 */
static inline size_t str_len(const Str *s) {
    return s->len;
}
