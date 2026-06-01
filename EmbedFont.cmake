# EmbedFont.cmake — Convierte FONT_TTF en un header C con los bytes embebidos
# Uso: cmake -DFONT_TTF=<ruta> -DFONT_DATA_H=<salida> -P EmbedFont.cmake

cmake_minimum_required(VERSION 3.24)

if(NOT FONT_TTF OR NOT FONT_DATA_H)
    message(FATAL_ERROR "Uso: cmake -DFONT_TTF=<ruta> -DFONT_DATA_H=<salida> -P EmbedFont.cmake")
endif()

file(READ "${FONT_TTF}" FONT_BYTES HEX)
string(LENGTH "${FONT_BYTES}" HEX_LEN)
math(EXPR FONT_SIZE "${HEX_LEN} / 2")

# Construir el array en grupos de 16 bytes por línea
set(ARRAY_BODY "")
set(i 0)
set(line_bytes "")
set(byte_count 0)

while(i LESS ${HEX_LEN})
    string(SUBSTRING "${FONT_BYTES}" ${i} 2 BYTE_HEX)
    string(APPEND line_bytes "0x${BYTE_HEX}, ")
    math(EXPR byte_count "${byte_count} + 1")
    math(EXPR i "${i} + 2")

    if(byte_count EQUAL 16 OR i EQUAL ${HEX_LEN})
        string(APPEND ARRAY_BODY "    ${line_bytes}\n")
        set(line_bytes "")
        set(byte_count 0)
    endif()
endwhile()

file(WRITE "${FONT_DATA_H}"
"/* Auto-generado por cmake/EmbedFont.cmake — no editar */
#pragma once
#include <stddef.h>
static const unsigned char g_font_data[] = {
${ARRAY_BODY}};
static const size_t g_font_size = ${FONT_SIZE};
")

message(STATUS "font_data.h generado: ${FONT_SIZE} bytes -> ${FONT_DATA_H}")
