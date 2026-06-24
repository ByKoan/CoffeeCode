/**
 * @file test_image_load.c
 * @brief Prueba headless de la carga de imagenes con SDL_image.
 *
 * El fondo del editor admite imagenes PNG, JPG y BMP.  Aqui se verifica, sin
 * abrir ventana ni renderer (IMG_Load devuelve una SDL_Surface en CPU, no
 * necesita video), que los tres formatos se decodifican a una superficie
 * valida con las dimensiones esperadas.  Asi se detecta en CI cualquier
 * regresion en la configuracion de SDL_image (p. ej. que el backend stb deje
 * de compilar el decodificador de PNG o JPG).
 *
 * Las rutas a los ficheros de prueba (fixtures) las inyecta CMake en compile
 * mediante COFFEE_IMG_FIXTURE_DIR, apuntando al arbol de fuentes del test.
 */
#include "ctests.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <stdio.h>
#include <string.h>

#ifndef COFFEE_IMG_FIXTURE_DIR
#error "Define COFFEE_IMG_FIXTURE_DIR con la ruta del directorio de fixtures"
#endif

/**
 * @brief Carga @p name desde el directorio de fixtures y comprueba que la
 *        superficie resultante es valida y mide 8x8.
 *
 * @return 1 si IMG_Load tuvo exito y las dimensiones coinciden; 0 si no.
 */
static int load_fixture_8x8(const char *name) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", COFFEE_IMG_FIXTURE_DIR, name);

    SDL_Surface *surf = IMG_Load(path);
    if (!surf) {
        fprintf(stderr, "IMG_Load(%s) fallo: %s\n", path, SDL_GetError());
        return 0;
    }
    int ok = (surf->w == 8 && surf->h == 8);
    if (!ok)
        fprintf(stderr, "%s tiene %dx%d (esperado 8x8)\n", name, surf->w,
                surf->h);
    SDL_DestroySurface(surf);
    return ok;
}

/** PNG: debe cargar (backend stb de SDL_image). */
static void test_load_png(void) { EXPECT_TRUE(load_fixture_8x8("sample.png")); }

/** JPG: debe cargar (backend stb de SDL_image). */
static void test_load_jpg(void) { EXPECT_TRUE(load_fixture_8x8("sample.jpg")); }

/** BMP: debe cargar (decodificador nativo de SDL_image). */
static void test_load_bmp(void) { EXPECT_TRUE(load_fixture_8x8("sample.bmp")); }

/** Una ruta inexistente debe fallar limpiamente (NULL, sin crash). */
static void test_load_missing(void) {
    char path[1024];
    snprintf(path, sizeof path, "%s/no_existe.png", COFFEE_IMG_FIXTURE_DIR);
    SDL_Surface *surf = IMG_Load(path);
    EXPECT_TRUE(surf == NULL);
    if (surf) SDL_DestroySurface(surf);
}

int main(void) {
    tt_suite("image_load");
    tt_run("PNG carga (8x8)", test_load_png);
    tt_run("JPG carga (8x8)", test_load_jpg);
    tt_run("BMP carga (8x8)", test_load_bmp);
    tt_run("ruta inexistente devuelve NULL", test_load_missing);
    return tt_summary();
}
