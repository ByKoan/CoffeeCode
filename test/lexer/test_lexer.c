/**
 * @file test_lexer.c
 * @brief Pruebas unitarias del tokenizador de C (lexer) con ctests.
 */
#include "ctests.h"
#include "lexer/lexer.h"
#include <string.h>

/**
 * @brief ¿Hay al menos un token de tipo @p type en @p lt?
 * @param lt   Resultado de tokenizar una línea.
 * @param type Tipo de token a buscar.
 * @return 1 si existe alguno de ese tipo; 0 si no.
 */
static int has_type(const LineTokens *lt, TokenType type) {
    for (int i = 0; i < lt->count; i++)
        if (lt->tokens[i].type == type) return 1;
    return 0;
}

/** Tokeniza @p text con el resaltador de C y guarda el resultado en @p out. */
static int tok_c(const char *text, LineTokens *out, int in_block) {
    const Highlighter *hl = &highlighter_c;
    return hl->tokenize_line(hl, text, (int)strlen(text), out, in_block);
}

/** for_path elige C para .c/.h y texto plano para .txt. */
static void test_for_path(void) {
    /* extensiones de C/C++ → resaltador de C */
    EXPECT_TRUE(highlighter_for_path("main.c") == &highlighter_c);
    EXPECT_TRUE(highlighter_for_path("editor.h") == &highlighter_c);
    /* extensión desconocida → texto plano */
    EXPECT_TRUE(highlighter_for_path("notas.txt") == &highlighter_none);
    /* sin nombre / NULL → asume C */
    EXPECT_TRUE(highlighter_for_path(NULL) == &highlighter_c);
    /* el por defecto también es C */
    EXPECT_TRUE(highlighter_default() == &highlighter_c);
}

/** "int x = 42;" produce un tipo, un número, operador y puntuación. */
static void test_linea_c(void) {
    LineTokens lt;
    int blk = tok_c("int x = 42;", &lt, 0);
    EXPECT_EQ_INT(blk, 0); /* no abre bloque de comentario */
    EXPECT_GT(lt.count, 0);
    EXPECT_TRUE(has_type(&lt, TOK_TYPE));        /* int */
    EXPECT_TRUE(has_type(&lt, TOK_NUMBER));      /* 42 */
    EXPECT_TRUE(has_type(&lt, TOK_OPERATOR));    /* = */
    EXPECT_TRUE(has_type(&lt, TOK_PUNCTUATION)); /* ; */
}

/** Una palabra reservada se clasifica como keyword. */
static void test_keyword(void) {
    LineTokens lt;
    tok_c("return 0;", &lt, 0);
    EXPECT_TRUE(has_type(&lt, TOK_KEYWORD)); /* return */
}

/** Una cadena entre comillas se tokeniza como TOK_STRING. */
static void test_string(void) {
    LineTokens lt;
    tok_c("\"hola\"", &lt, 0);
    EXPECT_TRUE(has_type(&lt, TOK_STRING));
}

/** Un comentario de línea // x se tokeniza como TOK_COMMENT. */
static void test_comentario_linea(void) {
    LineTokens lt;
    tok_c("// x", &lt, 0);
    EXPECT_TRUE(has_type(&lt, TOK_COMMENT));
}

/** Una directiva #include se tokeniza como TOK_PREPROCESSOR. */
static void test_preprocesador(void) {
    LineTokens lt;
    tok_c("#include <stdio.h>", &lt, 0);
    EXPECT_TRUE(has_type(&lt, TOK_PREPROCESSOR));
}

/**
 * Comentario de bloque multilínea: abrir un bloque sin cerrar deja
 * "dentro de bloque" (1); la línea de cierre con in_block=1 devuelve 0.
 */
static void test_bloque_multilinea(void) {
    LineTokens lt;
    /* abre bloque y no lo cierra: sigue dentro (1) */
    int blk = tok_c("/* empieza aqui", &lt, 0);
    EXPECT_EQ_INT(blk, 1);
    EXPECT_TRUE(has_type(&lt, TOK_COMMENT));

    /* línea de cierre, entrando ya dentro de un bloque: sale (0) */
    blk = tok_c("fin */", &lt, 1);
    EXPECT_EQ_INT(blk, 0);
    EXPECT_TRUE(has_type(&lt, TOK_COMMENT));
}

int main(void) {
    tt_suite("lexer");
    tt_run("highlighter_for_path por extension", test_for_path);
    tt_run("tokeniza linea de C", test_linea_c);
    tt_run("palabra reservada como keyword", test_keyword);
    tt_run("cadena como string", test_string);
    tt_run("comentario de linea", test_comentario_linea);
    tt_run("directiva de preprocesador", test_preprocesador);
    tt_run("comentario de bloque multilinea", test_bloque_multilinea);
    return tt_summary();
}
