# =============================================================================
# Extensiones de ejemplo de CoffeeCode (incremento E1).
# -----------------------------------------------------------------------------
# Cada extension es una DLL (.dll/.so) que SOLO enlaza el header de la API
# (include/ext/coffee_ext.h).  No depende de SDL ni de internals del IDE.
#
# Se compila cuando COFFEE_BUILD_EXTENSIONS=ON (default).  El sufijo de la
# biblioteca compartida (.dll/.so) se inyecta en el manifiesto via
# configure_file para que el campo `entry` apunte al fichero real.
# =============================================================================

# Sufijo de la DLL segun plataforma, para el manifiesto.
if(WIN32)
    set(COFFEE_EXT_DLL_SUFFIX ".dll")
else()
    set(COFFEE_EXT_DLL_SUFFIX ".so")
endif()

# --- Funcion auxiliar: declara una extension DLL + copia su manifiesto -------
#   add_coffee_extension(<nombre_dir> <fuente.c> [fuentes extra...])
#   Crea la DLL `coffee_<nombre>` sin prefijo (para un `entry` limpio en el
#   manifiesto) en el directorio de la extension, y configura el manifiesto.
function(add_coffee_extension extdir libname src)
    add_library(${libname} SHARED "${src}" ${ARGN})
    target_include_directories(${libname} PRIVATE "${CMAKE_SOURCE_DIR}/include")
    # Sin prefijo 'lib' para que el manifiesto referencie coffee_<nombre>.dll/.so
    set_target_properties(${libname} PROPERTIES
        PREFIX ""
        C_STANDARD 11
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${extdir}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${extdir}")
    # En multi-config (MSVC) evitar subcarpetas Debug/Release: forzar el dir
    foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${cfg}" CFG)
        set_target_properties(${libname} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${CFG} "${CMAKE_CURRENT_SOURCE_DIR}/${extdir}"
            RUNTIME_OUTPUT_DIRECTORY_${CFG} "${CMAKE_CURRENT_SOURCE_DIR}/${extdir}")
    endforeach()
endfunction()

# --- Extension de ejemplo: hello-c ------------------------------------------
add_coffee_extension(hello-c coffee_hello "${CMAKE_CURRENT_SOURCE_DIR}/hello-c/hello.c")

# Generar el manifiesto con el sufijo de DLL correcto.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/hello-c/coffee-extension.toml.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/hello-c/coffee-extension.toml"
    @ONLY)
