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

/** Cursor y borrado UTF-8: mover/borrar trata el carácter completo, no bytes.
 */
static void test_utf8_cursor(void) {
    Buffer b;
    buf_init(&b);
    buf_insert_str(&b, "caf\xC3\xA9", 5);  /* "café": é = C3 A9 (2 bytes) */
    EXPECT_EQ_INT((int)buf_length(&b), 5); /* 5 bytes */

    int line, col;
    buf_line_col(&b, buf_cursor_pos(&b), &line, &col);
    EXPECT_EQ_INT(col, 4); /* 4 caracteres (no 5 bytes) */

    /* mover a la izquierda salta el carácter é completo (2 bytes) */
    buf_move_left(&b);
    EXPECT_EQ_INT((int)buf_cursor_pos(&b), 3); /* byte inicial de é */
    buf_line_col(&b, buf_cursor_pos(&b), &line, &col);
    EXPECT_EQ_INT(col, 3);

    /* a la derecha vuelve a saltar é entero */
    buf_move_right(&b);
    EXPECT_EQ_INT((int)buf_cursor_pos(&b), 5);
    buf_line_col(&b, buf_cursor_pos(&b), &line, &col);
    EXPECT_EQ_INT(col, 4);

    /* backspace borra el carácter completo (é = 2 bytes) */
    buf_delete_before(&b);
    EXPECT_EQ_INT((int)buf_length(&b), 3);
    char out[16];
    dump(&b, out, sizeof out);
    EXPECT_EQ_STR(out, "caf");

    /* delete_after sobre un emoji de 4 bytes (U+1F600): se borra entero */
    buf_insert_str(&b, "\xF0\x9F\x98\x80", 4); /* "caf😀", cursor al final */
    buf_move_left(&b); /* cursor justo antes del emoji */
    EXPECT_EQ_INT((int)buf_cursor_pos(&b), 3);
    buf_delete_after(&b);
    EXPECT_EQ_INT((int)buf_length(&b), 3);
    dump(&b, out, sizeof out);
    EXPECT_EQ_STR(out, "caf");
    buf_free(&b);
}

/** Columnas = ancho de display: un carácter CJK ocupa 2 celdas. */
static void test_utf8_width(void) {
    Buffer b;
    buf_init(&b);
    /* "a日b": 日 = U+65E5 (E6 97 A5, 3 bytes, ancho 2 celdas) */
    buf_insert_str(&b,
                   "a\xE6\x97\xA5"
                   "b",
                   5);
    EXPECT_EQ_INT((int)buf_length(&b), 5); /* 5 bytes */

    int line, col;
    buf_line_col(&b, buf_cursor_pos(&b), &line, &col);
    EXPECT_EQ_INT(col, 4); /* a(1) + 日(2) + b(1) = 4 celdas */

    /* a mitad: justo tras 日 -> columna 3 (1 + 2) */
    buf_move_left(&b); /* cursor antes de 'b' */
    buf_line_col(&b, buf_cursor_pos(&b), &line, &col);
    EXPECT_EQ_INT(col, 3);
    buf_free(&b);
}

/** (linea, columna-en-caracteres) -> offset logico (convencion LSP/goto). */
static void test_offset_from_line_col_chars(void) {
    Buffer b;
    buf_init(&b);
    /* a \n b b \n c c c -> offsets 0..7 */
    buf_insert_str(&b, "a\nbb\nccc", 8);
    /* linea 0, col 0 = inicio del archivo */
    EXPECT_EQ_INT((int)buf_offset_from_line_col_chars(&b, 0, 0), 0);
    /* linea 1, col 0 = inicio de "bb" (offset 2) */
    EXPECT_EQ_INT((int)buf_offset_from_line_col_chars(&b, 1, 0), 2);
    /* linea 1, col 1 = segundo caracter de "bb" (offset 3) */
    EXPECT_EQ_INT((int)buf_offset_from_line_col_chars(&b, 1, 1), 3);
    /* linea 2, col 2 = tercer caracter de "ccc" (offset 7) */
    EXPECT_EQ_INT((int)buf_offset_from_line_col_chars(&b, 2, 2), 7);
    /* col mas alla del fin de linea: se recorta al '\n'/fin (offset 4 = fin "bb") */
    EXPECT_EQ_INT((int)buf_offset_from_line_col_chars(&b, 1, 99), 4);
    /* linea fuera de rango: se recorta a la ultima */
    EXPECT_EQ_INT((int)buf_offset_from_line_col_chars(&b, 99, 0), 5);
    /* col negativa: se recorta a 0 */
    EXPECT_EQ_INT((int)buf_offset_from_line_col_chars(&b, 2, -5), 5);
    buf_free(&b);

    /* UTF-8: la columna cuenta CARACTERES (no celdas ni bytes).  En "a<CJK>b"
     * el caracter CJK (E6 97 A5) ocupa 3 bytes y 2 celdas de display, pero 1
     * caracter: col 1 = el CJK (offset 1), col 2 = 'b' (offset 4). */
    Buffer u;
    buf_init(&u);
    buf_insert_str(&u,
                   "a\xE6\x97\xA5"
                   "b",
                   5);
    EXPECT_EQ_INT((int)buf_offset_from_line_col_chars(&u, 0, 0), 0); /* 'a'  */
    EXPECT_EQ_INT((int)buf_offset_from_line_col_chars(&u, 0, 1), 1); /* CJK  */
    EXPECT_EQ_INT((int)buf_offset_from_line_col_chars(&u, 0, 2), 4); /* 'b'  */
    buf_free(&u);
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
    tt_run("cursor y borrado UTF-8 (caracter completo)", test_utf8_cursor);
    tt_run("columnas por ancho de display (CJK = 2 celdas)", test_utf8_width);
    tt_run("(linea,col-caracteres) -> offset (convencion LSP)",
           test_offset_from_line_col_chars);
    return tt_summary();
}
