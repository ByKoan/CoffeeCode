/**
 * @file test_str.c
 * @brief Pruebas unitarias de la cadena dinámica (structs/str).
 *
 * Compilar y ejecutar:
 *   gcc -std=c11 -I include test/structs/test_str.c src/structs/str.c -o t && ./t
 */
#include "structs/str.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    Str s;
    str_init(&s);
    assert(strcmp(str_cstr(&s), "") == 0);   /* vacío seguro, nunca NULL */

    str_append(&s, "hola");
    str_push(&s, ' ');
    str_appendf(&s, "%d-%s", 42, "x");
    assert(strcmp(str_cstr(&s), "hola 42-x") == 0 && str_len(&s) == 9);

    str_insert(&s, 0, ">>", 2);
    assert(strcmp(str_cstr(&s), ">>hola 42-x") == 0);

    str_remove_range(&s, 0, 2);
    assert(strcmp(str_cstr(&s), "hola 42-x") == 0);

    str_set(&s, "nuevo");
    assert(strcmp(str_cstr(&s), "nuevo") == 0 && str_len(&s) == 5);

    /* muchos appends para forzar varios crecimientos y comprobar terminador */
    str_clear(&s);
    for (int i = 0; i < 1000; i++) str_push(&s, 'a');
    assert(str_len(&s) == 1000 && str_cstr(&s)[1000] == '\0');

    str_free(&s);

    printf("test_str: OK\n");
    return 0;
}
