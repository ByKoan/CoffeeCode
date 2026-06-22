/**
 * @file vex_semtokens.h
 * @brief Descodificacion PURA de los semantic tokens LSP (sin deps del IDE).
 *
 * El protocolo entrega los semantic tokens como un array plano de uint32 en
 * quintetos DELTA:
 *
 *   [deltaLine, deltaStartChar, length, tokenType, tokenModifiers]
 *
 * Donde @c deltaStartChar y @c length van en UNIDADES UTF-16 y son relativos al
 * token anterior (misma linea -> sumar deltaStartChar; deltaLine>0 -> nueva
 * linea y deltaStartChar es absoluto).  Este modulo:
 *
 *   (a) acumula los deltas a posiciones ABSOLUTAS (linea, startChar UTF-16,
 *       length UTF-16, tokenType, tokenModifiers), y
 *   (b) convierte columnas UTF-16 -> columnas de CARACTER (codepoints) usando
 *       el texto de la linea, que es lo que pide el editor (CoffeeSpan).
 *
 * Es C puro, sin SDL, sin cJSON, sin la API del IDE: asi se puede probar
 * headless y reusar.  El que descodifica un array entero le pasa un fetcher de
 * texto de linea (callback) para resolver la conversion de columnas.
 */
#ifndef VEX_SEMTOKENS_H
#define VEX_SEMTOKENS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Token absoluto ya descodificado (columnas aun en UNIDADES UTF-16). */
typedef struct {
    uint32_t line;        /**< linea 0-based. */
    uint32_t start_u16;   /**< columna de inicio en unidades UTF-16. */
    uint32_t len_u16;     /**< longitud en unidades UTF-16. */
    uint32_t type;        /**< indice de tokenType en la leyenda. */
    uint32_t modifiers;   /**< mascara de tokenModifiers. */
} VexSemToken;

/** Token absoluto con columnas ya en CODEPOINTS (lo que usa CoffeeSpan). */
typedef struct {
    uint32_t line;        /**< linea 0-based. */
    uint32_t start_cp;    /**< columna de inicio en codepoints. */
    uint32_t len_cp;      /**< longitud en codepoints. */
    uint32_t type;        /**< indice de tokenType en la leyenda. */
    uint32_t modifiers;   /**< mascara de tokenModifiers. */
} VexSemTokenCp;

/**
 * @brief Descodifica el array plano de quintetos a tokens ABSOLUTOS.
 *
 * Acumula los deltas: si @c deltaLine>0 avanza la linea y @c deltaStartChar es
 * absoluto; si @c deltaLine==0 suma @c deltaStartChar a la columna anterior.
 * Ignora una cola incompleta (longitud no multiplo de 5).
 *
 * @param data   Array plano (puede ser NULL si @p count==0).
 * @param count  Numero de uint32 en @p data.
 * @param[out] out Destino de los tokens (puede ser NULL si @p max_out==0).
 * @param max_out Capacidad de @p out en numero de tokens.
 * @return Numero de tokens que el array PRODUCE (puede exceder @p max_out; en
 *         ese caso solo se escribieron @p max_out).
 */
int vex_semtokens_decode(const uint32_t *data, size_t count, VexSemToken *out,
                         int max_out);

/**
 * @brief Convierte una columna en UNIDADES UTF-16 a columna en CODEPOINTS.
 *
 * Recorre el texto UTF-8 de la linea contando codepoints y, por cada uno,
 * cuantas unidades UTF-16 ocupa (1, o 2 si es un par subrogado, codepoint
 * >= 0x10000).  Devuelve el numero de codepoints que cubren @p u16 unidades.
 * Si @p u16 cae a mitad de un codepoint (no deberia con datos validos), redondea
 * al codepoint que lo contiene.  Tolera UTF-8 malformado avanzando byte a byte.
 *
 * @param line_utf8 Texto de la linea (UTF-8; no necesita NUL).
 * @param line_len  Longitud de la linea en BYTES.
 * @param u16       Columna objetivo en unidades UTF-16.
 * @return Columna equivalente en codepoints.
 */
uint32_t vex_utf16_units_to_codepoints(const char *line_utf8, size_t line_len,
                                       uint32_t u16);

/**
 * @brief Convierte (start_u16, len_u16) de un token a (start_cp, len_cp).
 *
 * Aplica @c vex_utf16_units_to_codepoints al inicio y al fin del token sobre el
 * texto de su linea, de modo que un token con multibyte queda bien medido en
 * codepoints.  Copia line/type/modifiers tal cual.
 *
 * @param tok       Token absoluto en unidades UTF-16.
 * @param line_utf8 Texto de la linea del token (UTF-8), o NULL/"" si se
 *                  desconoce (en cuyo caso las columnas se dejan igual, asumiendo
 *                  ASCII: 1 unidad == 1 codepoint).
 * @param line_len  Longitud de la linea en bytes.
 * @param[out] out  Token con columnas en codepoints.
 */
void vex_semtoken_to_cp(const VexSemToken *tok, const char *line_utf8,
                        size_t line_len, VexSemTokenCp *out);

#ifdef __cplusplus
}
#endif

#endif /* VEX_SEMTOKENS_H */
