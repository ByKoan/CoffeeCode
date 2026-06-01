#include "lexer.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ── Paleta de colores (tema oscuro) ────────────────────────────────────── */
const Color TOKEN_COLORS[TOK_COUNT] = {
    [TOK_DEFAULT]     = {0xCD, 0xC7, 0xBA, 0xFF},  /* blanco cálido   */
    [TOK_KEYWORD]     = {0xE0, 0x6C, 0x75, 0xFF},  /* rojo/rosa       */
    [TOK_TYPE]        = {0xE5, 0xC0, 0x7B, 0xFF},  /* amarillo        */
    [TOK_COMMENT]     = {0x5C, 0x63, 0x70, 0xFF},  /* gris            */
    [TOK_STRING]      = {0x98, 0xC3, 0x79, 0xFF},  /* verde           */
    [TOK_NUMBER]      = {0xD1, 0x9A, 0x66, 0xFF},  /* naranja         */
    [TOK_PREPROCESSOR]= {0xC6, 0x78, 0xDD, 0xFF},  /* morado          */
    [TOK_OPERATOR]    = {0x56, 0xB6, 0xC2, 0xFF},  /* cyan            */
    [TOK_PUNCTUATION] = {0xAB, 0xB2, 0xBF, 0xFF},  /* gris claro      */
};

/* ── Palabras clave de C ────────────────────────────────────────────────── */
static const char *KEYWORDS[] = {
    "auto","break","case","const","continue","default","do","else",
    "enum","extern","for","goto","if","inline","register","restrict",
    "return","sizeof","static","struct","switch","typedef","union",
    "volatile","while","NULL","true","false",NULL
};
static const char *TYPES[] = {
    "char","double","float","int","long","short","signed","unsigned",
    "void","bool","size_t","ptrdiff_t","intptr_t","uintptr_t",
    "int8_t","int16_t","int32_t","int64_t",
    "uint8_t","uint16_t","uint32_t","uint64_t","FILE",NULL
};

static int is_keyword(const char *w, int len) {
    for (int i = 0; KEYWORDS[i]; i++)
        if ((int)strlen(KEYWORDS[i]) == len &&
            strncmp(w, KEYWORDS[i], (size_t)len) == 0) return 1;
    return 0;
}
static int is_type(const char *w, int len) {
    for (int i = 0; TYPES[i]; i++)
        if ((int)strlen(TYPES[i]) == len &&
            strncmp(w, TYPES[i], (size_t)len) == 0) return 1;
    return 0;
}

/* ── Tokenizador por línea ──────────────────────────────────────────────── */
int lexer_tokenize_line(const char *text, int len,
                        LineTokens *out, int in_block_comment)
{
    out->count = 0;
    int i = 0;

#define PUSH(col_, len_, type_) do { \
    if (out->count < MAX_TOKENS_PER_LINE) { \
        out->tokens[out->count].col  = (col_); \
        out->tokens[out->count].len  = (len_); \
        out->tokens[out->count].type = (type_); \
        out->count++; \
    } \
} while(0)

    while (i < len) {
        /* ── dentro de bloque de comentario ── */
        if (in_block_comment) {
            int start = i;
            while (i < len) {
                if (i + 1 < len && text[i] == '*' && text[i+1] == '/') {
                    i += 2;
                    in_block_comment = 0;
                    break;
                }
                i++;
            }
            PUSH(start, i - start, TOK_COMMENT);
            continue;
        }

        char c = text[i];

        /* ── preprocesador ── */
        if (c == '#') {
            PUSH(i, len - i, TOK_PREPROCESSOR);
            i = len;
            continue;
        }

        /* ── comentario de línea ── */
        if (c == '/' && i + 1 < len && text[i+1] == '/') {
            PUSH(i, len - i, TOK_COMMENT);
            i = len;
            continue;
        }

        /* ── inicio de bloque de comentario ── */
        if (c == '/' && i + 1 < len && text[i+1] == '*') {
            int start = i;
            i += 2;
            in_block_comment = 1;
            while (i < len) {
                if (i + 1 < len && text[i] == '*' && text[i+1] == '/') {
                    i += 2;
                    in_block_comment = 0;
                    break;
                }
                i++;
            }
            PUSH(start, i - start, TOK_COMMENT);
            continue;
        }

        /* ── string ── */
        if (c == '"' || c == '\'') {
            char delim = c;
            int start = i++;
            while (i < len) {
                if (text[i] == '\\') { i += 2; continue; }
                if (text[i] == delim) { i++; break; }
                i++;
            }
            PUSH(start, i - start, TOK_STRING);
            continue;
        }

        /* ── número ── */
        if (isdigit((unsigned char)c) ||
            (c == '.' && i+1 < len && isdigit((unsigned char)text[i+1])))
        {
            int start = i;
            /* hex */
            if (c == '0' && i+1 < len && (text[i+1]=='x'||text[i+1]=='X')) {
                i += 2;
                while (i < len && isxdigit((unsigned char)text[i])) i++;
            } else {
                while (i < len && (isdigit((unsigned char)text[i]) ||
                       text[i]=='.'||text[i]=='e'||text[i]=='E'||
                       text[i]=='f'||text[i]=='F'||text[i]=='u'||
                       text[i]=='U'||text[i]=='l'||text[i]=='L')) i++;
            }
            PUSH(start, i - start, TOK_NUMBER);
            continue;
        }

        /* ── identificador / palabra clave ── */
        if (isalpha((unsigned char)c) || c == '_') {
            int start = i;
            while (i < len && (isalnum((unsigned char)text[i]) ||
                               text[i] == '_')) i++;
            int wlen = i - start;
            TokenType t = TOK_DEFAULT;
            if      (is_keyword(text + start, wlen)) t = TOK_KEYWORD;
            else if (is_type   (text + start, wlen)) t = TOK_TYPE;
            PUSH(start, wlen, t);
            continue;
        }

        /* ── operadores ── */
        if (strchr("+-*/%=<>&|^!~?:", c)) {
            PUSH(i, 1, TOK_OPERATOR);
            i++;
            continue;
        }

        /* ── puntuación ── */
        if (strchr("(){}[];,.", c)) {
            PUSH(i, 1, TOK_PUNCTUATION);
            i++;
            continue;
        }

        /* ── resto (espacio, etc.) ── */
        i++;
    }

#undef PUSH
    return in_block_comment;
}

/* ── Cache ──────────────────────────────────────────────────────────────── */
int lexer_cache_init(LexerCache *lc, int line_count) {
    lc->count = line_count;
    lc->lines = calloc((size_t)line_count, sizeof(LineTokens));
    lc->dirty = malloc((size_t)line_count * sizeof(int));
    if (!lc->lines || !lc->dirty) return 0;
    for (int i = 0; i < line_count; i++) lc->dirty[i] = 1;
    return 1;
}

void lexer_cache_free(LexerCache *lc) {
    free(lc->lines); lc->lines = NULL;
    free(lc->dirty); lc->dirty = NULL;
    lc->count = 0;
}

void lexer_cache_resize(LexerCache *lc, int new_count) {
    if (new_count == lc->count) return;
    lc->lines = realloc(lc->lines, (size_t)new_count * sizeof(LineTokens));
    lc->dirty = realloc(lc->dirty, (size_t)new_count * sizeof(int));
    for (int i = lc->count; i < new_count; i++) {
        lc->lines[i].count = 0;
        lc->dirty[i] = 1;
    }
    lc->count = new_count;
}

void lexer_cache_dirty(LexerCache *lc, int from_line) {
    for (int i = from_line; i < lc->count; i++) lc->dirty[i] = 1;
}
