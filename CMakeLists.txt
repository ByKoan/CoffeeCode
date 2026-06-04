cmake_minimum_required(VERSION 3.24)
project(CoffeeCode C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Force static everywhere — must be set before any FetchContent configures
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)

# ── SDL3 3.2.6 ───────────────────────────────────────────────────────────────
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

# ── Intercept SDL3_ttf's find_package(SDL3) via redirect ─────────────────────
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

# ── SDL3_ttf 3.2.2 ───────────────────────────────────────────────────────────
# Descargamos SDL3_ttf primero sin configurar para poder parchear su CMakeLists
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

# ── Generar font_data.h embebiendo assets/font.ttf ───────────────────────────
set(FONT_TTF "${CMAKE_SOURCE_DIR}/assets/font.ttf")
set(FONT_DATA_H "${CMAKE_SOURCE_DIR}/src/font_data.h")

if(NOT EXISTS "${FONT_DATA_H}")
    message(STATUS "Generando font_data.h desde font.ttf...")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -DFONT_TTF=${FONT_TTF}
            -DFONT_DATA_H=${FONT_DATA_H}
            -P "${CMAKE_SOURCE_DIR}/EmbedFont.cmake"
    )
endif()

# ── Executable ───────────────────────────────────────────────────────────────
file(GLOB SOURCES "src/*.c")

add_executable(CoffeeCode ${SOURCES})

target_link_libraries(CoffeeCode PRIVATE SDL3::SDL3-static SDL3_ttf::SDL3_ttf)

# ── Platform ─────────────────────────────────────────────────────────────────
if(WIN32)
    set_target_properties(CoffeeCode PROPERTIES WIN32_EXECUTABLE TRUE)
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

# ── Compile flags ────────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(CoffeeCode PRIVATE /W3 /utf-8)
    target_compile_definitions(CoffeeCode PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(CoffeeCode PRIVATE -Wall -O2
        -Wno-unused-function
        -Wno-unused-parameter
        -Wno-stringop-truncation)
endif()
