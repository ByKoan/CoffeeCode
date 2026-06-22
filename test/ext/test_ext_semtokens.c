/**
 * @file test_ext_semtokens.c
 * @brief Pruebas headless de la descodificacion pura de semantic tokens LSP.
 *
 * Verifica de forma determinista (sin SDL, sin servidor, sin la API del IDE) el
 * modulo vex_semtokens, que es lo "puro" del resaltado de Vex:
 *
 *   - vex_semtokens_decode: el array plano de quintetos DELTA
 *     [deltaLine, deltaStartChar, length, tokenType, tokenModifiers] se acumula
 *     a posiciones ABSOLUTAS (misma linea suma deltaStartChar; deltaLine>0 hace
 *     que deltaStartChar sea absoluto).  Una cola incompleta se ignora.
 *   - vex_utf16_units_to_codepoints: conversion de columnas UTF-16 -> codepoints
 *     sobre una linea con multibyte (acentos = 1 unidad, emoji fuera del BMP =
 *     par subrogado = 2 unidades).
 *   - vex_semtoken_to_cp: combina ambas para medir un token en codepoints.
 */
#include "ctests.h"
#include "vex_semtokens.h"

#include <string.h>

/* ------------------------------------------------------------------------- */
/* Decodificacion del array plano de quintetos DELTA -> tokens absolutos.     */
/* ------------------------------------------------------------------------- */

static void test_decode_basico_misma_y_otra_linea(void) {
    /* Tres tokens conocidos:
     *   t0: linea 0, char 0,  len 5, tipo 13 (keyword)
     *   t1: misma linea, +6   -> char 6,  len 3, tipo 1  (type)
     *   t2: +2 lineas, char 4 -> linea 2, char 4, len 2, tipo 11 (function)
     */
    uint32_t data[] = {
        0, 0, 5, 13, 0,
        0, 6, 3, 1, 0,
        2, 4, 2, 11, 4,
    };
    VexSemToken toks[8];
    int n = vex_semtokens_decode(data, sizeof data / sizeof data[0], toks, 8);
    EXPECT_EQ_INT(n, 3);

    EXPECT_EQ_UINT(toks[0].line, 0u);
    EXPECT_EQ_UINT(toks[0].start_u16, 0u);
    EXPECT_EQ_UINT(toks[0].len_u16, 5u);
    EXPECT_EQ_UINT(toks[0].type, 13u);
    EXPECT_EQ_UINT(toks[0].modifiers, 0u);

    /* Misma linea: la columna es relativa al token anterior (0 + 6). */
    EXPECT_EQ_UINT(toks[1].line, 0u);
    EXPECT_EQ_UINT(toks[1].start_u16, 6u);
    EXPECT_EQ_UINT(toks[1].len_u16, 3u);
    EXPECT_EQ_UINT(toks[1].type, 1u);

    /* deltaLine>0: avanza la linea y la columna pasa a ser absoluta. */
    EXPECT_EQ_UINT(toks[2].line, 2u);
    EXPECT_EQ_UINT(toks[2].start_u16, 4u);
    EXPECT_EQ_UINT(toks[2].len_u16, 2u);
    EXPECT_EQ_UINT(toks[2].type, 11u);
    EXPECT_EQ_UINT(toks[2].modifiers, 4u);
}

static void test_decode_cola_incompleta_se_ignora(void) {
    /* Un quinteto valido + 3 enteros sueltos (cola incompleta). */
    uint32_t data[] = {1, 2, 3, 4, 5, 9, 9, 9};
    VexSemToken toks[4];
    int n = vex_semtokens_decode(data, sizeof data / sizeof data[0], toks, 4);
    EXPECT_EQ_INT(n, 1);
    EXPECT_EQ_UINT(toks[0].line, 1u);
    EXPECT_EQ_UINT(toks[0].start_u16, 2u);
}

static void test_decode_cuenta_aunque_no_quepa(void) {
    /* Devuelve el numero PRODUCIDO aunque exceda max_out (solo escribe max_out). */
    uint32_t data[] = {
        0, 0, 1, 0, 0,
        0, 2, 1, 0, 0,
        0, 2, 1, 0, 0,
    };
    VexSemToken one;
    int n = vex_semtokens_decode(data, sizeof data / sizeof data[0], &one, 1);
    EXPECT_EQ_INT(n, 3);
    EXPECT_EQ_UINT(one.start_u16, 0u); /* el primero si se escribio */
}

static void test_decode_vacio(void) {
    int n = vex_semtokens_decode(NULL, 0, NULL, 0);
    EXPECT_EQ_INT(n, 0);
}

/* ------------------------------------------------------------------------- */
/* Conversion UTF-16 -> codepoints sobre lineas con multibyte.                */
/* ------------------------------------------------------------------------- */

static void test_utf16_ascii_identico(void) {
    const char *line = "hola mundo"; /* 10 ASCII: 1 unidad == 1 codepoint */
    EXPECT_EQ_UINT(vex_utf16_units_to_codepoints(line, strlen(line), 0u), 0u);
    EXPECT_EQ_UINT(vex_utf16_units_to_codepoints(line, strlen(line), 4u), 4u);
    EXPECT_EQ_UINT(vex_utf16_units_to_codepoints(line, strlen(line), 10u), 10u);
}

