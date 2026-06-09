/**
 * @file test_str.c
 * @brief Pruebas unitarias de la cadena dinámica (structs/str) con ctests.
 */
#include "ctests.h"
#include "structs/str.h"

/** Una cadena recién inicializada es la cadena vacía, nunca NULL. */
static void test_init_vacia(void) {
    Str s;
    str_init(&s);
    EXPECT_EQ_STR(str_cstr(&s), ""); /* vacío seguro, nunca NULL */
    str_free(&s);
}

/** append, push y appendf van concatenando texto al final. */
static void test_append_push_appendf(void) {
    Str s;
    str_init(&s);
    str_append(&s, "hola");
    str_push(&s, ' ');
    str_appendf(&s, "%d-%s", 42, "x");
    EXPECT_EQ_STR(str_cstr(&s), "hola 42-x");
    EXPECT_EQ_INT((int)str_len(&s), 9);
    str_free(&s);
}

/** insert abre hueco en una posición; remove_range lo cierra. */
static void test_insert_remove_range(void) {
    Str s;
    str_init(&s);
    str_append(&s, "hola 42-x");

    str_insert(&s, 0, ">>", 2);
    EXPECT_EQ_STR(str_cstr(&s), ">>hola 42-x");

    str_remove_range(&s, 0, 2);
    EXPECT_EQ_STR(str_cstr(&s), "hola 42-x");
    str_free(&s);
}

/** set reemplaza por completo el contenido. */
static void test_set(void) {
    Str s;
    str_init(&s);
    str_append(&s, "previo");

    str_set(&s, "nuevo");
    EXPECT_EQ_STR(str_cstr(&s), "nuevo");
    EXPECT_EQ_INT((int)str_len(&s), 5);
    str_free(&s);
}

/** Muchos push fuerzan varios crecimientos y mantienen el terminador. */
static void test_crecimiento_y_terminador(void) {
    Str s;
    str_init(&s);

    str_clear(&s);
    for (int i = 0; i < 1000; i++)
        str_push(&s, 'a');
    EXPECT_EQ_INT((int)str_len(&s), 1000);
    EXPECT_EQ_INT(str_cstr(&s)[1000], '\0');
    str_free(&s);
}

int main(void) {
    tt_suite("str");
    tt_run("init crea cadena vacia", test_init_vacia);
    tt_run("append, push y appendf", test_append_push_appendf);
    tt_run("insert y remove_range", test_insert_remove_range);
    tt_run("set reemplaza el contenido", test_set);
    tt_run("crecimiento y terminador", test_crecimiento_y_terminador);
    return tt_summary();
}
