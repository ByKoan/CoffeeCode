/**
 * @file test_encoding.c
 * @brief Pruebas del módulo de codificaciones (detección + ida y vuelta).
 */
#include "ctests.h"
#include "encoding/encoding.h"
#include <stdlib.h>
#include <string.h>

/** Codifica @p utf8 a @p enc y lo vuelve a decodificar: ¿recupera el original?
 */
static int roundtrip(TextEncoding enc, const char *utf8) {
    unsigned char *bytes = NULL;
    size_t blen = 0;
    if (!encoding_encode(enc, utf8, strlen(utf8), &bytes, &blen)) return 0;
    char *back = NULL;
    size_t backlen = 0;
    int ok = encoding_decode(enc, bytes, blen, &back, &backlen);
    int eq = ok && backlen == strlen(utf8) && memcmp(back, utf8, backlen) == 0;
    free(bytes);
    if (ok) free(back);
    return eq;
}

/* Cadenas de prueba (en UTF-8). */
#define S_ASCII "Hello, world! 123 #include <stdio.h>"
#define S_LATIN "caf\xC3\xA9 na\xC3\xAFve \xC3\xB1" /* café naïve ñ */
#define S_EURO "precio: 100\xE2\x82\xAC"            /* 100€ */
#define S_EMOJI "hi \xF0\x9F\x98\x80 end"           /* U+1F600 */

/** ASCII puro: ida y vuelta correcta en TODAS las codificaciones. */
static void test_roundtrip_ascii(void) {
    for (int e = 0; e < ENC_COUNT; e++)
        EXPECT_TRUE(roundtrip((TextEncoding)e, S_ASCII));
}

/** Texto Latin-1 (é, ï, ñ): ida y vuelta en las que lo representan. */
static void test_roundtrip_latin1(void) {
    EXPECT_TRUE(roundtrip(ENC_UTF8, S_LATIN));
    EXPECT_TRUE(roundtrip(ENC_UTF8_BOM, S_LATIN));
    EXPECT_TRUE(roundtrip(ENC_UTF16_LE, S_LATIN));
    EXPECT_TRUE(roundtrip(ENC_UTF16_BE, S_LATIN));
    EXPECT_TRUE(roundtrip(ENC_UTF32_LE, S_LATIN));
    EXPECT_TRUE(roundtrip(ENC_LATIN1, S_LATIN));
    EXPECT_TRUE(roundtrip(ENC_CP1252, S_LATIN));
}

/** El euro (U+20AC) está en CP1252 (0x80) pero NO en Latin-1 ni ASCII. */
static void test_euro(void) {
    EXPECT_TRUE(roundtrip(ENC_UTF8, S_EURO));
    EXPECT_TRUE(roundtrip(ENC_UTF16_LE, S_EURO));
    EXPECT_TRUE(roundtrip(ENC_UTF32_BE, S_EURO));
    EXPECT_TRUE(roundtrip(ENC_CP1252, S_EURO));
    /* En Latin-1/ASCII se pierde (se vuelve '?'): NO debe coincidir. */
    EXPECT_TRUE(!roundtrip(ENC_LATIN1, S_EURO));
    EXPECT_TRUE(!roundtrip(ENC_ASCII, S_EURO));
}

/** Un emoji fuera del BMP (par suplente en UTF-16) sobrevive en Unicode. */
static void test_emoji(void) {
    EXPECT_TRUE(roundtrip(ENC_UTF8, S_EMOJI));
    EXPECT_TRUE(roundtrip(ENC_UTF16_LE, S_EMOJI));
    EXPECT_TRUE(roundtrip(ENC_UTF16_BE, S_EMOJI));
    EXPECT_TRUE(roundtrip(ENC_UTF32_LE, S_EMOJI));
    EXPECT_TRUE(roundtrip(ENC_UTF32_BE, S_EMOJI));
}

/** CP1252 byte 0x80 decodifica al euro U+20AC (UTF-8 E2 82 AC). */
static void test_cp1252_euro_byte(void) {
    unsigned char in[1] = {0x80};
    char *out = NULL;
    size_t olen = 0;
    EXPECT_TRUE(encoding_decode(ENC_CP1252, in, 1, &out, &olen));
    EXPECT_EQ_INT((int)olen, 3);
    EXPECT_TRUE((unsigned char)out[0] == 0xE2 &&
                (unsigned char)out[1] == 0x82 && (unsigned char)out[2] == 0xAC);
    free(out);
}

/** La detección reconoce los BOMs y la heurística UTF-8 vs ANSI. */
static void test_detect(void) {
    int bom;
    unsigned char u8bom[] = {0xEF, 0xBB, 0xBF, 'h', 'i'};
    EXPECT_TRUE(encoding_detect(u8bom, sizeof u8bom, &bom) == ENC_UTF8_BOM);
    EXPECT_EQ_INT(bom, 1);

    unsigned char u16le[] = {0xFF, 0xFE, 'h', 0};
    EXPECT_TRUE(encoding_detect(u16le, sizeof u16le, &bom) == ENC_UTF16_LE);
    EXPECT_EQ_INT(bom, 1);

    unsigned char u32le[] = {0xFF, 0xFE, 0x00, 0x00};
    EXPECT_TRUE(encoding_detect(u32le, sizeof u32le, &bom) == ENC_UTF32_LE);

    unsigned char ascii[] = {'h', 'e', 'l', 'l', 'o'};
    EXPECT_TRUE(encoding_detect(ascii, sizeof ascii, &bom) == ENC_UTF8);
    EXPECT_EQ_INT(bom, 0);

    /* byte 0xF1 suelto no es UTF-8 válido => se asume ANSI (CP1252) */
    unsigned char ansi[] = {'a', 0xF1, 'o'}; /* "año" en CP1252/Latin-1 */
    EXPECT_TRUE(encoding_detect(ansi, sizeof ansi, &bom) == ENC_CP1252);
}

int main(void) {
    tt_suite("encoding");
    tt_run("roundtrip ASCII en todas", test_roundtrip_ascii);
    tt_run("roundtrip Latin-1", test_roundtrip_latin1);
    tt_run("euro: CP1252 si, Latin-1/ASCII no", test_euro);
    tt_run("emoji fuera del BMP", test_emoji);
    tt_run("CP1252 0x80 = euro", test_cp1252_euro_byte);
    tt_run("deteccion por BOM y heuristica", test_detect);
    return tt_summary();
}
