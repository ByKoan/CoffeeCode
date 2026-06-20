# =============================================================================
# Extensiones de ejemplo de CoffeeCode.
# -----------------------------------------------------------------------------
# Cada extension es una DLL (.dll/.so) que SOLO enlaza el header de la API
# (include/ext/coffee_ext.h).  No depende de SDL ni de internals del IDE.
#
# Se compila cuando COFFEE_BUILD_EXTENSIONS=ON (default).  La DLL y su
# manifiesto se generan en <build>/extensions/<nombre>/ -- el MISMO sitio del
# que el IDE las carga (SDL_GetBasePath()/extensions, ver editor.c), y NUNCA en
# el arbol fuente.  El sufijo de la biblioteca compartida (.dll/.so) se inyecta
# en el manifiesto via configure_file para que el campo `entry` apunte al
# fichero real.
# =============================================================================

# Sufijo de la DLL segun plataforma, para el manifiesto.
if(WIN32)
    set(COFFEE_EXT_DLL_SUFFIX ".dll")
else()
    set(COFFEE_EXT_DLL_SUFFIX ".so")
endif()

# Raiz donde el IDE busca extensiones: <dir del ejecutable>/extensions.  El
# ejecutable CoffeeCode no fija RUNTIME_OUTPUT_DIRECTORY, asi que vive en
# ${CMAKE_BINARY_DIR}; por tanto las extensiones van a ${CMAKE_BINARY_DIR}/extensions.
set(COFFEE_EXT_ROOT "${CMAKE_BINARY_DIR}/extensions")

# --- Funcion auxiliar: declara una extension DLL + genera su manifiesto -------
#   add_coffee_extension(<nombre_dir> <libname> <fuente.c> [fuentes extra...])
#   Crea la DLL `coffee_<nombre>` sin prefijo (para un `entry` limpio en el
#   manifiesto) en <build>/extensions/<nombre_dir>/.
function(add_coffee_extension extdir libname src)
    set(_outdir "${COFFEE_EXT_ROOT}/${extdir}")
    add_library(${libname} SHARED "${src}" ${ARGN})
    target_include_directories(${libname} PRIVATE "${CMAKE_SOURCE_DIR}/include")
    # Sin prefijo 'lib' para que el manifiesto referencie coffee_<nombre>.dll/.so
    set_target_properties(${libname} PROPERTIES
        PREFIX ""
        C_STANDARD 11
        LIBRARY_OUTPUT_DIRECTORY "${_outdir}"
        RUNTIME_OUTPUT_DIRECTORY "${_outdir}")
    # En multi-config (MSVC) evitar subcarpetas Debug/Release: forzar el dir.
    foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${cfg}" CFG)
        set_target_properties(${libname} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${CFG} "${_outdir}"
            RUNTIME_OUTPUT_DIRECTORY_${CFG} "${_outdir}")
    endforeach()
    # Genera el manifiesto junto a la DLL en el build dir (nunca en el fuente).
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/${extdir}/coffee-extension.toml.in"
        "${_outdir}/coffee-extension.toml"
        @ONLY)
    # La extension se construye SIEMPRE que se construye el ejecutable: asi
    # `cmake --build --target CoffeeCode` deja las DLLs de extension listas en
    # <build>/extensions/.
    if(TARGET CoffeeCode)
        add_dependencies(CoffeeCode ${libname})
    endif()
endfunction()

# --- Extension de ejemplo: hello-c ------------------------------------------
add_coffee_extension(hello-c coffee_hello "${CMAKE_CURRENT_SOURCE_DIR}/hello-c/hello.c")
