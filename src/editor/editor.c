/**
 * @file editor.c
 * @brief Núcleo del editor: arranque/cierre de SDL, bucle principal de eventos,
 *        carga de la fuente y utilidades de cursor/scroll/selección.
 *
 * @note SDL3 para recién llegados. SDL (Simple DirectMedia Layer) es la
 * librería que nos da una ventana del sistema operativo, un contexto de dibujo
 * 2D acelerado por GPU y una cola de eventos (teclado, ratón,
 * redimensionado...). SDL_ttf es su extensión para rasterizar texto a partir de
 * fuentes TrueType (usa FreeType por dentro). El flujo típico de una app SDL
 * es: inicializar subsistemas → crear ventana y renderer → bucle {leer eventos
 * → actualizar estado → dibujar} → destruir todo en orden inverso. Casi todas
 * las funciones SDL devuelven
 * @c false / @c NULL en error y dejan el motivo en @c SDL_GetError().
 */
#include "editor_internal.h"
#include "input/input.h"
#include "render/render.h"

/**
 * @brief Abre la fuente TrueType del editor desde disco.
 *
 * La fuente ya no va embebida en el binario, así que hay que localizar el
 * archivo
 * @c .ttf en tiempo de ejecución. Se prueba, en orden:
 *   1. La ruta de la variable de entorno @c COFFEECODE_FONT (permite al usuario
 *      elegir su propia fuente).
 *   2. @c font.ttf y @c assets/font.ttf junto al ejecutable. @c
 * SDL_GetBasePath() devuelve la carpeta donde está el binario (con separador
 * final), de modo que la app funciona aunque se lance desde otro directorio de
 * trabajo.
 *   3. Rutas relativas al directorio de trabajo actual (último recurso).
 *
 * @param size Tamaño de la fuente en puntos (lo que SDL_ttf llama "point
 * size").
 * @return La fuente abierta, o @c NULL si no se encontró ninguna en esas rutas.
 *         El llamante es dueño del puntero y debe cerrarlo con @c
 * TTF_CloseFont.
 */
static TTF_Font *load_editor_font(float size) {
    /* getenv: si el usuario definió COFFEECODE_FONT, esa fuente tiene
     * prioridad. */
    const char *env = getenv("COFFEECODE_FONT");
    if (env && env[0]) {
        /* TTF_OpenFont(ruta, tamaño): carga la fuente; NULL si la ruta no
         * existe o no es un .ttf válido. */
        TTF_Font *f = TTF_OpenFont(env, size);
        if (f) return f;
    }

    /* SDL_GetBasePath: carpeta del ejecutable (p. ej. "C:\\app\\"), con la
     * barra final incluida. Así construimos rutas absolutas robustas frente al
     * cwd. */
    const char *base = SDL_GetBasePath();
    if (base) {
        char path[1024];
        snprintf(path, sizeof(path), "%sfont.ttf", base);
        TTF_Font *f = TTF_OpenFont(path, size);
        if (f) return f;
        snprintf(path, sizeof(path), "%sassets/font.ttf", base);
        f = TTF_OpenFont(path, size);
        if (f) return f;
    }

    /* Último recurso: relativas al directorio de trabajo. */
    TTF_Font *f = TTF_OpenFont("assets/font.ttf", size);
    if (f) return f;
    return TTF_OpenFont("font.ttf", size);
}

/**
 * @brief Convierte una coordenada (línea, columna) a posición lógica del
 * buffer.
 *
 * El texto vive en un "gap buffer" que se direcciona con un único offset lineal
 * (la posición lógica: 0 = primer carácter del archivo). La UI, en cambio,
 * razona en (línea, columna). Esta función traduce de lo segundo a lo primero,
 * recortando (clamp) a un rango válido para que coordenadas fuera de pantalla
 * no se salgan.
 *
 * @param line Línea destino (se recorta a [0, nº_líneas-1]).
 * @param col  Columna destino (se recorta a [0, longitud_de_la_línea]).
 * @return Offset lógico correspondiente dentro del buffer.
 */
