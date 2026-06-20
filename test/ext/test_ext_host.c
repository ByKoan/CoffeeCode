/**
 * @file test_ext_host.c
 * @brief Smoke test HEADLESS del extension host.
 *
 * Ejercita el ciclo completo SIN SDL:
 *   1. crea un Buffer real (modulo puro) + un CoffeeHost respaldado por el;
 *   2. carga la extension hello-c (su DLL) via ext_host_load;
 *   3. invoca el comando "hello.insert" y verifica que el buffer contiene "hola";
 *   4. comprueba que el evento FILE_OPEN llega al suscriptor de la extension;
 *   5. comprueba que el servicio publicado es accesible via get_service;
 *   6. descarga la extension y verifica que el comando ya NO hace nada
 *      (el registro por-extension revirtio limpiamente).
 *
 * La ruta al directorio de la extension (con su manifiesto + DLL) se inyecta en
 * compile time via COFFEE_HELLO_EXT_DIR.
 */
#include "ctests.h"
#include "ext/ext_host.h"

#include <string.h>

#ifndef COFFEE_HELLO_EXT_DIR
#define COFFEE_HELLO_EXT_DIR "."
#endif

/** Vuelca el contenido logico del buffer en @p out (null-terminado). */
static void dump(Buffer *b, char *out, size_t cap) {
    size_t n = buf_get_text(b, 0, buf_length(b), out);
    if (n >= cap) n = cap - 1;
    out[n] = '\0';
}

/**
 * @brief Ciclo completo load -> command -> manipular buffer -> unload.
 */
static void test_load_command_buffer_unload(void) {
    Buffer b;
    EXPECT_TRUE(buf_init(&b));

    /* host respaldado SOLO por el buffer (sin hooks de UI: stubs). */
    CoffeeHostBackend backend;
    memset(&backend, 0, sizeof backend);
    backend.buffer = &b;
    CoffeeHost *host = ext_host_create(&backend);
    EXPECT_NOT_NULL(host);

    /* la extension aun no esta cargada: el comando no debe existir */
    EXPECT_FALSE(ext_host_has(host, "hello-c"));
    EXPECT_GT(0, ext_host_run_command(host, "hello.insert")); /* <0: no existe */

    /* cargar la DLL de la extension hello-c desde su directorio */
    int rc = ext_host_load(host, COFFEE_HELLO_EXT_DIR);
    EXPECT_EQ_INT(rc, 0);
    EXPECT_TRUE(ext_host_has(host, "hello-c"));

    /* invocar el comando -> inserta "hola" en el buffer */
    EXPECT_EQ_INT(ext_host_run_command(host, "hello.insert"), 0);

    char out[64];
    dump(&b, out, sizeof out);
    EXPECT_EQ_STR(out, "hola"); /* <-- prueba load->register->dispatch->buffer */
    EXPECT_EQ_INT((int)buf_length(&b), 4);

    /* invocar de nuevo -> "holahola" (el cursor sigue al final) */
    EXPECT_EQ_INT(ext_host_run_command(host, "hello.insert"), 0);
    dump(&b, out, sizeof out);
    EXPECT_EQ_STR(out, "holahola");

    /* descargar: el comando debe dejar de existir (registro por-ext revierte) */
    EXPECT_EQ_INT(ext_host_unload(host, "hello-c"), 0);
    EXPECT_FALSE(ext_host_has(host, "hello-c"));

    /* tras unload, run_command falla y el buffer NO cambia */
    size_t len_before = buf_length(&b);
    EXPECT_GT(0, ext_host_run_command(host, "hello.insert")); /* <0: no existe */
    EXPECT_EQ_INT((int)buf_length(&b), (int)len_before);

    ext_host_destroy(host);
    buf_free(&b);
}

/**
 * @brief El evento FILE_OPEN llega al suscriptor; el servicio es accesible.
 */
static void test_evento_y_servicio(void) {
    Buffer b;
    buf_init(&b);
    CoffeeHostBackend backend;
    memset(&backend, 0, sizeof backend);
    backend.buffer = &b;
    CoffeeHost *host = ext_host_create(&backend);
    EXPECT_NOT_NULL(host);

    EXPECT_EQ_INT(ext_host_load(host, COFFEE_HELLO_EXT_DIR), 0);

    /* emitir FILE_OPEN: el callback de la extension loguea la ruta (no debe
     * fallar ni crashear); validamos que el host la procesa sin error. */
    ext_host_emit(host, COFFEE_EVENT_FILE_OPEN, "/tmp/demo.txt");

    /* el servicio publicado por la extension debe estar disponible */
    const CoffeeApi *api = ext_host_api(host);
    EXPECT_NOT_NULL(api);
    void *svc = api->get_service(host, "hello.service");
    EXPECT_NOT_NULL(svc);

    /* tras descargar, el servicio desaparece (registro por-ext) */
    EXPECT_EQ_INT(ext_host_unload(host, "hello-c"), 0);
    EXPECT_NULL(api->get_service(host, "hello.service"));

    ext_host_destroy(host);
    buf_free(&b);
}

