/**
 * @file lexer.h
 * @brief Interfaz pública del tokenizador de sintaxis: tipos de token, paleta
 * de colores, cache de resaltado por línea y la interfaz Highlighter.
 *
 * El resaltado funciona por línea y se cachea: el tokenizador parte cada línea
 * en
 * ::Token, la ::LexerCache guarda esos tokens y marca como "sucias" las líneas
 * que cambiaron, y la interfaz ::Highlighter permite enchufar distintos
 * lenguajes detrás de la misma API. Ver @ref lexer.c para los detalles del
 * algoritmo.
 */
#pragma once
#include "structs/vec.h"
#include <stddef.h>
#include <stdint.h>

/* -- Tipos de token ------------------------------------------------------- */
/**
 * @brief Categoría léxica de un fragmento de texto, usada para colorearlo.
 *
 * Cada valor indexa ::TOKEN_COLORS. ::TOK_DEFAULT es el "sin categoría"
 * (identificador normal, espacios) y ::TOK_COUNT es el centinela con el número
 * total de tipos (sirve para dimensionar arrays, no es un tipo real).
 */
typedef enum {
    TOK_DEFAULT = 0,  /* texto sin categoría / identificador normal */
    TOK_KEYWORD,      /* palabra reservada (if, while, return...)   */
    TOK_TYPE,         /* nombre de tipo (int, char, size_t...)      */
    TOK_COMMENT,      /* comentario de línea o de bloque            */
    TOK_STRING,       /* literal de cadena o de carácter            */
    TOK_NUMBER,       /* literal numérico (hex, decimal, flotante)  */
    TOK_PREPROCESSOR, /* directiva de preprocesador (#include...)   */
    TOK_OPERATOR,     /* operador (+ - * / = < >...)                */
    TOK_PUNCTUATION,  /* puntuación (paréntesis, llaves, ; , ...)   */
    TOK_COUNT         /* nº de tipos (centinela para dimensionar)   */
} LexTokenType;

/** @brief Color RGBA de 8 bits por canal (ajusta el tema a tu gusto). */
typedef struct {
    uint8_t r, g, b, a;
} Color;

/** @brief Tabla de colores indexada por ::LexTokenType (definida en lexer.c). */
extern const Color TOKEN_COLORS[TOK_COUNT];

/* -- Resultado del lexer por línea ---------------------------------------- */
/** @brief Tope de tokens almacenados por línea (los que sobran se descartan).
 */
#define MAX_TOKENS_PER_LINE 512

/**
 * @brief Un token: un tramo de la línea con su categoría léxica.
 *
 * Se describe por posición (no por copia del texto): el token cubre las
 * columnas
 * @c [col, col+len) de su línea. El render usa @c col/@c len para localizar el
 * texto y @c type para elegir el color.
 */
typedef struct {
    int col; /* columna de inicio (0-based) */
    int len; /* longitud en caracteres      */
    LexTokenType type;
} Token;

/** @brief Conjunto de tokens resultante de tokenizar una línea. */
typedef struct {
    Token tokens[MAX_TOKENS_PER_LINE]; /* tokens de la línea (array fijo)   */
    int count;                         /* nº de tokens válidos en el array  */
} LineTokens;

/* -- Cache de resaltado --------------------------------------------------- */
/**
 * @brief Cache de tokens por línea con marcado de líneas sucias.
 *
 * Dos vectores paralelos indexados por número de línea: @c lines guarda los
 * tokens ya calculados y @c dirty indica qué líneas hay que re-tokenizar. Evita
 * re-analizar todo el archivo en cada frame; ver @ref lexer.c.
 */
typedef struct {
    Vec lines; /* Vec<LineTokens>: una entrada por línea de cache         */
    Vec dirty; /* Vec<int>: dirty[i] = 1 si la línea i debe re-tokenizarse */
} LexerCache;

/**
 * @brief Inicializa la cache con @p line_count líneas, todas sucias.
 * @param lc Cache a inicializar. @param line_count Líneas iniciales (>= 1).
 * @return 1 si la reserva de memoria fue bien; 0 si falló.
 */
int lexer_cache_init(LexerCache *lc, int line_count);
/** @brief Libera la memoria de la cache. @param lc Cache a liberar. */
void lexer_cache_free(LexerCache *lc);
/**
 * @brief Redimensiona la cache a @p new_count líneas (las nuevas, sucias).
 * @param lc Cache. @param new_count Nuevo número de líneas (>= 1).
 */
void lexer_cache_resize(LexerCache *lc, int new_count);
/**
 * @brief Marca sucias las líneas desde @p from_line hasta el final.
 * @param lc Cache. @param from_line Primera línea a ensuciar (inclusive).
 */
void lexer_cache_dirty(LexerCache *lc, int from_line);

/* Accesores (inline): el cache es Vec<LineTokens> + Vec<int> */
/** @brief Número de líneas en la cache. @param lc Cache. @return Total de
 * líneas. */
static inline int lexer_cache_count(const LexerCache *lc) {
    return (int)lc->lines.len;
}
/**
 * @brief Tokens cacheados de la línea @p i (sin comprobar límites).
 * @param lc Cache. @param i Índice de línea. @return Puntero a sus
 * ::LineTokens.
 */
static inline LineTokens *lexer_cache_line(LexerCache *lc, int i) {
    return (LineTokens *)vec_at(&lc->lines, (size_t)i);
}
/**
 * @brief Puntero al flag de "sucio" de la línea @p i (lectura/escritura).
 * @param lc Cache. @param i Índice de línea. @return Puntero al int dirty[i].
 */
static inline int *lexer_cache_dirty_at(LexerCache *lc, int i) {
    return (int *)vec_at(&lc->dirty, (size_t)i);
}

/* ── Resaltador: interfaz por punteros a función (estilo *_ops del kernel) ──
 * Permite enchufar distintos lenguajes. tokenize_line escribe los tokens de una
 * línea en `out` y devuelve 1 si la línea termina dentro de un bloque de
 * comentario (el estado se encadena entre líneas). */
/**
 * @brief Resaltador enchufable: nombre + función de tokenizado de un lenguaje.
 *
 * Es "OOP en C": @c tokenize_line recibe @c self (el propio objeto) como primer
 * parámetro, igual que un método recibe @c this. Distintos lenguajes son
 * distintas instancias con la misma firma, intercambiables por el render.
 */
typedef struct Highlighter {
    const char *name; /* nombre legible del lenguaje ("C", "texto"...) */
    /**
     * @brief Tokeniza una línea según este resaltador.
     * @param self El propio resaltador. @param text Línea. @param len Longitud.
     * @param out Destino de los tokens.
     * @param in_block_comment 1 si la línea empieza dentro de un bloque.
     * @return 1 si la línea termina dentro de un bloque de comentario.
     */
    int (*tokenize_line)(const struct Highlighter *self, const char *text,
                         int len, LineTokens *out, int in_block_comment);
} Highlighter;

extern const Highlighter highlighter_c;    /* lenguaje C/C++           */
extern const Highlighter highlighter_none; /* texto plano (sin tokens) */

/** @brief Resaltador por defecto (C). @return Puntero a ::highlighter_c. */
const Highlighter *highlighter_default(void); /* por defecto (C) */
/**
 * @brief Elige el resaltador según la extensión de @p path.
 * @param path Ruta del archivo (puede ser @c NULL). @return Resaltador elegido.
 */
const Highlighter *highlighter_for_path(const char *path); /* según extensión */