size_t editor_pos_from_line_col(Editor *e, int line, int col) {
    Buffer *b = e->buf;
    int total = buf_line_count(b);
    if (line < 0) line = 0;
    if (line >= total) line = total - 1;

    /* inicio de la línea: O(1) consultando el índice de líneas del buffer */
    size_t line_start = buf_line_offset(b, line);

    /* fin de la línea (sin el '\n'): O(longitud de la línea) */
    size_t line_end = buf_line_end(b, line_start);

    /* recortar la columna para no pasar del final real de la línea */
    int line_len = (int)(line_end - line_start);
    if (col < 0) col = 0;
    if (col > line_len) col = line_len;

    return line_start + (size_t)col;
}

/**
 * @brief Recalcula (cursor_line, cursor_col) a partir de la posición del
 * buffer.
 *
 * El buffer mantiene su propio "cursor" (la posición lógica donde está el hueco
 * del gap buffer). Tras cualquier edición o movimiento conviene reflejar esa
 * posición en las coordenadas (línea, columna) que usa el render. Es la
 * operación inversa de ::editor_pos_from_line_col.
 */
void editor_sync_cursor(Editor *e) {
    size_t pos = buf_cursor_pos(e->buf); /* offset lógico del cursor */
    int line, col;
    buf_line_col(e->buf, pos, &line, &col); /* offset -> (línea, columna) */
    e->cursor_line = line;
    e->cursor_col = col;
}

/**
 * @brief Desplaza el scroll lo justo para que el cursor quede dentro de la
 * vista.
 *
 * @c scroll_line / @c scroll_col son la primera línea/columna visible (en
 * celdas, no en píxeles). Se calcula cuántas líneas y columnas caben restando
 * al tamaño de ventana las zonas de UI (barra de navegación, pestañas, status,
 * atajos, panel lateral, gutter y padding) y dividiendo por el alto de línea y
 * el ancho de carácter. Si el cursor se sale por arriba/izquierda o por
 * abajo/derecha, se mueve el scroll el mínimo necesario para volver a
 * encuadrarlo.
 */
void editor_ensure_visible(Editor *e) {
    int left_off = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    int vis_lines = (e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT -
                     SHORTCUT_HEIGHT) /
                    LINE_HEIGHT;
    int vis_cols =
        (e->win_w - left_off - GUTTER_WIDTH - PADDING_LEFT) / e->char_w;

    if (e->cursor_line < e->scroll_line) e->scroll_line = e->cursor_line;
    if (e->cursor_line >= e->scroll_line + vis_lines)
        e->scroll_line = e->cursor_line - vis_lines + 1;

    if (e->cursor_col < e->scroll_col) e->scroll_col = e->cursor_col;
    if (e->cursor_col >= e->scroll_col + vis_cols)
        e->scroll_col = e->cursor_col - vis_cols + 1;

    if (e->scroll_line < 0) e->scroll_line = 0;
    if (e->scroll_col < 0) e->scroll_col = 0;
}

/**
 * @brief Reajusta la cache del lexer al nº de líneas y marca sucias las
 * afectadas.
 *
 * El resaltado de sintaxis se cachea por línea para no re-tokenizar todo en
 * cada frame. Tras una edición hay que: (1) redimensionar la cache si cambió el
 * número de líneas, y (2) marcar como "sucias" (pendientes de re-tokenizar) las
 * líneas desde la editada hacia abajo, porque un cambio (p. ej. abrir un
 * comentario de bloque) puede afectar a las líneas siguientes.
 *
 * @param from_line Primera línea que hay que volver a resaltar.
 */
void editor_update_lexer(Editor *e, int from_line) {
    int total = buf_line_count(e->buf);
    lexer_cache_resize(e->lex, total);
    lexer_cache_dirty(e->lex, from_line);
}

