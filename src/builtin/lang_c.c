/**
 * @file lang_c.c
 * @brief Resaltador de C como extension nativa embebida (ver lang_c.h).
 *
 * Porta el tokenizador de C que antes vivia en el core (lexer.c) a la API de
 * resaltado de extensiones (::CoffeeHighlightFn).  La maquina de estados es la
 * misma de siempre (misma prioridad de reconocimiento y mismas listas de
 * palabras), de modo que .c/.cpp/.h se colorean EXACTAMENTE igual que antes; la
 * unica diferencia es que ahora:
 *
 *   - emite ::CoffeeSpan en COLUMNAS DE CARACTER (codepoints), no en bytes;
 *   - el color de cada tramo lo toma del TEMA activo (el userdata es un
 *     @c const Theme*), no de una paleta fija del core.
 *
 * Asi el coloreado de C deja de estar hardcodeado en el render y pasa a ser una
 * extension que usa el mismo contrato que cualquier DLL externa, pero compilada
 * dentro del ejecutable y registrada en proceso al arrancar.
 */
#include "builtin/lang_c.h"

#include "lexer/lexer.h" /* LexTokenType + Theme.tokens indexado por el */
#include "utf8/utf8.h"   /* utf8_decode (bytes -> codepoints) */

#include <ctype.h>
#include <string.h>

/* -- Listas de palabras de C (identicas a las del antiguo lexer del core) --- */

/** Palabras reservadas de C que se pintan como ::TOK_KEYWORD. */
static const char *C_KEYWORDS[] = {
    "auto",   "break",  "case",     "const",    "continue", "default",
    "do",     "else",   "enum",     "extern",   "for",      "goto",
    "if",     "inline", "register", "restrict", "return",   "sizeof",
    "static", "struct", "switch",   "typedef",  "union",    "volatile",
    "while",  "NULL",   "true",     "false",    NULL};

/** Nombres de tipo de C que se pintan como ::TOK_TYPE. */
static const char *C_TYPES[] = {
    "char",     "double",    "float",    "int",      "long",    "short",
    "signed",   "unsigned",  "void",     "bool",     "size_t",  "ptrdiff_t",
    "intptr_t", "uintptr_t", "int8_t",   "int16_t",  "int32_t", "int64_t",
    "uint8_t",  "uint16_t",  "uint32_t", "uint64_t", "FILE",    NULL};

/* -- Reglas del tokenizador (copia 1:1 de la logica original) ---------------- */

/** Esta la palabra @p w (de @p len bytes, sin NUL) en la lista @p list? */
static int c_in_word_list(const char *const *list, const char *w, int len) {
    for (int i = 0; list[i]; i++)
        if ((int)strlen(list[i]) == len &&
            strncmp(w, list[i], (size_t)len) == 0)
            return 1;
    return 0;
}

/**
 * @brief Avanza @p *i hasta pasar el cierre del comentario de bloque.
 * @return 1 si el bloque sigue abierto al acabar la linea; 0 si se cerro.
 */
static int c_scan_to_comment_end(const char *text, int len, int *i) {
    while (*i < len) {
        if (*i + 1 < len && text[*i] == '*' && text[*i + 1] == '/') {
            *i += 2; /* consumir los dos caracteres del cierre */
            return 0;
        }
        (*i)++;
    }
    return 1; /* sigue abierto al final de la linea */
}

/* -- Conversion de offset de byte a columna de caracter (codepoints) -------- */

/**
 * @brief Numero de codepoints en @p text[0, byte_off).
 *
 * El tokenizador trabaja en bytes (como el original); para emitir ::CoffeeSpan en
 * columnas de caracter convertimos los offsets de byte a codepoints aqui.  Para
 * texto ASCII (lo normal en C) bytes y columnas coinciden y esto es un simple
 * conteo; para identificadores/strings con UTF-8 multibyte cuenta codepoints.
 */
static uint32_t c_cols_upto(const char *text, int len, int byte_off) {
    if (byte_off > len) byte_off = len;
    uint32_t cols = 0;
    int b = 0;
    while (b < byte_off) {
        uint32_t cp;
        int n = utf8_decode(text + b, len - b, &cp);
        if (n <= 0) n = 1; /* defensa ante bytes invalidos */
        b += n;
        cols++;
    }
    return cols;
}

/* -- La funcion registrada (::CoffeeHighlightFn) ----------------------------- */

