/**
 * @file vesta.c
 * @brief Extension de CoffeeCode que compila y ejecuta Vex via libvesta.
 *
 * Conecta el IDE con el compilador + VM del lenguaje Vex (libvesta).  La
 * extension enlaza la DLL libvesta y expone tres comandos en el IDE:
 *
 *   - "vesta.run"      : ejecuta el texto del buffer activo (vesta_eval) y
 *                        vuelca el codigo de salida al panel de salida.
 *   - "vesta.show-ir"  : compila el buffer al IR SSA (vesta_compile_to_ir) y
 *                        muestra el IR en una pestana nueva (o en el panel si
 *                        no hay manipulacion de pestana disponible).
 *   - "vesta.compile"  : compila el buffer a bytecode .velb (vesta_compile) y
 *                        reporta el tamano resultante al panel + barra de estado.
 *
 * La salida estandar del programa Vex ejecutado (println, etc.) va al stdout
 * del proceso del IDE; el panel de salida solo recibe el resumen (codigo de
 * salida / IR / tamano) y los mensajes de error de libvesta.
 *
 * Solo enlaza coffee_ext.h (la API del IDE) y capi/vesta.h (la API de
 * libvesta).  Se compila como DLL y carga libvesta.dll en runtime.
 */
#include "ext/coffee_ext.h"

#include "capi/vesta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Lee el texto completo del buffer activo en un buffer del heap.
 *
 * Reserva (longitud + 1) bytes, copia [0, longitud) con buffer_get_text y
 * termina la cadena en NUL.  El llamante libera con free.  Devuelve NULL si
 * el buffer esta vacio o falla la reserva.
 */
static char *read_active_buffer(CoffeeHost *host, const CoffeeApi *api) {
    size_t len = api->buffer_length(host);
    char *text = (char *)malloc(len + 1);
    if (!text) return NULL;
    /* buffer_get_text copia los bytes del rango [0, len) sin NUL final. */
    size_t got = api->buffer_get_text(host, 0, len, text, len + 1);
    text[got] = '\0';
    return text;
}

/**
 * @brief Vuelca un mensaje de error de libvesta al panel y lo libera.
 *
 * libvesta asigna out_err en su heap; se debe liberar con vesta_free (no con
 * el free del CRT del llamante).  Acepta NULL de forma segura.
 */
static void report_vesta_error(CoffeeHost *host, const CoffeeApi *api,
                               char *err) {
    if (err) {
        api->output_append(host, "[vesta] error: ");
        api->output_append(host, err);
        api->output_append(host, "\n");
        vesta_free(err);
    } else {
        api->output_append(host, "[vesta] error desconocido\n");
    }
}

/**
 * @brief Comando "vesta.run": compila + ejecuta el buffer activo.
 *
 * Toma el texto del buffer, lo pasa a vesta_eval y vuelca el codigo de salida
 * (registro R0 de main) al panel de salida.  En error, muestra el mensaje de
 * libvesta sin propagar excepciones ni crashear el IDE.
 */
static void cmd_vesta_run(CoffeeHost *host, void *userdata) {
    const CoffeeApi *api = (const CoffeeApi *)userdata;
    if (!api) return;

    char *src = read_active_buffer(host, api);
    if (!src) {
        api->output_clear(host);
        api->output_append(host, "[vesta] buffer vacio o sin memoria\n");
        api->set_status(host, "vesta: nada que ejecutar");
        return;
    }

    api->output_clear(host);
    api->output_append(host, "[vesta] ejecutando buffer...\n");

    int exit_code = 0;
    char *err = NULL;
    int rc = vesta_eval(src, "buffer", &exit_code, &err);
    free(src);

    if (rc != 0) {
        report_vesta_error(host, api, err);
        api->set_status(host, "vesta: error de ejecucion");
        return;
    }

    /* convertir el codigo de salida a texto para el panel + estado */
    char line[96];
    snprintf(line, sizeof(line), "[vesta] exit-code = %d\n", exit_code);
    api->output_append(host, line);

    char status[64];
    snprintf(status, sizeof(status), "vesta: exit-code = %d", exit_code);
    api->set_status(host, status);
}

/**
 * @brief Comando "vesta.show-ir": muestra el IR SSA del buffer activo.
 *
 * Compila el buffer al texto del IR (vesta_compile_to_ir).  Si el IDE permite
 * abrir una pestana nueva, escribe el IR ahi; en caso contrario, lo vuelca al
 * panel de salida.  En error, reporta el mensaje de libvesta.
 */
