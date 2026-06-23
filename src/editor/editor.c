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
#include "builtin/lang_c.h"
#include "editor_internal.h"
#include "ext/ext_host.h"
#include "input/input.h"
#include "layout/layout.h"
#include "render/render.h"
#include "session/layout_persist.h"
#include "utf8/utf8.h"
#include <SDL3_image/SDL_image.h>

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
static TTF_Font *load_editor_font(float size, const char *path) {
    /* 1) ruta explícita elegida en preferencias (settings.font_path) */
    if (path && path[0]) {
        TTF_Font *f = TTF_OpenFont(path, size);
        if (f) return f;
    }
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

void editor_reload_font(Editor *e) {
    /* En una ventana secundaria la fuente es COMPARTIDA (la posee la principal):
     * no la recargamos ni la cerramos aqui para no liberar un puntero ajeno.  El
     * cambio de fuente se hace en la ventana principal y las secundarias lo
     * reflejan al copiar de nuevo sus metricas (no critico para v1). */
    if (e->is_secondary) return;
    /* Cargar la nueva fuente en una variable temporal: si falla (ruta inválida
     * o tamaño imposible) se conserva la actual y no se rompe el editor. */
    TTF_Font *nf =
        load_editor_font(e->settings.font_size, e->settings.font_path);
    if (!nf) return;

    if (e->font) TTF_CloseFont(e->font);
    e->font = nf;
    e->font_size = e->settings.font_size;
    e->line_height = e->settings.font_size + 4;

    /* Re-medir el ancho de carácter de la fuente nueva. */
    int w = 0, h = 0;
    TTF_GetStringSize(e->font, "M", 1, &w, &h);
    e->char_w = w > 0 ? w : e->font_size / 2;
    e->needs_redraw = 1;
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

    /* La columna es ANCHO DE DISPLAY (celdas): avanzar carácter a carácter
     * acumulando su ancho hasta alcanzar `col`, sin pasar del final de la
     * línea. Si `col` cae DENTRO de un carácter de doble ancho, se para justo
     * antes (el cursor se ajusta al límite de carácter más cercano por la
     * izquierda). */
    if (col < 0) col = 0;
    size_t p = line_start;
    int w = 0;
    while (p < line_end && w < col) {
        uint32_t cp;
        int n = buf_decode_at(b, p, line_end, &cp);
        int cw = utf8_cp_width(cp);
        if (w + cw > col) break; /* col cae dentro de un carácter ancho */
        w += cw;
        p += (size_t)n;
    }
    return p;
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
    int bottom_h = e->bottom_panel_open ? e->bottom_panel_h : 0;
    int vis_lines = (e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT -
                     editor_shortcut_h(e) - bottom_h) /
                    e->line_height;
    int vis_cols =
        (e->win_w - left_off - editor_gutter_w(e) - PADDING_LEFT) / e->char_w;

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

/* -- Hooks de UI que el editor da al extension host ------------------
 * El host invoca estos callbacks (con el Editor como userdata) cuando una
 * extension usa CoffeeApi::set_status / output_append / output_clear /
 * request_repaint, de modo que el efecto llegue a la UI real del IDE. */

/** set_status: guarda el mensaje para mostrarlo en la barra de estado real. */
static void ext_hook_set_status(void *ud, const char *msg) {
    Editor *e = (Editor *)ud;
    if (!e) return;
    snprintf(e->ext_status, sizeof(e->ext_status), "%s", msg ? msg : "");
    e->needs_redraw = 1;
}

/** output_append: anyade texto al canal por defecto "salida" del panel
 *  inferior.  Abre el panel inferior para que el usuario VEA la salida. */
static void ext_hook_output_append(void *ud, const char *text) {
    Editor *e = (Editor *)ud;
    if (!e || !text) return;
    panel_append(&e->panels, PANEL_DEFAULT_CHANNEL, text);
    e->bottom_panel_open = 1; /* mostrar la salida cuando llega texto */
    e->needs_redraw = 1;
}

/** output_clear: vacia el canal por defecto "salida". */
static void ext_hook_output_clear(void *ud) {
    Editor *e = (Editor *)ud;
    if (!e) return;
    panel_clear(&e->panels, PANEL_DEFAULT_CHANNEL);
    e->needs_redraw = 1;
}

/** register_channel: registra una pestana (canal) en el panel inferior. */
static int ext_hook_register_channel(void *ud, const char *id,
                                     const char *title) {
    Editor *e = (Editor *)ud;
    if (!e) return -1;
    int idx = panel_register(&e->panels, id, title);
    e->needs_redraw = 1;
    return idx >= 0 ? 0 : -1;
}

/** channel_append: anyade texto a un canal del panel inferior por id. */
static void ext_hook_channel_append(void *ud, const char *id,
                                    const char *text) {
    Editor *e = (Editor *)ud;
    if (!e || !id || !text) return;
    panel_append(&e->panels, id, text);
    e->bottom_panel_open = 1; /* mostrar el panel cuando llega texto */
    e->needs_redraw = 1;
}

/** channel_clear: vacia un canal del panel inferior por id. */
static void ext_hook_channel_clear(void *ud, const char *id) {
    Editor *e = (Editor *)ud;
    if (!e || !id) return;
    panel_clear(&e->panels, id);
    e->needs_redraw = 1;
}

/** log_line: vuelca un mensaje de log al canal "logs" del panel inferior. */
static void ext_hook_log_line(void *ud, int level, const char *msg) {
    Editor *e = (Editor *)ud;
    if (!e || !msg) return;
    static const char *lv[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    int i = (level >= 0 && level <= 3) ? level : 1;
    char line[320];
    snprintf(line, sizeof(line), "[%s] %s\n", lv[i], msg);
    panel_append(&e->panels, "logs", line);
    e->needs_redraw = 1;
}

/** request_repaint: pide al editor redibujar en el proximo frame. */
static void ext_hook_request_repaint(void *ud) {
    Editor *e = (Editor *)ud;
    if (e) e->needs_redraw = 1;
}

/* --- Hooks del popup de hover con pestanas (ABI v7) --- */
static char *hv_dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}
static void hover_free_tabs(HoverPopup *h) {
    for (int i = 0; i < h->n_tabs; ++i) {
        free(h->tab_names[i]);
        h->tab_names[i] = NULL;
        free(h->tab_content[i]);
        h->tab_content[i] = NULL;
    }
    h->n_tabs = 0;
}
/* Decodifica un contenido de pestana (godbolt 0x1D / diff 0x1E / texto) a
 * texto plano legible.  Devuelve un buffer heap (el caller libera). */
static char *hover_decode_plain(const char *c) {
    if (!c) return NULL;
    size_t n = strlen(c);
    char *out = (char *)malloc(n * 2 + 64);
    if (!out) return NULL;
    int o = 0;
    if (c[0] == 0x1D) { /* vista godbolt: kind \x1f ... */
        const char *p = strchr(c, '\n');
        p = p ? p + 1 : c + 1;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            char line[2048];
            size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            memcpy(line, p, cpy);
            line[cpy] = 0;
            char kind = line[0];
            char *f1 = strchr(line, 0x1F), *c1 = NULL, *c2 = NULL, *c3 = NULL;
            if (f1) {
                *f1 = 0;
                c1 = f1 + 1;
                char *f2 = strchr(c1, 0x1F);
                if (f2) {
                    *f2 = 0;
                    c2 = f2 + 1;
                    char *f3 = strchr(c2, 0x1F);
                    if (f3) { *f3 = 0; c3 = f3 + 1; }
                }
            }
            if (kind == 'H')
                o += snprintf(out + o, n * 2 + 64 - o, "; %s\n", c1 ? c1 : "");
            else if (kind == 'S')
                o += snprintf(out + o, n * 2 + 64 - o, "L%-4s  %s\n",
                              c1 ? c1 : "", c2 ? c2 : "");
            else if (kind == 'A') {
                if (c2 && c2[0])
                    o += snprintf(out + o, n * 2 + 64 - o, "L%-4s  +%-5s  %s\n",
                                  c1 ? c1 : "", c2, c3 ? c3 : "");
                else
                    o += snprintf(out + o, n * 2 + 64 - o, "L%-4s  %s\n",
                                  c1 ? c1 : "", c3 ? c3 : "");
            }
            if (!nl) break;
            p = nl + 1;
        }
    } else if (c[0] == 0x1E) { /* diff lado a lado: lm \x1f L \x1f rm \x1f R */
        const char *p = strchr(c, '\n');
        p = p ? p + 1 : c + 1;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            char line[2048];
            size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            memcpy(line, p, cpy);
            line[cpy] = 0;
            char *s1 = strchr(line, 0x1F);
            char *L = line, *R = (char *)"";
            if (s1) {
                *s1 = 0;
                L = s1 + 1;
                char *s2 = strchr(L, 0x1F);
                if (s2) {
                    *s2 = 0;
                    char *mr = s2 + 1;
                    char *s3 = strchr(mr, 0x1F);
                    if (s3) R = s3 + 1;
                }
            }
            o += snprintf(out + o, n * 2 + 64 - o, "%-44s | %s\n", L, R);
            if (!nl) break;
            p = nl + 1;
        }
    } else { /* texto plano / markdown: copiar tal cual */
        memcpy(out, c, n + 1);
        o = (int)n;
    }
    out[o] = 0;
    return out;
}

