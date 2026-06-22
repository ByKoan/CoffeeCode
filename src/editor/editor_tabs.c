/**
 * @file editor_tabs.c
 * @brief Gestión de pestañas (crear, abrir, cerrar, cambiar) y guardado/
 *        restauración del estado de cada pestaña.
 *
 * Diseño: @c e->buf / @c e->lex / @c e->undo son PUNTEROS directos a
 * @c tabs[active_tab] (no copias por valor), así nunca divergen aunque el
 * buffer haga realloc. @ref editor_tab_load_state redirige esos punteros y
 * restaura los escalares; @ref editor_tab_save_state guarda solo los escalares.
 *
 * Recarga eficiente: al volver a una pestaña NO modificada cuyo fichero cambió
 * en disco (mtime distinto), se re-lee; si hay cambios sin guardar nunca se
 * pisa el trabajo del usuario.
 */
#include "editor_internal.h"
#include "editor/tab_reorder.h"
#include "ext/ext_host.h"

/* Colapsa una hoja vacía del árbol de dock (definida más abajo); la usa
 * editor_tab_close al quedarse una hoja sin pestañas. */
static void editor_unsplit(Editor *e, int group);

/* Indice del flotante cuyo group_id es @p g, o -1 (definida mas abajo); la usa
 * editor_tab_reorder para reparar la invariante del grupo origen flotante. */
static int editor_float_by_group(Editor *e, int g);

/* Notifica al extension host (si existe) que la pestana activa cambio: fija el
 * buffer activo y emite COFFEE_EVENT_FILE_OPEN con la ruta del archivo. */
static void editor_ext_notify_open(Editor *e, const char *path) {
    if (!e->ext_host) return;
    CoffeeHost *host = (CoffeeHost *)e->ext_host;
    ext_host_set_buffer(host, e->buf);
    ext_host_emit(host, COFFEE_EVENT_FILE_OPEN, path);
}

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
 * @brief Lee @p path del disco, detecta su codificación y la decodifica a UTF-8
 *        en el buffer de la pestaña.
 *
 * El buffer del editor siempre trabaja en UTF-8; aquí se convierte el contenido
 * del archivo (sea ANSI, UTF-16…) a UTF-8 al cargar. Devuelve la codificación
 * detectada para guardarla en la pestaña (se usará al volver a guardar).
 *
 * @param t    Pestaña destino (su buffer se reemplaza).
 * @param path Ruta del archivo.
 * @return La codificación detectada (UTF-8 si el archivo no se pudo leer).
 */
static TextEncoding tab_load_decoded(EditorTab *t, const char *path) {
    size_t rawlen = 0;
    void *raw = SDL_LoadFile(path, &rawlen); /* bytes crudos del archivo */
    if (!raw) {
        buf_load_mem(&t->buf, NULL, 0); /* sin archivo: buffer vacío */
        return ENC_UTF8;
    }
    TextEncoding enc =
        encoding_detect((const unsigned char *)raw, rawlen, NULL);
    char *utf8 = NULL;
    size_t utf8len = 0;
    if (encoding_decode(enc, (const unsigned char *)raw, rawlen, &utf8,
                        &utf8len)) {
        buf_load_mem(&t->buf, utf8, utf8len);
        free(utf8);
    } else {
        buf_load_mem(&t->buf, NULL, 0); /* fallo de decodificación */
    }
    SDL_free(raw);
    return enc;
}

void editor_reopen_with_encoding(Editor *e, TextEncoding enc) {
    if (e->tab_count == 0 || !e->filepath[0]) return; /* sin archivo en disco */
    EditorTab *t = &e->tabs[e->active_tab];

    size_t rawlen = 0;
    void *raw = SDL_LoadFile(t->filepath, &rawlen);
    if (!raw) return;
    char *utf8 = NULL;
    size_t utf8len = 0;
    int ok = encoding_decode(enc, (const unsigned char *)raw, rawlen, &utf8,
                             &utf8len);
    SDL_free(raw);
    if (!ok) return;

    /* reemplazar el contenido del buffer con el re-decodificado */
    buf_free(&t->buf);
    buf_init(&t->buf);
    buf_load_mem(&t->buf, utf8, utf8len);
    free(utf8);

    /* la cache del lexer ya no vale: rehacerla al nº de líneas actual */
    lexer_cache_free(&t->lex);
    int total = buf_line_count(&t->buf);
    lexer_cache_init(&t->lex, total > 0 ? total : 1);

    /* e->buf/e->lex ya apuntan a &t->buf/&t->lex (misma dirección): siguen
     * válidos. Solo reseteamos los escalares en vivo y la codificación. */
    t->encoding = enc;
    e->encoding = enc;
    e->cursor_line = e->cursor_col = 0;
    e->scroll_line = e->scroll_col = 0;
    e->modified = 0;
    t->modified = 0;
    e->needs_redraw = 1;
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
    t->encoding = e->encoding;
}

