#pragma once
/**
 * @file encoding.h
 * @brief Conversión entre codificaciones de texto y UTF-8 (la representación
 *        interna del editor).
 *
 * El buffer del editor SIEMPRE guarda UTF-8 (es lo que esperan SDL_ttf y todo
 * el render). La codificación es una propiedad del ARCHIVO: al abrir se
 * decodifica de su codificación a UTF-8; al guardar se codifica UTF-8 a la
 * codificación elegida. Este módulo es independiente de SDL (texto puro).
 */
#include <stddef.h>

/** Codificaciones soportadas. */
typedef enum {
    ENC_UTF8 = 0, /**< UTF-8 sin BOM.                 */
    ENC_UTF8_BOM, /**< UTF-8 con BOM (EF BB BF).      */
    ENC_UTF16_LE, /**< UTF-16 little-endian (+BOM).   */
    ENC_UTF16_BE, /**< UTF-16 big-endian (+BOM).      */
    ENC_UTF32_LE, /**< UTF-32 little-endian (+BOM).   */
    ENC_UTF32_BE, /**< UTF-32 big-endian (+BOM).      */
    ENC_CP1252,   /**< Windows-1252 (ANSI).           */
    ENC_LATIN1,   /**< ISO-8859-1 (Latin-1).          */
    ENC_ASCII,    /**< US-ASCII (7 bits).             */
    ENC_COUNT
} TextEncoding;

/** Nombre legible de la codificación (p. ej. "UTF-8", "UTF-16 LE", "ANSI"). */
const char *encoding_name(TextEncoding enc);

/**
 * @brief Detecta la codificación de @p data por BOM y, si no hay, por
 * heurística.
 *
 * Reconoce BOMs de UTF-8/UTF-16/UTF-32. Sin BOM: si el contenido es UTF-8
 * válido se asume UTF-8; si no, Windows-1252 (ANSI). Por defecto UTF-8.
 *
 * @param data     Bytes crudos del archivo.
 * @param len      Longitud en bytes.
 * @param has_bom  [out, opcional] 1 si se encontró un BOM.
 * @return La codificación detectada.
 */
TextEncoding encoding_detect(const unsigned char *data, size_t len,
                             int *has_bom);

/**
 * @brief Decodifica @p in (en @p enc) a UTF-8 recién reservado con malloc.
 *
 * Salta el BOM si lo hay. Los bytes/secuencias inválidos se sustituyen por el
 * carácter de reemplazo U+FFFD. El llamante debe @c free(*out).
 *
 * @return 1 si ok (aunque la entrada esté vacía); 0 si falló la reserva.
 */
int encoding_decode(TextEncoding enc, const unsigned char *in, size_t in_len,
                    char **out, size_t *out_len);

/**
 * @brief Codifica UTF-8 @p in a @p enc en un bloque recién reservado con
 * malloc.
 *
 * Antepone el BOM en las codificaciones que lo llevan (UTF-8 BOM, UTF-16/32).
 * Los caracteres no representables en la codificación destino se sustituyen por
 * '?'. El llamante debe @c free(*out).
 *
 * @return 1 si ok; 0 si falló la reserva.
 */
int encoding_encode(TextEncoding enc, const char *in, size_t in_len,
                    unsigned char **out, size_t *out_len);