char *hover_copy_active_text(HoverPopup *h) {
    if (!h || h->active_tab < 0 || h->active_tab >= h->n_tabs) return NULL;
    const char *c = h->tab_content[h->active_tab];
    if (!c) return NULL;
    /* IR multi-vista (0x1C): extraer la sub-vista activa y decodificarla. */
    if (c[0] == 0x1C) {
        const char *p = c + 1;
        const char *sep = strchr(p, 0x1C);
        const char *sub_begin, *sub_end;
        if (h->ir_submode == 0) {
            sub_begin = p;
            sub_end = sep ? sep : c + strlen(c);
        } else {
            sub_begin = sep ? sep + 1 : c + strlen(c);
            sub_end = c + strlen(c);
        }
        size_t sl = (size_t)(sub_end - sub_begin);
        char *sub = (char *)malloc(sl + 1);
        if (!sub) return NULL;
        memcpy(sub, sub_begin, sl);
        sub[sl] = 0;
        char *res = hover_decode_plain(sub);
        free(sub);
        return res;
    }
    return hover_decode_plain(c);
}

static void ext_hook_show_hover(void *ud, const char *const *names, int n) {
    Editor *e = (Editor *)ud;
    if (!e) return;
    HoverPopup *h = &e->hover;
    hover_free_tabs(h);
    if (n > HOVER_MAX_TABS) n = HOVER_MAX_TABS;
    if (n < 0) n = 0;
    for (int i = 0; i < n; ++i) {
        h->tab_names[i] = hv_dup(names && names[i] ? names[i] : "");
        h->tab_content[i] = NULL; /* NULL = "cargando" */
    }
    h->n_tabs = n;
    h->active_tab = 0;
    h->scroll = 0;
    h->visible = (n > 0);
    h->needs_place = 1; /* el render lo coloca desde el ancla y fija el tamano */
    h->dragging = 0;
    h->resizing = 0;
    /* Estado de la vista godbolt: linea fijada limpia; ancho de columnas se
     * conserva entre hovers (gb_split_pct) salvo init a 0 = default. */
    h->gb_active = 0;
    h->gb_split_drag = 0;
    h->gb_sel_line = -1;
    h->ir_active = 0;
    h->ir_submode = 0;
    /* anchor_x/anchor_y los fijo la deteccion de mouse-rest al disparar el
     * evento (input_mouse.c); show_hover los reusa tal cual. */
    e->needs_redraw = 1;
}
static void ext_hook_set_hover_tab(void *ud, int index, const char *content) {
    Editor *e = (Editor *)ud;
    if (!e) return;
    HoverPopup *h = &e->hover;
    if (index < 0 || index >= h->n_tabs) return;
    free(h->tab_content[index]);
    h->tab_content[index] = hv_dup(content ? content : "");
    e->needs_redraw = 1;
}
/* Cierra el popup de hover (libera las pestanas).  Publico: lo usan el hook de
 * la API y el input (al mover el raton lejos / Esc / click fuera). */
