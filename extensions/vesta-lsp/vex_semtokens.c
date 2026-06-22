/**
 * @file vex_semtokens.c
 * @brief Implementacion pura de la descodificacion de semantic tokens LSP.
 *
 * Ver vex_semtokens.h.  No depende de nada del IDE ni del SDK: solo del CRT.
 */
#include "vex_semtokens.h"

int vex_semtokens_decode(const uint32_t *data, size_t count, VexSemToken *out,
                         int max_out) {
    /* El array es una secuencia de quintetos; ignoramos cualquier cola que no
     * complete un quinteto. */
    size_t quints = count / 5u;

    uint32_t line = 0;  /* linea absoluta acumulada. */
    uint32_t chr = 0;   /* columna absoluta acumulada (unidades UTF-16). */
    int produced = 0;

    for (size_t i = 0; i < quints; ++i) {
        const uint32_t *q = data + i * 5u;
        uint32_t d_line = q[0];
        uint32_t d_char = q[1];
        uint32_t len = q[2];
        uint32_t type = q[3];
        uint32_t mods = q[4];

        if (d_line > 0) {
            /* Nueva linea: avanzar y la columna pasa a ser absoluta. */
            line += d_line;
            chr = d_char;
        } else {
            /* Misma linea: la columna es relativa al token anterior. */
            chr += d_char;
        }

        if (out && produced < max_out) {
            out[produced].line = line;
            out[produced].start_u16 = chr;
            out[produced].len_u16 = len;
            out[produced].type = type;
            out[produced].modifiers = mods;
        }
        produced++;
    }

    return produced;
}

uint32_t vex_utf16_units_to_codepoints(const char *line_utf8, size_t line_len,
                                       uint32_t u16) {
    if (!line_utf8 || line_len == 0 || u16 == 0) return 0;

    size_t byte = 0;     /* posicion en bytes dentro de la linea. */
    uint32_t units = 0;  /* unidades UTF-16 consumidas. */
    uint32_t cps = 0;    /* codepoints consumidos. */

    while (byte < line_len && units < u16) {
        unsigned char b0 = (unsigned char)line_utf8[byte];
        size_t adv;      /* bytes que ocupa este codepoint en UTF-8. */
        uint32_t cp_u16; /* unidades UTF-16 que ocupa este codepoint. */

        if (b0 < 0x80u) {
            adv = 1;
            cp_u16 = 1;
        } else if ((b0 & 0xE0u) == 0xC0u) {
            adv = 2;
            cp_u16 = 1; /* U+0080..U+07FF -> 1 unidad. */
        } else if ((b0 & 0xF0u) == 0xE0u) {
            adv = 3;
            cp_u16 = 1; /* U+0800..U+FFFF -> 1 unidad. */
        } else if ((b0 & 0xF8u) == 0xF0u) {
            adv = 4;
            cp_u16 = 2; /* U+10000.. -> par subrogado (2 unidades). */
        } else {
            /* Byte de continuacion suelto o invalido: avanzar 1 byte como un
             * "caracter" de 1 unidad para no atascarse (tolerancia). */
            adv = 1;
            cp_u16 = 1;
        }

        /* No leer mas alla del fin de la linea: si la secuencia se sale, la
         * tratamos como 1 byte / 1 unidad. */
        if (byte + adv > line_len) {
            adv = 1;
            cp_u16 = 1;
        }

        /* Si este codepoint nos llevaria por encima del objetivo (u16 cae a
         * mitad de un par subrogado), lo contamos entero: el token cubre ese
         * codepoint. */
        units += cp_u16;
        cps += 1;
        byte += adv;
    }

    return cps;
}

void vex_semtoken_to_cp(const VexSemToken *tok, const char *line_utf8,
                        size_t line_len, VexSemTokenCp *out) {
    if (!tok || !out) return;
    out->line = tok->line;
    out->type = tok->type;
    out->modifiers = tok->modifiers;

    if (!line_utf8 || line_len == 0) {
        /* Sin texto de linea: asumir ASCII (1 unidad == 1 codepoint). */
        out->start_cp = tok->start_u16;
        out->len_cp = tok->len_u16;
        return;
    }

    uint32_t start_cp =
        vex_utf16_units_to_codepoints(line_utf8, line_len, tok->start_u16);
    uint32_t end_cp = vex_utf16_units_to_codepoints(
        line_utf8, line_len, tok->start_u16 + tok->len_u16);

    out->start_cp = start_cp;
    out->len_cp = (end_cp >= start_cp) ? (end_cp - start_cp) : 0;
}
