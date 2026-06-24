/**
 * @file test_lexer.c
 * @brief Pruebas del resaltador de C embebido (builtin/lang_c) con ctests.
 *
 * El core ya no tiene resaltador propio: el de C es una extension nativa
 * embebida (::coffee_builtin_c_highlight) que emite ::CoffeeSpan coloreados
 * tomando los colores del TEMA activo.  Estos tests ejercitan esa funcion con
 * un tema conocido y comprueban que cada construccion de C (tipo, numero,
 * operador, puntuacion, keyword, string, comentario de linea y de bloque
 * multilinea) produce un tramo con el color de su categoria en el tema.
 */
#include "builtin/lang_c.h"
#include "ctests.h"
#include "lexer/lexer.h"   /* LexTokenType (categorias del tema) */
#include "render/theme.h"  /* Theme + theme_preset */
#include <string.h>

/* Coincide el color del tramo (CoffeeColor) con el del tema (Color)?  El span
 * lleva el color final que decidio la extension; el tema lo define en Color. */
static int span_is(CoffeeColor a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/* Hay algun tramo en @p out cuyo color sea el del tema para la categoria
 * @p type?  Es la forma de comprobar "hay un token de este tipo" ahora que el
 * span lleva el color final (no la categoria). */
static int has_color_of(const Theme *th, const CoffeeSpan *out, int n,
                        LexTokenType type) {
    Color want = th->tokens[type];
    for (int i = 0; i < n; i++)
        if (span_is(out[i].color, want)) return 1;
    return 0;
}

/* Resalta @p text con el resaltador de C embebido sobre el tema @p th. */
static int hl_c(const Theme *th, const char *text, CoffeeSpan *out, int max,
                int in_block, int *out_block) {
    return coffee_builtin_c_highlight((void *)th, text, (int)strlen(text),
                                      in_block, out, max, out_block);
}

/** "int x = 42;" produce un tipo, un numero, operador y puntuacion. */
static void test_linea_c(void) {
    Theme th = theme_preset(0);
    CoffeeSpan out[64];
    int blk = 0;
    int n = hl_c(&th, "int x = 42;", out, 64, 0, &blk);
    EXPECT_EQ_INT(blk, 0); /* no abre bloque de comentario */
    EXPECT_GT(n, 0);
    EXPECT_TRUE(has_color_of(&th, out, n, TOK_TYPE));        /* int */
    EXPECT_TRUE(has_color_of(&th, out, n, TOK_NUMBER));      /* 42 */
    EXPECT_TRUE(has_color_of(&th, out, n, TOK_OPERATOR));    /* = */
    EXPECT_TRUE(has_color_of(&th, out, n, TOK_PUNCTUATION)); /* ; */
}

/** Una palabra reservada se clasifica como keyword. */
static void test_keyword(void) {
    Theme th = theme_preset(0);
    CoffeeSpan out[64];
    int n = hl_c(&th, "return 0;", out, 64, 0, NULL);
    EXPECT_TRUE(has_color_of(&th, out, n, TOK_KEYWORD)); /* return */
}

/** Una cadena entre comillas se tokeniza como string. */
static void test_string(void) {
    Theme th = theme_preset(0);
    CoffeeSpan out[64];
    int n = hl_c(&th, "\"hola\"", out, 64, 0, NULL);
    EXPECT_TRUE(has_color_of(&th, out, n, TOK_STRING));
}

/** Un comentario de linea // x se tokeniza como comentario. */
static void test_comentario_linea(void) {
    Theme th = theme_preset(0);
    CoffeeSpan out[64];
    int n = hl_c(&th, "// x", out, 64, 0, NULL);
    EXPECT_TRUE(has_color_of(&th, out, n, TOK_COMMENT));
}

/** Una directiva #include se tokeniza como preprocesador. */
static void test_preprocesador(void) {
    Theme th = theme_preset(0);
    CoffeeSpan out[64];
    int n = hl_c(&th, "#include <stdio.h>", out, 64, 0, NULL);
    EXPECT_TRUE(has_color_of(&th, out, n, TOK_PREPROCESSOR));
}

/**
 * Comentario de bloque multilinea: abrir un bloque sin cerrar deja "dentro de
 * bloque" (out_block=1); la linea de cierre con in_block=1 devuelve out_block=0.
 */
static void test_bloque_multilinea(void) {
    Theme th = theme_preset(0);
    CoffeeSpan out[64];
    int blk = 0;
    int n = hl_c(&th, "/* empieza aqui", out, 64, 0, &blk);
    EXPECT_EQ_INT(blk, 1); /* sigue dentro del bloque */
    EXPECT_TRUE(has_color_of(&th, out, n, TOK_COMMENT));

    /* linea de cierre, entrando ya dentro de un bloque: sale (0) */
    n = hl_c(&th, "fin */", out, 64, 1, &blk);
    EXPECT_EQ_INT(blk, 0);
    EXPECT_TRUE(has_color_of(&th, out, n, TOK_COMMENT));
}

/** Las columnas se emiten en CODEPOINTS: un acento previo desplaza una columna,
 *  no dos (aunque ocupe dos bytes en UTF-8). */
static void test_columnas_codepoints(void) {
    Theme th = theme_preset(0);
    CoffeeSpan out[64];
    /* "a" + e-acento (2 bytes) + " int": el tipo 'int' empieza en la columna 4
     * (codepoints: a, e-acento, espacio, i) aunque su byte de inicio sea 5. */
    int n = hl_c(&th, "a\xC3\xA9 int", out, 64, 0, NULL);
    int found = 0;
    Color want = th.tokens[TOK_TYPE];
    for (int i = 0; i < n; i++)
        if (span_is(out[i].color, want)) {
            EXPECT_EQ_INT((int)out[i].start_col, 3); /* a, e-acento, espacio */
            EXPECT_EQ_INT((int)out[i].len, 3);       /* i n t */
            found = 1;
        }
    EXPECT_TRUE(found);
}

int main(void) {
    tt_suite("lexer");
    tt_run("tokeniza linea de C", test_linea_c);
    tt_run("palabra reservada como keyword", test_keyword);
    tt_run("cadena como string", test_string);
    tt_run("comentario de linea", test_comentario_linea);
    tt_run("directiva de preprocesador", test_preprocesador);
    tt_run("comentario de bloque multilinea", test_bloque_multilinea);
    tt_run("columnas en codepoints (UTF-8)", test_columnas_codepoints);
    return tt_summary();
}
