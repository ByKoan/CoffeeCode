# =============================================================================
# Extensiones de CoffeeCode (descubrimiento + utilidades comunes).
# -----------------------------------------------------------------------------
# El IDE NO conoce ninguna extension concreta: este fichero solo (1) expone unas
# utilidades para declarar una extension de forma uniforme y (2) DESCUBRE y
# construye toda subcarpeta de extensions/ que traiga su propio CMakeLists.txt.
#
# Cada extension es una DLL (.dll/.so) que enlaza el header de la API
# (include/ext/coffee_ext.h) y, si necesita dependencias propias (otras librerias,
# descargas, etc.), las resuelve en SU PROPIO CMakeLists.txt.  La DLL y su
# manifiesto se generan en <build>/extensions/<nombre>/ -- el MISMO sitio del que
# el IDE las carga (SDL_GetBasePath()/extensions, ver editor.c), nunca en el
# arbol fuente.  El sufijo de la biblioteca (.dll/.so) se inyecta en el
# manifiesto via configure_file para que el campo `entry` apunte al fichero real.
# =============================================================================

# Sufijo de la DLL segun plataforma, para el manifiesto (lo usan los .toml.in).
if(WIN32)
    set(COFFEE_EXT_DLL_SUFFIX ".dll")
else()
    set(COFFEE_EXT_DLL_SUFFIX ".so")
endif()

# Raiz donde el IDE busca extensiones: <dir del ejecutable>/extensions.  El
# ejecutable CoffeeCode no fija RUNTIME_OUTPUT_DIRECTORY, asi que vive en
# ${CMAKE_BINARY_DIR}; por tanto las extensiones van a ${CMAKE_BINARY_DIR}/extensions.
set(COFFEE_EXT_ROOT "${CMAKE_BINARY_DIR}/extensions")

# Directorio de includes del IDE (coffee_ext.h).  Lo usan las extensiones.
set(COFFEE_IDE_INCLUDE "${CMAKE_SOURCE_DIR}/include")

# --- Utilidad: termina de configurar un target de extension ya creado --------
#   coffee_extension_finalize(<libname>)
#   Coloca la DLL (sin prefijo 'lib', para un `entry` limpio) en
#   <build>/extensions/<carpeta>/, genera su manifiesto desde el .toml.in de la
#   propia extension, y la encadena al ejecutable para que se construya con el.
#   La carpeta de salida se deduce del nombre del directorio de la extension.
#   La usan las extensiones que crean su target a mano (p.ej. para enlazar deps).
function(coffee_extension_finalize libname)
    get_filename_component(_extdir "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
    set(_outdir "${COFFEE_EXT_ROOT}/${_extdir}")
    target_include_directories(${libname} PRIVATE "${COFFEE_IDE_INCLUDE}")
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
    # Manifiesto junto a la DLL en el build dir (nunca en el fuente).
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/coffee-extension.toml.in"
        "${_outdir}/coffee-extension.toml"
        @ONLY)
    # La extension se construye SIEMPRE que se construye el ejecutable.
    if(TARGET CoffeeCode)
        add_dependencies(CoffeeCode ${libname})
    endif()
    # Exponer el dir de salida al llamante (para copiar deps con POST_BUILD).
    set(COFFEE_EXT_OUTDIR "${_outdir}" PARENT_SCOPE)
endfunction()

# --- Utilidad: declara una extension simple (solo fuentes C, sin deps) -------
#   add_coffee_extension(<libname> <fuente.c> [fuentes extra...])
#   Crea la DLL y la finaliza.  Para extensiones sin dependencias externas.
function(add_coffee_extension libname)
    add_library(${libname} SHARED ${ARGN})
    coffee_extension_finalize(${libname})
endfunction()

# --- Descubrimiento: construir toda subcarpeta con su propio CMakeLists.txt ---
# El IDE no nombra ninguna extension: simplemente recorre extensions/ y delega
# en el CMakeLists.txt de cada una (que se declara a si misma y resuelve sus
# dependencias).  Asi anyadir una extension = crear su carpeta con su CMake.
file(GLOB _coffee_ext_children RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
     "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(_child ${_coffee_ext_children})
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_child}"
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_child}/CMakeLists.txt")
        add_subdirectory("${_child}")
    endif()
endforeach()
