#include "editor/editor.h"
#include <stdio.h>
#include <stdlib.h>

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL3/SDL_main.h>

/* -- Log a fichero: solo activo en builds Debug --------------------------- */
#ifdef _DEBUG
static FILE *g_log = NULL;
static void log_init(void) {
    g_log = fopen("coffeecode_log.txt", "w");
    if (g_log) {
        fprintf(g_log, "=== CoffeeCode inicio ===\n");
        fflush(g_log);
    }
}
static void log_msg(const char *msg) {
    if (g_log) {
        fprintf(g_log, "%s\n", msg);
        fflush(g_log);
    }
}
static void log_close(void) {
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}
#else
static void log_init(void) {}
static void log_msg(const char *msg) {
    (void)msg;
}
static void log_close(void) {}
#endif

int main(int argc, char *argv[]) {
    log_init();
    log_msg("main() arrancado");

    const char *filepath = (argc > 1) ? argv[1] : NULL;

    Editor *e = (Editor *)calloc(1, sizeof(Editor));
    if (!e) {
        log_msg("ERROR: no se pudo alojar Editor");
        log_close();
        return 1;
    }

    log_msg("Llamando editor_init...");
    if (!editor_init(e, filepath)) {
        log_msg("ERROR: editor_init fallo");
        editor_free(e);
        free(e);
        log_close();
        return 1;
    }

    log_msg("editor_init OK, entrando en editor_run");
    editor_run(e);
    log_msg("editor_run terminado, saliendo");
    editor_free(e);
    free(e);
    log_close();
    return 0;
}
