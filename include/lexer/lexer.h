#pragma once
#include <stddef.h>
#include <stdint.h>
#include "structs/vec.h"

/* -- Tipos de token ------------------------------------------------------- */
typedef enum {
    TOK_DEFAULT = 0,
    TOK_KEYWORD,
    TOK_TYPE,
    TOK_COMMENT,
    TOK_STRING,
    TOK_NUMBER,
    TOK_PREPROCESSOR,
    TOK_OPERATOR,
    TOK_PUNCTUATION,
    TOK_COUNT
} TokenType;

/* Colores RGBA para cada tipo de token (ajusta a tu gusto) */
typedef struct { uint8_t r, g, b, a; } Color;

extern const Color TOKEN_COLORS[TOK_COUNT];

/* -- Resultado del lexer por línea ---------------------------------------- */
#define MAX_TOKENS_PER_LINE 512

typedef struct {
    int       col;    /* columna de inicio (0-based) */
    int       len;    /* longitud en caracteres      */
    TokenType type;
} Token;

typedef struct {
    Token  tokens[MAX_TOKENS_PER_LINE];
    int    count;
} LineTokens;

/* -- Cache de resaltado --------------------------------------------------- */
typedef struct {
    Vec lines;   /* Vec<LineTokens>: una entrada por línea de cache         */
    Vec dirty;   /* Vec<int>: dirty[i] = 1 si la línea i debe re-tokenizarse */
} LexerCache;

int  lexer_cache_init   (LexerCache *lc, int line_count);
void lexer_cache_free   (LexerCache *lc);
void lexer_cache_resize (LexerCache *lc, int new_count);
void lexer_cache_dirty  (LexerCache *lc, int from_line);

/* Accesores (inline): el cache es Vec<LineTokens> + Vec<int> */
static inline int lexer_cache_count(const LexerCache *lc) {
    return (int)lc->lines.len;
}
static inline LineTokens *lexer_cache_line(LexerCache *lc, int i) {
    return (LineTokens *)vec_at(&lc->lines, (size_t)i);
}
static inline int *lexer_cache_dirty_at(LexerCache *lc, int i) {
    return (int *)vec_at(&lc->dirty, (size_t)i);
}

/*
 * Tokeniza `text` (longitud `len`, sin '\n') y escribe en `out`.
 * `in_block_comment` indica si la línea empieza dentro de un bloque.
 * Devuelve 1 si la línea termina dentro de un bloque de comentario.
 */
int lexer_tokenize_line(const char *text, int len,
                        LineTokens *out, int in_block_comment);
