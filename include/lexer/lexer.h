/**
 * @file lexer.h
 * @brief Cache de resaltado por linea + tipos de color compartidos.
 *
 * El core de CoffeeCode YA NO trae resaltador de sintaxis propio: el coloreado
 * lo aportan las extensiones (ver @ref ext/coffee_ext.h y @ref ext/ext_host.h).
 * Este modulo conserva solo las piezas que NO son especificas de ningun
 * lenguaje:
 *
 *   - El tipo ::Color (RGBA) usado por todo el render y el tema.
 *   - El enum ::LexTokenType, que es la lista de CATEGORIAS de token del tema
 *     (::Theme.tokens se indexa por el): lo usan el render para el color de
 *     texto por defecto (::TOK_DEFAULT) y el lenguaje C embebido para elegir su
 *     color por categoria.  NO impone un resaltador: una extension puede usar
 *     cualquier color RGBA libre sin pasar por estas categorias.
 *   - La ::LexerCache: cache de tramos coloreados por linea (con marcado de
 *     lineas sucias) que el render rellena con los tramos que devuelve el
 *     resaltador REGISTRADO por una extension.  Cada ::Token guarda ya su color
 *     final (no una categoria), porque el color lo decide la extension.
 */
#pragma once
#include "structs/vec.h"
#include <stddef.h>
#include <stdint.h>

/* -- Color ---------------------------------------------------------------- */
/** @brief Color RGBA de 8 bits por canal. */
typedef struct {
    uint8_t r, g, b, a;
} Color;

/* -- Categorias de token (paleta del tema) -------------------------------- */
/**
 * @brief Categoria lexica usada para indexar la paleta del tema (::Theme.tokens).
 *
 * NO es un contrato de resaltado: es solo la lista de colores que el tema
 * define.  El render usa ::TOK_DEFAULT como color de texto por defecto; el
 * lenguaje C embebido mapea sus tokens a estas categorias para tomar sus
 * colores del tema.  Una extension de otro lenguaje puede ignorar este enum y
 * emitir cualquier color RGBA.  ::TOK_COUNT es el centinela (numero de
 * categorias) para dimensionar arrays, no es una categoria real.
 */
typedef enum {
    TOK_DEFAULT = 0,  /* texto sin categoria / color por defecto    */
    TOK_KEYWORD,      /* palabra reservada (if, while, return...)   */
    TOK_TYPE,         /* nombre de tipo (int, char, size_t...)      */
    TOK_COMMENT,      /* comentario de linea o de bloque            */
    TOK_STRING,       /* literal de cadena o de caracter            */
    TOK_NUMBER,       /* literal numerico (hex, decimal, flotante)  */
    TOK_PREPROCESSOR, /* directiva de preprocesador (#include...)   */
    TOK_OPERATOR,     /* operador (+ - * / = < >...)                */
    TOK_PUNCTUATION,  /* puntuacion (parentesis, llaves, ; , ...)   */
    TOK_COUNT         /* num. de categorias (centinela para dimensionar) */
} LexTokenType;

/* -- Resultado del resaltado por linea ------------------------------------ */
/** @brief Tope de tramos almacenados por linea (los que sobran se descartan). */
#define MAX_TOKENS_PER_LINE 512

/**
 * @brief Un tramo coloreado: un fragmento de la linea con su color final.
 *
 * Se describe por posicion en BYTES: cubre @c [col, col+len) de su linea.  El
 * render usa @c col/@c len para localizar el texto y @c color para pintarlo.  A
 * diferencia del modelo antiguo, @c color es el color final (RGBA) que decidio
 * el resaltador de la extension, no una categoria a resolver contra una paleta
 * fija del core.
 */
typedef struct {
    int col;     /* columna de inicio en BYTES (0-based) */
    int len;     /* longitud en BYTES                    */
    Color color; /* color final del tramo               */
} Token;

/** @brief Conjunto de tramos resultante de resaltar una linea. */
typedef struct {
    Token tokens[MAX_TOKENS_PER_LINE]; /* tramos de la linea (array fijo)   */
    int count;                         /* num. de tramos validos en el array */
} LineTokens;

/* -- Cache de resaltado --------------------------------------------------- */
/**
 * @brief Cache de tramos por linea con marcado de lineas sucias.
 *
 * Dos vectores paralelos indexados por numero de linea: @c lines guarda los
 * tramos ya calculados y @c dirty indica que lineas hay que re-resaltar.  Evita
 * re-analizar todo el archivo en cada frame.  La rellena el render llamando al
 * resaltador REGISTRADO por una extension para el lenguaje del archivo.
 */
typedef struct {
    Vec lines; /* Vec<LineTokens>: una entrada por linea de cache         */
    Vec dirty; /* Vec<int>: dirty[i] = 1 si la linea i debe re-resaltarse  */
} LexerCache;

/**
 * @brief Inicializa la cache con @p line_count lineas, todas sucias.
 * @param lc Cache a inicializar. @param line_count Lineas iniciales (>= 1).
 * @return 1 si la reserva de memoria fue bien; 0 si fallo.
 */
int lexer_cache_init(LexerCache *lc, int line_count);
/** @brief Libera la memoria de la cache. @param lc Cache a liberar. */
void lexer_cache_free(LexerCache *lc);
/**
 * @brief Redimensiona la cache a @p new_count lineas (las nuevas, sucias).
 * @param lc Cache. @param new_count Nuevo numero de lineas (>= 1).
 */
void lexer_cache_resize(LexerCache *lc, int new_count);
/**
 * @brief Marca sucias las lineas desde @p from_line hasta el final.
 * @param lc Cache. @param from_line Primera linea a ensuciar (inclusive).
 */
void lexer_cache_dirty(LexerCache *lc, int from_line);

/* Accesores (inline): el cache es Vec<LineTokens> + Vec<int> */
/** @brief Numero de lineas en la cache. @param lc Cache. @return Total de
 * lineas. */
static inline int lexer_cache_count(const LexerCache *lc) {
    return (int)lc->lines.len;
}
/**
 * @brief Tramos cacheados de la linea @p i (sin comprobar limites).
 * @param lc Cache. @param i Indice de linea. @return Puntero a sus
 * ::LineTokens.
 */
static inline LineTokens *lexer_cache_line(LexerCache *lc, int i) {
    return (LineTokens *)vec_at(&lc->lines, (size_t)i);
}
/**
 * @brief Puntero al flag de "sucio" de la linea @p i (lectura/escritura).
 * @param lc Cache. @param i Indice de linea. @return Puntero al int dirty[i].
 */
static inline int *lexer_cache_dirty_at(LexerCache *lc, int i) {
    return (int *)vec_at(&lc->dirty, (size_t)i);
}
