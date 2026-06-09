cmake_minimum_required(VERSION 3.24)
project(CoffeeCode C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# compile_commands.json para indexado/autocompletado preciso en CLion, VS Code y clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Tipo de build por defecto cuando no se pasa -DCMAKE_BUILD_TYPE.
# La optimización la dicta el tipo de build (Release => -O3, Debug => -g -O0).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build (Debug/Release/RelWithDebInfo)" FORCE)
endif()

# Modo "native": optimiza para la microarquitectura de ESTA máquina (-march=native).
# El binario resultante puede no ejecutar en otras CPUs. Lo activa Compile.bat native.
option(COFFEE_NATIVE "Compilar optimizado para la CPU local (-march=native)" OFF)

# Sanitizers (ASan + UBSan) en builds Debug. OFF por defecto: en TDM-GCC/Windows
# puede no enlazar por falta de runtime; funciona en Linux o con Clang.
option(COFFEE_SANITIZE "Activar AddressSanitizer + UBSan en Debug" OFF)

# Compilar la batería de tests unitarios (con la librería ctests, auto-descargada).
# ON por defecto; desactívala con -DCOFFEE_BUILD_TESTS=OFF si solo quieres el editor.
option(COFFEE_BUILD_TESTS "Compilar los tests unitarios (ctests)" ON)

# Compilar la aplicación CoffeeCode (y sus dependencias SDL3/SDL3_ttf). Ponlo en
# OFF para compilar SOLO los tests, sin descargar ni construir SDL (mucho más
# rápido). Lo usa "Compile.bat test".
option(COFFEE_BUILD_APP "Compilar la aplicación CoffeeCode (requiere SDL)" ON)

# Force static everywhere — must be set before any FetchContent configures
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)

# === Aplicación CoffeeCode (SDL3/SDL3_ttf + el ejecutable) ===================
# Todo este bloque solo se procesa si COFFEE_BUILD_APP=ON. Con OFF se omite la
# descarga de SDL y la app, dejando únicamente los tests (build mucho más ligero).
if(COFFEE_BUILD_APP)

# -- SDL3 3.2.6 ---------------------------------------------------------------
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL3)

# -- Intercept SDL3_ttf's find_package(SDL3) via redirect ---------------------
set(CMAKE_FIND_PACKAGE_REDIRECTS_DIR "${CMAKE_BINARY_DIR}/redirects")
file(MAKE_DIRECTORY "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}")
file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/SDL3-config.cmake" [[
if(TARGET SDL3::SDL3-static AND NOT TARGET SDL3::SDL3)
    add_library(SDL3::SDL3 ALIAS SDL3::SDL3-static)
endif()
set(SDL3_FOUND             TRUE)
set(SDL3_Headers_FOUND     TRUE)
set(SDL3_SDL3_FOUND        TRUE)
set(SDL3_SDL3-static_FOUND TRUE)
set(SDL3_LIBRARIES         SDL3::SDL3)
set(SDL3_STATIC_LIBRARIES  SDL3::SDL3-static)
]])
file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/SDL3-config-version.cmake" [[
set(PACKAGE_VERSION "3.2.6")
set(PACKAGE_VERSION_COMPATIBLE TRUE)
set(PACKAGE_VERSION_EXACT TRUE)
]])

# -- SDL3_ttf 3.2.2 -----------------------------------------------------------
# Usamos el flujo Declare+Populate+add_subdirectory para poder compilar
# Freetype vendorizado antes de que SDL3_ttf lo busque.  CMP0169=OLD suprime
# el warning de deprecación de FetchContent_Populate con detalles declarados.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

FetchContent_Declare(
    SDL3_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG        release-3.2.2
    GIT_SHALLOW    FALSE
)
FetchContent_Populate(SDL3_ttf)

# Compilar freetype vendored ANTES de que SDL3_ttf lo busque
set(FREETYPE_SRC "${sdl3_ttf_SOURCE_DIR}/external/freetype")
set(FREETYPE_BINARY "${CMAKE_BINARY_DIR}/_deps/freetype-build")

# Deshabilitar dependencias opcionales de freetype que no tenemos
set(FT_DISABLE_ZLIB    TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2   TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG     TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI  TRUE CACHE BOOL "" FORCE)
add_subdirectory("${FREETYPE_SRC}" "${FREETYPE_BINARY}")