/**
 * @brief Obtiene el rango lógico [from, to) que cubre la selección actual.
 *
 * La selección se define por dos puntos: el "ancla" (donde empezó, guardado
 * como línea/columna) y el cursor (donde está ahora). El usuario puede
 * seleccionar hacia delante o hacia atrás, así que se ordenan para que @p from
 * <= @p to.
 *
 * @param[out] from Offset lógico de inicio de la selección.
 * @param[out] to   Offset lógico de fin de la selección.
 * @return 1 si hay una selección no vacía; 0 si no hay selección o está vacía.
 */
int editor_sel_range(Editor *e, size_t *from, size_t *to) {
    if (!e->sel_active) return 0;
    size_t anchor =
        editor_pos_from_line_col(e, e->sel_anchor_line, e->sel_anchor_col);
    size_t cursor = buf_cursor_pos(e->buf);
    if (anchor <= cursor) { /* selección hacia delante */
        *from = anchor;
        *to = cursor;
    } else { /* selección hacia atrás: intercambiar */
        *from = cursor;
        *to = anchor;
    }
    return (*from != *to);
}

/** Desactiva la selección actual (no borra texto, solo quita el resaltado). */
void editor_sel_clear(Editor *e) {
    e->sel_active = 0;
}

/**
 * @brief Reinicia el timer del parpadeo del cursor y lo pone visible.
 *
 * Debe llamarse cada vez que el cursor se mueve o se edita texto, para que el
 * cursor arranque siempre visible tras una acción del usuario y no aparezca
 * en su fase "oculta" justo después de pulsar una tecla.
 */
