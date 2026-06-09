/**
 * @file editor_tabs.c
 * @brief Gestión de pestañas (crear, abrir, cerrar, cambiar) y guardado/
 *        restauración del estado de cada pestaña.
 *
 * Diseño: @c e->buf / @c e->lex / @c e->undo / @c e->hl son PUNTEROS directos a
 * @c tabs[active_tab] (no copias por valor), así nunca divergen aunque el
 * buffer haga realloc. @ref editor_tab_load_state redirige esos punteros y
 * restaura los escalares; @ref editor_tab_save_state guarda solo los escalares.
 *
 * Recarga eficiente: al volver a una pestaña NO modificada cuyo fichero cambió
 * en disco (mtime distinto), se re-lee; si hay cambios sin guardar nunca se
 * pisa el trabajo del usuario.
 */
#include "editor_internal.h"

/**
 * @brief Devuelve el tiempo de última modificación (mtime) de un fichero.
 *
 * El mtime es la marca temporal que el sistema de ficheros actualiza cada vez
 * que se escribe el archivo. Se usa para detectar cambios externos (otro editor
 * o proceso que tocó el archivo) y decidir si hay que recargarlo.
 *
 * @param path Ruta del fichero a consultar.
 * @return El mtime como entero (segundos epoch), o 0 si @p path es NULL/vacío
 *         o el fichero no existe (@c stat falla).
 */
static long file_mtime(const char *path) {
    if (!path || !path[0]) return 0; /* sin ruta: no hay nada que consultar */
    struct stat st;
    /* stat rellena `st` con metadatos del fichero; devuelve != 0 en error. */
    if (stat(path, &st) != 0) return 0;
    return (long)st.st_mtime; /* st_mtime: instante de la última modificación */
}

/**
 * @brief Inicializa una pestaña nueva y vacía: buffer de texto y pila de undo.
 *
 * Deja todos los escalares a cero (cursor, scroll, flags) y construye las dos
 * estructuras dinámicas que posee la pestaña: el gap buffer (vacío) y el ring
 * de undo con capacidad @c UNDO_MAX. NO crea aún la cache del lexer (eso lo
 * hace el llamante, que conoce el nº de líneas del contenido).
 *
 * @param t Pestaña a inicializar.
 */
static void tab_init(EditorTab *t) {
    memset(t, 0, sizeof(*t)); /* punteros a NULL, flags y escalares en 0 */
    buf_init(&t->buf);
    /* ring de undo: buffer circular de UNDO_MAX entradas de tamaño UndoEntry */
    ring_init(&t->undo.entries, sizeof(UndoEntry), UNDO_MAX);
}

/**
 * @brief Vuelca el estado escalar "en vivo" del editor a la pestaña activa.
 *
 * Mientras una pestaña está activa, su cursor/scroll/selección viven en los
 * campos del @c Editor (no en la @c EditorTab). Antes de cambiar de pestaña hay
 * que guardar esos valores en la @c EditorTab para no perderlos al volver. NO
 * copia @c buf/lex/undo: esos son punteros que ya apuntan al almacenamiento de
 * la pestaña, así que no pueden divergir.
 *
 * @param e Editor cuyo estado escalar se guarda en la pestaña activa.
 */
void editor_tab_save_state(Editor *e) {
    if (e->tab_count == 0) return; /* no hay pestaña donde guardar */
    EditorTab *t = &e->tabs[e->active_tab];
    t->cursor_line = e->cursor_line;
    t->cursor_col = e->cursor_col;
    t->scroll_line = e->scroll_line;
    t->scroll_col = e->scroll_col;
    t->modified = e->modified;
    t->sel_active = e->sel_active;
    t->sel_anchor_line = e->sel_anchor_line;
    t->sel_anchor_col = e->sel_anchor_col;
}

/**
 * @brief Hace de la pestaña activa la "actual": redirige punteros, restaura
 *        escalares y, si procede, recarga el fichero desde disco.
 *
 * Es la operación inversa de ::editor_tab_save_state y el corazón del modelo de
 * pestañas. Paso a paso:
 *   1. Apunta @c e->buf / @c e->lex / @c e->undo / @c e->hl al almacenamiento
 * de
 *      @c tabs[active_tab]. A partir de aquí todo el editor opera sobre esa
 *      pestaña sin copiar nada (los punteros comparten el mismo
 * almacenamiento).
 *   2. Recarga eficiente: si la pestaña tiene ruta y NO tiene cambios sin
 * guardar, se compara el mtime actual del fichero con el de la última carga. Si
 *      difieren (alguien editó el archivo fuera del editor), se descarta el
 * buffer viejo, se re-lee del disco, se reinicia la cache del lexer y se
 * resetean cursor/scroll. Si hay cambios sin guardar (@c modified) NUNCA se
 * recarga, para no pisar el trabajo del usuario.
 *   3. Restaura en el editor los escalares (cursor, scroll, selección,
 * modified) y la ruta guardados en la pestaña.
 *
 * @param e Editor sobre el que cargar el estado de su pestaña activa.
 */
