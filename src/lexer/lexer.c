/**
 * @file lexer.c
 * @brief Cache de tramos coloreados por linea (con marcado de lineas sucias).
 *
 * El core de CoffeeCode YA NO tiene resaltador de sintaxis propio: el coloreado
 * lo aportan las extensiones (el lenguaje C es una extension nativa embebida,
 * ver @ref builtin/lang_c.c; los demas son DLLs).  Lo unico que queda aqui es la
 * CACHE de resaltado por linea: tokenizar/resaltar en cada frame todas las
 * lineas visibles seria caro, asi que se guardan los tramos ya calculados de
 * cada linea y se marcan como "sucias" (dirty) solo las que cambiaron.  El
 * render re-resalta perezosamente solo lo sucio llamando al resaltador
 * REGISTRADO por una extension.  Como el estado de comentario de bloque se
 * encadena entre lineas, al ensuciar una linea hay que ensuciar tambien todas
 * las de abajo (ver ::lexer_cache_dirty).
 */
#include "lexer/lexer.h"

/* -- Cache de tramos por linea --------------------------------------------- */

/**
 * @brief Inicializa la cache con @p line_count lineas, todas marcadas sucias.
 *
 * Reserva dos vectores paralelos indexados por numero de linea: @c lines guarda
 * los tramos ya calculados de cada linea y @c dirty marca con 1 las lineas que
 * aun deben (re)resaltarse.  Al arrancar nada esta calculado todavia, asi que
 * todas se dejan sucias para que el primer render las procese.
 *
 * @param lc         Cache a inicializar.
 * @param line_count Numero de lineas iniciales (se fuerza a >= 1).
 * @return 1 si las reservas de memoria fueron bien; 0 si alguna fallo.
 */
int lexer_cache_init(LexerCache *lc, int line_count) {
    if (line_count < 1) line_count = 1; /* al menos una linea (archivo vacio) */
    vec_init(&lc->lines, sizeof(LineTokens)); /* Vec<LineTokens> */
    vec_init(&lc->dirty, sizeof(int));        /* Vec<int> paralelo */
    /* vec_resize pone a cero las entradas nuevas => LineTokens.count = 0 */
    if (!vec_resize(&lc->lines, (size_t)line_count)) return 0;
    if (!vec_resize(&lc->dirty, (size_t)line_count)) return 0;
    for (int i = 0; i < line_count; i++)
        *(int *)vec_at(&lc->dirty, (size_t)i) = 1; /* todas sucias de inicio */
    return 1;
}

/** @brief Libera los dos vectores internos de la cache. @param lc Cache a
 * liberar. */
void lexer_cache_free(LexerCache *lc) {
    vec_free(&lc->lines);
    vec_free(&lc->dirty);
}

/**
 * @brief Ajusta la cache a @p new_count lineas (las nuevas quedan sucias).
 *
 * Se llama tras una edicion que cambio el numero de lineas.  Si el tamano no
 * cambio no hace nada.  Al crecer, las entradas nuevas se ponen a cero (tramos
 * vacios) y se marcan sucias para que se resalten; al encoger simplemente se
 * recortan los vectores.
 *
 * @param lc        Cache a redimensionar.
 * @param new_count Nuevo numero de lineas (se fuerza a >= 1).
 */
void lexer_cache_resize(LexerCache *lc, int new_count) {
    if (new_count < 1) new_count = 1;
    int old = (int)lc->lines.len; /* tamano anterior */
    if (new_count == old) return; /* nada que hacer si no cambio */

    /* nuevas LineTokens a cero (count=0) */
    vec_resize(&lc->lines, (size_t)new_count);
    vec_resize(&lc->dirty, (size_t)new_count);
    for (int i = old; i < new_count; i++)
        /* ensuciar solo las anyadidas */
        *(int *)vec_at(&lc->dirty, (size_t)i) = 1;
}

/**
 * @brief Marca como sucias todas las lineas desde @p from_line hasta el final.
 *
 * Ensucia "hacia abajo" y no solo la linea editada porque el estado de bloque
 * de comentario se encadena: abrir o cerrar un @c /\* ... *\/ en @p from_line
 * puede cambiar como se resaltan todas las lineas siguientes.  Re-resaltar de
 * mas es seguro; re-resaltar de menos dejaria resaltado obsoleto.
 *
 * @param lc        Cache a ensuciar.
 * @param from_line Primera linea (inclusive) a marcar como pendiente.
 */
void lexer_cache_dirty(LexerCache *lc, int from_line) {
    int n = (int)lc->dirty.len;
    for (int i = from_line; i < n; i++)
        *(int *)vec_at(&lc->dirty, (size_t)i) = 1;
}
