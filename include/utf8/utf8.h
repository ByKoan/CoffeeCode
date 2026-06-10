#pragma once
/**
 * @file utf8.h
 * @brief Utilidades UTF-8: decodificación y ancho de display de un carácter.
 *
 * El editor mide las columnas en CELDAS de pantalla (ancho de display), no en
 * bytes ni en caracteres: un carácter de doble ancho (CJK, kana, hangul, emoji)
 * ocupa 2 celdas; una marca combinante, 0; el resto, 1. ::utf8_cp_width es una
 * aproximación tipo @c wcwidth (East Asian Width); es exacta con una fuente
 * monoespaciada que dibuje esos caracteres a doble ancho.
 */
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Decodifica el primer carácter UTF-8 de @p s (hasta @p maxlen bytes).
 *
 * @param s      Bytes UTF-8.
 * @param maxlen Bytes disponibles en @p s.
 * @param[out] cp Punto de código (U+FFFD si la secuencia es inválida/truncada).
 * @return Número de bytes consumidos (>= 1; 0 solo si @p maxlen <= 0).
 */
int utf8_decode(const char *s, int maxlen, uint32_t *cp);

/**
 * @brief Ancho de display (en celdas) del punto de código @p cp.
 * @return 0 (combinante), 2 (doble ancho) o 1 (normal).
 */
int utf8_cp_width(uint32_t cp);