# Crear alias y variables que find_package(Freetype REQUIRED) espera encontrar
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
set(FREETYPE_FOUND        TRUE  CACHE BOOL   "" FORCE)
set(FREETYPE_LIBRARY      freetype CACHE STRING "" FORCE)
set(FREETYPE_LIBRARIES    freetype CACHE STRING "" FORCE)
set(FREETYPE_INCLUDE_DIRS "${FREETYPE_SRC}/include" CACHE STRING "" FORCE)
set(FREETYPE_INCLUDE_DIR  "${FREETYPE_SRC}/include" CACHE STRING "" FORCE)
set(FREETYPE_VERSION_STRING "2.13" CACHE STRING "" FORCE)

# Escribir el redirect ANTES de add_subdirectory(SDL3_ttf)
file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/freetype-config.cmake" "
set(FREETYPE_FOUND TRUE)
set(FREETYPE_LIBRARIES freetype)
set(FREETYPE_INCLUDE_DIRS \"${FREETYPE_SRC}/include\")
set(FREETYPE_INCLUDE_DIR  \"${FREETYPE_SRC}/include\")
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
")
file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/freetype-config-version.cmake" "
set(PACKAGE_VERSION \"2.13\")
set(PACKAGE_VERSION_COMPATIBLE TRUE)
set(PACKAGE_VERSION_EXACT TRUE)
")

# Ahora configurar SDL3_ttf — encontrará Freetype via cache+redirect
set(SDL3TTF_VENDORED  ON  CACHE BOOL "" FORCE)
set(SDL3TTF_SAMPLES   OFF CACHE BOOL "" FORCE)
set(SDL3TTF_INSTALL   OFF CACHE BOOL "" FORCE)
set(SDL3TTF_HARFBUZZ  OFF CACHE BOOL "" FORCE)
set(SDL3TTF_PLUTOSVG  OFF CACHE BOOL "" FORCE)
add_subdirectory("${sdl3_ttf_SOURCE_DIR}" "${sdl3_ttf_BINARY_DIR}")

# -- Icono (.ico en la raíz del proyecto) -------------------------------------
# Fuentes: main.c en la raíz de src/ + cada módulo en src/<modulo>/<modulo>.c
# CONFIGURE_DEPENDS: CMake re-escanea en cada build, así un módulo nuevo
# (src/<modulo>/<modulo>.c) se recoge sin reconfigurar a mano.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.c")

if(WIN32)
    set(ICON_ICO "${CMAKE_SOURCE_DIR}/assets/icono.ico")
    if(EXISTS "${ICON_ICO}")
        configure_file(
            "${CMAKE_SOURCE_DIR}/src/app.rc"
            "${CMAKE_BINARY_DIR}/app_configured.rc"
            @ONLY
        )
        list(APPEND SOURCES "${CMAKE_BINARY_DIR}/app_configured.rc")
        # windres no declara que el recurso depende del .ico. Lo hacemos explicito
        # para que cambiar el icono fuerce recompilar el .rc (si no, queda cacheado).
        set_source_files_properties(
            "${CMAKE_BINARY_DIR}/app_configured.rc"
            PROPERTIES OBJECT_DEPENDS "${ICON_ICO}"
        )
        message(STATUS "Icono embebido: ${ICON_ICO}")
    else()
        message(WARNING "No se encontró icono.ico en la raíz del proyecto.")
    endif()
endif()

add_executable(CoffeeCode ${SOURCES})

# La fuente ya no va embebida: se carga en runtime (SDL_GetBasePath()/font.ttf).
# Copiamos font.ttf junto al ejecutable tras compilar.
add_custom_command(TARGET CoffeeCode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/font.ttf"
            "$<TARGET_FILE_DIR:CoffeeCode>/font.ttf"
    COMMENT "Copiando font.ttf junto al ejecutable")

# Cabeceras en include/, organizadas en subcarpetas por módulo
# (los includes usan prefijo de módulo: #include "editor/editor.h")
target_include_directories(CoffeeCode PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(CoffeeCode PRIVATE SDL3::SDL3-static SDL3_ttf::SDL3_ttf)

# -- Platform -----------------------------------------------------------------
if(WIN32)
    # Consola visible en Debug (ver stdout/printf); ventana sin consola en el resto
    set_target_properties(CoffeeCode PROPERTIES
        WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
    if(MSVC)
        target_link_options(CoffeeCode PRIVATE /ENTRY:mainCRTStartup)
    else()
        target_link_options(CoffeeCode PRIVATE -Wl,--entry,mainCRTStartup)
    endif()
    target_link_libraries(CoffeeCode PRIVATE winmm imm32 version setupapi)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(CoffeeCode PRIVATE m dl pthread)
endif()

# -- Compile flags ------------------------------------------------------------
if(MSVC)
    target_compile_options(CoffeeCode PRIVATE /W3 /utf-8)
    target_compile_definitions(CoffeeCode PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(COFFEE_NATIVE)
        # MSVC no tiene -march=native; AVX2 es la aproximación razonable.
        target_compile_options(CoffeeCode PRIVATE /arch:AVX2)
    endif()
else()
    # Sin -O fijo: el nivel de optimización lo aporta CMAKE_BUILD_TYPE.
    target_compile_options(CoffeeCode PRIVATE -Wall
        -Wno-unused-function
        -Wno-unused-parameter
        -Wno-stringop-truncation)
    if(COFFEE_NATIVE)
        target_compile_options(CoffeeCode PRIVATE -march=native -mtune=native)
    endif()
    # Info de depuración rica SOLO en Debug: macros (-g3), formato gdb (-ggdb3),
    # sin optimizar (-O0), frames fiables y DWARF-4 (compatible con el gdb de CLion/MinGW).
    target_compile_options(CoffeeCode PRIVATE
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Debug>:-ggdb3>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-gdwarf-4>)
endif()

# Define COFFEE_DEBUG solo en builds Debug (asserts / logging condicional en el código)
target_compile_definitions(CoffeeCode PRIVATE $<$<CONFIG:Debug>:COFFEE_DEBUG>)

# Sanitizers (ASan + UBSan) en Debug cuando COFFEE_SANITIZE=ON. Antes de aplicarlos
# se comprueba que el toolchain pueda enlazarlos (TDM-GCC/MinGW no suele traer
# libasan): si no, se avisa y se ignoran, en vez de fallar con un error de enlace.
if(COFFEE_SANITIZE AND NOT MSVC)
    include(CheckLinkerFlag)
    check_linker_flag(C "-fsanitize=address,undefined" COFFEE_SAN_LINKS)
    if(COFFEE_SAN_LINKS)
        target_compile_options(CoffeeCode PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>
            $<$<CONFIG:Debug>:-fno-sanitize-recover=all>)
        target_link_options(CoffeeCode PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
        message(STATUS "Sanitizers: ASan+UBSan activos en Debug")
    else()
        message(WARNING "COFFEE_SANITIZE=ON pero el toolchain no enlaza sanitizers "
                        "(falta libasan/libubsan). Se ignoran. Usa Clang/LLVM-MinGW "
                        "o compila en Linux para ASan/UBSan.")
    endif()
endif()

# -- LTO / IPO (optimización entre módulos) -----------------------------------
# Se activa en Release y RelWithDebInfo, lo que cubre los modos release y native
# de Compile.bat (ambos compilan con CMAKE_BUILD_TYPE=Release).
include(CheckIPOSupported)
check_ipo_supported(RESULT COFFEE_IPO_OK OUTPUT COFFEE_IPO_MSG)
if(COFFEE_IPO_OK)
    set_target_properties(CoffeeCode PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE        TRUE
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
else()
    message(STATUS "LTO no disponible: ${COFFEE_IPO_MSG}")
endif()

endif() # COFFEE_BUILD_APP

# -- Tests unitarios (ctests) -------------------------------------------------
# La librería de tests se auto-descarga con FetchContent (no hay que instalarla).
# Solo cubre los módulos "puros" (sin SDL): structs, buffer y lexer.
# Ejecuta la batería con:   ctest --test-dir <build> --output-on-failure
if(COFFEE_BUILD_TESTS)
    FetchContent_Declare(
        ctests
        GIT_REPOSITORY https://github.com/desmonHak/ctests.git
        GIT_TAG        v1.0.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ctests)
    enable_testing()
    add_subdirectory(test)
endif()

# -- Resumen de configuración -------------------------------------------------
message(STATUS "----------------------------------------------")
message(STATUS " CoffeeCode")
message(STATUS "   build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "   native       : ${COFFEE_NATIVE}")
message(STATUS "   sanitizers   : ${COFFEE_SANITIZE}")
message(STATUS "   tests        : ${COFFEE_BUILD_TESTS}")
message(STATUS "   LTO (Release): ${COFFEE_IPO_OK}")
message(STATUS "----------------------------------------------")
