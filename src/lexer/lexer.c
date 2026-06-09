/**
 * @file lexer.c
 * @brief Tokenizador para el resaltado de sintaxis de C y cache de tokens por
 *        línea, más los resaltadores enchufables (interfaz Highlighter).
 */
#include "lexer/lexer.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/** Paleta de colores de los tokens (tema oscuro). */
const Color TOKEN_COLORS[TOK_COUNT] = {
    [TOK_DEFAULT] = {0xCD, 0xC7, 0xBA, 0xFF},      /* blanco cálido   */
    [TOK_KEYWORD] = {0xE0, 0x6C, 0x75, 0xFF},      /* rojo/rosa       */
    [TOK_TYPE] = {0xE5, 0xC0, 0x7B, 0xFF},         /* amarillo        */
    [TOK_COMMENT] = {0x5C, 0x63, 0x70, 0xFF},      /* gris            */
    [TOK_STRING] = {0x98, 0xC3, 0x79, 0xFF},       /* verde           */
    [TOK_NUMBER] = {0xD1, 0x9A, 0x66, 0xFF},       /* naranja         */
    [TOK_PREPROCESSOR] = {0xC6, 0x78, 0xDD, 0xFF}, /* morado          */
    [TOK_OPERATOR] = {0x56, 0xB6, 0xC2, 0xFF},     /* cyan            */
    [TOK_PUNCTUATION] = {0xAB, 0xB2, 0xBF, 0xFF},  /* gris claro      */
};

static const char *KEYWORDS[] = {"auto",   "break",  "case",     "const",    "continue", "default",
                                 "do",     "else",   "enum",     "extern",   "for",      "goto",
                                 "if",     "inline", "register", "restrict", "return",   "sizeof",
                                 "static", "struct", "switch",   "typedef",  "union",    "volatile",
                                 "while",  "NULL",   "true",     "false",    NULL};
static const char *TYPES[] = {
    "char",    "double",  "float",   "int",       "long",     "short",     "signed", "unsigned",
    "void",    "bool",    "size_t",  "ptrdiff_t", "intptr_t", "uintptr_t", "int8_t", "int16_t",
    "int32_t", "int64_t", "uint8_t", "uint16_t",  "uint32_t", "uint64_t",  "FILE",   NULL};

/** ¿Está la palabra @p w (de @p len caracteres) en la lista terminada en NULL? */
static int in_word_list(const char *const *list, const char *w, int len) {
    for (int i = 0; list[i]; i++)
        if ((int)strlen(list[i]) == len && strncmp(w, list[i], (size_t)len) == 0) return 1;
    return 0;
}

/** Avanza @p *i hasta pasar el cierre @c *\/. @return 1 si el bloque sigue abierto. */
static int scan_to_comment_end(const char *text, int len, int *i) {
    while (*i < len) {
        if (*i + 1 < len && text[*i] == '*' && text[*i + 1] == '/') {
            *i += 2;
            return 0; /* comentario cerrado */
        }
        (*i)++;
    }
    return 1; /* sigue abierto al final de la línea */
}

/**
 * @brief Tokeniza una línea de C en @p out.
 * @param in_block_comment 1 si la línea empieza dentro de un bloque de comentario.
 * @return 1 si la línea termina aún dentro de un bloque de comentario.
 */