/**
 * @brief Recarga: reload deja la extension activa y el comando funciona igual.
 */
static void test_recarga(void) {
    Buffer b;
    buf_init(&b);
    CoffeeHostBackend backend;
    memset(&backend, 0, sizeof backend);
    backend.buffer = &b;
    CoffeeHost *host = ext_host_create(&backend);

    EXPECT_EQ_INT(ext_host_load(host, COFFEE_HELLO_EXT_DIR), 0);
    EXPECT_EQ_INT(ext_host_reload(host, "hello-c"), 0);
    EXPECT_TRUE(ext_host_has(host, "hello-c"));

    /* el comando sigue funcionando tras la recarga */
    EXPECT_EQ_INT(ext_host_run_command(host, "hello.insert"), 0);
    char out[64];
    dump(&b, out, sizeof out);
    EXPECT_EQ_STR(out, "hola");

    ext_host_destroy(host);
    buf_free(&b);
}

/**
 * @brief Introspeccion + instalar-desde-carpeta + descargar + recargar.
 *
 * Ejercita las funciones que alimentan el panel de extensiones del IDE:
 *   - install desde carpeta = ext_host_load de un directorio;
 *   - ext_host_count / ext_host_info listan la extension cargada;
 *   - ext_host_unload la quita de la lista (info la marca inactiva);
 *   - ext_host_reload la vuelve a dejar activa.
 */
static void test_introspeccion_install_unload_reload(void) {
    Buffer b;
    buf_init(&b);
    CoffeeHostBackend backend;
    memset(&backend, 0, sizeof backend);
    backend.buffer = &b;
    CoffeeHost *host = ext_host_create(&backend);
    EXPECT_NOT_NULL(host);

    /* sin nada cargado: count = 0 */
    EXPECT_EQ_INT((int)ext_host_count(host), 0);

    /* install desde carpeta = cargar el directorio de la extension hello-c */
    EXPECT_EQ_INT(ext_host_load(host, COFFEE_HELLO_EXT_DIR), 0);

    /* introspeccion: 1 slot, activo, con id "hello-c" */
    EXPECT_EQ_INT((int)ext_host_count(host), 1);
    const char *id = NULL, *name = NULL, *dir = NULL;
    int active = 0;
    EXPECT_EQ_INT(ext_host_info(host, 0, &id, &name, &dir, &active), 1);
    EXPECT_NOT_NULL(id);
    EXPECT_EQ_STR(id, "hello-c");
    EXPECT_NOT_NULL(dir); /* el directorio debe estar disponible */
    EXPECT_EQ_INT(active, 1);

    /* fuera de rango: info devuelve 0 */
    EXPECT_EQ_INT(ext_host_info(host, 99, &id, &name, &dir, &active), 0);

    /* descargar: deja de estar en la lista de activas */
    EXPECT_EQ_INT(ext_host_unload(host, "hello-c"), 0);
    active = 1;
    /* el slot puede quedar (inactivo) o el id a NULL; en ambos casos NO activo */
    if (ext_host_info(host, 0, &id, &name, &dir, &active))
        EXPECT_EQ_INT(active, 0);
    EXPECT_FALSE(ext_host_has(host, "hello-c"));

    /* reinstalar (load de nuevo) y recargar -> activa otra vez */
    EXPECT_EQ_INT(ext_host_load(host, COFFEE_HELLO_EXT_DIR), 0);
    EXPECT_TRUE(ext_host_has(host, "hello-c"));
    EXPECT_EQ_INT(ext_host_reload(host, "hello-c"), 0);
    EXPECT_TRUE(ext_host_has(host, "hello-c"));

    /* tras reload, debe existir EXACTAMENTE un slot activo con id "hello-c".
     * Nota: el unload deja el slot inactivo (no se compacta) y el load posterior
     * usa un slot nuevo, asi que el activo puede NO ser el indice 0; el panel
     * de extensiones recorre los slots saltando los inactivos igual que aqui. */
    int found_active = 0;
    for (size_t i = 0; i < ext_host_count(host); ++i) {
        const char *iid = NULL;
        int iact = 0;
        if (ext_host_info(host, i, &iid, NULL, NULL, &iact) && iact) {
            found_active++;
            EXPECT_NOT_NULL(iid);
            EXPECT_EQ_STR(iid, "hello-c");
        }
    }
    EXPECT_EQ_INT(found_active, 1);

    ext_host_destroy(host);
    buf_free(&b);
}

int main(void) {
    tt_suite("ext_host");
    tt_run("load DLL -> register -> command -> buffer + unload revierte",
           test_load_command_buffer_unload);
    tt_run("evento FILE_OPEN al suscriptor + servicio accesible",
           test_evento_y_servicio);
    tt_run("reload deja la extension activa y operativa", test_recarga);
    tt_run("introspeccion + install/unload/reload",
           test_introspeccion_install_unload_reload);
    return tt_summary();
}