static void editor_tab_load_state(Editor *e) {
    EditorTab *t = &e->tabs[e->active_tab];

    /* (1) Redirigir los punteros del editor al almacenamiento de esta pestaña.
     */
    e->buf = &t->buf;
    e->lex = &t->lex;
    e->undo = &t->undo;
    e->hl = t->hl;

    /* (2) Recarga eficiente: solo si tiene ruta, no está modificado y cambió el
     * mtime respecto a la última lectura desde disco. */
    if (t->filepath[0] && !t->modified) {
        long current_mtime = file_mtime(t->filepath);
        if (current_mtime != 0 && current_mtime != t->loaded_mtime) {
            /* el fichero cambió fuera del editor: re-leer desde cero */
            buf_free(&t->buf);
            buf_init(&t->buf);
            buf_load_file(&t->buf, t->filepath);
            t->loaded_mtime = current_mtime; /* recordar el nuevo mtime */

            /* la cache del lexer ya no vale: rehacerla al nº de líneas actual
             */
            lexer_cache_free(&t->lex);
            int total = buf_line_count(&t->buf);
            lexer_cache_init(&t->lex, total > 0 ? total : 1);

            t->cursor_line = t->cursor_col = 0; /* reset tras recarga externa */
            t->scroll_line = t->scroll_col = 0;
        }
    }

    /* (3) Restaurar los escalares y la ruta en los campos "en vivo" del editor.
     */
    e->cursor_line = t->cursor_line;
    e->cursor_col = t->cursor_col;
    e->scroll_line = t->scroll_line;
    e->scroll_col = t->scroll_col;
    e->modified = t->modified;
    e->sel_active = t->sel_active;
    e->sel_anchor_line = t->sel_anchor_line;
    e->sel_anchor_col = t->sel_anchor_col;
    strncpy(e->filepath, t->filepath, sizeof(e->filepath) - 1);
}

/**
 * @brief Crea una pestaña nueva vacía (sin fichero asociado) y la activa.
 *
 * Guarda primero el estado de la pestaña actual (si la hay), reserva el
 * siguiente hueco del array @c tabs, lo inicializa con buffer/undo/lexer vacíos
 * y un resaltador por defecto, y carga su estado para que pase a ser la activa.
 * No hace nada si ya se alcanzó @c MAX_TABS.
 *
 * @param e Editor donde crear la pestaña.
 */
void editor_tab_new(Editor *e) {
    if (e->tab_count >= MAX_TABS) return; /* tope de pestañas alcanzado */
    if (e->tab_count > 0)
        editor_tab_save_state(e); /* preservar la pestaña actual */

    int idx = e->tab_count++; /* índice de la nueva pestaña; sube el contador */
    EditorTab *t = &e->tabs[idx];
    tab_init(t);
    lexer_cache_init(&t->lex, 1); /* cache para 1 línea (el buffer vacío) */
    t->hl =
        highlighter_default(); /* sin extensión conocida: resaltador genérico */

    e->active_tab = idx;
    editor_tab_load_state(e); /* hacerla la activa (redirige punteros) */
    e->needs_redraw = 1;
}

/**
 * @brief Abre el fichero @p path en una pestaña, activándola.
 *
 * Si ese fichero ya está abierto en alguna pestaña, simplemente cambia a ella
 * (guardando antes el estado de la actual y recargando si su mtime cambió). Si
 * no estaba abierto, crea una pestaña nueva, carga el contenido del disco,
 * registra el mtime y la ruta, inicializa la cache del lexer al nº de líneas y
 * elige el resaltador según la extensión del archivo. No abre nada si se
 * alcanzó
 * @c MAX_TABS.
 *
 * @param e    Editor donde abrir el fichero.
 * @param path Ruta del fichero a abrir.
 */
