cmake_minimum_required(VERSION 3.24)
project(CoffeeCode C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# compile_commands.json para indexado/autocompletado preciso en CLion, VS Code y clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Tipo de build por defecto cuando no se pasa -DCMAKE_BUILD_TYPE.
# La optimización la dicta el tipo de build (Release => -O3, Debug => -g -O0)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build (Debug/Release/RelWithDebInfo)" FORCE)
endif()

# Modo "native": optimiza para la microarquitectura de ESTA máquina (-march=native).
# El binario resultante puede no ejecutar en otras CPUs. Lo activa Compile.bat native.
option(COFFEE_NATIVE     "Compilar optimizado para la CPU local (-march=native)" OFF)

# Sanitizers (ASan + UBSan) en builds Debug. OFF por defecto: en TDM-GCC/Windows
# puede no enlazar por falta de runtime; funciona en Linux o con Clang.
option(COFFEE_SANITIZE   "Activar AddressSanitizer + UBSan en Debug" OFF)

# Compilar la batería de tests unitarios (con la librería ctests, auto-descargada).
option(COFFEE_BUILD_TESTS "Compilar los tests unitarios (ctests)" ON)

# Compilar la aplicación CoffeeCode (y sus dependencias SDL3/SDL3_ttf).
option(COFFEE_BUILD_APP  "Compilar la aplicación CoffeeCode (requiere SDL)" ON)

# Force static everywhere — must be set before any FetchContent configures
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# Silenciar warnings de dependencias externas (SDL, freetype, libjpeg…)
# Se aplica ANTES de FetchContent para que las subdeps lo hereden.
# Solo afecta a compiladores GCC/Clang; MSVC usa /w directamente.
# ---------------------------------------------------------------------------
if(NOT MSVC)
    # Quitar -Wall/-Wextra del scope global para que las subdeps no los vean
    string(REPLACE "-Wall"   "" CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}")
    string(REPLACE "-Wextra" "" CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}")
    string(REPLACE "-Wall"   "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "-Wextra" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    # Añadir -w al scope global: silencia warnings en TODAS las subdeps
    # (se sobreescribirá solo para CoffeeCode con -Wall más adelante)
    add_compile_options(-w)
else()
    add_compile_options(/w)
endif()

include(FetchContent)

# ===========================================================================
# Aplicación CoffeeCode (SDL3 / SDL3_ttf / SDL3_image + ejecutable)
# ===========================================================================
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

set(SDLTTF_VENDORED ON  CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES  OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL  OFF CACHE BOOL "" FORCE)
add_subdirectory("${sdl3_ttf_SOURCE_DIR}" "${sdl3_ttf_BINARY_DIR}")

# -- SDL3_image 3.2.0 ---------------------------------------------------------
# SDLIMAGE_BACKEND_STB=ON  → usa stb_image (C puro) para JPEG y PNG.
#   Efecto: NO se compila libjpeg-turbo ni libpng vendorizados, por lo que
#   NO se necesita Perl (que libjpeg-turbo usa para generar tablas SIMD).
#
# AVIF y TIFF los desactivamos porque su cadena de deps (libavif/dav1d/libtiff)
# es enorme, lenta de compilar y no la necesitamos en un editor de código.
# WEBP también lo desactivamos (libwebp vendorizado añade minutos de build).
# Los formatos simples sin deps pesadas (BMP, GIF, QOI, TGA, SVG…) se mantienen.
FetchContent_Declare(
    SDL3_image
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
    GIT_TAG        release-3.2.0
    GIT_SHALLOW    FALSE
)
set(SDLIMAGE_VENDORED      ON  CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES       OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_INSTALL       OFF CACHE BOOL "" FORCE)

# Usar stb_image para JPEG+PNG → elimina libjpeg-turbo → elimina Perl
set(SDLIMAGE_BACKEND_STB   ON  CACHE BOOL "" FORCE)

# Desactivar formatos con dependencias pesadas que no necesita un editor de código
set(SDLIMAGE_AVIF          OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF           OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP          OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JXL           OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3_image)

# -- Fuentes ------------------------------------------------------------------
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

add_custom_command(TARGET CoffeeCode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/font.ttf"
            "$<TARGET_FILE_DIR:CoffeeCode>/font.ttf"
    COMMENT "Copiando font.ttf junto al ejecutable")

target_include_directories(CoffeeCode PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(CoffeeCode PRIVATE SDL3::SDL3-static SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image)

# -- Platform -----------------------------------------------------------------
if(WIN32)
    set_target_properties(CoffeeCode PROPERTIES
        WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
    if(MSVC)
        target_link_options(CoffeeCode PRIVATE /ENTRY:mainCRTStartup)
    else()
        target_link_options(CoffeeCode PRIVATE -Wl,--entry,mainCRTStartup)
    endif()
    target_link_libraries(CoffeeCode PRIVATE winmm imm32 version setupapi
                                             ole32 uuid shell32)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(CoffeeCode PRIVATE m dl pthread)
endif()

# -- Flags de compilación (SOLO para CoffeeCode, no para las deps) ------------
if(MSVC)
    target_compile_options(CoffeeCode PRIVATE /W3 /utf-8)
    target_compile_definitions(CoffeeCode PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(COFFEE_NATIVE)
        target_compile_options(CoffeeCode PRIVATE /arch:AVX2)
    endif()
else()
    # Reactivar warnings SOLO para nuestro código (sobreescribe el -w global)
    target_compile_options(CoffeeCode PRIVATE
        -Wall
        -Wno-unused-function
        -Wno-unused-parameter
        -Wno-stringop-truncation)
    if(COFFEE_NATIVE)
        target_compile_options(CoffeeCode PRIVATE -march=native -mtune=native)
    endif()
    target_compile_options(CoffeeCode PRIVATE
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Debug>:-ggdb3>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-gdwarf-4>)
endif()

target_compile_definitions(CoffeeCode PRIVATE $<$<CONFIG:Debug>:COFFEE_DEBUG>)

# -- Sanitizers ---------------------------------------------------------------
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
                        "(falta libasan/libubsan). Se ignoran.")
    endif()
endif()

# -- LTO / IPO ----------------------------------------------------------------
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

# -- Tests unitarios ----------------------------------------------------------
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

# -- Resumen ------------------------------------------------------------------
message(STATUS "----------------------------------------------")
message(STATUS " CoffeeCode")
message(STATUS "   build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "   native       : ${COFFEE_NATIVE}")
message(STATUS "   sanitizers   : ${COFFEE_SANITIZE}")
message(STATUS "   tests        : ${COFFEE_BUILD_TESTS}")
message(STATUS "   LTO (Release): ${COFFEE_IPO_OK}")
message(STATUS "----------------------------------------------")