static int lexer_tokenize_line(const char *text, int len, LineTokens *out, int in_block_comment) {
    out->count = 0;
    int i = 0;

#define PUSH(col_, len_, type_)                                                                    \
    do {                                                                                           \
        if (out->count < MAX_TOKENS_PER_LINE) {                                                    \
            out->tokens[out->count].col = (col_);                                                  \
            out->tokens[out->count].len = (len_);                                                  \
            out->tokens[out->count].type = (type_);                                                \
            out->count++;                                                                          \
        }                                                                                          \
    } while (0)

    while (i < len) {
        /* continuación de un bloque de comentario abierto en líneas previas */
        if (in_block_comment) {
            int start = i;
            in_block_comment = scan_to_comment_end(text, len, &i);
            PUSH(start, i - start, TOK_COMMENT);
            continue;
        }

        char c = text[i];

        if (c == '#') { /* preprocesador: hasta el fin de línea */
            PUSH(i, len - i, TOK_PREPROCESSOR);
            i = len;
            continue;
        }

        if (c == '/' && i + 1 < len && text[i + 1] == '/') { /* comentario de línea */
            PUSH(i, len - i, TOK_COMMENT);
            i = len;
            continue;
        }

        if (c == '/' && i + 1 < len && text[i + 1] == '*') { /* inicio de bloque */
            int start = i;
            i += 2;
            in_block_comment = scan_to_comment_end(text, len, &i);
            PUSH(start, i - start, TOK_COMMENT);
            continue;
        }

        if (c == '"' || c == '\'') { /* string o carácter */
            char delim = c;
            int start = i++;
            while (i < len) {
                if (text[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (text[i] == delim) {
                    i++;
                    break;
                }
                i++;
            }
            PUSH(start, i - start, TOK_STRING);
            continue;
        }

        if (isdigit((unsigned char)c) ||
            (c == '.' && i + 1 < len && isdigit((unsigned char)text[i + 1]))) { /* número */
            int start = i;
            if (c == '0' && i + 1 < len && (text[i + 1] == 'x' || text[i + 1] == 'X')) { /* hex */
                i += 2;
                while (i < len && isxdigit((unsigned char)text[i]))
                    i++;
            } else {
                while (i < len &&
                       (isdigit((unsigned char)text[i]) || text[i] == '.' || text[i] == 'e' ||
                        text[i] == 'E' || text[i] == 'f' || text[i] == 'F' || text[i] == 'u' ||
                        text[i] == 'U' || text[i] == 'l' || text[i] == 'L'))
                    i++;
            }
            PUSH(start, i - start, TOK_NUMBER);
            continue;
        }

        if (isalpha((unsigned char)c) || c == '_') { /* identificador / palabra clave / tipo */
            int start = i;
            while (i < len && (isalnum((unsigned char)text[i]) || text[i] == '_'))
                i++;
            int wlen = i - start;
            TokenType t = TOK_DEFAULT;
            if (in_word_list(KEYWORDS, text + start, wlen))
                t = TOK_KEYWORD;
            else if (in_word_list(TYPES, text + start, wlen))
                t = TOK_TYPE;
            PUSH(start, wlen, t);
            continue;
        }

        if (strchr("+-*/%=<>&|^!~?:", c)) { /* operador */
            PUSH(i, 1, TOK_OPERATOR);
            i++;
            continue;
        }

        if (strchr("(){}[];,.", c)) { /* puntuación */
            PUSH(i, 1, TOK_PUNCTUATION);
            i++;
            continue;
        }

        i++; /* resto (espacios, etc.) */
    }

#undef PUSH
    return in_block_comment;
}

/* ── Cache de tokens por línea ────────────────────────────────────────────── */

/** Inicializa la cache con @p line_count líneas, todas marcadas sucias. */
int lexer_cache_init(LexerCache *lc, int line_count) {
    if (line_count < 1) line_count = 1;
    vec_init(&lc->lines, sizeof(LineTokens));
    vec_init(&lc->dirty, sizeof(int));
    /* vec_resize pone a cero las entradas nuevas => LineTokens.count = 0 */
    if (!vec_resize(&lc->lines, (size_t)line_count)) return 0;
    if (!vec_resize(&lc->dirty, (size_t)line_count)) return 0;
    for (int i = 0; i < line_count; i++)
        *(int *)vec_at(&lc->dirty, (size_t)i) = 1;
    return 1;
}

void lexer_cache_free(LexerCache *lc) {
    vec_free(&lc->lines);
    vec_free(&lc->dirty);
}

/** Ajusta la cache a @p new_count líneas (las nuevas quedan sucias). */
void lexer_cache_resize(LexerCache *lc, int new_count) {
    if (new_count < 1) new_count = 1;
    int old = (int)lc->lines.len;
    if (new_count == old) return;

    vec_resize(&lc->lines, (size_t)new_count); /* nuevas LineTokens a cero (count=0) */
    vec_resize(&lc->dirty, (size_t)new_count);
    for (int i = old; i < new_count; i++)
        *(int *)vec_at(&lc->dirty, (size_t)i) = 1;
}

/** Marca como sucias todas las líneas desde @p from_line hasta el final. */
void lexer_cache_dirty(LexerCache *lc, int from_line) {
    int n = (int)lc->dirty.len;
    for (int i = from_line; i < n; i++)
        *(int *)vec_at(&lc->dirty, (size_t)i) = 1;
}

/* ── Interfaz Highlighter (resaltadores enchufables) ──────────────────────── */

/** Resaltador de C: delega en el tokenizador de arriba. */
static int hl_c_tokenize(const Highlighter *self, const char *text, int len, LineTokens *out,
                         int in_block) {
    (void)self;
    return lexer_tokenize_line(text, len, out, in_block);
}
const Highlighter highlighter_c = {"C", hl_c_tokenize};

/** Resaltador nulo: texto plano, sin tokens (render usa el color por defecto). */
static int hl_none_tokenize(const Highlighter *self, const char *text, int len, LineTokens *out,
                            int in_block) {
    (void)self;
    (void)text;
    (void)len;
    (void)in_block;
    out->count = 0;
    return 0;
}
const Highlighter highlighter_none = {"texto", hl_none_tokenize};

/** Comparación de extensión case-insensitive (sin depender de SDL/POSIX). */
static int ext_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

const Highlighter *highlighter_default(void) {
    return &highlighter_c;
}

const Highlighter *highlighter_for_path(const char *path) {
    if (!path || !path[0]) return &highlighter_c;
    const char *dot = strrchr(path, '.');
    if (!dot) return &highlighter_none;

    static const char *c_exts[] = {".c", ".h", ".cpp", ".cc", ".cxx", ".hpp", ".hh", ".hxx", NULL};
    for (int i = 0; c_exts[i]; i++)
        if (ext_eq(dot, c_exts[i])) return &highlighter_c;
    return &highlighter_none;
}
