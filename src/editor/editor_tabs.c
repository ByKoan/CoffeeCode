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
#include "ext/ext_host.h"

/* Colapsa una hoja vacía del árbol de dock (definida más abajo); la usa
 * editor_tab_close al quedarse una hoja sin pestañas. */
static void editor_unsplit(Editor *e, int group);

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
    t->hl =
        highlighter_default(); /* sin extensión conocida: resaltador genérico */
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
    t->hl = highlighter_for_path(path); /* resaltador según la extensión */
    t->group = e->active_group; /* la nueva pestaña vive en el grupo enfocado */

    e->active_tab = idx;
    e->group_active_tab[e->active_group] = idx;
    editor_tab_load_state(e);
    editor_update_lexer(e, 0); /* marcar todas las líneas para re-tokenizar */
    editor_sync_cursor(e); /* fijar cursor_line/col desde la pos del buffer */
    editor_ext_notify_open(e, t->filepath); /* host: buffer activo + FILE_OPEN */

    /* Arrancar cliente LSP si hay servidor disponible para este lenguaje */
    {
        const char *lang = lsp_language_id_for_path(path);
        const char *cmd = lang ? lsp_server_cmd_for_language(lang) : NULL;
        if (cmd) {
            /* Construir URI del workspace (directorio del archivo) */
            char ws_uri[512];
            char ws_path[512];
            strncpy(ws_path, path, sizeof(ws_path) - 1);
            char *last_sep = strrchr(ws_path, '/');
#ifdef _WIN32
            char *last_sep2 = strrchr(ws_path, '\\');
            if (!last_sep || (last_sep2 && last_sep2 > last_sep))
                last_sep = last_sep2;
#endif
            if (last_sep)
                *last_sep = '\0';
            else
                strncpy(ws_path, ".", sizeof(ws_path) - 1);
            path_to_uri(ws_path, ws_uri, sizeof(ws_uri));

            if (lsp_server_available(lang)) {
                /* El servidor ya esta instalado: arrancarlo directamente */
                if (lsp_start(&t->lsp, cmd, ws_uri)) {
                    size_t txt_len = buf_length(&t->buf);
                    char *txt = (char *)malloc(txt_len + 1);
                    if (txt) {
                        buf_get_text(&t->buf, 0, txt_len, txt);
                        txt[txt_len] = '\0';
                        lsp_open(&t->lsp, path, lang, txt);
                        free(txt);
                        lsp_tokens_full(&t->lsp);
                        t->lsp_active = 1;
                    }
                }
            } else {
                /* No esta instalado: lanzar instalacion automatica en
                 * background. El bucle principal sondeara lsp_install_poll()
                 * cada frame y, cuando termine, reintentara lsp_start()
                 * automaticamente. */
                lsp_install_async(&t->lsp_install, lang);
            }
        }
    }

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
    /* Detener el cliente LSP si estaba activo */
    if (t->lsp_active) {
        lsp_close(&t->lsp);
        lsp_stop(&t->lsp);
        t->lsp_active = 0;
    }
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
        e->hl = NULL;
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
    e->hl = t->hl;
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
    if (e->tab_count == 0) return;   /* sin nada que dividir */
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
