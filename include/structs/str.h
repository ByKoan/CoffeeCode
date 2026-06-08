/**
 * @file str.h
 * @brief Cadena dinámica segura: crece sola y mantiene siempre el terminador @c '\0'.
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

#include <stddef.h>
#include <stdarg.h>

/**
 * @brief Cadena dinámica.
 *
 * Invariante: si @c cap @c > @c 0 entonces @c data[len]=='\0' y @c cap @c >= @c len+1.
 */
typedef struct {
    char   *data; /**< Buffer de caracteres terminado en @c '\0' (o @c NULL si vacío sin reservar). */
    size_t  len;  /**< Longitud en bytes, sin contar el terminador. */
    size_t  cap;  /**< Capacidad del buffer en bytes (incluye el terminador). */
} Str;

/**
 * @brief Inicializa una cadena vacía (no reserva memoria).
 * @return 1 siempre.
 */
int str_init(Str *s);

/**
 * @brief Inicializa una cadena reservando capacidad para @p cap caracteres.
 * @return 1 en éxito, 0 si falló la reserva.
 */
int str_init_cap(Str *s, size_t cap);

/**
 * @brief Libera la memoria y deja la cadena vacía reutilizable.
 */
void str_free(Str *s);

/**
 * @brief Garantiza espacio para al menos @p min_len caracteres (+ terminador).
 * @return 1 en éxito, 0 si falló la reasignación.
 */
int str_reserve(Str *s, size_t min_len);

/**
 * @brief Vacía la cadena (longitud 0) conservando la capacidad.
 */
void str_clear(Str *s);

/**
 * @brief Añade un carácter al final.
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_push(Str *s, char c);

/**
 * @brief Añade una cadena C terminada en @c '\0'.
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_append(Str *s, const char *cstr);

/**
 * @brief Añade @p n bytes desde @p buf (pueden contener @c '\0' intermedios).
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_append_len(Str *s, const char *buf, size_t n);

/**
 * @brief Añade el contenido de otra cadena dinámica.
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
 * @return 1 en éxito, 0 si falló al crecer.
 */
int str_set(Str *s, const char *cstr);

/**
 * @brief Inserta @p n bytes de @p buf en la posición @p pos.
 * @param pos Posición en @c [0, len].
 * @return 1 en éxito, 0 si @p pos fuera de rango o falló al crecer.
 */
int str_insert(Str *s, size_t pos, const char *buf, size_t n);

/**
 * @brief Elimina el rango de caracteres @c [from, to).
 * @return 1 en éxito, 0 si el rango es inválido.
 */
int str_remove_range(Str *s, size_t from, size_t to);

/**
 * @brief Devuelve la cadena como @c const @c char* terminada en @c '\0'.
 * @return Puntero válido y null-terminado; nunca @c NULL (cadena vacía => "").
 */
const char *str_cstr(const Str *s);

/**
 * @brief Longitud de la cadena en bytes (sin el terminador).
 */
static inline size_t str_len(const Str *s) { return s->len; }