int coffee_builtin_c_highlight(void *ud, const char *line_utf8, int line_len,
                               int in_block_comment, CoffeeSpan *out,
                               int max_out, int *out_block) {
    const Theme *theme = (const Theme *)ud;
    if (out_block) *out_block = 0;
    if (!theme || !line_utf8) return 0;

    int len = line_len;
    int i = 0;        /* indice de lectura en BYTES */
    int n_out = 0;    /* tramos emitidos */
    int in_block = in_block_comment;

    /* Emite un tramo [byte_col, byte_col+byte_len) con el color del tipo @p t,
     * convirtiendo el rango de bytes a columnas de caracter.  No desborda @p out. */
#define EMIT(byte_col_, byte_len_, type_)                                      \
    do {                                                                       \
        if (out && n_out < max_out) {                                         \
            Color col_ = theme->tokens[(type_)];                              \
            uint32_t c0 = c_cols_upto(line_utf8, len, (byte_col_));           \
            uint32_t c1 =                                                     \
                c_cols_upto(line_utf8, len, (byte_col_) + (byte_len_));       \
            out[n_out].start_col = c0;                                        \
            out[n_out].len = (c1 >= c0) ? (c1 - c0) : 0;                      \
            out[n_out].color.r = col_.r;                                      \
            out[n_out].color.g = col_.g;                                      \
            out[n_out].color.b = col_.b;                                      \
            out[n_out].color.a = col_.a;                                      \
            n_out++;                                                          \
        }                                                                      \
    } while (0)

    while (i < len) {
        /* continuacion de un bloque de comentario abierto en lineas previas */
        if (in_block) {
            int start = i;
            in_block = c_scan_to_comment_end(line_utf8, len, &i);
            EMIT(start, i - start, TOK_COMMENT);
            continue;
        }

        char c = line_utf8[i];

        if (c == '#') { /* preprocesador: hasta el fin de linea */
            EMIT(i, len - i, TOK_PREPROCESSOR);
            i = len;
            continue;
        }

        if (c == '/' && i + 1 < len && line_utf8[i + 1] == '/') { /* // ... */
            EMIT(i, len - i, TOK_COMMENT);
            i = len;
            continue;
        }

        if (c == '/' && i + 1 < len && line_utf8[i + 1] == '*') { /* /\* ... */
            int start = i;
            i += 2;
            in_block = c_scan_to_comment_end(line_utf8, len, &i);
            EMIT(start, i - start, TOK_COMMENT);
            continue;
        }

        if (c == '"' || c == '\'') { /* string o caracter */
            char delim = c;
            int start = i++;
            while (i < len) {
                if (line_utf8[i] == '\\') { /* escape: protege al siguiente */
                    i += 2;
                    continue;
                }
                if (line_utf8[i] == delim) {
                    i++; /* incluir la comilla de cierre */
                    break;
                }
                i++;
            }
            EMIT(start, i - start, TOK_STRING);
            continue;
        }

        if (isdigit((unsigned char)c) ||
            (c == '.' && i + 1 < len &&
             isdigit((unsigned char)line_utf8[i + 1]))) { /* numero */
            int start = i;
            if (c == '0' && i + 1 < len &&
                (line_utf8[i + 1] == 'x' || line_utf8[i + 1] == 'X')) { /* hex */
                i += 2;
                while (i < len && isxdigit((unsigned char)line_utf8[i])) i++;
            } else {
                while (i < len &&
                       (isdigit((unsigned char)line_utf8[i]) ||
                        line_utf8[i] == '.' || line_utf8[i] == 'e' ||
                        line_utf8[i] == 'E' || line_utf8[i] == 'f' ||
                        line_utf8[i] == 'F' || line_utf8[i] == 'u' ||
                        line_utf8[i] == 'U' || line_utf8[i] == 'l' ||
                        line_utf8[i] == 'L'))
                    i++;
            }
            EMIT(start, i - start, TOK_NUMBER);
            continue;
        }

        if (isalpha((unsigned char)c) || c == '_') { /* identificador/kw/tipo */
            int start = i;
            while (i < len &&
                   (isalnum((unsigned char)line_utf8[i]) || line_utf8[i] == '_'))
                i++;
            int wlen = i - start;
            LexTokenType t = TOK_DEFAULT;
            if (c_in_word_list(C_KEYWORDS, line_utf8 + start, wlen))
                t = TOK_KEYWORD;
            else if (c_in_word_list(C_TYPES, line_utf8 + start, wlen))
                t = TOK_TYPE;
            EMIT(start, wlen, t);
            continue;
        }

        if (strchr("+-*/%=<>&|^!~?:", c)) { /* operador */
            EMIT(i, 1, TOK_OPERATOR);
            i++;
            continue;
        }

        if (strchr("(){}[];,.", c)) { /* puntuacion */
            EMIT(i, 1, TOK_PUNCTUATION);
            i++;
            continue;
        }

        i++; /* resto (espacios, bytes UTF-8 de continuacion): sin emitir */
    }

#undef EMIT
    if (out_block) *out_block = in_block;
    return n_out;
}

void coffee_builtin_c_register(CoffeeHost *host, const CoffeeApi *api,
                               const Theme *theme) {
    if (!host || !api || !theme) return;
    if (!api->register_highlighter) return; /* host sin ABI de resaltado */
    static const char *C_EXTS[] = {".c",   ".h",  ".cpp", ".cc",
                                   ".cxx", ".hpp", ".hh", ".hxx"};
    api->register_highlighter(host, C_EXTS,
                              (int)(sizeof(C_EXTS) / sizeof(C_EXTS[0])),
                              coffee_builtin_c_highlight, (void *)theme);
}