void editor_tab_open(Editor *e, const char *path) {
    /* ¿ya está abierto en alguna pestaña? Entonces solo activar esa. */
    for (int i = 0; i < e->tab_count; i++) {
        if (strcmp(e->tabs[i].filepath, path) == 0) {
            if (i == e->active_tab)
                return; /* ya es la activa: nada que hacer */
            editor_tab_save_state(e);
            e->active_tab = i;
            editor_tab_load_state(e); /* recarga si cambió el mtime */
            editor_update_lexer(e, 0);
            e->needs_redraw = 1;
            return;
        }
    }
    if (e->tab_count >= MAX_TABS) return;           /* tope de pestañas */
    if (e->tab_count > 0) editor_tab_save_state(e); /* preservar la actual */

    /* No estaba abierto: crear una pestaña nueva y cargar el fichero. */
    int idx = e->tab_count++;
    EditorTab *t = &e->tabs[idx];
    tab_init(t);
    buf_load_file(&t->buf, path);       /* leer el contenido del disco */
    t->loaded_mtime = file_mtime(path); /* recordar su mtime para recargas */
    strncpy(t->filepath, path, sizeof(t->filepath) - 1);
    int total = buf_line_count(&t->buf);
    lexer_cache_init(&t->lex, total > 0 ? total : 1); /* cache por línea */
    t->hl = highlighter_for_path(path); /* resaltador según la extensión */

    e->active_tab = idx;
    editor_tab_load_state(e);
    editor_update_lexer(e, 0); /* marcar todas las líneas para re-tokenizar */
    editor_sync_cursor(e); /* fijar cursor_line/col desde la pos del buffer */
    e->needs_redraw = 1;
}

/**
 * @brief Libera los recursos dinámicos de una pestaña: buffer, lexer y undo.
 *
 * No toca los punteros @c e->buf/lex/undo (el llamante se encarga de que no
 * queden apuntando a memoria liberada). La pila de undo posee los @c text de
 * cada entrada (cadenas malloc'd), así que hay que liberarlos uno a uno ANTES
 * de destruir el ring que los contiene.
 *
 * @param t Pestaña cuyos recursos se liberan.
 */
void tab_free_resources(EditorTab *t) {
    buf_free(&t->buf);
    lexer_cache_free(&t->lex);
    /* la pila de undo posee los `text` de cada entrada: liberarlos antes del
     * ring */
    for (size_t i = 0; i < ring_len(&t->undo.entries); i++)
        free(((UndoEntry *)ring_at(&t->undo.entries, i))->text);
    ring_free(&t->undo.entries);
}

/**
 * @brief Cierra la pestaña activa y activa otra (o deja el editor sin
 * pestañas).
 *
 * Libera los recursos de la pestaña activa, compacta el array @c tabs
 * desplazando a la izquierda las pestañas posteriores y limpia el hueco que
 * queda al final. Si no queda ninguna pestaña, pone los punteros a NULL y
 * resetea el estado (el render mostrará la pantalla de bienvenida). Si quedan
 * pestañas, ajusta
 * @c active_tab al rango válido y carga el estado de la nueva pestaña activa.
 *
 * @param e Editor cuya pestaña activa se cierra.
 */
void editor_tab_close(Editor *e) {
    if (e->tab_count == 0) return; /* no hay nada que cerrar */

    tab_free_resources(&e->tabs[e->active_tab]);

    /* desplazar las pestañas restantes y limpiar el hueco final */
    for (int i = e->active_tab; i < e->tab_count - 1; i++)
        e->tabs[i] =
            e->tabs[i + 1]; /* copia superficial: ok, no hay alias internos */
    memset(&e->tabs[e->tab_count - 1], 0,
           sizeof(EditorTab)); /* limpiar el duplicado */
    e->tab_count--;

    if (e->tab_count == 0) {
        /* sin pestañas: punteros a NULL (render_frame muestra la bienvenida) */
        e->buf = NULL;
        e->lex = NULL;
        e->undo = NULL;
        e->hl = NULL;
        e->active_tab = 0;
        e->filepath[0] = '\0';
        e->modified = 0;
        e->cursor_line = e->cursor_col = 0;
        e->scroll_line = e->scroll_col = 0;
        editor_sel_clear(e);
    } else {
        /* si la activa era la última, retroceder al nuevo último índice */
        if (e->active_tab >= e->tab_count) e->active_tab = e->tab_count - 1;
        editor_tab_load_state(e);
        editor_update_lexer(e, 0);
        editor_sync_cursor(e);
    }
    e->needs_redraw = 1;
}

/**
 * @brief Cambia a la pestaña de índice @p i.
 *
 * Guarda el estado de la pestaña actual y carga el de la pestaña @p i (lo que
 * puede recargarla del disco si su mtime cambió y no tiene cambios sin
 * guardar). No hace nada si @p i está fuera de rango o ya es la pestaña activa.
 *
 * @param e Editor sobre el que cambiar de pestaña.
 * @param i Índice de la pestaña destino (0-based).
 */
void editor_tab_switch(Editor *e, int i) {
    if (i < 0 || i >= e->tab_count || i == e->active_tab) return;
    editor_tab_save_state(e);
    e->active_tab = i;
    editor_tab_load_state(e); /* recarga si cambió el mtime y no hay cambios */
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}