void editor_cursor_blink_reset(Editor *e) {
    e->cursor_visible = 1;
    e->cursor_blink_ms = SDL_GetTicks();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ARRANQUE / CIERRE / BUCLE PRINCIPAL
 * ═══════════════════════════════════════════════════════════════════════════
 */

/**
 * @brief Inicializa SDL, crea la ventana/renderer, carga la fuente y prepara el
 *        editor; opcionalmente abre un archivo inicial.
 *
 * Sigue el arranque canónico de una app SDL3, abortando si cualquier paso
 * falla:
 *   1. @c SDL_Init(SDL_INIT_VIDEO): arranca el subsistema de vídeo (ventanas y
 *      eventos). Es obligatorio antes de crear ventanas o renderers.
 *   2. @c SDL_CreateWindow: crea la ventana del SO. @c SDL_WINDOW_RESIZABLE
 * permite al usuario redimensionarla (generará eventos de resize).
 *   3. @c SDL_CreateRenderer(win, NULL): crea el contexto de dibujo 2D
 * acelerado;
 *      @c NULL deja que SDL elija el mejor backend (Direct3D, OpenGL,
 * Vulkan...).
 *   4. @c SDL_SetRenderVSync(r, 1): sincroniza el "present" con el refresco del
 *      monitor para evitar tearing y no quemar CPU/GPU dibujando de más.
 *   5. @c SDL_StartTextInput: activa los eventos de texto (@c
 * SDL_EVENT_TEXT_INPUT), que entregan caracteres ya compuestos (acentos, IME) —
 * distinto de las teclas crudas. Imprescindible para escribir texto
 * correctamente.
 *   6. @c TTF_Init + ::load_editor_font: arranca SDL_ttf y abre la fuente.
 *
 * @param e        Editor a inicializar (se pone a cero al entrar).
 * @param filepath Archivo a abrir al arrancar, o @c NULL/"" para empezar vacío.
 * @return 1 si todo fue bien; 0 si algún paso de SDL falló (motivo en stderr).
 */
int editor_init(Editor *e, const char *filepath) {
    memset(e, 0, sizeof(*e)); /* todo a cero: punteros NULL y flags en 0 */
    e->running = 1;
    e->needs_redraw = 1;
    e->menu_hovered = -1;
    e->find.result_line = -1;
    e->cursor_visible = 1;          /* cursor visible al arrancar */
    e->cursor_blink_ms = SDL_GetTicks(); /* iniciar timer del parpadeo */

    /* -- Subsistema de vídeo de SDL -- */
#ifdef _DEBUG
    fprintf(stderr, "STEP: SDL_Init\n"); /* trazas de arranque solo en debug */
#endif
    /* SDL_Init devuelve false en error; SDL_GetError() da el texto del fallo.
     */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 0;
    }

    /* -- Ventana del sistema operativo -- */
    e->win_w = 1200; /* tamaño inicial en píxeles */
    e->win_h = 800;
#ifdef _DEBUG
    fprintf(stderr, "STEP: SDL_CreateWindow\n");
#endif
    e->window = SDL_CreateWindow("CoffeeCode", e->win_w, e->win_h,
                                 SDL_WINDOW_RESIZABLE);
    if (!e->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 0;
    }

    /* -- Renderer (contexto de dibujo 2D) ligado a la ventana -- */
#ifdef _DEBUG
    fprintf(stderr, "STEP: SDL_CreateRenderer\n");
#endif
    e->renderer =
        SDL_CreateRenderer(e->window, NULL); /* NULL = backend automático */
    if (!e->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 0;
    }
    SDL_SetRenderVSync(e->renderer,
                       1);         /* sincronizar con el refresco del monitor */
    SDL_StartTextInput(e->window); /* habilitar eventos de texto (escritura) */

    /* -- SDL_ttf (rasterizado de fuentes) -- */
#ifdef _DEBUG
    fprintf(stderr, "STEP: TTF_Init\n");
#endif
    if (!TTF_Init()) {
        fprintf(stderr, "TTF_Init: %s\n", SDL_GetError());
        return 0;
    }

#ifdef _DEBUG
    fprintf(stderr, "STEP: load font\n");
#endif
    e->font = load_editor_font(FONT_SIZE);
    if (!e->font) {
        fprintf(
            stderr,
            "No se pudo cargar la fuente (font.ttf). Coloca font.ttf junto al "
            "ejecutable o define COFFEECODE_FONT.\n");
        return 0;
    }

    /* Ancho de carácter: como la fuente es monoespaciada, todos los glifos
     * miden lo mismo. Medimos la "M" con TTF_GetStringSize (devuelve píxeles) y
     * usamos ese ancho para alinear texto, cursor y columnas en una rejilla
     * fija. */
    {
        int w = 0, h = 0;
        TTF_GetStringSize(e->font, "M", 1, &w, &h);
        e->char_w =
            w > 0 ? w : FONT_SIZE / 2; /* fallback si la medida fallara */
    }

    /* -- Panel explorador de archivos -- */
    ftree_init(&e->ftree);

    /* -- Pestañas --
     * Si se pasó un filepath, abrir ese archivo en la primera pestaña; si no,
     * arrancar sin pestañas (tab_count = 0 → render muestra la bienvenida).
     * buf/lex/undo a NULL: apuntarán al almacenamiento de la pestaña activa. */
    e->tab_count = 0;
    e->active_tab = 0;
    e->buf = NULL;
    e->lex = NULL;
    e->undo = NULL;

    if (filepath && filepath[0]) {
#ifdef _DEBUG
        fprintf(stderr, "STEP: opening initial file\n");
#endif
        editor_tab_open(e, filepath);
        SDL_SetWindowTitle(e->window,
                           filepath); /* título de la ventana = ruta */
    }

#ifdef _DEBUG
    fprintf(stderr, "STEP: editor_init OK\n");
#endif
    return 1;
}

/**
 * @brief Libera todas las pestañas y destruye los recursos de SDL.
 *
 * El orden importa: se destruyen los recursos en el sentido inverso a su
 * creación (fuente → text input → renderer → ventana → subsistemas), porque
 * unos dependen de otros (p. ej. el renderer pertenece a la ventana). @c
 * TTF_Quit y @c SDL_Quit cierran las librerías al final.
 */
