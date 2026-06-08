# EmbedIcon.cmake
# Verifica que assets/icono.ico exista y lo expone para compilarlo dentro del binario.
#
# Variables de entrada:
#   OUT_DIR  – directorio raíz del proyecto (donde está la carpeta assets/)

cmake_minimum_required(VERSION 3.16)

set(ICO_PATH "${OUT_DIR}/assets/icono.ico")

if(NOT EXISTS "${ICO_PATH}")
    message(FATAL_ERROR
        "[EmbedIcon] No se encontró el icono en: ${ICO_PATH}\n"
        "Asegúrate de que assets/icono.ico existe en el repositorio."
    )
endif()

message(STATUS "[EmbedIcon] Icono encontrado: ${ICO_PATH}")