static void cmd_vesta_show_ir(CoffeeHost *host, void *userdata) {
    const CoffeeApi *api = (const CoffeeApi *)userdata;
    if (!api) return;

    char *src = read_active_buffer(host, api);
    if (!src) {
        api->output_clear(host);
        api->output_append(host, "[vesta] buffer vacio o sin memoria\n");
        api->set_status(host, "vesta: nada que compilar");
        return;
    }

    char *ir = NULL;
    char *err = NULL;
    int rc = vesta_compile_to_ir(src, "buffer", &ir, &err);
    free(src);

    if (rc != 0) {
        api->output_clear(host);
        report_vesta_error(host, api, err);
        api->set_status(host, "vesta: error compilando IR");
        return;
    }

    /* preferir una pestana nueva: el IR puede ser largo y se navega mejor */
    if (api->new_tab && api->buffer_insert) {
        api->new_tab(host);
        api->buffer_insert(host, ir ? ir : "");
        api->set_status(host, "vesta: IR generado en pestana nueva");
    } else {
        api->output_clear(host);
        api->output_append(host, "[vesta] IR SSA:\n");
        api->output_append(host, ir ? ir : "");
        api->output_append(host, "\n");
        api->set_status(host, "vesta: IR en el panel de salida");
    }

    vesta_free(ir);
}

/**
 * @brief Comando "vesta.compile": compila el buffer a bytecode .velb.
 *
 * Compila el buffer a .velb en memoria (vesta_compile) y reporta el tamano en
 * bytes del bytecode resultante al panel + barra de estado.  El bytecode no se
 * persiste a disco en esta version: el objetivo es validar la compilacion.
 */
static void cmd_vesta_compile(CoffeeHost *host, void *userdata) {
    const CoffeeApi *api = (const CoffeeApi *)userdata;
    if (!api) return;

    char *src = read_active_buffer(host, api);
    if (!src) {
        api->output_clear(host);
        api->output_append(host, "[vesta] buffer vacio o sin memoria\n");
        api->set_status(host, "vesta: nada que compilar");
        return;
    }

    api->output_clear(host);

    unsigned char *velb = NULL;
    size_t velb_len = 0;
    char *err = NULL;
    int rc = vesta_compile(src, "buffer", &velb, &velb_len, &err);
    free(src);

    if (rc != 0) {
        report_vesta_error(host, api, err);
        api->set_status(host, "vesta: error de compilacion");
        return;
    }

    char line[96];
    snprintf(line, sizeof(line), "[vesta] .velb compilado: %zu bytes\n",
             velb_len);
    api->output_append(host, line);

    char status[64];
    snprintf(status, sizeof(status), "vesta: .velb %zu bytes", velb_len);
    api->set_status(host, status);

    vesta_free(velb);
}

/**
 * @brief Punto de entrada de la extension.
 *
 * El host lo invoca tras cargar la DLL.  Comprueba la ABI, registra los tres
 * comandos (pasando la api como userdata) y loguea la version de libvesta.
 * Devuelve 0 en exito (!=0 -> el host la descarta).
 */
COFFEE_EXTENSION_EXPORT int coffee_extension_register(CoffeeHost *host,
                                                      const CoffeeApi *api) {
    if (!api) return 1;
    /* rechazar si el host habla un ABI mas nuevo del que entendemos */
    if (api->abi_version > COFFEE_ABI_VERSION) return 2;

    api->register_command(host, "vesta.run", "Ejecutar Vex", cmd_vesta_run,
                          (void *)api);
    api->register_command(host, "vesta.show-ir", "Ver IR del archivo",
                          cmd_vesta_show_ir, (void *)api);
    api->register_command(host, "vesta.compile", "Compilar a .velb",
                          cmd_vesta_compile, (void *)api);

    /* registrar la version de libvesta enlazada para diagnostico */
    const char *ver = vesta_version();
    api->log(host, COFFEE_LOG_INFO, ver ? ver : "libvesta (version desconocida)");
    api->log(host, COFFEE_LOG_INFO, "extension vesta activada");
    return 0;
}

/**
 * @brief Punto de salida opcional de la extension.
 *
 * El host ya revierte los comandos registrados; aqui no hay recursos propios
 * que liberar (libvesta gestiona su propio estado global).
 */
COFFEE_EXTENSION_EXPORT void coffee_extension_unregister(CoffeeHost *host) {
    (void)host;
}
