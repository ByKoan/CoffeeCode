/**
 * @file test_ext_errors.c
 * @brief Verifica que el extension host captura un MOTIVO legible en cada
 *        ruta de fallo de carga (lo que el IDE muestra al usuario).
 *
 * Tres casos, todos comprobando que @c ext_host_last_error devuelve un mensaje
 * NO vacio y RELEVANTE:
 *   (a) cargar un directorio inexistente -> el error menciona el manifiesto
 *       no encontrado;
 *   (b) cargar un dir con un manifiesto que apunta a una DLL inexistente -> el
 *       error menciona el fallo de carga de la DLL (texto + codigo del SO);
 *   (c) cargar una DLL real que NO exporta coffee_extension_register -> el
 *       error menciona que falta el simbolo de entrada.
 *
 * Los fixtures (un dir temporal con su coffee-extension.toml) se generan en
 * runtime bajo COFFEE_ERR_TMP_DIR, inyectado en compile time por CMake.
 */
#include "ctests.h"
#include "ext/ext_host.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define COFFEE_MKDIR(p) _mkdir(p)
#define COFFEE_DLL_SUFFIX ".dll"
#else
#include <sys/stat.h>
#include <sys/types.h>
#define COFFEE_MKDIR(p) mkdir((p), 0777)
#define COFFEE_DLL_SUFFIX ".so"
#endif

#ifndef COFFEE_ERR_TMP_DIR
#define COFFEE_ERR_TMP_DIR "."
#endif

/** Crea un CoffeeHost minimal (sin buffer ni hooks de UI). */
static CoffeeHost *make_host(void) {
    CoffeeHostBackend backend;
    memset(&backend, 0, sizeof backend);
    return ext_host_create(&backend);
}

/** Escribe @p content en @p path; 1 = ok. */
static int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fputs(content, f);
    fclose(f);
    return 1;
}

/** Copia binaria de @p src a @p dst; 1 = ok, 0 = fallo (p.ej. src ausente). */
static int copy_file_binary(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    char buf[8192];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = 0;
            break;
        }
    }
    fclose(in);
    fclose(out);
    return ok;
}

/**
 * @brief (a) Directorio inexistente -> error de manifiesto no encontrado.
 */
static void test_dir_inexistente(void) {
    CoffeeHost *host = make_host();
    EXPECT_NOT_NULL(host);

    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/no_existe_para_nada_12345", COFFEE_ERR_TMP_DIR);
    int rc = ext_host_load(host, dir);
    EXPECT_GT(0, rc); /* < 0: fallo */

    const char *err = ext_host_last_error(host);
    EXPECT_NOT_NULL(err);
    EXPECT_TRUE(err[0] != '\0');     /* mensaje NO vacio */
    EXPECT_CONTAINS(err, "manifiesto"); /* menciona el manifiesto ausente */

    ext_host_destroy(host);
}

/**
 * @brief (b) Manifiesto que apunta a una DLL inexistente -> error de carga de
 *        DLL con el texto del sistema operativo.
 */
static void test_dll_inexistente(void) {
    CoffeeHost *host = make_host();
    EXPECT_NOT_NULL(host);

    /* crear un dir de fixture con un manifiesto que referencia una DLL que no
     * existe en el disco. */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/fixture_dll_inexistente", COFFEE_ERR_TMP_DIR);
    COFFEE_MKDIR(dir);

    char manifest[1024];
    snprintf(manifest, sizeof(manifest), "%s/coffee-extension.toml", dir);
    EXPECT_TRUE(write_file(manifest,
                           "[extension]\n"
                           "id = \"fixture-bad-dll\"\n"
                           "entry = \"no_existe_esta_dll_98765.dll\"\n"
                           "abi = 1\n"));

    int rc = ext_host_load(host, dir);
    EXPECT_GT(0, rc);

    const char *err = ext_host_last_error(host);
    EXPECT_NOT_NULL(err);
    EXPECT_TRUE(err[0] != '\0');
    /* el mensaje debe mencionar que no se pudo cargar la DLL */
    EXPECT_CONTAINS(err, "cargar");

    ext_host_destroy(host);
}

/**
 * @brief (c) DLL real que NO exporta coffee_extension_register -> error de
 *        simbolo de entrada ausente.
 *
 * Copiamos una DLL del sistema (que carga sin problema pero no exporta el
 * entry) dentro del dir de fixture con un nombre relativo, y la referimos via
 * el manifiesto.  Asi el host la carga con exito y falla EXACTAMENTE en la
 * resolucion del simbolo coffee_extension_register, que es el camino que
 * queremos ejercitar.  Si la DLL fuente no esta disponible en el entorno, se
 * omite el caso (entornos minimos sin esa libreria).
 */
static void test_simbolo_ausente(void) {
    CoffeeHost *host = make_host();
    EXPECT_NOT_NULL(host);

    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/fixture_sin_entry", COFFEE_ERR_TMP_DIR);
    COFFEE_MKDIR(dir);

    /* nombre relativo de la copia dentro del fixture (lo que ira en `entry`). */
    const char *entry_name = "biblioteca_sin_entry" COFFEE_DLL_SUFFIX;
    char dst[1024];
    snprintf(dst, sizeof(dst), "%s/%s", dir, entry_name);

    /* candidatos de DLL del sistema que existen y cargan pero no exportan el
     * entry de la extension. */
#if defined(_WIN32)
    const char *candidates[] = {"C:\\Windows\\System32\\kernel32.dll",
                                "C:\\Windows\\System32\\msvcrt.dll", NULL};
#else
    const char *candidates[] = {"/lib/x86_64-linux-gnu/libc.so.6",
                                "/usr/lib/libc.so.6", "/lib/libc.so.6", NULL};
#endif
    int copied = 0;
    for (int i = 0; candidates[i]; ++i) {
        if (copy_file_binary(candidates[i], dst)) {
            copied = 1;
            break;
        }
    }
    if (!copied) {
        tt_skip("no hay una DLL del sistema sin entry disponible para copiar");
        ext_host_destroy(host);
        return;
    }

    char manifest[1024];
    snprintf(manifest, sizeof(manifest), "%s/coffee-extension.toml", dir);
    char content[512];
    snprintf(content, sizeof(content),
             "[extension]\n"
             "id = \"fixture-no-entry\"\n"
             "entry = \"%s\"\n"
             "abi = 1\n",
             entry_name);
    EXPECT_TRUE(write_file(manifest, content));

    int rc = ext_host_load(host, dir);
    EXPECT_GT(0, rc);

    const char *err = ext_host_last_error(host);
    EXPECT_NOT_NULL(err);
    EXPECT_TRUE(err[0] != '\0');
    /* el mensaje debe mencionar el simbolo de entrada ausente */
    EXPECT_CONTAINS(err, "coffee_extension_register");

    ext_host_destroy(host);
}

int main(void) {
    tt_suite("ext_errors");
    tt_run("dir inexistente -> error de manifiesto", test_dir_inexistente);
    tt_run("DLL inexistente -> error de carga de DLL (codigo del SO)",
           test_dll_inexistente);
    tt_run("DLL sin coffee_extension_register -> error de simbolo de entrada",
           test_simbolo_ausente);
    return tt_summary();
}