/**
 * @brief Hace de la pestaña activa la "actual": redirige punteros, restaura
 *        escalares y, si procede, recarga el fichero desde disco.
 *
 * Es la operación inversa de ::editor_tab_save_state y el corazón del modelo de
 * pestañas. Paso a paso:
 *   1. Apunta @c e->buf / @c e->lex / @c e->undo al almacenamiento de
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

    /* (2) Recarga eficiente: solo si tiene ruta, no está modificado y cambió el
     * mtime respecto a la última lectura desde disco. */
    if (t->filepath[0] && !t->modified) {
        long current_mtime = file_mtime(t->filepath);
        if (current_mtime != 0 && current_mtime != t->loaded_mtime) {
            /* el fichero cambió fuera del editor: re-leer desde cero */
            buf_free(&t->buf);
            buf_init(&t->buf);
            t->encoding = tab_load_decoded(t, t->filepath);
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
    e->encoding = t->encoding;
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
    /* El resaltado lo decide el render por la extension del archivo (via el
     * host de extensiones); una pestaña nueva sin ruta sale en texto plano. */
    t->group = e->active_group; /* la nueva pestaña vive en el grupo enfocado */

    e->active_tab = idx;
    e->group_active_tab[e->active_group] = idx;
    editor_tab_load_state(e); /* hacerla la activa (redirige punteros) */
    e->needs_redraw = 1;
}

/**
 * @brief Abre el fichero @p path en una pestaña, activándola.
 *
 * Si ese fichero ya está abierto en alguna pestaña, simplemente cambia a ella
 * (guardando antes el estado de la actual y recargando si su mtime cambió). Si
 * no estaba abierto, crea una pestaña nueva, carga el contenido del disco,
 * registra el mtime y la ruta e inicializa la cache del lexer al nº de líneas
 * (el resaltado lo decide el render por la extensión, via el host de
 * extensiones). No abre nada si se alcanzó @c MAX_TABS.
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
            /* enfocar el grupo al que pertenece la pestaña y activarla allí */
            e->active_group = e->tabs[i].group;
            e->active_tab = i;
            e->group_active_tab[e->active_group] = i;
            editor_tab_load_state(e); /* recarga si cambió el mtime */
            editor_update_lexer(e, 0);
            editor_ext_notify_open(e, e->tabs[i].filepath); /* host: FILE_OPEN */
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
    t->encoding = tab_load_decoded(t, path); /* leer y decodificar a UTF-8 */
    t->loaded_mtime = file_mtime(path); /* recordar su mtime para recargas */
    strncpy(t->filepath, path, sizeof(t->filepath) - 1);
    int total = buf_line_count(&t->buf);
    lexer_cache_init(&t->lex, total > 0 ? total : 1); /* cache por línea */
    /* El resaltado se resuelve en el render por la extension del archivo (via el
     * host de extensiones); aqui no se fija ningun resaltador. */
    t->group = e->active_group; /* la nueva pestaña vive en el grupo enfocado */

    e->active_tab = idx;
    e->group_active_tab[e->active_group] = idx;
    editor_tab_load_state(e);
    editor_update_lexer(e, 0); /* marcar todas las líneas para re-tokenizar */
    editor_sync_cursor(e); /* fijar cursor_line/col desde la pos del buffer */
    editor_ext_notify_open(e, t->filepath); /* host: buffer activo + FILE_OPEN */

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

    int closed = e->active_tab;          /* índice global que se elimina */
    int closed_group = e->tabs[closed].group; /* grupo de la pestaña cerrada */

    /* Las decoraciones del host se asocian por direccion de Buffer.  Al cerrar
     * se libera el buffer cerrado y se COMPACTA el array (las pestañas
     * posteriores se mueven a slots de menor indice = otras direcciones).  Para
     * que ninguna decoracion quede colgando de una direccion reciclada o
     * asociada al archivo equivocado, descartamos las de todos los buffers del
     * rango afectado por la compactacion; la extension (p.ej. el LSP) las
     * re-emite al recibir el siguiente FILE_OPEN. */
    if (e->ext_host) {
        CoffeeHost *host = (CoffeeHost *)e->ext_host;
        for (int i = closed; i < e->tab_count; i++)
            ext_host_drop_buffer(host, &e->tabs[i].buf);
    }

    tab_free_resources(&e->tabs[closed]);

    /* desplazar las pestañas restantes y limpiar el hueco final */
    for (int i = closed; i < e->tab_count - 1; i++)
        e->tabs[i] =
            e->tabs[i + 1]; /* copia superficial: ok, no hay alias internos */
    memset(&e->tabs[e->tab_count - 1], 0,
           sizeof(EditorTab)); /* limpiar el duplicado */
    e->tab_count--;

    /* Reparar los índices globales guardados por grupo: cualquiera mayor que el
     * cerrado se desplazó una posición hacia atrás. */
    for (int g = 0; g < MAX_GROUPS; g++)
        if (e->group_active_tab[g] > closed) e->group_active_tab[g]--;

    if (e->tab_count == 0) {
        /* sin pestañas: punteros a NULL (render_frame muestra la bienvenida) */
        e->buf = NULL;
        e->lex = NULL;
        e->undo = NULL;
        e->active_tab = 0;
        e->filepath[0] = '\0';
        e->modified = 0;
        e->cursor_line = e->cursor_col = 0;
        e->scroll_line = e->scroll_col = 0;
        editor_sel_clear(e);
        /* sin pestañas no hay división posible: volver a una sola hoja */
        dock_init_single(&e->dock, 0);
        e->active_group = 0;
        e->pane_active = 0;
        for (int g = 0; g < MAX_GROUPS; g++) e->group_active_tab[g] = 0;
        e->needs_redraw = 1;
        return;
    }

    /* Si el editor está dividido, comprobar si la hoja de la pestaña cerrada se
     * quedó sin pestañas; en ese caso colapsar esa hoja (el hermano hereda). */
    if (e->dock.leaf_count > 1) {
        int remaining = 0;
        for (int i = 0; i < e->tab_count; i++)
            if (e->tabs[i].group == closed_group) remaining++;
        if (remaining == 0) {
            /* esa hoja desaparece: su hermano ocupa el espacio.  Enfocar una
             * pestaña válida de las que sobreviven (la que el árbol dejó con el
             * foco tras colapsar). */
            editor_unsplit(e, closed_group);
            int focus_group = e->dock.nodes[e->dock.focused_leaf].group_id;
            e->active_group = focus_group;
            int next = -1;
            for (int i = 0; i < e->tab_count; i++)
                if (e->tabs[i].group == focus_group) { next = i; break; }
            if (next < 0) next = e->tab_count - 1; /* defensivo */
            e->active_tab = next;
            e->group_active_tab[focus_group] = next;
            editor_tab_load_state(e);
            editor_update_lexer(e, 0);
            editor_sync_cursor(e);
            e->needs_redraw = 1;
            return;
        }
        /* la hoja sobrevive: elegir otra de SUS pestañas como activa */
        int next = -1;
        for (int i = 0; i < e->tab_count; i++)
            if (e->tabs[i].group == closed_group) { next = i; break; }
        e->active_group = closed_group;
        e->dock.focused_leaf = dock_leaf_by_group(&e->dock, closed_group);
        e->active_tab = next;
        e->group_active_tab[closed_group] = next;
        editor_tab_load_state(e);
        editor_update_lexer(e, 0);
        editor_sync_cursor(e);
        e->needs_redraw = 1;
        return;
    }

    /* Caso sin división (una sola hoja): comportamiento de siempre. */
    if (e->active_tab >= e->tab_count) e->active_tab = e->tab_count - 1;
    e->group_active_tab[e->active_group] = e->active_tab;
    editor_tab_load_state(e);
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
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
    /* la pestaña pasa a pertenecer al grupo enfocado y queda como su activa */
    e->tabs[i].group = e->active_group;
    e->group_active_tab[e->active_group] = i;
    editor_tab_load_state(e); /* recarga si cambió el mtime y no hay cambios */
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

/* -- División del editor (árbol de dock) ----------------------------------- */

DockRect editor_dock_area(Editor *e) {
    /* horizontal: tras el explorador (o su botón) y antes del panel de ext. */
    int left = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    int right = e->win_w - (e->ext_panel_open ? e->ext_panel_w : 0);
    /* vertical: bajo la navbar y sobre el panel inferior / status / atajos.
     * El borde superior incluye la franja de la barra de pestañas de cada
     * hoja (cada hoja dibuja la suya en su propio borde superior). */
    int top = NAVBAR_HEIGHT;
    int bottom_h = (e->bottom_panel_open ? e->bottom_panel_h : 0);
    int bottom = e->win_h - STATUS_HEIGHT - editor_shortcut_h(e) - bottom_h;
    DockRect r;
    r.x = left;
    r.y = top;
    r.w = right - left;
    r.h = bottom - top;
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    return r;
}

void editor_render_bind_tab(Editor *e, int idx) {
    if (idx < 0 || idx >= e->tab_count) return;
    EditorTab *t = &e->tabs[idx];
    /* redirigir punteros al almacenamiento de esta pestaña (sin recargar) */
    e->buf = &t->buf;
    e->lex = &t->lex;
    e->undo = &t->undo;
    /* y volcar sus escalares de vista a los campos en vivo para dibujar */
    e->cursor_line = t->cursor_line;
    e->cursor_col = t->cursor_col;
    e->scroll_line = t->scroll_line;
    e->scroll_col = t->scroll_col;
    e->modified = t->modified;
    e->sel_active = t->sel_active;
    e->sel_anchor_line = t->sel_anchor_line;
    e->sel_anchor_col = t->sel_anchor_col;
    e->encoding = t->encoding;
    strncpy(e->filepath, t->filepath, sizeof(e->filepath) - 1);
}

void editor_focus_group(Editor *e, int g) {
    int leaf = dock_leaf_by_group(&e->dock, g);
    if (leaf == DOCK_NONE) return; /* no hay hoja con ese group_id */
    if (g == e->active_group) return;         /* ya enfocado */
    if (e->tab_count == 0) return;            /* sin pestañas: nada que enfocar */

    editor_tab_save_state(e); /* preservar la vista del grupo actual */
    e->active_group = g;
    e->dock.focused_leaf = leaf; /* el árbol también recuerda la hoja con foco */
    /* la pestaña activa pasa a ser la registrada para ese grupo */
    int idx = e->group_active_tab[g];
    if (idx < 0 || idx >= e->tab_count) idx = 0; /* defensivo */
    e->active_tab = idx;
    editor_tab_load_state(e); /* e->buf y escalares -> pestaña del grupo g */
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

void editor_split_dir(Editor *e, DockOrient orient) {
    if (e->tab_count == 0) {
        /* sin nada que dividir: avisar en la barra de estado en vez de no hacer
         * nada en silencio (en la pantalla de bienvenida el usuario no sabria
         * por que el atajo o el boton no responden). */
        snprintf(e->ext_status, sizeof(e->ext_status),
                 "Abre o crea un archivo (Ctrl+N) para dividir el editor");
        e->needs_redraw = 1;
        return;
    }
    if (e->dock.leaf_count >= DOCK_MAX_LEAVES) return; /* tope de hojas */

    int src_group = e->active_group;                     /* grupo enfocado */
    int src_leaf = dock_leaf_by_group(&e->dock, src_group);
    if (src_leaf == DOCK_NONE) return; /* defensivo: foco sin hoja */

    int new_group = dock_alloc_group_id(&e->dock); /* group_id libre */
    if (new_group == DOCK_NONE) return;            /* no quedan ids */

    editor_tab_save_state(e); /* preservar el estado de la pestaña actual */

    /* La hoja origen conserva sus pestañas; la hoja nueva arranca con una.  Si
     * la hoja origen tiene más de una pestaña, MOVEMOS la activa a la nueva; si
     * solo tiene esa, creamos una pestaña vacía para no dejar la origen sin
     * contenido. */
    int src_tab_count = 0;
    for (int i = 0; i < e->tab_count; i++)
        if (e->tabs[i].group == src_group) src_tab_count++;

    /* dividir el árbol: la hoja origen se vuelve un split con la hoja nueva */
    int new_leaf = dock_split_leaf(&e->dock, src_leaf, orient, new_group);
    if (new_leaf == DOCK_NONE) return; /* no se pudo dividir (sin pool) */

    int moved_tab;
    if (src_tab_count > 1) {
        /* mover la pestaña activa actual a la hoja nueva */
        moved_tab = e->active_tab;
        e->tabs[moved_tab].group = new_group;
        /* la hoja origen se queda con otra de SUS pestañas como activa */
        int src_tab = -1;
        for (int i = 0; i < e->tab_count; i++)
            if (e->tabs[i].group == src_group) { src_tab = i; break; }
        e->group_active_tab[src_group] = src_tab; /* >=0: quedaba al menos una */
    } else {
        /* una sola pestaña en la hoja origen: dejarla ahí y crear una vacía en
         * la hoja nueva. */
        int orig_tab = e->active_tab;
        if (e->tab_count >= MAX_TABS) {
            /* sin sitio para otra pestaña: revertir la división del árbol */
            dock_remove_leaf(&e->dock, new_leaf);
            return;
        }
        /* editor_tab_new crea la pestaña en el grupo enfocado (src_group) y la
         * activa; hay que reasignarla a la hoja nueva y restaurar la activa de
         * la origen. */
        editor_tab_new(e);          /* crea y activa una pestaña vacía */
        moved_tab = e->active_tab;  /* la recién creada */
        e->tabs[moved_tab].group = new_group;
        e->group_active_tab[src_group] = orig_tab; /* origen conserva la suya */
    }

    e->group_active_tab[new_group] = moved_tab;
    /* enfocar la hoja nueva para que reciba el teclado */
    e->active_group = src_group;       /* base coherente antes de re-enfocar */
    e->active_tab = e->group_active_tab[src_group];
    editor_tab_load_state(e);
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    editor_focus_group(e, new_group);
    e->needs_redraw = 1;
}

void editor_split(Editor *e) { editor_split_dir(e, DOCK_VERTICAL); }

int editor_drag_target(Editor *e, int mx, int my, int *out_group, int *out_zone,
                       DockRect *out_rect) {
    DockRect area = editor_dock_area(e);
    DockLeafRect leaves[DOCK_MAX_LEAVES];
    int n = dock_compute_leaf_rects(&e->dock, area, leaves, DOCK_MAX_LEAVES);
    for (int i = 0; i < n; i++) {
        DockRect r = leaves[i].rect;
        if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
            if (out_group) *out_group = leaves[i].group_id;
            if (out_zone) *out_zone = (int)dock_drop_zone(r, mx, my);
            if (out_rect) *out_rect = r;
            return 1;
        }
    }
    return 0;
}

/** Cuenta cuantas pestanas pertenecen al grupo @p group. */
static int editor_group_tab_count(Editor *e, int group) {
    int n = 0;
    for (int i = 0; i < e->tab_count; i++)
        if (e->tabs[i].group == group) n++;
    return n;
}

/** Primer indice global de pestana del grupo @p group, o -1 si ninguna. */
static int editor_group_first_tab(Editor *e, int group) {
    for (int i = 0; i < e->tab_count; i++)
        if (e->tabs[i].group == group) return i;
    return -1;
}

/* Indice valido de la pestana activa de @p g (o -1 si vacio).  Transcribe la
 * logica pura de tab_membership.c sobre e->tabs[].group para no copiar todo el
 * array de grupos a cada consulta del render.  Ver tab_membership.h. */
int editor_group_valid_active_tab(Editor *e, int g) {
    int saved = e->group_active_tab[g];
    if (saved >= 0 && saved < e->tab_count && e->tabs[saved].group == g)
        return saved; /* el guardado sigue vivo y pertenece al grupo */
    return editor_group_first_tab(e, g); /* si no, la primera del grupo, o -1 */
}

/* Restaura la invariante de @p g: deja group_active_tab[g] apuntando a una
 * pestana del grupo, o a -1 si quedo vacio.  Se llama tras cada mutacion de
 * membresia (detach a flotante, drop entre hojas, cierre). */
static void editor_group_repair_active(Editor *e, int g) {
    e->group_active_tab[g] = editor_group_valid_active_tab(e, g);
}

void editor_tab_drop(Editor *e, int tab, int target_group, int zone) {
    if (e->tab_count == 0) return;
    if (tab < 0 || tab >= e->tab_count) return;
    if (zone == DOCK_DZ_NONE) return;

    int src_group = e->tabs[tab].group; /* hoja origen de la pestana */

    /* la hoja destino debe existir en el arbol */
    int target_leaf = dock_leaf_by_group(&e->dock, target_group);
    if (target_leaf == DOCK_NONE) return;

    editor_tab_save_state(e); /* preservar la vista actual antes de barajar */

    if (zone == DOCK_DZ_CENTER) {
        /* CENTER: mover la pestana al grupo destino.  No-op si ya esta ahi. */
        if (src_group == target_group) return;
        e->tabs[tab].group = target_group;
        e->group_active_tab[target_group] = tab; /* queda activa en destino */

        /* si la hoja origen se quedo sin pestanas, colapsarla (hermano hereda) */
        if (e->dock.leaf_count > 1 && editor_group_tab_count(e, src_group) == 0)
            editor_unsplit(e, src_group);
    } else {
        /* zonas de borde: dividir la hoja destino y mover la pestana a la nueva.
         * No tiene sentido dividir si la pestana arrastrada es la UNICA de su
         * propia hoja y ademas la hoja destino es esa misma (se dividiria una
         * hoja para moverle su unica pestana: termina igual).  En ese caso
         * tratarlo como no-op. */
        if (src_group == target_group &&
            editor_group_tab_count(e, src_group) <= 1)
            return;

        if (e->dock.leaf_count >= DOCK_MAX_LEAVES) return; /* tope de hojas */
        int new_group = dock_alloc_group_id(&e->dock);
        if (new_group == DOCK_NONE) return; /* sin ids libres */

        /* orientacion + lado segun la zona */
        DockOrient orient = (zone == DOCK_DZ_LEFT || zone == DOCK_DZ_RIGHT)
                                ? DOCK_VERTICAL
                                : DOCK_HORIZONTAL;
        int new_first = (zone == DOCK_DZ_LEFT || zone == DOCK_DZ_TOP);

        int new_leaf = dock_split_leaf_side(&e->dock, target_leaf, orient,
                                            new_group, new_first);
        if (new_leaf == DOCK_NONE) return; /* no se pudo dividir */

        /* mover la pestana arrastrada a la hoja nueva */
        e->tabs[tab].group = new_group;
        e->group_active_tab[new_group] = tab;

        /* el dock_split clono el group_id original de la hoja destino en la otra
         * mitad; su pestana activa registrada sigue siendo valida.  Si la hoja
         * origen (distinta de la destino) se quedo vacia, colapsarla. */
        if (src_group != target_group &&
            editor_group_tab_count(e, src_group) == 0)
            editor_unsplit(e, src_group);

        target_group = new_group; /* enfocar la hoja nueva con la pestana movida */
    }

    /* reparar pestana activa de la hoja origen si sigue viva (invariante: queda
     * apuntando a una pestana suya, o a -1 si se quedo sin pestanas). */
    int sleaf = dock_leaf_by_group(&e->dock, src_group);
    if (sleaf != DOCK_NONE)
        editor_group_repair_active(e, src_group);

    /* enfocar la hoja destino con la pestana recien movida como activa */
    int focus_group = target_group;
    if (dock_leaf_by_group(&e->dock, focus_group) == DOCK_NONE) {
        /* defensivo: si por algun motivo no existe, caer a la hoja con foco */
        focus_group = e->dock.nodes[e->dock.focused_leaf].group_id;
    }
    e->active_group = focus_group;
    e->dock.focused_leaf = dock_leaf_by_group(&e->dock, focus_group);
    e->active_tab = e->group_active_tab[focus_group];
    if (e->active_tab < 0 || e->active_tab >= e->tab_count) {
        int first = editor_group_first_tab(e, focus_group);
        e->active_tab = (first >= 0) ? first : 0;
        e->group_active_tab[focus_group] = e->active_tab;
    }
    if (e->dock.leaf_count <= 1) e->pane_active = 0;
    editor_tab_load_state(e);
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

void editor_tab_reorder(Editor *e, int tab, int target_group, int insert_pos) {
    if (e->tab_count == 0) return;
    if (tab < 0 || tab >= e->tab_count) return;

    /* la hoja/flotante destino debe existir (dock o flotante). */
    int target_leaf = dock_leaf_by_group(&e->dock, target_group);
    int target_float = -1;
    for (int i = 0; i < e->float_count; i++)
        if (e->floats[i].group_id == target_group) { target_float = i; break; }
    if (target_leaf == DOCK_NONE && target_float < 0) return; /* destino invalido */

    int src_group = e->tabs[tab].group;

    /* No-op: reordenar a la MISMA posicion logica dentro del mismo grupo.  Si el
     * grupo no cambia y la posicion de insercion no altera el orden, se evita el
     * trabajo (y el cambio de foco/seleccion).  Se calcula comparando contra la
     * posicion actual de `tab` dentro de su grupo. */
    if (src_group == target_group) {
        int cur_pos = 0;
        for (int i = 0; i < tab; i++)
            if (e->tabs[i].group == src_group) cur_pos++;
        /* insertar en cur_pos o cur_pos+1 deja el orden intacto */
        if (insert_pos == cur_pos || insert_pos == cur_pos + 1) {
            e->needs_redraw = 1;
            return;
        }
    }

    editor_tab_save_state(e); /* preservar la vista actual antes de barajar */

    /* snapshot del grupo de cada pestana para la logica pura */
    int groups[MAX_TABS];
    for (int i = 0; i < e->tab_count; i++) groups[i] = e->tabs[i].group;

    int new_order[MAX_TABS];
    int moved = tab_reorder_move(groups, e->tab_count, tab, target_group,
                                 insert_pos, &e->active_tab, e->group_active_tab,
                                 MAX_GROUPS, new_order);

    /* aplicar la permutacion a la tabla de pestanas (reordenar e->tabs[]). */
    EditorTab tmp[MAX_TABS];
    for (int i = 0; i < e->tab_count; i++) tmp[i] = e->tabs[new_order[i]];
    for (int i = 0; i < e->tab_count; i++) e->tabs[i] = tmp[i];
    /* la logica pura ya fijo groups[]; reflejar el grupo de la pestana movida. */
    e->tabs[moved].group = target_group;

    /* si la hoja origen se quedo sin pestanas, colapsarla (solo dock). */
    if (src_group != target_group && e->dock.leaf_count > 1 &&
        editor_group_tab_count(e, src_group) == 0)
        editor_unsplit(e, src_group);

    /* reparar la invariante de la hoja/flotante origen si sigue viva. */
    if (dock_leaf_by_group(&e->dock, src_group) != DOCK_NONE ||
        editor_float_by_group(e, src_group) >= 0)
        editor_group_repair_active(e, src_group);

    /* enfocar el grupo destino con la pestana movida como activa. */
    e->active_group = target_group;
    int dleaf = dock_leaf_by_group(&e->dock, target_group);
    if (dleaf != DOCK_NONE) e->dock.focused_leaf = dleaf;
    e->active_tab = moved;
    e->group_active_tab[target_group] = moved;
    if (e->dock.leaf_count <= 1) e->pane_active = 0;

    editor_tab_load_state(e);
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

/**
 * @brief Colapsa una hoja del árbol cuando se queda sin pestañas: la elimina y
 *        su hermano ocupa el sitio.  Si quedaba una sola hoja, no hace nada.
 *
 * Se llama desde editor_tab_close al cerrar la última pestaña de una hoja.  El
 * estado de pane queda inactivo si tras esto solo queda una hoja.
 *
 * @param e     Editor.
 * @param group group_id de la hoja que se quedó vacía.
 */
static void editor_unsplit(Editor *e, int group) {
    int leaf = dock_leaf_by_group(&e->dock, group);
    if (leaf == DOCK_NONE) return;
    dock_remove_leaf(&e->dock, leaf); /* el hermano hereda el espacio */
    if (e->dock.leaf_count <= 1) e->pane_active = 0; /* sin división: limpiar */
}

/* -- Paneles flotantes ----------------------------------------------------- */

/**
 * @brief Indica si algun panel flotante usa el group_id @p g.
 */
static int editor_group_in_floats(Editor *e, int g) {
    for (int i = 0; i < e->float_count; i++)
        if (e->floats[i].group_id == g) return 1;
    return 0;
}

/**
 * @brief Asigna el primer group_id libre en [0, MAX_GROUPS) que no use NI una
 *        hoja del dock NI un flotante.
 *
 * dock_alloc_group_id solo evita las hojas del dock (rango [0,DOCK_MAX_LEAVES));
 * los flotantes comparten el mismo espacio de ids (group_active_tab[] se indexa
 * por group_id), asi que aqui se escanea el rango completo evitando ambos.
 *
 * @return Un group_id sin usar, o -1 si todos estan ocupados.
 */
static int editor_alloc_group_id(Editor *e) {
    for (int g = 0; g < MAX_GROUPS; g++) {
        if (dock_leaf_by_group(&e->dock, g) != DOCK_NONE) continue; /* hoja */
        if (editor_group_in_floats(e, g)) continue;                 /* flotante */
        return g;
    }
    return -1;
}

/** Indice del flotante cuyo group_id es @p g, o -1 si ninguno. */
static int editor_float_by_group(Editor *e, int g) {
    for (int i = 0; i < e->float_count; i++)
        if (e->floats[i].group_id == g) return i;
    return -1;
}

Rect editor_float_bounds(Editor *e) {
    /* toda la ventana bajo la navbar y sobre la barra de estado: asi la barra de
     * titulo de un flotante nunca tapa la navbar ni desaparece bajo el status. */
    Rect b;
    b.x = 0;
    b.y = NAVBAR_HEIGHT;
    b.w = e->win_w;
    b.h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT - editor_shortcut_h(e);
    if (b.w < 0) b.w = 0;
    if (b.h < 0) b.h = 0;
    return b;
}

void editor_float_focus(Editor *e, int fi) {
    if (fi < 0 || fi >= e->float_count) return;
    /* traer al frente: rotar el array para que el flotante quede el ultimo */
    FloatPanel fp = e->floats[fi];
    for (int i = fi; i < e->float_count - 1; i++) e->floats[i] = e->floats[i + 1];
    e->floats[e->float_count - 1] = fp;
    /* enfocar su grupo (carga su pestana activa como la activa del editor) */
    editor_tab_save_state(e);
    e->active_group = fp.group_id;
    int idx = e->group_active_tab[fp.group_id];
    if (idx < 0 || idx >= e->tab_count) idx = editor_group_first_tab(e, fp.group_id);
    if (idx < 0) idx = 0; /* defensivo */
    e->active_tab = idx;
    e->group_active_tab[fp.group_id] = idx;
    editor_tab_load_state(e);
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

void editor_float_detach_tab(Editor *e, int tab, int cx, int cy) {
    if (e->tab_count == 0) return;
    if (tab < 0 || tab >= e->tab_count) return;
    if (e->float_count >= FLOAT_MAX_PANELS) return; /* sin sitio para mas flotantes */

    int new_group = editor_alloc_group_id(e);
    if (new_group < 0) return; /* sin ids libres */

    int src_group = e->tabs[tab].group; /* hoja/flotante origen */
    int src_is_float = (editor_float_by_group(e, src_group) >= 0);

    editor_tab_save_state(e);

    /* mover la pestana al grupo nuevo */
    e->tabs[tab].group = new_group;
    e->group_active_tab[new_group] = tab;

    /* construir el flotante centrado en (cx,cy), recortado a los limites */
    FloatPanel fp;
    fp.group_id = new_group;
    fp.rect.w = FLOAT_DEFAULT_W;
    fp.rect.h = FLOAT_DEFAULT_H;
    Rect bounds = editor_float_bounds(e);
    fp.rect = float_clamp_move(fp.rect, cx - FLOAT_DEFAULT_W / 2,
                               cy - FLOAT_TITLEBAR_H / 2, bounds);
    e->floats[e->float_count++] = fp;

    /* si la hoja de dock origen se quedo vacia, colapsarla; si el origen era otro
     * flotante que se quedo sin pestanas, cerrarlo. */
    if (!src_is_float) {
        if (e->dock.leaf_count > 1 && editor_group_tab_count(e, src_group) == 0)
            editor_unsplit(e, src_group);
    } else {
        if (editor_group_tab_count(e, src_group) == 0) {
            int sf = editor_float_by_group(e, src_group);
            if (sf >= 0) {
                for (int i = sf; i < e->float_count - 1; i++)
                    e->floats[i] = e->floats[i + 1];
                e->float_count--;
            }
        }
    }

    /* Restaurar la invariante en la hoja/flotante origen.  CRITICO: si el origen
     * era la hoja raiz del dock (que NO colapsa) y perdio su UNICA pestana, su
     * group_active_tab seguia apuntando a la pestana ya movida al flotante; sin
     * fijarlo a -1 el dock dibujaria el buffer del flotante (bug del buffer
     * compartido).  editor_group_repair_active lo deja en -1 cuando el grupo
     * queda vacio. */
    if (dock_leaf_by_group(&e->dock, src_group) != DOCK_NONE ||
        editor_float_by_group(e, src_group) >= 0)
        editor_group_repair_active(e, src_group);

    /* enfocar el flotante recien creado (queda al frente) */
    int fi = editor_float_by_group(e, new_group);
    editor_float_focus(e, fi);
    e->needs_redraw = 1;
}

void editor_float_close(Editor *e, int fi) {
    if (fi < 0 || fi >= e->float_count) return;
    int group = e->floats[fi].group_id;

    /* cerrar todas las pestanas de ese grupo.  editor_tab_close compacta el array
     * de pestanas, asi que se reescanea desde el principio en cada vuelta. */
    int guard = 0;
    for (;;) {
        int found = -1;
        for (int i = 0; i < e->tab_count; i++)
            if (e->tabs[i].group == group) { found = i; break; }
        if (found < 0) break;
        e->active_tab = found;
        e->active_group = group; /* base coherente para editor_tab_close */
        editor_tab_close(e);
        if (++guard > MAX_TABS + 1) break; /* defensivo anti-bucle */
    }

    /* quitar el flotante del array (su indice puede haber cambiado si
     * editor_tab_close toco floats, asi que se busca por group_id). */
    int idx = editor_float_by_group(e, group);
    if (idx >= 0) {
        for (int i = idx; i < e->float_count - 1; i++)
            e->floats[i] = e->floats[i + 1];
        e->float_count--;
    }

    /* el foco puede haber quedado en este grupo muerto: reubicarlo */
    if (e->active_group == group || dock_leaf_by_group(&e->dock, e->active_group) ==
                                        DOCK_NONE) {
        int g = e->dock.nodes[e->dock.focused_leaf].group_id;
        e->active_group = g;
        if (e->tab_count > 0) {
            int idx2 = e->group_active_tab[g];
            if (idx2 < 0 || idx2 >= e->tab_count || e->tabs[idx2].group != g)
                idx2 = editor_group_first_tab(e, g);
            if (idx2 >= 0) {
                e->active_tab = idx2;
                e->group_active_tab[g] = idx2;
                editor_tab_load_state(e);
                editor_update_lexer(e, 0);
                editor_sync_cursor(e);
            }
        }
    }
    e->needs_redraw = 1;
}

void editor_float_gc_empty(Editor *e) {
    for (int i = 0; i < e->float_count;) {
        if (editor_group_tab_count(e, e->floats[i].group_id) == 0) {
            for (int j = i; j < e->float_count - 1; j++)
                e->floats[j] = e->floats[j + 1];
            e->float_count--; /* no avanzar i: el siguiente ocupo este hueco */
        } else {
            i++;
        }
    }
}

void editor_float_dock(Editor *e, int fi) {
    if (fi < 0 || fi >= e->float_count) return;
    int group = e->floats[fi].group_id;
    int active = e->group_active_tab[group]; /* pestana activa del flotante */

    /* hoja de dock destino = la enfocada del arbol */
    int dst_group = e->dock.nodes[e->dock.focused_leaf].group_id;
    if (dst_group == group) {
        /* el foco esta en el propio flotante: elegir cualquier hoja del dock */
        dst_group = e->dock.nodes[e->dock.root].kind == DOCK_LEAF
                        ? e->dock.nodes[e->dock.root].group_id
                        : -1;
        if (dst_group < 0) {
            /* buscar la primera hoja del arbol */
            DockRect area = editor_dock_area(e);
            DockLeafRect leaves[DOCK_MAX_LEAVES];
            int n = dock_compute_leaf_rects(&e->dock, area, leaves,
                                            DOCK_MAX_LEAVES);
            if (n > 0) dst_group = leaves[0].group_id;
        }
    }
    if (dst_group < 0 || dock_leaf_by_group(&e->dock, dst_group) == DOCK_NONE)
        return; /* sin destino valido */

    editor_tab_save_state(e);

    /* mover TODAS las pestanas del flotante a la hoja destino */
    for (int i = 0; i < e->tab_count; i++)
        if (e->tabs[i].group == group) e->tabs[i].group = dst_group;
    if (active >= 0 && active < e->tab_count && e->tabs[active].group == dst_group)
        e->group_active_tab[dst_group] = active; /* su activa sigue siendo activa */

    /* quitar el flotante del array */
    int idx = editor_float_by_group(e, group);
    if (idx >= 0) {
        for (int i = idx; i < e->float_count - 1; i++)
            e->floats[i] = e->floats[i + 1];
        e->float_count--;
    }

    /* enfocar la hoja destino con la pestana movida */
    e->active_group = dst_group;
    e->dock.focused_leaf = dock_leaf_by_group(&e->dock, dst_group);
    int idx2 = e->group_active_tab[dst_group];
    if (idx2 < 0 || idx2 >= e->tab_count || e->tabs[idx2].group != dst_group)
        idx2 = editor_group_first_tab(e, dst_group);
    if (idx2 >= 0) {
        e->active_tab = idx2;
        e->group_active_tab[dst_group] = idx2;
    }
    editor_tab_load_state(e);
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

int editor_float_dock_target(Editor *e, int drag_fi, int mx, int my,
                             int *out_group, int *out_zone) {
    /* Sobre OTRO flotante: nunca se acopla (el flotante esta encima del dock). */
    for (int i = 0; i < e->float_count; i++) {
        if (i == drag_fi) continue;            /* el propio flotante no estorba */
        if (rect_has(e->floats[i].rect, mx, my)) return 0;
    }
    /* Sobre una hoja del dock: su group_id + la zona de drop dentro de su rect. */
    int group = -1, zone = DOCK_DZ_NONE;
    if (!editor_drag_target(e, mx, my, &group, &zone, NULL)) return 0;
    if (zone == DOCK_DZ_NONE) return 0;
    if (out_group) *out_group = group;
    if (out_zone) *out_zone = zone;
    return 1;
}

void editor_float_dock_to(Editor *e, int fi, int target_group, int zone) {
    if (fi < 0 || fi >= e->float_count) return;
    if (zone == DOCK_DZ_NONE) return;

    int group = e->floats[fi].group_id;        /* grupo del flotante a acoplar */
    int active = e->group_active_tab[group];   /* su pestana activa            */

    /* la hoja destino debe existir en el arbol de dock */
    int target_leaf = dock_leaf_by_group(&e->dock, target_group);
    if (target_leaf == DOCK_NONE) return;

    editor_tab_save_state(e);

    int dst_group = target_group; /* grupo final donde caeran las pestanas */

    if (zone != DOCK_DZ_CENTER) {
        /* zonas de borde: dividir la hoja destino y volcar las pestanas a la
         * hoja nueva (misma maquinaria que editor_tab_drop). */
        if (e->dock.leaf_count >= DOCK_MAX_LEAVES) return; /* tope de hojas */
        int new_group = dock_alloc_group_id(&e->dock);
        if (new_group == DOCK_NONE) return; /* sin ids libres */

        DockOrient orient = (zone == DOCK_DZ_LEFT || zone == DOCK_DZ_RIGHT)
                                ? DOCK_VERTICAL
                                : DOCK_HORIZONTAL;
        int new_first = (zone == DOCK_DZ_LEFT || zone == DOCK_DZ_TOP);
        int new_leaf = dock_split_leaf_side(&e->dock, target_leaf, orient,
                                            new_group, new_first);
        if (new_leaf == DOCK_NONE) return; /* no se pudo dividir */
        dst_group = new_group;
    }

    /* mover TODAS las pestanas del flotante a la hoja destino */
    for (int i = 0; i < e->tab_count; i++)
        if (e->tabs[i].group == group) e->tabs[i].group = dst_group;
    if (active >= 0 && active < e->tab_count && e->tabs[active].group == dst_group)
        e->group_active_tab[dst_group] = active; /* su activa sigue activa */

    /* quitar el flotante del array (su grupo ya no tiene pestanas) */
    int idx = editor_float_by_group(e, group);
    if (idx >= 0) {
        for (int i = idx; i < e->float_count - 1; i++)
            e->floats[i] = e->floats[i + 1];
        e->float_count--;
    }

    /* enfocar la hoja destino con la pestana movida como activa */
    e->active_group = dst_group;
    e->dock.focused_leaf = dock_leaf_by_group(&e->dock, dst_group);
    int fidx = e->group_active_tab[dst_group];
    if (fidx < 0 || fidx >= e->tab_count || e->tabs[fidx].group != dst_group)
        fidx = editor_group_first_tab(e, dst_group);
    if (fidx >= 0) {
        e->active_tab = fidx;
        e->group_active_tab[dst_group] = fidx;
    }
    if (e->dock.leaf_count <= 1) e->pane_active = 0;
    editor_tab_load_state(e);
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

void editor_focus_detached_group(Editor *e, int group) {
    if (e->tab_count == 0) return;
    int idx = editor_group_valid_active_tab(e, group);
    if (idx < 0) return; /* grupo vacio: nada que enfocar */
    if (group == e->active_group && idx == e->active_tab) return; /* ya activo */

    editor_tab_save_state(e); /* preservar la vista del grupo actual */
    e->active_group = group;
    e->active_tab = idx;
    e->group_active_tab[group] = idx;
    editor_tab_load_state(e); /* e->buf y escalares -> pestana del grupo */
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

void editor_detached_reattach_group(Editor *e, int group) {
    /* sin pestanas en el grupo: nada que re-acoplar */
    if (editor_group_tab_count(e, group) == 0) return;

    int active = e->group_active_tab[group]; /* pestana activa del grupo origen */

    /* hoja de dock destino = la enfocada del arbol.  Si el foco quedo en el
     * grupo de la ventana desprendida (no es una hoja del dock), elegir la
     * primera hoja del arbol. */
    int dst_group = e->dock.nodes[e->dock.focused_leaf].group_id;
    if (dock_leaf_by_group(&e->dock, dst_group) == DOCK_NONE) {
        DockRect area = editor_dock_area(e);
        DockLeafRect leaves[DOCK_MAX_LEAVES];
        int n = dock_compute_leaf_rects(&e->dock, area, leaves, DOCK_MAX_LEAVES);
        dst_group = (n > 0) ? leaves[0].group_id : -1;
    }
    if (dst_group < 0 || dock_leaf_by_group(&e->dock, dst_group) == DOCK_NONE)
        return; /* sin destino valido */

    editor_tab_save_state(e);

    /* mover TODAS las pestanas del grupo origen a la hoja destino */
    for (int i = 0; i < e->tab_count; i++)
        if (e->tabs[i].group == group) e->tabs[i].group = dst_group;
    if (active >= 0 && active < e->tab_count && e->tabs[active].group == dst_group)
        e->group_active_tab[dst_group] = active; /* su activa sigue activa */

    /* enfocar la hoja destino con una pestana valida del grupo */
    e->active_group = dst_group;
    e->dock.focused_leaf = dock_leaf_by_group(&e->dock, dst_group);
    int idx = e->group_active_tab[dst_group];
    if (idx < 0 || idx >= e->tab_count || e->tabs[idx].group != dst_group)
        idx = editor_group_first_tab(e, dst_group);
    if (idx >= 0) {
        e->active_tab = idx;
        e->group_active_tab[dst_group] = idx;
    }
    if (e->dock.leaf_count <= 1) e->pane_active = 0;
    editor_tab_load_state(e);
    editor_update_lexer(e, 0);
    editor_sync_cursor(e);
    e->needs_redraw = 1;
}

/* ===========================================================================
 *  Transferencia de pestanas ENTRE instancias de Editor (multi-ventana)
 *
 * Cada ventana del IDE es un Editor completo (su propio tabs[]/dock/grupos).
 * Mover una pestana de una ventana a otra es una COPIA SUPERFICIAL del struct
 * EditorTab (que POSEE su buf/lex/undo sin alias internos: el mismo
 * razonamiento que editor_tab_close al compactar tabs[]) seguida de limpiar el
 * slot origen.  No se re-lee del disco ni se recrea ningun recurso.
 * =========================================================================== */

/* group_id de la hoja del dock con foco de @p dst (destino de las pestanas
 * movidas).  Si el foco no apunta a una hoja viva, devuelve la primera hoja del
 * arbol; -1 si @p dst no tiene ninguna hoja (no deberia pasar). */
static int editor_dst_focus_group(Editor *dst) {
    int g = dst->dock.nodes[dst->dock.focused_leaf].group_id;
    if (dock_leaf_by_group(&dst->dock, g) != DOCK_NONE) return g;
    DockRect area = editor_dock_area(dst);
    DockLeafRect leaves[DOCK_MAX_LEAVES];
    int n = dock_compute_leaf_rects(&dst->dock, area, leaves, DOCK_MAX_LEAVES);
    return (n > 0) ? leaves[0].group_id : -1;
}

/* Reenfoca @p dst sobre @p dst_group con una pestana valida como activa (tras
 * recibir pestanas movidas).  Vuelca el estado "en vivo" a esa pestana. */
static void editor_focus_dst_group(Editor *dst, int dst_group) {
    dst->active_group = dst_group;
    dst->dock.focused_leaf = dock_leaf_by_group(&dst->dock, dst_group);
    int idx = dst->group_active_tab[dst_group];
    if (idx < 0 || idx >= dst->tab_count || dst->tabs[idx].group != dst_group)
        idx = editor_group_first_tab(dst, dst_group);
    if (idx >= 0) {
        dst->active_tab = idx;
        dst->group_active_tab[dst_group] = idx;
        if (dst->dock.leaf_count <= 1) dst->pane_active = 0;
        editor_tab_load_state(dst);
        editor_update_lexer(dst, 0);
        editor_sync_cursor(dst);
    }
    dst->needs_redraw = 1;
}

/* Quita del array tabs[] de @p src la pestana de indice @p idx (ya copiada al
 * destino), compactando y reparando los indices guardados de @p src
 * (active_tab + group_active_tab[]) igual que editor_tab_close. */
static void editor_src_remove_tab(Editor *src, int idx) {
    /* el slot ya fue copiado al destino: NO liberar sus recursos aqui. */
    for (int i = idx; i < src->tab_count - 1; i++) src->tabs[i] = src->tabs[i + 1];
    memset(&src->tabs[src->tab_count - 1], 0, sizeof(EditorTab));
    src->tab_count--;
    if (src->active_tab > idx) src->active_tab--;
    for (int g = 0; g < MAX_GROUPS; g++)
        if (src->group_active_tab[g] > idx) src->group_active_tab[g]--;
}

/* Mueve la pestana de indice @p src_idx de @p src al final de tabs[] de @p dst,
 * asignandole el grupo @p dst_group.  Devuelve el indice destino, o -1 si @p dst
 * no tiene sitio (MAX_TABS).  Copia superficial + limpieza del slot origen. */
static int editor_move_tab_between(Editor *src, int src_idx, Editor *dst,
                                   int dst_group) {
    if (dst->tab_count >= MAX_TABS) return -1;
    int dst_idx = dst->tab_count++;
    dst->tabs[dst_idx] = src->tabs[src_idx]; /* copia superficial del struct */
    dst->tabs[dst_idx].group = dst_group;
    editor_src_remove_tab(src, src_idx);
    return dst_idx;
}

/* Deja @p src sin pestanas en el estado de bienvenida (una sola hoja vacia). */
static void editor_reset_empty(Editor *src) {
    src->buf = NULL;
    src->lex = NULL;
    src->undo = NULL;
    src->active_tab = 0;
    src->filepath[0] = '\0';
    src->modified = 0;
    dock_init_single(&src->dock, 0);
    src->active_group = 0;
    src->pane_active = 0;
    for (int g = 0; g < MAX_GROUPS; g++) src->group_active_tab[g] = 0;
}

/* Reenfoca @p src tras perder pestanas: si quedan, sobre una pestana viva; si no,
 * deja el estado de bienvenida. */
static void editor_src_refocus_after_loss(Editor *src) {
    if (src->tab_count == 0) {
        editor_reset_empty(src);
        return;
    }
    int fg = src->dock.nodes[src->dock.focused_leaf].group_id;
    int idx = editor_group_valid_active_tab(src, fg);
    if (idx < 0) { /* el grupo con foco quedo vacio: tomar cualquier pestana */
        idx = 0;
        fg = src->tabs[0].group;
    }
    src->active_group = fg;
    src->dock.focused_leaf = dock_leaf_by_group(&src->dock, fg);
    if (src->dock.focused_leaf == DOCK_NONE) src->dock.focused_leaf = 0;
    src->active_tab = idx;
    src->group_active_tab[fg] = idx;
    if (src->dock.leaf_count <= 1) src->pane_active = 0;
    editor_tab_load_state(src);
    editor_update_lexer(src, 0);
    editor_sync_cursor(src);
}

void editor_transfer_group(Editor *src, int src_group, Editor *dst) {
    if (!src || !dst) return;
    if (editor_group_tab_count(src, src_group) == 0) return;

    int dst_group = editor_dst_focus_group(dst);
    if (dst_group < 0) return;

    if (src->tab_count > 0) editor_tab_save_state(src);
    if (dst->tab_count > 0) editor_tab_save_state(dst);

    /* indice global en src de la pestana activa del grupo origen (para hacerla
     * activa en el destino tras moverla). */
    int src_active = editor_group_valid_active_tab(src, src_group);
    int dst_active = -1;

    /* mover todas las pestanas del grupo origen: cada move compacta src, asi que
     * se re-escanea desde el principio. */
    for (;;) {
        int si = editor_group_first_tab(src, src_group);
        if (si < 0) break;
        int was_active = (si == src_active);
        if (src_active > si) src_active--; /* el move desplazara los > si */
        int di = editor_move_tab_between(src, si, dst, dst_group);
        if (di < 0) break; /* destino lleno: dejar el resto en origen */
        if (was_active) dst_active = di;
    }

    if (dst_active >= 0) dst->group_active_tab[dst_group] = dst_active;

    /* si la hoja origen quedo vacia y src estaba dividido, colapsarla. */
    if (src->dock.leaf_count > 1 &&
        editor_group_tab_count(src, src_group) == 0 &&
        dock_leaf_by_group(&src->dock, src_group) != DOCK_NONE) {
        editor_unsplit(src, src_group);
    }
    editor_src_refocus_after_loss(src);
    src->needs_redraw = 1;

    editor_focus_dst_group(dst, dst_group);
}

/* Tras mover UNA pestana al grupo @p dst_group de @p dst, la recoloca segun la
 * zona de drop @p zone dentro de @p dst: CENTER la deja en ese grupo; las zonas
 * de borde dividen la hoja destino y mueven la pestana a la hoja nueva.  @p tab
 * es el indice GLOBAL en dst de la pestana recien movida.  Devuelve el group_id
 * final donde quedo la pestana (puede diferir de @p dst_group si hubo division),
 * o @p dst_group si no se pudo dividir (cae a CENTER).  Misma logica de borde que
 * editor_tab_drop, pero la pestana YA esta en @p dst. */
static int editor_place_tab_zone(Editor *dst, int tab, int dst_group, int zone) {
    if (zone == DOCK_DZ_NONE || zone == DOCK_DZ_CENTER) return dst_group;

    int target_leaf = dock_leaf_by_group(&dst->dock, dst_group);
    if (target_leaf == DOCK_NONE) return dst_group;
    if (dst->dock.leaf_count >= DOCK_MAX_LEAVES) return dst_group; /* tope */
    int new_group = dock_alloc_group_id(&dst->dock);
    if (new_group == DOCK_NONE) return dst_group; /* sin ids libres */

    DockOrient orient = (zone == DOCK_DZ_LEFT || zone == DOCK_DZ_RIGHT)
                            ? DOCK_VERTICAL
                            : DOCK_HORIZONTAL;
    int new_first = (zone == DOCK_DZ_LEFT || zone == DOCK_DZ_TOP);
    int new_leaf = dock_split_leaf_side(&dst->dock, target_leaf, orient,
                                        new_group, new_first);
    if (new_leaf == DOCK_NONE) return dst_group; /* no se pudo dividir */

    dst->tabs[tab].group = new_group;             /* mover a la hoja nueva */
    dst->group_active_tab[new_group] = tab;
    return new_group;
}

void editor_transfer_tab(Editor *src, int tab, Editor *dst, int dst_mx,
                         int dst_my) {
    if (!src || !dst || src == dst) return;
    if (tab < 0 || tab >= src->tab_count) return;

    /* hoja + zona destino bajo el cursor en la ventana receptora.  Si el cursor
     * no cae sobre ninguna hoja (raro: borde de la ventana), usar la hoja con
     * foco de dst y zona CENTER. */
    int dst_group = -1, zone = DOCK_DZ_NONE;
    if (!editor_drag_target(dst, dst_mx, dst_my, &dst_group, &zone, NULL) ||
        dst_group < 0 || zone == DOCK_DZ_NONE) {
        dst_group = editor_dst_focus_group(dst);
        zone = DOCK_DZ_CENTER;
    }
    if (dst_group < 0) return; /* dst sin hojas (no deberia pasar) */

    if (src->tab_count > 0) editor_tab_save_state(src);
    if (dst->tab_count > 0) editor_tab_save_state(dst);

    int src_group = src->tabs[tab].group; /* hoja origen de la pestana */

    /* mover la pestana (copia superficial del struct + limpieza del slot src). */
    int di = editor_move_tab_between(src, tab, dst, dst_group);
    if (di < 0) {                /* destino lleno: deshacer (nada movido) y salir */
        editor_src_refocus_after_loss(src); /* re-enlazar la vista de src */
        return;
    }
    dst->group_active_tab[dst_group] = di;

    /* aplicar la zona de drop en dst (CENTER deja en dst_group; borde divide). */
    int final_group = editor_place_tab_zone(dst, di, dst_group, zone);

    /* reparar el origen: colapsar su hoja si quedo vacia (y src estaba dividido)
     * y re-enfocar sobre una pestana viva (o el estado de bienvenida). */
    if (src->dock.leaf_count > 1 &&
        editor_group_tab_count(src, src_group) == 0 &&
        dock_leaf_by_group(&src->dock, src_group) != DOCK_NONE) {
        editor_unsplit(src, src_group);
    }
    editor_src_refocus_after_loss(src);
    src->needs_redraw = 1;

    /* enfocar dst sobre la hoja final con la pestana movida como activa. */
    editor_focus_dst_group(dst, final_group);
}

void editor_merge_all(Editor *src, Editor *dst) {
    if (!src || !dst || src->tab_count == 0) return;

    int dst_group = editor_dst_focus_group(dst);
    if (dst_group < 0) return;

    editor_tab_save_state(src);
    if (dst->tab_count > 0) editor_tab_save_state(dst);

    /* pestana globalmente activa de src -> activa en el destino tras moverla */
    int src_active = (src->active_tab >= 0 && src->active_tab < src->tab_count)
                         ? src->active_tab
                         : 0;
    int dst_active = -1;

    /* mover TODAS las pestanas (aplanando sus grupos) a la hoja destino */
    while (src->tab_count > 0) {
        int was_active = (0 == src_active);
        if (src_active > 0) src_active--;
        int di = editor_move_tab_between(src, 0, dst, dst_group);
        if (di < 0) break; /* destino lleno */
        if (was_active) dst_active = di;
    }

    if (dst_active >= 0) dst->group_active_tab[dst_group] = dst_active;

    if (src->tab_count == 0) editor_reset_empty(src);
    src->needs_redraw = 1;
    editor_focus_dst_group(dst, dst_group);
}
