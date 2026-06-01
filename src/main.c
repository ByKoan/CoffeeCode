#include "editor.h"
#include <stdio.h>

/* En Windows con WIN32_EXECUTABLE, SDL_main.h redefine main como SDL_main.
   SDL_MAIN_HANDLED suprime ese comportamiento y deja main() intacto. */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

int main(int argc, char *argv[]) {
    const char *filepath = (argc > 1) ? argv[1] : NULL;

    Editor e;
    if (!editor_init(&e, filepath)) {
        fprintf(stderr, "Error al inicializar el editor.\n");
        editor_free(&e);
        return 1;
    }

    editor_run(&e);
    editor_free(&e);
    return 0;
}
