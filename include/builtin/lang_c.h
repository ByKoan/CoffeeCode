/**
 * @file lang_c.h
 * @brief Lenguaje C como extension NATIVA EMBEBIDA en el ejecutable.
 *
 * El core de CoffeeCode ya no trae resaltador de sintaxis propio: el coloreado
 * lo aportan las extensiones via el CoffeeApi (register_highlighter).  El unico
 * lenguaje integrado de base es C, pero NO esta hardcodeado en el render: se
 * implementa como una extension que se compila DENTRO del IDE y se registra en
 * proceso al arrancar, usando exactamente la misma API que cualquier extension
 * DLL externa.
 *
 * El resaltador de C lee sus colores del TEMA activo del editor (se le pasa un
 * puntero estable al ::Theme como userdata), de modo que .c/.cpp/.h se ven
 * igual que siempre y siguen cambiando con el tema (oscuro/claro).
 */
#ifndef COFFEE_BUILTIN_LANG_C_H
#define COFFEE_BUILTIN_LANG_C_H

#include "ext/coffee_ext.h"
#include "render/theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registra el resaltador de C como extension embebida en el host.
 *
 * Asocia el tokenizador de C a las extensiones de archivo de C/C++ (.c, .h,
 * .cpp, .cc, .cxx, .hpp, .hh, .hxx) via @c api->register_highlighter.  El
 * resaltador colorea cada linea leyendo la paleta de @p theme (puntero estable
 * al tema vivo del editor), por lo que respeta el tema activo.
 *
 * No-op si @p host, @p api o @p theme son NULL, o si el host no soporta la ABI
 * de resaltado (register_highlighter ausente).
 *
 * @param host  Host de extensiones del editor.
 * @param api   Vtable del CoffeeApi del host.
 * @param theme Puntero ESTABLE al tema vivo del editor (p.ej. &e->theme).
 */
void coffee_builtin_c_register(CoffeeHost *host, const CoffeeApi *api,
                               const Theme *theme);

/**
 * @brief Tokeniza UNA linea de C en tramos coloreados (la funcion registrada).
 *
 * Expuesta para los tests: tiene la firma ::CoffeeHighlightFn.  @p ud debe ser un
 * @c const Theme* (la paleta de la que toma los colores).  Devuelve el numero de
 * tramos escritos en @p out (en COLUMNAS DE CARACTER) y, via @p out_block, si la
 * linea termina dentro de un comentario de bloque.
 */
int coffee_builtin_c_highlight(void *ud, const char *line_utf8, int line_len,
                               int in_block_comment, CoffeeSpan *out,
                               int max_out, int *out_block);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* COFFEE_BUILTIN_LANG_C_H */
