/**
 * @file main.c
 * @brief Punto de entrada de CoffeeCode: parseo de argumentos y ciclo de vida.
 *
 * Este archivo contiene el @c main() y un pequeño logger a fichero (solo en
 * Debug). Toda la lógica vive en el módulo editor; aquí solo se reserva el @c
 * Editor, se lanza @c editor_init → @c editor_run → @c editor_free y se libera
 * la memoria.
 *
 * @note Sobre @c SDL_MAIN_HANDLED. Normalmente SDL redefine @c main mediante
 * una macro (en @c <SDL3/SDL_main.h>): tu @c main pasa a llamarse @c SDL_main y
 * SDL provee el verdadero punto de entrada del sistema (en Windows, @c WinMain)
 * que hace preparativos antes de llamarte. Aquí NO queremos ese comportamiento:
 * definiendo
 * @c SDL_MAIN_HANDLED le decimos a SDL que gestionamos el @c main nosotros, así
 * que NO renombra nada y nuestro @c main es el entry point real. A cambio,
 * somos responsables de avisar a SDL con @c SDL_SetMainReady() antes de @c
 * SDL_Init (eso ocurre dentro de editor.h / editor_init, no aquí). Este patrón
 * da control total sobre el arranque y evita el "WinMain mágico" de SDL.
 */
#include "editor/editor.h"
#include <stdio.h>
#include <stdlib.h>

/* SDL_MAIN_HANDLED debe estar definido antes de incluir SDL_main.h para que la
 * cabecera NO redefina nuestro main (ver nota del encabezado). editor.h ya
 * suele definirlo; el guard evita una redefinición si así fuera. */
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL3/SDL_main.h>

/* -- Log a fichero: solo activo en builds Debug --------------------------- */
#ifdef _DEBUG
static FILE *g_log = NULL; /* handle del fichero de log (NULL si no se abrió) */

/** @brief Abre el fichero de log y escribe la cabecera de inicio (solo Debug).
 */
static void log_init(void) {
    g_log = fopen("coffeecode_log.txt", "w");
    if (g_log) {
        fprintf(g_log, "=== CoffeeCode inicio ===\n");
        fflush(g_log); /* volcar ya por si la app crashea después */
    }
}

/**
 * @brief Escribe una línea en el log si está abierto (solo Debug).
 * @param msg Mensaje a registrar (se le añade un salto de línea).
 */
static void log_msg(const char *msg) {
    if (g_log) {
        fprintf(g_log, "%s\n", msg);
        fflush(
            g_log); /* flush inmediato: el log sobrevive a un cierre brusco */
    }
}

/** @brief Cierra el fichero de log si estaba abierto (solo Debug). */
static void log_close(void) {
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}
#else
/* En Release el logger se compila como funciones vacías (sin coste ni fichero).
 */
static void log_init(void) {}
static void log_msg(const char *msg) {
    (void)msg; /* evitar warning de parámetro sin usar */
}
static void log_close(void) {}
#endif

/**
 * @brief Punto de entrada del programa.
 *
 * Reserva el @c Editor en el heap (puesto a cero con @c calloc), lo inicializa
 * (abriendo el archivo pasado por línea de comandos, si lo hay), ejecuta el
 * bucle principal y libera todo al salir. Cada paso se registra en el log de
 * Debug.
 *
 * @param argc Número de argumentos de línea de comandos.
 * @param argv Vector de argumentos; @c argv[1], si existe, es el archivo a
 * abrir.
 * @return 0 si todo fue bien; 1 si falló la reserva de memoria o @c
 * editor_init.
 */
int main(int argc, char *argv[]) {
    log_init();
    log_msg("main() arrancado");

    /* Primer argumento (si lo hay) = ruta del archivo a abrir al arrancar. */
    const char *filepath = (argc > 1) ? argv[1] : NULL;

    /* Reservar el Editor en heap y ponerlo a cero (calloc). Es grande, por eso
     * no se pone en la pila. */
    Editor *e = (Editor *)calloc(1, sizeof(Editor));
    if (!e) {
        log_msg("ERROR: no se pudo alojar Editor");
        log_close();
        return 1;
    }

    /* Inicializar SDL/ventana/fuente/editor y abrir el archivo inicial. */
    log_msg("Llamando editor_init...");
    if (!editor_init(e, filepath)) {
        log_msg("ERROR: editor_init fallo");
        editor_free(e); /* liberar lo que sí se llegó a crear */
        free(e);
        log_close();
        return 1;
    }

    /* Bucle principal: bloquea hasta que el usuario cierra la app. */
    log_msg("editor_init OK, entrando en editor_run");
    editor_run(e);
    log_msg("editor_run terminado, saliendo");

    /* Cierre ordenado: recursos de SDL primero, luego la memoria del Editor. */
    editor_free(e);
    free(e);
    log_close();

    /* Salida dura: omite la limpieza atexit / DLL_PROCESS_DETACH del CRT al
     * salir.  Una extension puede enlazar un runtime de terceros cuya
     * destruccion global aborta en el cierre; el IDE ya libero sus recursos
     * (editor_free) y cerro su log, asi que terminar aqui evita arrastrar ese
     * fallo de terceros al cerrar la ventana. */
    fflush(stdout);
    fflush(stderr);
    _Exit(0);
    return 0; /* inalcanzable: _Exit no retorna */
}