void editor_hover_hide(Editor *e) {
    if (!e) return;
    hover_free_tabs(&e->hover);
    e->hover.visible = 0;
    e->hover.rest_fired = 0;
    e->needs_redraw = 1;
}
static void ext_hook_hide_hover(void *ud) {
    editor_hover_hide((Editor *)ud);
}

/** workspace_root: ruta de la carpeta abierta en el explorador, o NULL.
 *  Devuelve el puntero ESTABLE al buffer interno del FileTree (valido hasta el
 *  siguiente ftree_load), no una copia temporal. */
static const char *ext_hook_workspace_root(void *ud) {
    Editor *e = (Editor *)ud;
    if (!e) return NULL;
    return e->ftree.root_path[0] ? e->ftree.root_path : NULL;
}

/* Ruta del archivo de la pestana activa (o NULL si es un buffer sin guardar o
 * no hay pestanas).  e->filepath sigue al buffer activo (lo usan el titulo de
 * ventana y el autoguardado), asi que es la fuente correcta del "archivo
 * actual" que consultan las extensiones (p.ej. el cliente LSP).  Sin esto las
 * extensiones no sabian que archivo se esta editando (current_path() == NULL). */
static const char *ext_hook_current_path(void *ud) {
    Editor *e = (Editor *)ud;
    if (!e) return NULL;
    return e->filepath[0] ? e->filepath : NULL;
}

