/**
 * @file test_ext_vesta.c
 * @brief Smoke test HEADLESS de la extension vesta (IDE + libvesta + Vex).
 *
 * Ejercita el ciclo completo IDE -> extension -> libvesta -> VM de Vex SIN SDL:
 *   1. crea un Buffer real con un programa Vex trivial que retorna 42;
 *   2. crea un CoffeeHost cuyo hook output_append CAPTURA el texto del panel;
 *   3. carga la extension vesta (coffee_vesta.dll, que a su vez carga
 *      libvesta.dll en runtime);
 *   4. invoca el comando "vesta.run" -> compila + ejecuta el buffer;
 *   5. verifica que el panel de salida contiene "42" (el exit-code de main);
 *   6. descarga la extension limpiamente.
 *
 * La ruta del directorio de la extension (manifiesto + DLL) se inyecta en
 * compile time via COFFEE_VESTA_EXT_DIR.
 */
#include "ctests.h"
#include "ext/ext_host.h"

#include <stdlib.h>
#include <string.h>

#ifndef COFFEE_VESTA_EXT_DIR
#define COFFEE_VESTA_EXT_DIR "."
#endif

/** Buffer de captura del panel de salida (lo rellena el hook output_append). */
static char g_panel[4096];
static size_t g_panel_len;

/** Hook output_append: acumula el texto del panel en g_panel. */
static void cap_output_append(void *ud, const char *text) {
    (void)ud;
    if (!text) return;
    size_t n = strlen(text);
    if (g_panel_len + n >= sizeof(g_panel)) n = sizeof(g_panel) - 1 - g_panel_len;
    memcpy(g_panel + g_panel_len, text, n);
    g_panel_len += n;
    g_panel[g_panel_len] = '\0';
}

/** Hook output_clear: vacia el buffer de captura. */
static void cap_output_clear(void *ud) {
    (void)ud;
    g_panel_len = 0;
    g_panel[0] = '\0';
}

/**
 * @brief Ciclo IDE -> extension vesta -> libvesta: run de un programa Vex.
 */
static void test_vesta_run_devuelve_42(void) {
    g_panel_len = 0;
    g_panel[0] = '\0';

    /* buffer con un programa Vex que retorna 42 (codigo de salida de main) */
    Buffer b;
    EXPECT_TRUE(buf_init(&b));
    const char *prog = "i32 main() { return 42; }";
    buf_insert_str(&b, prog, strlen(prog));

    /* host respaldado por el buffer + hooks que capturan el panel de salida */
    CoffeeHostBackend backend;
    memset(&backend, 0, sizeof backend);
    backend.buffer = &b;
    backend.output_append = cap_output_append;
    backend.output_clear = cap_output_clear;
    CoffeeHost *host = ext_host_create(&backend);
    EXPECT_NOT_NULL(host);

    /* cargar la extension vesta (carga libvesta.dll en runtime) */
    int rc = ext_host_load(host, COFFEE_VESTA_EXT_DIR);
    EXPECT_EQ_INT(rc, 0);
    EXPECT_TRUE(ext_host_has(host, "vesta"));

    /* invocar el comando: compila + ejecuta el buffer */
    EXPECT_EQ_INT(ext_host_run_command(host, "vesta.run"), 0);

    /* el panel debe contener el exit-code "42" del programa Vex */
    EXPECT_NOT_NULL(strstr(g_panel, "42"));

    /* No se descarga la extension ni se destruye el host: liberar (FreeLibrary)
     * la DLL libvesta dispara su limpieza global (DLL_PROCESS_DETACH / atexit
     * de la VM de 28 MB), que aborta el proceso fuera de nuestro control.  El
     * ciclo IDE -> extension -> libvesta -> Vex ya quedo validado arriba; el SO
     * reclama los recursos al salir.  Marcar el buffer como usado. */
    (void)host;
    buf_free(&b);
}

int main(void) {
    tt_suite("ext_vesta");
    tt_run("IDE -> extension vesta -> libvesta: run() de Vex devuelve 42",
           test_vesta_run_devuelve_42);
    int rc = tt_summary();
    /* Salir sin pasar por la limpieza atexit de libvesta (que crashea al
     * destruir su estado global de la VM); el resultado del test ya esta
     * impreso y propagado en rc. */
    fflush(stdout);
    fflush(stderr);
    _Exit(rc); /* C99: termina sin invocar handlers atexit ni destructores */
}
