cmake_minimum_required(VERSION 3.24)
project(ide C)

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
set(SDL3TTF_VENDORED ON  CACHE BOOL "" FORCE)
set(SDL3TTF_SAMPLES  OFF CACHE BOOL "" FORCE)
set(SDL3TTF_INSTALL  OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG        release-3.2.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL3_ttf)

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

add_executable(ide ${SOURCES})

# SDL3_ttf creates SDL3_ttf::SDL3_ttf (alias to whichever variant was built)
target_link_libraries(ide PRIVATE SDL3::SDL3-static SDL3_ttf::SDL3_ttf)

# ── Platform ─────────────────────────────────────────────────────────────────
if(WIN32)
    set_target_properties(ide PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(ide PRIVATE /ENTRY:mainCRTStartup)
    target_link_libraries(ide PRIVATE winmm imm32 version setupapi)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(ide PRIVATE m dl pthread)
endif()

# ── Fuente embebida en el binario (font_data.h generado en configure time) ──
# No se necesita copiar font.ttf junto al ejecutable.

# ── Compile flags ────────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(ide PRIVATE /W3 /utf-8)
    target_compile_definitions(ide PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(ide PRIVATE -Wall -Wextra -O2)
endif()
