/**
 * @file str.c
 * @brief Implementación de la cadena dinámica segura (ver str.h).
 */
#include "structs/str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Capacidad mínima al crecer desde vacío (incluye terminador). */
#define STR_MIN_CAP 16

/** Cadena vacía estática para devolver desde ::str_cstr cuando no hay buffer. */
static const char STR_EMPTY[1] = {'\0'};

/**
 * @brief Garantiza capacidad para @p need bytes (terminador incluido).
 * @return 1 en éxito, 0 si falló @c realloc.
 */
static int str_grow(Str *s, size_t need) {
    if (need <= s->cap) return 1;

    size_t cap = s->cap ? s->cap : STR_MIN_CAP;
    while (cap < need)
        cap <<= 1;

    char *nd = realloc(s->data, cap);
    if (!nd) return 0;

    s->data = nd;
    s->cap = cap;
    return 1;
}

int str_init(Str *s) {
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
    return 1;
}

int str_init_cap(Str *s, size_t cap) {
    str_init(s);
    if (!str_grow(s, cap + 1)) return 0;
    s->data[0] = '\0';
    return 1;
}

void str_free(Str *s) {
    free(s->data);
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

int str_reserve(Str *s, size_t min_len) {
    if (!str_grow(s, min_len + 1)) return 0;
    s->data[s->len] = '\0';
    return 1;
}

void str_clear(Str *s) {
    s->len = 0;
    if (s->data) s->data[0] = '\0';
}

int str_push(Str *s, char c) {
    if (!str_grow(s, s->len + 2)) return 0;
    s->data[s->len++] = c;
    s->data[s->len] = '\0';
    return 1;
}

int str_append_len(Str *s, const char *buf, size_t n) {
    if (n == 0) return 1;
    if (!str_grow(s, s->len + n + 1)) return 0;
    memcpy(s->data + s->len, buf, n);
    s->len += n;
    s->data[s->len] = '\0';
    return 1;
}

int str_append(Str *s, const char *cstr) {
    return str_append_len(s, cstr, strlen(cstr));
}

int str_append_str(Str *s, const Str *o) {
    return str_append_len(s, o->data ? o->data : "", o->len);
}

int str_appendf(Str *s, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);

    /* primera pasada: averiguar cuántos bytes hacen falta */
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (need < 0) {
        va_end(ap2);
        return 0;
    }

    if (!str_grow(s, s->len + (size_t)need + 1)) {
        va_end(ap2);
        return 0;
    }

    /* segunda pasada: escribir en el buffer ya dimensionado */
    vsnprintf(s->data + s->len, (size_t)need + 1, fmt, ap2);
    va_end(ap2);
    s->len += (size_t)need;
    return 1;
}

int str_set(Str *s, const char *cstr) {
    s->len = 0;
    return str_append(s, cstr);
}

int str_insert(Str *s, size_t pos, const char *buf, size_t n) {
    if (pos > s->len) return 0;
    if (n == 0) return 1;
    if (!str_grow(s, s->len + n + 1)) return 0;

    /* desplazar la cola (incluido el terminador) y copiar */
    memmove(s->data + pos + n, s->data + pos, s->len - pos + 1);
    memcpy(s->data + pos, buf, n);
    s->len += n;
    return 1;
}

int str_remove_range(Str *s, size_t from, size_t to) {
    if (from > to || to > s->len) return 0;
    if (from == to) return 1;

    memmove(s->data + from, s->data + to, s->len - to);
    s->len -= (to - from);
    s->data[s->len] = '\0';
    return 1;
}

const char *str_cstr(const Str *s) {
    return s->data ? s->data : STR_EMPTY;
}
