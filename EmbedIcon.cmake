# EmbedIcon.cmake
# Convierte icono.svg → icono.ico (Windows) e icono.png (Linux/general)
# Requiere ImageMagick (convert) o Inkscape + ImageMagick en PATH.
#
# Variables de entrada:
#   SVG_PATH   – ruta absoluta al archivo icono.svg
#   OUT_DIR    – directorio donde se generarán icono.ico e icono.png

cmake_minimum_required(VERSION 3.16)

# ── Buscar herramientas ───────────────────────────────────────────────────────
find_program(INKSCAPE_EXE  inkscape)
find_program(CONVERT_EXE   convert   NAMES magick convert)   # ImageMagick
find_program(RSVG_EXE      rsvg-convert)

set(PNG_PATH "${OUT_DIR}/icono.png")
set(ICO_PATH "${OUT_DIR}/icono.ico")

# ── SVG → PNG ─────────────────────────────────────────────────────────────────
if(INKSCAPE_EXE)
    message(STATUS "[EmbedIcon] Usando Inkscape para SVG→PNG")
    execute_process(
        COMMAND "${INKSCAPE_EXE}"
            --export-type=png
            --export-width=256
            --export-height=256
            --export-filename="${PNG_PATH}"
            "${SVG_PATH}"
        RESULT_VARIABLE _res
    )
elseif(RSVG_EXE)
    message(STATUS "[EmbedIcon] Usando rsvg-convert para SVG→PNG")
    execute_process(
        COMMAND "${RSVG_EXE}" -w 256 -h 256 -o "${PNG_PATH}" "${SVG_PATH}"
        RESULT_VARIABLE _res
    )
elseif(CONVERT_EXE)
    message(STATUS "[EmbedIcon] Usando ImageMagick convert para SVG→PNG")
    execute_process(
        COMMAND "${CONVERT_EXE}" -background none -resize 256x256 "${SVG_PATH}" "${PNG_PATH}"
        RESULT_VARIABLE _res
    )
else()
    message(FATAL_ERROR "[EmbedIcon] No se encontró Inkscape, rsvg-convert ni ImageMagick. "
        "Instala uno de ellos para poder generar el icono desde icono.svg.")
endif()

if(NOT EXISTS "${PNG_PATH}")
    message(FATAL_ERROR "[EmbedIcon] Falló la generación de ${PNG_PATH}")
endif()

# ── PNG → ICO (solo necesario en Windows) ─────────────────────────────────────
if(CONVERT_EXE)
    message(STATUS "[EmbedIcon] Generando icono.ico con ImageMagick")
    execute_process(
        COMMAND "${CONVERT_EXE}"
            "${PNG_PATH}"
            -define icon:auto-resize=256,128,64,48,32,16
            "${ICO_PATH}"
        RESULT_VARIABLE _res
    )
    if(NOT EXISTS "${ICO_PATH}")
        message(WARNING "[EmbedIcon] No se pudo generar icono.ico. "
            "El .exe se compilará sin icono embebido.")
    endif()
else()
    message(WARNING "[EmbedIcon] ImageMagick no encontrado: no se generará icono.ico. "
        "Instala ImageMagick para empaquetar el icono en el .exe de Windows.")
endif()
