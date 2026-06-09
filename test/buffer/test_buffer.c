/**
 * @file test_buffer.c
 * @brief Pruebas unitarias del gap buffer e índice de líneas (buffer) con
 * ctests.
 */
#include "buffer/buffer.h"
#include "ctests.h"
#include <string.h>

/** Vuelca el contenido lógico completo del buffer en @p out (null-terminado).
 */
static void dump(const Buffer *b, char *out, size_t cap) {
    size_t n = buf_get_text(b, 0, buf_length(b), out);
    if (n >= cap) n = cap - 1;
    out[n] = '\0';
}

/** Un buffer recién creado está vacío pero ya tiene una línea (la 0). */
static void test_init_vacio(void) {
    Buffer b;
    EXPECT_TRUE(buf_init(&b));
    EXPECT_EQ_INT((int)buf_length(&b), 0);
    EXPECT_EQ_INT(buf_line_count(&b), 1);
    EXPECT_EQ_INT((int)buf_cursor_pos(&b), 0);
    buf_free(&b);
}

/** insert_str escribe en el cursor; el contenido y la longitud cuadran. */
static void test_insert_y_contenido(void) {
    Buffer b;
    buf_init(&b);
    buf_insert_str(&b, "hello", 5);
    EXPECT_EQ_INT((int)buf_length(&b), 5);
    EXPECT_EQ_INT((int)buf_cursor_pos(&b), 5);
    EXPECT_EQ_INT(buf_char_at(&b, 0), 'h');
    EXPECT_EQ_INT(buf_char_at(&b, 4), 'o');

    char out[64];
    dump(&b, out, sizeof out);
    EXPECT_EQ_STR(out, "hello");
    buf_free(&b);
}

/** Mover el cursor al medio e insertar deja el hueco (gap) en su sitio. */
static void test_mover_e_insertar(void) {
    Buffer b;
    buf_init(&b);
    buf_insert_str(&b, "helloworld", 10);
    buf_move_to(&b, 5);
    EXPECT_EQ_INT((int)buf_cursor_pos(&b), 5);
    buf_insert_str(&b, " ", 1);

    char out[64];
    dump(&b, out, sizeof out);
    EXPECT_EQ_STR(out, "hello world");
    buf_free(&b);
}

/** delete_before borra antes del cursor; delete_range borra un tramo. */
static void test_borrado(void) {
    Buffer b;
    buf_init(&b);
    buf_insert_str(&b, "hello", 5);
    buf_delete_before(&b); /* cursor al final: borra la 'o' */

    char out[64];
    dump(&b, out, sizeof out);
    EXPECT_EQ_STR(out, "hell");

    buf_delete_range(&b, 1, 3); /* borra "el" -> "hl" */
    dump(&b, out, sizeof out);
    EXPECT_EQ_STR(out, "hl");
    buf_free(&b);
}

/** El índice de líneas: conteo, offset de inicio, (línea,col) y fin de línea.
 */
static void test_indice_lineas(void) {
    Buffer b;
    buf_init(&b);
    /* a \n b b \n c c c  ->  offsets: 0,1,2,3,4,5,6,7 */
    buf_insert_str(&b, "a\nbb\nccc", 8);
    EXPECT_EQ_INT(buf_line_count(&b), 3);
    EXPECT_EQ_INT((int)buf_line_offset(&b, 0), 0);
    EXPECT_EQ_INT((int)buf_line_offset(&b, 1), 2);
    EXPECT_EQ_INT((int)buf_line_offset(&b, 2), 5);

    int line = -1, col = -1;
    buf_line_col(&b, 6, &line, &col); /* segundo carácter de "ccc" */
    EXPECT_EQ_INT(line, 2);
    EXPECT_EQ_INT(col, 1);

    /* la línea 1 es "bb": su fin (antes del '\n') está en el offset 4 */
    EXPECT_EQ_INT((int)buf_line_end(&b, buf_line_offset(&b, 1)), 4);
    buf_free(&b);
}

/** Borrar un '\n' fusiona su línea con la siguiente. */
static void test_borrar_salto_une_lineas(void) {
    Buffer b;
    buf_init(&b);
    buf_insert_str(&b, "a\nb", 3);
    EXPECT_EQ_INT(buf_line_count(&b), 2);

    buf_delete_range(&b, 1, 2); /* borra el '\n' del medio */
    EXPECT_EQ_INT(buf_line_count(&b), 1);

    char out[16];
    dump(&b, out, sizeof out);
    EXPECT_EQ_STR(out, "ab");
    buf_free(&b);
}

int main(void) {
    tt_suite("buffer");
    tt_run("init deja un buffer vacio con 1 linea", test_init_vacio);
    tt_run("insert_str y lectura de contenido", test_insert_y_contenido);
    tt_run("mover el cursor e insertar en medio", test_mover_e_insertar);
    tt_run("delete_before y delete_range", test_borrado);
    tt_run("indice de lineas (count/offset/col/end)", test_indice_lineas);
    tt_run("borrar un salto de linea une dos lineas",
           test_borrar_salto_une_lineas);
    return tt_summary();
}