/** goto_location: abre @p path (o cambia a su pestana) y mueve el cursor a
 *  (@p line, @p col) 0-based (col en CARACTERES), haciendo scroll para que
 *  quede visible.  Devuelve 1 si se abrio, 0 si no. */
static int ext_hook_goto_location(void *ud, const char *path, int line,
                                  int col) {
    Editor *e = (Editor *)ud;
    if (!e || !path || !path[0]) return 0;
    /* editor_tab_open reusa la pestana si el archivo ya esta abierto, o abre una
     * nueva; tras volver, e->buf y e->active_tab reflejan ese archivo. */
    editor_tab_open(e, path);
    if (!e->buf) return 0; /* el archivo no se pudo abrir */
    /* (linea, columna-en-caracteres) -> offset logico (convencion LSP). */
    size_t pos = buf_offset_from_line_col_chars(e->buf, line, col);
    buf_move_to(e->buf, pos);
    editor_sync_cursor(e);     /* refrescar cursor_line/col desde la pos */
    editor_ensure_visible(e);  /* desplazar el scroll para que se vea */
    editor_cursor_blink_reset(e);
    e->needs_redraw = 1;
    return 1;
}

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
    e->cursor_visible = 1;               /* cursor visible al arrancar */
    e->cursor_blink_ms = SDL_GetTicks(); /* iniciar timer del parpadeo */
    e->ext_panel_w = LAYOUT_EXT_DEFAULT_W; /* ancho inicial del panel de exts */
    e->dragging_divider = DIVIDER_NONE;    /* sin arrastre de divisor activo */
    e->hovered_divider = DIVIDER_NONE;     /* sin divisor bajo el cursor     */

    /* Panel inferior: cerrado al arrancar, alto inicial sensato, canales
     * integrados (Salida/Logs/Terminal) registrados en el almacen. */
    panel_store_init(&e->panels);
    e->bottom_panel_open = 0;
    e->bottom_panel_h = LAYOUT_BOTTOM_DEFAULT_H;
    e->bottom_active_chan = 0; /* "Salida" es el primer canal */
    e->bottom_focused = 0;
    e->bottom_sel_anchor = -1;
    e->bottom_sel_caret = -1;
    e->bottom_sel_active = 0;
    e->bottom_selecting = 0;

    /* preferencias persistentes: cargarlas y aplicar las que afectan al estado
     * inicial (las demás las leen render/input directamente de e->settings). */
    settings_load(&e->settings);
    e->autosave = e->settings.autosave;
    e->theme = theme_preset(e->settings.theme); /* paleta de colores activa */
    fonts_scan(&e->fonts); /* fuentes del sistema para el selector */

    /* fondo personalizado: inicializar (la textura se carga mas abajo, ya con
     * el renderer creado, solo si el modo activo es "imagen"). */
    e->background_texture = NULL;
    e->background_w = 0;
    e->background_h = 0;
    e->background_view_open = 0;
    /* cache de miniaturas de la galeria: vacia e invalidada (se construye al
     * entrar a la sub-pantalla Fondos en modo Imagen). */
    for (int i = 0; i < BG_GALLERY_MAX; i++) {
        e->bg_thumb[i] = NULL;
        e->bg_thumb_w[i] = 0;
        e->bg_thumb_h[i] = 0;
    }
    e->bg_thumb_count = 0;
    e->bg_thumb_valid = 0;
    e->bg_gallery_scroll = 0;

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
    /* SDL_WINDOW_TRANSPARENT habilita el alfa por pixel del framebuffer, que
     * usa el modo de fondo Transparente para dejar ver el escritorio.  En los
     * modos opacos el frame se limpia con alfa 255, asi que la apariencia es
     * identica a una ventana normal. */
    e->window = SDL_CreateWindow("CoffeeCode", e->win_w, e->win_h,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_TRANSPARENT);
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
    e->font_size = e->settings.font_size;
    e->line_height = e->settings.font_size + 4; /* alto de línea (16 -> 20) */
    e->font = load_editor_font(e->font_size, e->settings.font_path);
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
            w > 0 ? w : e->font_size / 2; /* fallback si la medida fallara */
    }

    /* -- Panel explorador de archivos -- */
    ftree_init(&e->ftree);

    /* Cargar la imagen de fondo ahora que el renderer ya esta creado, solo si
     * el modo activo es "imagen" y hay una ruta guardada. */
    if (e->settings.background_mode == BG_MODE_IMAGE &&
        e->settings.background_path[0]) {
        editor_load_background(e, e->settings.background_path);
    }

    /* -- Pestañas --
     * Si se pasó un filepath, abrir ese archivo en la primera pestaña; si no,
     * arrancar sin pestañas (tab_count = 0 → render muestra la bienvenida).
     * buf/lex/undo a NULL: apuntarán al almacenamiento de la pestaña activa. */
    e->tab_count = 0;
    e->active_tab = 0;
    /* Editor sin dividir por defecto: una única hoja a pantalla completa
     * (group_id 0).  El árbol de dock arranca con esa hoja como raíz. */
    dock_init_single(&e->dock, 0);
    e->active_group = 0;
    e->pane_active = 0;
    e->dock_drag_split = DOCK_NONE; /* sin divisor de dock en arrastre */
    e->dock_drag_orient = DOCK_VERTICAL;
    e->drag_tab = -1;      /* sin pestana en arrastre/candidata */
    e->dragging_tab = 0;
    e->tab_reorder_group = -1; /* sin objetivo de barra para reordenar */
    e->float_count = 0;    /* sin paneles flotantes al arrancar  */
    e->float_drag = -1;    /* sin flotante en arrastre            */
    e->float_resizing = 0;
    e->float_resize_edges = 0;
    e->float_dock_target_group = -1; /* sin destino de re-acople al arrancar */
    e->float_dock_zone = DOCK_DZ_NONE;
    e->detached_count = 0;           /* sin ventanas desprendidas al arrancar */
    e->detached_focus_group = -1;    /* el foco del teclado en la principal   */
    e->buf = NULL;
    e->lex = NULL;
    e->undo = NULL;

    /* -- Extension host ----------------------------------------------
     * Crear el host (sin buffer aun: se fija al abrir/cambiar de pestana) y
     * cargar el directorio de extensiones junto al ejecutable, si existe.  El
     * host es opcional: si no hay extensiones, ext_host_load_dir devuelve 0 y
     * todo sigue funcionando como el editor SDL de siempre. */
    {
        CoffeeHostBackend backend;
        memset(&backend, 0, sizeof backend);
        backend.buffer = NULL; /* aun no hay pestana activa */
        backend.ud = e;
        /* Hooks de UI: conectan el CoffeeApi a la UI real del IDE. */
        backend.set_status = ext_hook_set_status;
        backend.output_append = ext_hook_output_append;
        backend.output_clear = ext_hook_output_clear;
        backend.register_channel = ext_hook_register_channel;
        backend.channel_append = ext_hook_channel_append;
        backend.channel_clear = ext_hook_channel_clear;
        backend.log_line = ext_hook_log_line;
        backend.request_repaint = ext_hook_request_repaint;
        backend.show_hover = ext_hook_show_hover;
        backend.set_hover_tab = ext_hook_set_hover_tab;
        backend.hide_hover = ext_hook_hide_hover;
        backend.workspace_root = ext_hook_workspace_root;
        backend.goto_location = ext_hook_goto_location;
        backend.current_path = ext_hook_current_path;
        CoffeeHost *host = ext_host_create(&backend);
        e->ext_host = host;
        if (host) {
            /* Lenguaje base EMBEBIDO: el C es la unica extension de lenguaje
             * integrada en el ejecutable.  Se registra en proceso (con la misma
             * API que las DLLs) ANTES de cargar las extensiones externas, y
             * colorea leyendo el tema vivo del editor (&e->theme es estable).
             * Primero su entrada en el panel de extensiones (nativa, no
             * descargable); luego el resaltador. */
            ext_host_register_builtin(
                host, "lang-c", "C / C++", "1.0.0", "CoffeeCode",
                "Resaltado de sintaxis de C/C++ embebido (lenguaje base)");
            coffee_builtin_c_register(host, ext_host_api(host), &e->theme);
            const char *base = SDL_GetBasePath();
            char extdir[1024];
            if (base)
                snprintf(extdir, sizeof(extdir), "%sextensions", base);
            else
                snprintf(extdir, sizeof(extdir), "extensions");
            int n = ext_host_load_dir(host, extdir);
            if (n > 0)
                fprintf(stderr, "[ext-host] %d extension(es) cargada(s)\n", n);
        }
    }

    if (filepath && filepath[0]) {
#ifdef _DEBUG
        fprintf(stderr, "STEP: opening initial file\n");
#endif
        editor_tab_open(e, filepath);
        SDL_SetWindowTitle(e->window,
                           filepath); /* título de la ventana = ruta */
    } else {
        /* Sin archivo por argumento: intentar restaurar la disposicion de la
         * ultima sesion (pestanas, arbol de paneles, flotantes y tamanos).  Si
         * no hay layout guardado, esta corrupto o queda vacio, layout_restore
         * devuelve 0 y el arranque sigue siendo el de siempre (bienvenida). */
        if (layout_restore(e) && e->tab_count > 0 && e->filepath[0])
            SDL_SetWindowTitle(e->window, e->filepath);
    }