static void test_utf16_acentos_dentro_bmp(void) {
    /* "ñoño": cada caracter ñ/o ocupa en UTF-16 1 unidad, pero ñ son 2 bytes en
     * UTF-8.  Tras "ñoño" (4 codepoints, 4 unidades UTF-16) viene " x".
     * Texto: ñ o ñ o (space) x  -> 6 codepoints, 6 unidades UTF-16. */
    const char *line = "\xC3\xB1o\xC3\xB1o x"; /* UTF-8 */
    size_t len = strlen(line);
    /* 4 unidades UTF-16 cubren los 4 primeros codepoints (ñoño). */
    EXPECT_EQ_UINT(vex_utf16_units_to_codepoints(line, len, 4u), 4u);
    /* 6 unidades -> 6 codepoints (incluye el espacio y la x). */
    EXPECT_EQ_UINT(vex_utf16_units_to_codepoints(line, len, 6u), 6u);
}

static void test_utf16_emoji_par_subrogado(void) {
    /* U+1F600 (emoji, fuera del BMP): 4 bytes UTF-8, 2 unidades UTF-16, 1
     * codepoint.  Texto: "ab" + emoji + "c".
     *   codepoints: a b <emoji> c       -> 4
     *   utf16:      a(1) b(1) emoji(2) c(1) -> total 5 unidades
     */
    const char *line = "ab\xF0\x9F\x98\x80""c";
    size_t len = strlen(line);
    /* 2 unidades -> 2 codepoints (a, b), antes del emoji. */
    EXPECT_EQ_UINT(vex_utf16_units_to_codepoints(line, len, 2u), 2u);
    /* 4 unidades -> 3 codepoints (a, b, emoji): el emoji consume 2 unidades. */
    EXPECT_EQ_UINT(vex_utf16_units_to_codepoints(line, len, 4u), 3u);
    /* 5 unidades -> 4 codepoints (incluye la c final). */
    EXPECT_EQ_UINT(vex_utf16_units_to_codepoints(line, len, 5u), 4u);
}

/* ------------------------------------------------------------------------- */
/* Combinacion: token UTF-16 -> token en codepoints.                          */
/* ------------------------------------------------------------------------- */

static void test_token_to_cp_ascii(void) {
    VexSemToken t = {0, 6, 3, 1, 0}; /* start 6, len 3 en unidades */
    const char *line = "let abc = 1";
    VexSemTokenCp cp;
    vex_semtoken_to_cp(&t, line, strlen(line), &cp);
    EXPECT_EQ_UINT(cp.line, 0u);
    EXPECT_EQ_UINT(cp.start_cp, 6u);
    EXPECT_EQ_UINT(cp.len_cp, 3u);
    EXPECT_EQ_UINT(cp.type, 1u);
}

static void test_token_to_cp_multibyte(void) {
    /* Linea con emoji al inicio: "<emoji> foo".  En UTF-16 el emoji son 2
     * unidades; un token sobre "foo" empieza en la unidad 3 (emoji=2 + espacio=1)
     * con longitud 3.  En codepoints empieza en 2 (emoji=1 + espacio=1). */
    const char *line = "\xF0\x9F\x98\x80 foo";
    VexSemToken t = {5, 3, 3, 11, 0}; /* start_u16=3, len_u16=3 */
    VexSemTokenCp cp;
    vex_semtoken_to_cp(&t, line, strlen(line), &cp);
    EXPECT_EQ_UINT(cp.line, 5u);
    EXPECT_EQ_UINT(cp.start_cp, 2u); /* tras emoji(1cp) + espacio(1cp) */
    EXPECT_EQ_UINT(cp.len_cp, 3u);   /* "foo" son 3 codepoints */
}

static void test_token_to_cp_sin_linea_asume_ascii(void) {
    /* Sin texto de linea: deja las columnas igual (1 unidad == 1 codepoint). */
    VexSemToken t = {0, 4, 2, 0, 0};
    VexSemTokenCp cp;
    vex_semtoken_to_cp(&t, NULL, 0, &cp);
    EXPECT_EQ_UINT(cp.start_cp, 4u);
    EXPECT_EQ_UINT(cp.len_cp, 2u);
}

int main(void) {
    tt_suite("ext_semtokens");
    tt_run("decode: deltas misma linea + salto de linea -> absolutos",
           test_decode_basico_misma_y_otra_linea);
    tt_run("decode: cola incompleta (no multiplo de 5) se ignora",
           test_decode_cola_incompleta_se_ignora);
    tt_run("decode: cuenta el total aunque no quepa en max_out",
           test_decode_cuenta_aunque_no_quepa);
    tt_run("decode: array vacio devuelve 0", test_decode_vacio);
    tt_run("utf16: ASCII -> 1 unidad == 1 codepoint", test_utf16_ascii_identico);
    tt_run("utf16: acentos (BMP) cuentan 1 unidad por codepoint",
           test_utf16_acentos_dentro_bmp);
    tt_run("utf16: emoji fuera del BMP cuenta 2 unidades (par subrogado)",
           test_utf16_emoji_par_subrogado);
    tt_run("token->cp: ASCII conserva columnas", test_token_to_cp_ascii);
    tt_run("token->cp: multibyte mide en codepoints", test_token_to_cp_multibyte);
    tt_run("token->cp: sin texto de linea asume ASCII",
           test_token_to_cp_sin_linea_asume_ascii);
    return tt_summary();
}
