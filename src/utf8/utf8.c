/**
 * @file utf8.c
 * @brief Decodificación UTF-8 y ancho de display por carácter (ver utf8.h).
 */
#include "utf8/utf8.h"

int utf8_decode(const char *s, int maxlen, uint32_t *cp) {
    if (maxlen <= 0) {
        *cp = 0;
        return 0;
    }
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) { /* ASCII */
        *cp = c;
        return 1;
    }
    int n;
    uint32_t u;
    if ((c & 0xE0) == 0xC0) {
        n = 2;
        u = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3;
        u = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4;
        u = c & 0x07;
    } else { /* byte de continuación suelto o lead inválido */
        *cp = 0xFFFD;
        return 1;
    }
    if (n > maxlen) { /* secuencia truncada */
        *cp = 0xFFFD;
        return 1;
    }
    for (int i = 1; i < n; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) { /* continuación inválida */
            *cp = 0xFFFD;
            return 1;
        }
        u = (u << 6) | ((unsigned char)s[i] & 0x3F);
    }
    *cp = u;
    return n;
}

int utf8_cp_width(uint32_t cp) {
    /* Marcas combinantes (y algunos diacríticos): ancho 0 (van sobre la base).
     */
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE20 && cp <= 0xFE2F))
        return 0;

    /* Doble ancho: CJK, kana, hangul, formas fullwidth, emojis... */
    if ((cp >= 0x1100 && cp <= 0x115F) ||   /* Hangul Jamo                 */
        (cp >= 0x2E80 && cp <= 0x303E) ||   /* radicales CJK, Kangxi       */
        (cp >= 0x3041 && cp <= 0x33FF) ||   /* kana, símbolos CJK          */
        (cp >= 0x3400 && cp <= 0x4DBF) ||   /* CJK Ext. A                  */
        (cp >= 0x4E00 && cp <= 0x9FFF) ||   /* CJK Unificado               */
        (cp >= 0xA000 && cp <= 0xA4CF) ||   /* Yi                          */
        (cp >= 0xAC00 && cp <= 0xD7A3) ||   /* sílabas Hangul              */
        (cp >= 0xF900 && cp <= 0xFAFF) ||   /* CJK Compatibilidad          */
        (cp >= 0xFE30 && cp <= 0xFE4F) ||   /* formas CJK Compatibilidad   */
        (cp >= 0xFF00 && cp <= 0xFF60) ||   /* formas Fullwidth            */
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||   /* signos Fullwidth            */
        (cp >= 0x1F000 && cp <= 0x1FAFF) || /* emojis y símbolos         */
        (cp >= 0x20000 && cp <= 0x3FFFD))   /* planos suplementarios CJK */
        return 2;

    return 1;
}