#ifdef _DEBUG
    fprintf(stderr, "STEP: editor_init OK\n");
#endif
    return 1;
}

int editor_init_secondary(Editor *e, Editor *primary, int w, int h) {
    if (!e || !primary) return 0;
    memset(e, 0, sizeof(*e)); /* todo a cero: punteros NULL y flags en 0 */
    e->is_secondary = 1;      /* NO posee recursos compartidos */
    e->running = 1;
    e->needs_redraw = 1;
    e->menu_hovered = -1;
    e->find.result_line = -1;
    e->cursor_visible = 1;
    e->cursor_blink_ms = SDL_GetTicks();
    e->ext_panel_w = LAYOUT_EXT_DEFAULT_W;
    e->dragging_divider = DIVIDER_NONE;
    e->hovered_divider = DIVIDER_NONE;

    /* Panel inferior propio (cada ventana es un IDE completo). */
    panel_store_init(&e->panels);
    e->bottom_panel_open = 0;
    e->bottom_panel_h = LAYOUT_BOTTOM_DEFAULT_H;
    e->bottom_active_chan = 0;
    e->bottom_focused = 0;
    e->bottom_sel_anchor = -1;
    e->bottom_sel_caret = -1;
    e->bottom_sel_active = 0;
    e->bottom_selecting = 0;

    /* -- Recursos COMPARTIDOS de la ventana principal (por puntero/valor) ----
     * La fuente (TTF_Font*) es independiente del renderer en SDL_ttf: draw_text
     * crea la textura sobre e->renderer en cada llamada, asi que la MISMA fuente
     * sirve para varios renderers.  El ext_host es uno solo para todo el IDE.  Se
     * copian tambien las metricas de fuente, el tema y las preferencias (estas
     * por valor: las secundarias no las re-guardan).  La lista de fuentes del
     * selector queda vacia en la secundaria (no abre preferencias de fuente). */
    e->settings = primary->settings;
    e->autosave = primary->autosave;
    e->theme = primary->theme;
    e->font = primary->font;             /* COMPARTIDA: no se cierra al liberar */
    e->font_size = primary->font_size;
    e->line_height = primary->line_height;
    e->char_w = primary->char_w;
    e->ext_host = primary->ext_host;     /* COMPARTIDO: no se destruye al liberar */

    /* -- Ventana del SO propia de esta instancia -- */
    if (w < 300) w = 300; /* tamano minimo sensato para un IDE completo */
    if (h < 200) h = 200;
    e->win_w = w;
    e->win_h = h;
    e->window = SDL_CreateWindow("CoffeeCode", w, h,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_TRANSPARENT);
    if (!e->window) {
        fprintf(stderr, "SDL_CreateWindow (sec): %s\n", SDL_GetError());
        return 0;
    }
    e->renderer = SDL_CreateRenderer(e->window, NULL);
    if (!e->renderer) {
        fprintf(stderr, "SDL_CreateRenderer (sec): %s\n", SDL_GetError());
        SDL_DestroyWindow(e->window);
        e->window = NULL;
        return 0;
    }
    SDL_SetRenderVSync(e->renderer, 1);
    SDL_StartTextInput(e->window);

    /* -- Explorador propio + arbol de dock de una hoja vacia (sin pestanas) -- */
    ftree_init(&e->ftree);
    e->tab_count = 0;
    e->active_tab = 0;
    dock_init_single(&e->dock, 0);
    e->active_group = 0;
    e->pane_active = 0;
    e->dock_drag_split = DOCK_NONE;
    e->dock_drag_orient = DOCK_VERTICAL;
    e->drag_tab = -1;
    e->dragging_tab = 0;
    e->tab_reorder_group = -1;
    e->float_count = 0;
    e->float_drag = -1;
    e->float_resizing = 0;
    e->float_resize_edges = 0;
    e->float_dock_target_group = -1;
    e->float_dock_zone = DOCK_DZ_NONE;
    e->detached_count = 0;        /* las secundarias no anidan desprendidas (v1) */
    e->detached_focus_group = -1;
    e->buf = NULL;
    e->lex = NULL;
    e->undo = NULL;
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
    /* -- Ventana SECUNDARIA: libera SOLO lo que POSEE -----------------------
     * Sus pestanas (buf/lex/undo) y su explorador son propios; su window/renderer
     * los creo en editor_init_secondary.  NO toca la fuente, el ext_host ni los
     * subsistemas SDL/TTF: son de la ventana principal, que los libera al final.
     * No persiste disposicion (lo hace la principal con su propio estado). */
    if (e->is_secondary) {
        for (int i = 0; i < e->tab_count; i++)
            tab_free_resources(&e->tabs[i]);
        ftree_free(&e->ftree);
        /* PanelStore es POD de arrays fijos: no posee memoria que liberar. */
        if (e->window) SDL_StopTextInput(e->window);
        if (e->renderer) SDL_DestroyRenderer(e->renderer);
        if (e->window) SDL_DestroyWindow(e->window);
        e->renderer = NULL;
        e->window = NULL;
        return;
    }

    /* Persistir la disposicion actual ANTES de liberar nada: necesita las rutas
     * de las pestanas y el arbol de paneles aun vivos.  Si la capa de aplicacion
     * ya guardo la SESION COMPLETA (principal + secundarias), no re-escribir aqui
     * solo-principal (sobreescribiria las secundarias).  Con init aislado (sin
     * App) el flag esta a 0 y se guarda como siempre: cero regresion. */
    if (!e->layout_save_suppressed) layout_save(e);

    /* Destruir el host de extensiones ANTES de liberar las pestanas: emite
     * COFFEE_EVENT_SHUTDOWN y descarga las DLLs mientras los buffers aun viven. */
    if (e->ext_host) {
        ext_host_destroy((CoffeeHost *)e->ext_host);
        e->ext_host = NULL;
    }
    /* Destruir las ventanas desprendidas (renderer + window de cada una) antes de
     * liberar las pestanas: sus pestanas viven en tabs[] y se liberan abajo. */
    for (int i = 0; i < e->detached_count; i++) {
        if (e->detached[i].renderer)
            SDL_DestroyRenderer((SDL_Renderer *)e->detached[i].renderer);
        if (e->detached[i].window) {
            SDL_StopTextInput((SDL_Window *)e->detached[i].window);
            SDL_DestroyWindow((SDL_Window *)e->detached[i].window);
        }
    }
    e->detached_count = 0;

    for (int i = 0; i < e->tab_count; i++)
        tab_free_resources(&e->tabs[i]); /* buffer/lexer/undo de cada pestaña */
    ftree_free(&e->ftree);
    fonts_free(&e->fonts);               /* lista de fuentes del sistema */
    if (e->background_texture)
        SDL_DestroyTexture(e->background_texture); /* liberar textura de fondo */
    editor_bg_thumbs_free(e); /* liberar miniaturas de la galeria de fondos */
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
 * Así la app mantiene respuesta inmediata ante entrada del usuario Y
 * animaciones fluidas (cursor parpadeante) sin quemar CPU cuando no hay
 * actividad.
 */
#define CURSOR_BLINK_MS 530 /* medio periodo del parpadeo del cursor (ms) */

void editor_frame_tasks(Editor *e) {
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

    /* Hover: si el raton lleva parado sobre texto, disparar el evento (la
     * extension LSP abre el popup con la info del simbolo). */
    editor_hover_tick(e);
}

void editor_run(Editor *e) {
    SDL_Event ev;
    while (e->running) {
        /* Esperar un evento hasta 16 ms (= 1 frame a 60 Hz).
         * Si llega antes, procesarlo; si no, el timeout fuerza la siguiente
         * iteración garantizando que siempre revisamos el blink y redibujamos.
         */
        if (SDL_WaitEventTimeout(&ev, 16)) {
            input_handle_event(e, &ev);
            /* Drenar el resto de la cola sin bloquear */
            while (SDL_PollEvent(&ev))
                input_handle_event(e, &ev);
        }

        editor_frame_tasks(e); /* autoguardado + parpadeo del cursor */

        /* Redibujar solo si algo cambió desde el último frame. */
        if (e->needs_redraw) {
            render_frame(e);                /* ventana principal */
            if (e->detached_count > 0)      /* ventanas desprendidas (si las hay) */
                editor_render_detached(e);  /* cada una en su renderer propio */
            e->needs_redraw = 0;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FONDO PERSONALIZADO
 * ═══════════════════════════════════════════════════════════════════════════
 */

/**
 * @brief Carga una imagen como textura de fondo del editor.
 *
 * Usa SDL_image para soportar PNG, JPG, BMP, GIF (primer frame), TIFF, WebP,
 * etc. La textura se sube a GPU una sola vez y se reutiliza en cada frame,
 * sin coste de CPU adicional.
 *
 * Si @p path es NULL o vacío, libera la textura actual (queda sin fondo).
 *
 * @param e    Editor destino (debe tener e->renderer ya inicializado).
 * @param path Ruta a la imagen. "" o NULL = limpiar el fondo actual.
 * @return 1 si se cargó (o limpió) correctamente; 0 si falló la carga.
 */
int editor_load_background(Editor *e, const char *path) {
    /* Si no hay ruta o está vacía, limpiar el fondo actual */
    if (!path || !path[0]) {
        if (e->background_texture) {
            SDL_DestroyTexture(e->background_texture);
            e->background_texture = NULL;
            e->background_w = 0;
            e->background_h = 0;
        }
        return 1;
    }

    /* Cargar la imagen con SDL_image */
    SDL_Surface *surf = IMG_Load(path);
    if (!surf) {
        fprintf(stderr, "[CoffeeCode] No se pudo cargar la imagen de fondo: %s\n"
                        "             Razón: %s\n", path, SDL_GetError());
        return 0;
    }

    /* Destruir la textura anterior si existía */
    if (e->background_texture) {
        SDL_DestroyTexture(e->background_texture);
        e->background_texture = NULL;
    }

    /* Convertir la superficie (CPU) a textura (GPU) */
    e->background_texture = SDL_CreateTextureFromSurface(e->renderer, surf);
    e->background_w = surf->w;
    e->background_h = surf->h;
    SDL_DestroySurface(surf);

    /* Forzar el modo de mezcla alpha: SDL_CreateTextureFromSurface deja la
     * textura en BLENDMODE_NONE cuando la imagen es RGB opaca (sin canal
     * alpha), y en ese estado SDL_SetTextureAlphaMod NO surte efecto (la imagen
     * se dibuja siempre opaca).  Con BLEND, la opacidad configurada por el
     * usuario se aplica al pintar el fondo. */
    if (e->background_texture)
        SDL_SetTextureBlendMode(e->background_texture, SDL_BLENDMODE_BLEND);

    if (!e->background_texture) {
        fprintf(stderr, "[CoffeeCode] Error creando textura de fondo: %s\n",
                SDL_GetError());
        return 0;
    }

    fprintf(stdout, "[CoffeeCode] Fondo cargado: %s (%dx%d)\n",
            path, e->background_w, e->background_h);
    return 1;
}

/* -- Cache de miniaturas de la galeria de fondos --------------------------- */

void editor_bg_thumbs_free(Editor *e) {
    for (int i = 0; i < e->bg_thumb_count; i++) {
        if (e->bg_thumb[i]) SDL_DestroyTexture(e->bg_thumb[i]);
        e->bg_thumb[i] = NULL;
        e->bg_thumb_w[i] = 0;
        e->bg_thumb_h[i] = 0;
    }
    e->bg_thumb_count = 0;
    e->bg_thumb_valid = 0; /* tras liberar, la cache esta vacia y desactualizada */
}

void editor_bg_thumbs_invalidate(Editor *e) { e->bg_thumb_valid = 0; }

void editor_bg_thumbs_build(Editor *e) {
    if (e->bg_thumb_valid) return; /* ya esta al dia: nada que hacer */
    editor_bg_thumbs_free(e);      /* descartar lo previo antes de reconstruir */

    int n = e->settings.background_gallery_count;
    if (n > BG_GALLERY_MAX) n = BG_GALLERY_MAX;
    for (int i = 0; i < n; i++) {
        const char *path = e->settings.background_gallery[i];
        e->bg_thumb[i] = NULL;
        e->bg_thumb_w[i] = 0;
        e->bg_thumb_h[i] = 0;
        if (!path[0]) continue;
        /* Cargar la imagen a una superficie (CPU) y subirla como textura.  Si
         * falla (ruta borrada / formato roto), la entrada queda NULL y el render
         * dibuja un placeholder: nunca se cae por una imagen invalida. */
        SDL_Surface *surf = IMG_Load(path);
        if (!surf) continue;
        e->bg_thumb[i] = SDL_CreateTextureFromSurface(e->renderer, surf);
        e->bg_thumb_w[i] = surf->w;
        e->bg_thumb_h[i] = surf->h;
        SDL_DestroySurface(surf);
    }
    e->bg_thumb_count = n;
    e->bg_thumb_valid = 1;
}
