/**
 * @file encoding.c
 * @brief Implementación de la conversión entre codificaciones y UTF-8.
 *
 * Pipeline común: decodificar = bytes(enc) -> puntos de código -> UTF-8;
 * codificar = UTF-8 -> puntos de código -> bytes(enc). Los puntos de código son
 * enteros Unicode (0..0x10FFFF).
 */
#include "encoding/encoding.h"

#include <stdlib.h>
#include <string.h>

#define REPLACEMENT 0xFFFD /* U+FFFD: carácter de reemplazo */

const char *encoding_name(TextEncoding enc) {
    switch (enc) {
    case ENC_UTF8: return "UTF-8";
    case ENC_UTF8_BOM: return "UTF-8 BOM";
    case ENC_UTF16_LE: return "UTF-16 LE";
    case ENC_UTF16_BE: return "UTF-16 BE";
    case ENC_UTF32_LE: return "UTF-32 LE";
    case ENC_UTF32_BE: return "UTF-32 BE";
    case ENC_CP1252: return "ANSI";
    case ENC_LATIN1: return "Latin-1";
    case ENC_ASCII: return "ASCII";
    default: return "?";
    }
}

/* -- Windows-1252: bytes 0x80..0x9F -> Unicode (los huecos = el propio byte) -
 */
static const unsigned int CP1252_HI[32] = {
    0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
    0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};

/* -- Buffer de bytes dinámico --------------------------------------------- */
typedef struct {
    unsigned char *d;
    size_t len, cap;
} Buf;

static int buf_grow(Buf *b, size_t extra) {
    if (b->len + extra <= b->cap) return 1;
    size_t nc = b->cap ? b->cap : 256;
    while (nc < b->len + extra)
        nc *= 2;
    unsigned char *nd = realloc(b->d, nc);
    if (!nd) return 0;
    b->d = nd;
    b->cap = nc;
    return 1;
}

static int buf_put(Buf *b, const unsigned char *p, size_t n) {
    if (!buf_grow(b, n)) return 0;
    memcpy(b->d + b->len, p, n);
    b->len += n;
    return 1;
}

static int buf_byte(Buf *b, unsigned char c) {
    if (!buf_grow(b, 1)) return 0;
    b->d[b->len++] = c;
    return 1;
}

/* -- UTF-8 (un punto de código) ------------------------------------------- */

/** Escribe el punto de código @p cp como UTF-8 en @p o; devuelve nº de bytes.
 */