void editor_free(Editor *e) {
    for (int i = 0; i < e->tab_count; i++)
        tab_free_resources(&e->tabs[i]); /* buffer/lexer/undo de cada pestaña */
    ftree_free(&e->ftree);
    if (e->font) TTF_CloseFont(e->font); /* liberar la fuente abierta */
    if (e->renderer)
        SDL_StopTextInput(e->window); /* desactivar eventos de texto */
    if (e->renderer) SDL_DestroyRenderer(e->renderer);
    if (e->window) SDL_DestroyWindow(e->window);
    TTF_Quit(); /* cerrar SDL_ttf */
    SDL_Quit(); /* cerrar SDL */
}

/**
 * @brief Bucle principal: espera eventos, los procesa, autoguarda y redibuja.
 *
 * El bucle corre a 60 FPS garantizados mediante SDL_WaitEventTimeout con
 * un timeout de 16 ms (~1 frame a 60 Hz). En cada iteración:
 *   - Se procesan todos los eventos pendientes de la cola.
 *   - Se actualiza el parpadeo del cursor (530 ms encendido / 530 ms apagado),
 *     marcando needs_redraw cuando cambia de estado para no dibujar de más.
 *   - Se redibuja si algo cambió (needs_redraw activo).
 *
 * Así la app mantiene respuesta inmediata ante entrada del usuario Y animaciones
 * fluidas (cursor parpadeante) sin quemar CPU cuando no hay actividad.
 */
#define CURSOR_BLINK_MS 530  /* medio periodo del parpadeo del cursor (ms) */

void editor_run(Editor *e) {
    SDL_Event ev;
    while (e->running) {
        /* Esperar un evento hasta 16 ms (= 1 frame a 60 Hz).
         * Si llega antes, procesarlo; si no, el timeout fuerza la siguiente
         * iteración garantizando que siempre revisamos el blink y redibujamos. */
        if (SDL_WaitEventTimeout(&ev, 16)) {
            input_handle_event(e, &ev);
            /* Drenar el resto de la cola sin bloquear */
            while (SDL_PollEvent(&ev))
                input_handle_event(e, &ev);
        }

        /* Autoguardado: si está activado y hay cambios sin guardar, persistir
         * el archivo, pero como mucho una vez cada 300 ms (SDL_GetTicks da los
         * ms transcurridos desde SDL_Init) para no escribir en disco en cada
         * tecla. */
        if (e->autosave && e->modified && e->filepath[0]) {
            Uint64 now = SDL_GetTicks();
            if (now - e->autosave_last_ms >= 300) {
                if (buf_save_file(e->buf, e->filepath)) {
                    e->modified = 0;
                    /* sincronizar de vuelta a la pestaña: ruta, flag y mtime */
                    if (e->tab_count > 0) {
                        EditorTab *_t = &e->tabs[e->active_tab];
                        strncpy(_t->filepath, e->filepath, 511);
                        _t->modified = 0;
                        {
                            struct stat _st;
                            _t->loaded_mtime = (stat(e->filepath, &_st) == 0)
                                                   ? (long)_st.st_mtime
                                                   : 0;
                        }
                    }
                    e->needs_redraw = 1;
                }
                e->autosave_last_ms = now;
            }
        }

        /* Parpadeo del cursor: alternar visibilidad cada CURSOR_BLINK_MS.
         * Solo se marca needs_redraw cuando cambia el estado, evitando
         * redibujos innecesarios cuando el cursor no ha cambiado. */
        {
            Uint64 now = SDL_GetTicks();
            if (now - e->cursor_blink_ms >= CURSOR_BLINK_MS) {
                e->cursor_visible = !e->cursor_visible;
                e->cursor_blink_ms = now;
                e->needs_redraw = 1;
            }
        }

        /* Redibujar solo si algo cambió desde el último frame. */
        if (e->needs_redraw) {
            render_frame(e);
            e->needs_redraw = 0;
        }
    }
}

