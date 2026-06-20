# =============================================================================
# Extension vesta: compilar y ejecutar codigo del lenguaje Vex desde el IDE.
# -----------------------------------------------------------------------------
# Esta extension enlaza libvesta (la DLL C-ABI del compilador + VM de Vex, del
# proyecto VestaVM, https://github.com/desmonHak/VM.git; su target en ese CMake
# es `vesta_ffi`, SHARED, OUTPUT_NAME "vesta").  La extension resuelve esa
# dependencia POR SI MISMA, sin que el IDE sepa nada de ella, siguiendo TRES
# caminos en orden:
#
#   1. OVERRIDE LOCAL (rapido): si VESTA_DIR apunta a un checkout de VestaVM ya
#      construido (existe ${VESTA_DIR}/cmake-build-release/libvesta.dll.a), se
#      enlaza ESA import lib y se copian sus DLLs de runtime (libvesta + OpenSSL)
#      junto a la extension.  Para devs que ya tienen VestaVM local.
#
#   2. FETCH DEL REPOSITORIO (portable): si no hay override local valido y la
#      opcion COFFEE_FETCH_VESTA esta ON (default), se clona el repo via
#      FetchContent y se compila el target vesta_ffi.  No depende de ninguna
#      ruta de maquina ni disco.
#
#   3. SKIP GRACEFUL: si no hay local y COFFEE_FETCH_VESTA esta OFF, la extension
#      se OMITE.  El resto del IDE y de las extensiones siguen compilando.
#
# Variables de control:
#   VESTA_DIR           Raiz de un checkout local de VestaVM (camino 1).
#   COFFEE_FETCH_VESTA  Si ON, permite el fetch del repo cuando no hay local.
# =============================================================================

set(VESTA_DIR "F:/C/VM" CACHE PATH "Raiz de un checkout local de VestaVM (libvesta)")
option(COFFEE_FETCH_VESTA "Clonar y compilar libvesta desde el repo si no hay checkout local" ON)

set(_vesta_build "${VESTA_DIR}/cmake-build-release")
set(_vesta_importlib "${_vesta_build}/libvesta.dll.a")
set(_vesta_include "${VESTA_DIR}/include")
set(_vesta_outdir "${COFFEE_EXT_ROOT}/vesta")

# Crea el target coffee_vesta, lo enlaza contra libvesta y lo finaliza con la
# utilidad comun del IDE (salida en <build>/extensions/vesta/ + manifiesto +
# dependencia del ejecutable).  `link_target` es la import lib local o el target
# `vesta_ffi` traido por FetchContent.
function(_coffee_setup_vesta link_target extra_include)
    add_library(coffee_vesta SHARED "${CMAKE_CURRENT_SOURCE_DIR}/vesta.c")
    if(extra_include)
        target_include_directories(coffee_vesta PRIVATE "${extra_include}")  # capi/vesta.h
    endif()
    target_link_libraries(coffee_vesta PRIVATE "${link_target}")
    coffee_extension_finalize(coffee_vesta)
endfunction()

if(EXISTS "${_vesta_importlib}")
    # --- Camino 1: override local ya construido ------------------------------
    message(STATUS "extension vesta: usando libvesta local en ${_vesta_build}")
    _coffee_setup_vesta("${_vesta_importlib}" "${_vesta_include}")
    # libvesta y sus dependencias de runtime (OpenSSL) deben estar junto a la DLL
    # de la extension para que cargue en ejecucion.  Se copian tras el build.
    add_custom_command(TARGET coffee_vesta POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_vesta_build}/libvesta.dll" "${_vesta_outdir}/libvesta.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_vesta_build}/libssl-3-x64.dll" "${_vesta_outdir}/libssl-3-x64.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_vesta_build}/libcrypto-3-x64.dll" "${_vesta_outdir}/libcrypto-3-x64.dll"
        COMMENT "Copiando libvesta + deps junto a la extension vesta")

elseif(COFFEE_FETCH_VESTA)
    # --- Camino 2: clonar y compilar libvesta desde el repositorio -----------
    # Solo se referencia el target vesta_ffi; el resto de VestaVM queda inerte
    # (FetchContent_MakeAvailable no construye lo no referenciado).  Apagamos los
    # modulos prescindibles para acortar el build.
    message(STATUS "extension vesta: sin checkout local; clonando libvesta desde el repositorio")
    include(FetchContent)

    set(VESTA_BUILD_FFI          ON  CACHE BOOL "" FORCE)  # libvesta (lo que necesitamos)
    set(VESTA_BUILD_STDLIB       OFF CACHE BOOL "" FORCE)  # libreria estandar nativa (no requerida)
    set(VESTA_BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)  # ejemplos y plugins
    set(VESTA_BUILD_PREPROCESSOR ON  CACHE BOOL "" FORCE)  # vpp: lo enlaza vesta_ffi

    FetchContent_Declare(vestavm
        GIT_REPOSITORY https://github.com/desmonHak/VM.git
        GIT_TAG release
        GIT_SUBMODULES_RECURSE TRUE
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(vestavm)

    if(TARGET vesta_ffi)
        _coffee_setup_vesta(vesta_ffi "${vestavm_SOURCE_DIR}/include")
        add_dependencies(coffee_vesta vesta_ffi)
        # Copia la DLL de vesta_ffi (resuelta por generator-expression).  Las
        # DLLs de OpenSSL acompanan a libvesta: se intentan localizar y copiar.
        add_custom_command(TARGET coffee_vesta POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:vesta_ffi>" "${_vesta_outdir}/$<TARGET_FILE_NAME:vesta_ffi>"
            COMMENT "Copiando libvesta (vesta_ffi) junto a la extension vesta")
        if(WIN32 AND DEFINED OPENSSL_LIB_DIR)
            foreach(_ssl_dll libssl-3-x64.dll libcrypto-3-x64.dll)
                if(EXISTS "${OPENSSL_LIB_DIR}/${_ssl_dll}")
                    add_custom_command(TARGET coffee_vesta POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${OPENSSL_LIB_DIR}/${_ssl_dll}" "${_vesta_outdir}/${_ssl_dll}"
                        COMMENT "Copiando ${_ssl_dll} junto a la extension vesta")
                endif()
            endforeach()
        endif()
    else()
        message(WARNING "extension vesta omitida: el fetch de VestaVM no expuso el target vesta_ffi")
    endif()

else()
    # --- Camino 3: skip graceful ---------------------------------------------
    message(STATUS "extension vesta omitida: define VESTA_DIR (checkout local construido) o activa COFFEE_FETCH_VESTA")
endif()