static int utf8_put(unsigned int cp, unsigned char *o) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = REPLACEMENT;
    if (cp < 0x80) {
        o[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        o[0] = (unsigned char)(0xC0 | (cp >> 6));
        o[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        o[0] = (unsigned char)(0xE0 | (cp >> 12));
        o[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (unsigned char)(0xF0 | (cp >> 18));
    o[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    o[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

/** Lee un punto de código UTF-8 de @p s; devuelve bytes consumidos (>=1). */
static int utf8_get(const unsigned char *s, size_t len, unsigned int *cp) {
    unsigned char c = s[0];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    int n;
    unsigned int u;
    if ((c & 0xE0) == 0xC0) {
        n = 2;
        u = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3;
        u = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4;
        u = c & 0x07;
    } else {
        *cp = REPLACEMENT;
        return 1; /* byte de continuación o inválido */
    }
    if ((size_t)n > len) {
        *cp = REPLACEMENT;
        return 1;
    }
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *cp = REPLACEMENT;
            return 1;
        }
        u = (u << 6) | (s[i] & 0x3F);
    }
    *cp = u;
    return n;
}

/** ¿Es @p data UTF-8 válido (para la heurística de detección)? */
static int is_valid_utf8(const unsigned char *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        unsigned char c = data[i];
        if (c < 0x80) {
            i++;
            continue;
        }
        int n;
        if ((c & 0xE0) == 0xC0)
            n = 2;
        else if ((c & 0xF0) == 0xE0)
            n = 3;
        else if ((c & 0xF8) == 0xF0)
            n = 4;
        else
            return 0;
        if (i + (size_t)n > len) return 0;
        for (int k = 1; k < n; k++)
            if ((data[i + k] & 0xC0) != 0x80) return 0;
        i += n;
    }
    return 1;
}

/* -- Detección ------------------------------------------------------------- */
TextEncoding encoding_detect(const unsigned char *data, size_t len,
                             int *has_bom) {
    if (has_bom) *has_bom = 0;
    /* BOMs (de más largo a más corto para no confundir UTF-32 con UTF-16). */
    if (len >= 4 && data[0] == 0xFF && data[1] == 0xFE && data[2] == 0x00 &&
        data[3] == 0x00) {
        if (has_bom) *has_bom = 1;
        return ENC_UTF32_LE;
    }
    if (len >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0xFE &&
        data[3] == 0xFF) {
        if (has_bom) *has_bom = 1;
        return ENC_UTF32_BE;
    }
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        if (has_bom) *has_bom = 1;
        return ENC_UTF16_LE;
    }
    if (len >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        if (has_bom) *has_bom = 1;
        return ENC_UTF16_BE;
    }
    if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        if (has_bom) *has_bom = 1;
        return ENC_UTF8_BOM;
    }
    /* Sin BOM: si es UTF-8 válido, UTF-8; si no, ANSI (Windows-1252). */
    if (is_valid_utf8(data, len)) return ENC_UTF8;
    return ENC_CP1252;
}

/* -- Decodificación (enc -> UTF-8) ---------------------------------------- */
int encoding_decode(TextEncoding enc, const unsigned char *in, size_t in_len,
                    char **out, size_t *out_len) {
    Buf b = {0};
    unsigned char tmp[4];

    switch (enc) {
    case ENC_ASCII:
        for (size_t i = 0; i < in_len; i++) {
            unsigned int cp = (in[i] < 0x80) ? in[i] : REPLACEMENT;
            if (!buf_put(&b, tmp, (size_t)utf8_put(cp, tmp))) goto fail;
        }
        break;
    case ENC_LATIN1:
        for (size_t i = 0; i < in_len; i++)
            if (!buf_put(&b, tmp, (size_t)utf8_put(in[i], tmp))) goto fail;
        break;
    case ENC_CP1252:
        for (size_t i = 0; i < in_len; i++) {
            unsigned int cp = (in[i] >= 0x80 && in[i] <= 0x9F)
                                  ? CP1252_HI[in[i] - 0x80]
                                  : in[i];
            if (!buf_put(&b, tmp, (size_t)utf8_put(cp, tmp))) goto fail;
        }
        break;
    case ENC_UTF8:
    case ENC_UTF8_BOM: {
        size_t i = (enc == ENC_UTF8_BOM && in_len >= 3) ? 3 : 0;
        if (i == 0 && in_len >= 3 && in[0] == 0xEF && in[1] == 0xBB &&
            in[2] == 0xBF)
            i = 3; /* saltar BOM aunque enc sea UTF-8 a secas */
        if (i < in_len && !buf_put(&b, in + i, in_len - i)) goto fail;
        break;
    }
    case ENC_UTF16_LE:
    case ENC_UTF16_BE: {
        int le = (enc == ENC_UTF16_LE);
        size_t i = 0;
        if (in_len >= 2 && ((le && in[0] == 0xFF && in[1] == 0xFE) ||
                            (!le && in[0] == 0xFE && in[1] == 0xFF)))
            i = 2; /* saltar BOM */
        for (; i + 1 < in_len; i += 2) {
            unsigned int u =
                le ? (in[i] | (in[i + 1] << 8)) : ((in[i] << 8) | in[i + 1]);
            if (u >= 0xD800 && u <= 0xDBFF && i + 3 < in_len) {
                unsigned int lo = le ? (in[i + 2] | (in[i + 3] << 8))
                                     : ((in[i + 2] << 8) | in[i + 3]);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
            }
            if (!buf_put(&b, tmp, (size_t)utf8_put(u, tmp))) goto fail;
        }
        break;
    }
    case ENC_UTF32_LE:
    case ENC_UTF32_BE: {
        int le = (enc == ENC_UTF32_LE);
        size_t i = 0;
        if (in_len >= 4 && ((le && in[0] == 0xFF && in[1] == 0xFE &&
                             in[2] == 0 && in[3] == 0) ||
                            (!le && in[0] == 0 && in[1] == 0 && in[2] == 0xFE &&
                             in[3] == 0xFF)))
            i = 4; /* saltar BOM */
        for (; i + 3 < in_len; i += 4) {
            unsigned int u =
                le ? ((unsigned)in[i] | ((unsigned)in[i + 1] << 8) |
                      ((unsigned)in[i + 2] << 16) | ((unsigned)in[i + 3] << 24))
                   : (((unsigned)in[i] << 24) | ((unsigned)in[i + 1] << 16) |
                      ((unsigned)in[i + 2] << 8) | (unsigned)in[i + 3]);
            if (!buf_put(&b, tmp, (size_t)utf8_put(u, tmp))) goto fail;
        }
        break;
    }
    default: break;
    }

    *out = (char *)b.d;
    *out_len = b.len;
    return 1;
fail:
    free(b.d);
    return 0;
}

/* -- Codificación (UTF-8 -> enc) ------------------------------------------ */

/** Mapea un punto de código a un byte Windows-1252, o '?' si no representable.
 */
static unsigned char cp_to_cp1252(unsigned int cp) {
    if (cp < 0x80) return (unsigned char)cp;
    if (cp >= 0xA0 && cp <= 0xFF) return (unsigned char)cp;
    for (int i = 0; i < 32; i++)
        if (CP1252_HI[i] == cp) return (unsigned char)(0x80 + i);
    return '?';
}

int encoding_encode(TextEncoding enc, const char *in_, size_t in_len,
                    unsigned char **out, size_t *out_len) {
    const unsigned char *in = (const unsigned char *)in_;
    Buf b = {0};

    /* BOM inicial según la codificación. */
    int ok = 1;
    switch (enc) {
    case ENC_UTF8_BOM:
        ok = buf_put(&b, (const unsigned char[]){0xEF, 0xBB, 0xBF}, 3);
        break;
    case ENC_UTF16_LE:
        ok = buf_put(&b, (const unsigned char[]){0xFF, 0xFE}, 2);
        break;
    case ENC_UTF16_BE:
        ok = buf_put(&b, (const unsigned char[]){0xFE, 0xFF}, 2);
        break;
    case ENC_UTF32_LE:
        ok = buf_put(&b, (const unsigned char[]){0xFF, 0xFE, 0, 0}, 4);
        break;
    case ENC_UTF32_BE:
        ok = buf_put(&b, (const unsigned char[]){0, 0, 0xFE, 0xFF}, 4);
        break;
    default: break;
    }
    if (!ok) goto fail;

    /* UTF-8 (con o sin BOM): los bytes ya están en UTF-8, se copian tal cual.
     */
    if (enc == ENC_UTF8 || enc == ENC_UTF8_BOM) {
        if (in_len && !buf_put(&b, in, in_len)) goto fail;
        *out = b.d;
        *out_len = b.len;
        return 1;
    }

    size_t i = 0;
    while (i < in_len) {
        unsigned int cp;
        i += (size_t)utf8_get(in + i, in_len - i, &cp);

        switch (enc) {
        case ENC_ASCII:
            if (!buf_byte(&b, cp < 0x80 ? (unsigned char)cp : '?')) goto fail;
            break;
        case ENC_LATIN1:
            if (!buf_byte(&b, cp < 0x100 ? (unsigned char)cp : '?')) goto fail;
            break;
        case ENC_CP1252:
            if (!buf_byte(&b, cp_to_cp1252(cp))) goto fail;
            break;
        case ENC_UTF16_LE:
        case ENC_UTF16_BE: {
            int le = (enc == ENC_UTF16_LE);
            unsigned int units[2];
            int nu;
            if (cp >= 0x10000) {
                cp -= 0x10000;
                units[0] = 0xD800 + (cp >> 10);
                units[1] = 0xDC00 + (cp & 0x3FF);
                nu = 2;
            } else {
                units[0] = cp;
                nu = 1;
            }
            for (int k = 0; k < nu; k++) {
                unsigned int u = units[k];
                unsigned char by[2] = {
                    le ? (unsigned char)(u & 0xFF) : (unsigned char)(u >> 8),
                    le ? (unsigned char)(u >> 8) : (unsigned char)(u & 0xFF)};
                if (!buf_put(&b, by, 2)) goto fail;
            }
            break;
        }
        case ENC_UTF32_LE:
        case ENC_UTF32_BE: {
            int le = (enc == ENC_UTF32_LE);
            unsigned char by[4];
            if (le) {
                by[0] = cp & 0xFF;
                by[1] = (cp >> 8) & 0xFF;
                by[2] = (cp >> 16) & 0xFF;
                by[3] = (cp >> 24) & 0xFF;
            } else {
                by[0] = (cp >> 24) & 0xFF;
                by[1] = (cp >> 16) & 0xFF;
                by[2] = (cp >> 8) & 0xFF;
                by[3] = cp & 0xFF;
            }
            if (!buf_put(&b, by, 4)) goto fail;
            break;
        }
        default: break;
        }
    }

    *out = b.d;
    *out_len = b.len;
    return 1;
fail:
    free(b.d);
    return 0;
}
