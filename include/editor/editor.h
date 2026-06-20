#pragma once

/* Debe definirse antes de cualquier include de SDL para que SDL3
   no redefina main() en Windows */
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#include "buffer/buffer.h"
#include "encoding/encoding.h"
#include "filetree/filetree.h"
#include "fonts/fonts.h"
#include "lexer/lexer.h"
#include "lsp/lsp.h"
#include "lsp/lsp_install.h"
#include "render/theme.h"
#include "render/ui_hit.h"
#include "settings/settings.h"
#include "structs/ring.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

/* -- Constantes de UI ----------------------------------------------------- */
/* El tamaño de fuente y el alto de línea son AHORA runtime (e->font_size /
 * e->line_height), derivados de las preferencias; ya no son constantes. */
#define GUTTER_WIDTH 52
#define PADDING_LEFT 8
#define STATUS_HEIGHT 24
#define NAVBAR_HEIGHT 30
#define TAB_SIZE 4
#define TAB_BAR_HEIGHT 28  /* altura de la barra de pestañas */
#define MAX_TABS 16        /* máximo de archivos abiertos    */
#define SHORTCUT_HEIGHT 26 /* alto de la banda de atajos (si se muestra) */

/* -- Undo/Redo ------------------------------------------------------------ */
#define UNDO_MAX 256 /* máximo de entradas en la pila de undo */

typedef enum {
    UNDO_INSERT, /* se insertaron `len` bytes en `pos`  */
    UNDO_DELETE  /* se borraron `len` bytes en `pos`    */
} UndoType;

/* Una entrada del historial: una edición atómica que se puede deshacer/rehacer.
 */
typedef struct {
    UndoType type;
    size_t pos;            /* posición lógica en el buffer        */
    char *text;            /* bytes afectados (malloc'd)          */
    size_t len;            /* longitud en bytes                   */
    int cursor_line_after; /* línea del cursor tras la edición (para restaurar)
                            */
    int cursor_col_after;  /* columna del cursor tras la edición  */
} UndoEntry;

typedef struct {
    Ring entries; /* Ring<UndoEntry> de capacidad UNDO_MAX (buffer circular) */
    int redo_top; /* cuántas entradas se pueden rehacer                       */
} UndoStack;

/* -- Pestaña (archivo abierto) -------------------------------------------
 * Cada pestaña POSEE su almacenamiento (buf/lex/undo); los punteros del Editor
 * apuntan a la pestaña activa. Aquí se guardan también los escalares de la
 * pestaña cuando no es la activa (cursor, scroll, selección). */
typedef struct {
    Buffer buf;            /* gap buffer con el texto del archivo            */
    LexerCache lex;        /* cache de tokens por línea (resaltado)          */
    UndoStack undo;        /* pila de undo/redo de esta pestaña              */
    const Highlighter *hl; /* resaltador según el lenguaje del archivo */
    LspClient lsp;  /* cliente LSP para este archivo (puede estar inactivo) */
    int lsp_active; /* 1 si el cliente LSP está iniciado y en uso */
    LspInstallJob
        lsp_install;    /* trabajo de instalación automática (si procede) */
    char filepath[512]; /* ruta del archivo, o "" si es nuevo sin guardar */
    int modified;       /* 1 si hay cambios sin guardar                   */
    long loaded_mtime;  /* mtime del fichero en la última carga desde disco */
    TextEncoding encoding;       /* codificación con la que se carga/guarda */
    int cursor_line, cursor_col; /* posición del cursor guardada            */
    int scroll_line, scroll_col; /* desplazamiento de la vista guardado     */
    int sel_active;              /* 1 si la selección está activa           */
    int sel_anchor_line, sel_anchor_col; /* ancla (inicio) de la selección  */
    /* La geometría de la pestaña (su rectángulo y el botón de cerrar) la
     * registra el render en e->ui por índice (UI_LIST_TAB / UI_LIST_TAB_CLOSE).
     */
} EditorTab;

/* -- Barra de búsqueda ---------------------------------------------------- */
#define FIND_BAR_MAX 256

typedef struct {
    int visible;              /* 1 = barra de búsqueda mostrada            */
    char query[FIND_BAR_MAX]; /* texto a buscar                           */
    int query_len;            /* longitud actual de `query`                */
    int result_line;          /* -1 = sin resultado                        */
    int result_col;           /* columna de la coincidencia actual         */
    int match_count;          /* total de coincidencias (0 = sin resultados) */
    int match_index;          /* índice de la coincidencia actual (0-based)  */
    /* reemplazo */
    char replace[FIND_BAR_MAX]; /* texto de reemplazo                     */
    int replace_len;            /* longitud actual de `replace`           */
    int replace_focused;        /* 0 = foco en buscar, 1 = foco en reemplazar */
    int bar_focused; /* 1 = teclado va a la barra, 0 = va al editor */
    /* selección de texto dentro de los inputs (-1 = sin selección) */
    int query_sel_start, query_sel_end; /* rango seleccionado en buscar    */
    int replace_sel_start,
        replace_sel_end; /* rango seleccionado en reemplazar */
    /* La geometría de los controles (campos, flechas, botón Reemplazar y marco)
     * ya no se guarda aquí: la registra el render en e->ui (ver
     * render/ui_hit.h) y el input la consulta con ui_hit(UI_FIND_*). */
} FindBar;

/* -- Estado global del editor --------------------------------------------- */
typedef struct {
    /* SDL */
    SDL_Window *window;     /* ventana del sistema operativo            */
    SDL_Renderer *renderer; /* contexto de dibujo 2D acelerado          */
    TTF_Font *font;         /* fuente monoespaciada cargada en runtime  */
    int win_w, win_h;       /* tamaño de la ventana en píxeles          */
    int char_w;             /* ancho de un carácter en px (monoespaciada) */
    int font_size;          /* tamaño de la fuente en px (de settings)  */
    int line_height;        /* alto de línea del área de texto en px     */

    /* -- pestañas -- */
    EditorTab tabs[MAX_TABS]; /* archivos abiertos (array fijo)         */
    int tab_count;            /* nº de pestañas abiertas                */
    int active_tab;           /* índice de la pestaña activa            */

    /* Punteros al tab activo — NUNCA copias por valor.
     * Apuntan directamente a tabs[active_tab].buf/lex/undo;
     * se actualizan en editor_tab_load_state(). */
    Buffer *buf;
    LexerCache *lex;
    const Highlighter *hl; /* resaltador del tab activo */

    /* vista */
    int scroll_line; /* primera línea visible (celdas, no px)          */
    int scroll_col;  /* primera columna visible                        */
    int cursor_line; /* línea del cursor                               */
    int cursor_col;  /* columna del cursor                             */

    /* selección */
    int sel_active;      /* 1 si hay selección activa                  */
    int sel_anchor_line; /* línea del ancla (donde empezó la selección) */
    int sel_anchor_col;  /* columna del ancla                          */

    /* archivo */
    char filepath[512];    /* ruta del archivo de la pestaña activa      */
    int modified;          /* 1 si hay cambios sin guardar               */
    TextEncoding encoding; /* codificación de la pestaña activa (espejo) */

    /* navbar / menú archivo */
    int menu_open;    /* 1 = desplegable visible */
    int menu_hovered; /* índice del item bajo el cursor, -1 = ninguno */

    /* autoguardado */
    int autosave;            /* 1 = autoguardado activado */
    Uint64 autosave_last_ms; /* timestamp del último autoguardado */

    /* explorador de carpetas lateral */
    FileTree ftree;

    /* -- NUEVO: undo/redo ------------------------------------------------ */
    UndoStack *undo;

    /* -- NUEVO: barra de búsqueda (Ctrl+F) ------------------------------ */
    FindBar find;

    /* -- selección con ratón --------------------------------------------- */
    int mouse_selecting; /* 1 = botón izq. pulsado sobre texto   */

    /* -- scrollbar draggable -------------------------------------------- */
    int scrollbar_dragging;        /* 1 mientras se arrastra la scrollbar   */
    int scrollbar_drag_start_y;    /* Y del ratón al empezar a arrastrar    */
    int scrollbar_drag_start_line; /* scroll_line al empezar a arrastrar   */

    /* estado */
    int running;      /* 0 termina el bucle principal                   */
    int needs_redraw; /* 1 = hay que redibujar en el próximo frame      */

    /* cursor parpadeante */
    Uint64
        cursor_blink_ms; /* timestamp del último cambio de estado del cursor */
    int cursor_visible;  /* 1 = cursor visible, 0 = cursor oculto (blink)    */

    /* geometría de los controles de UI del último frame (hit-test compartido
     * entre render e input; ver render/ui_hit.h). */
    UiRegistry ui;

    /* preferencias persistentes (tema, fuente, tab, autoguardado...) */
    Settings settings;
    int settings_open; /* 1 = mostrando la pantalla de preferencias */

    /* fuentes del sistema descubiertas (para el selector de preferencias) */
    FontList fonts;
    int font_list_scroll; /* primera fila visible en la lista de fuentes */

    /* selector de codificación (popup desde la barra de estado) */
    int enc_popup;        /* 1 = popup de codificación abierto          */
    int enc_popup_mode;   /* 0 = reabrir con, 1 = guardar como          */
    int enc_popup_scroll; /* primera fila visible de la lista           */

    /* paleta de colores activa (preset según settings.theme; ver
     * render/theme.h) */
    Theme theme;

    /* -- Extension host (sistema de extensiones) -----------
     * Puntero opaco a CoffeeHost (include/ext/ext_host.h).  Se declara como
     * void* para no acoplar este header al de extensiones.  NULL si el host no
     * se inicializo (p.ej. sin directorio de extensiones).  El editor crea el
     * host en editor_init, le fija el buffer activo, emite eventos del IDE y lo
     * destruye en editor_free. */
    void *ext_host;

    /* -- UI del sistema de extensiones -----------------------------
     * Estado de la interfaz que el IDE expone a las extensiones. */
    int ext_panel_open;       /* 1 = panel de extensiones visible */
    char ext_status[256];     /* mensaje de barra de estado puesto por una
                                 extension (CoffeeApi::set_status); vacio si
                                 ninguna lo fijo */
    char ext_output[4096];    /* buffer del panel de salida (output_append);
                                 acumula texto que las extensiones emiten */
    size_t ext_output_len;    /* bytes usados de ext_output */
    int ext_panel_scroll;     /* scroll vertical del panel de extensiones */
    int ext_install_mode;     /* 1 = el proximo dialogo de carpeta instala una
                                 extension (ext_host_load) en lugar de abrir el
                                 explorador (ftree_load) */
} Editor;

/* -- Dimensiones efectivas según preferencias ----------------------------- */
/** Ancho del gutter (0 si los números de línea están ocultos). */
static inline int editor_gutter_w(const Editor *e) {
    return e->settings.show_line_numbers ? GUTTER_WIDTH : 0;
}
/** Alto de la barra de atajos inferior (0 si está oculta). */
static inline int editor_shortcut_h(const Editor *e) {
    return e->settings.show_shortcuts ? SHORTCUT_HEIGHT : 0;
}

/* -- Ciclo de vida -------------------------------------------------------- */
int editor_init(Editor *e, const char *filepath);
void editor_free(Editor *e);
void editor_run(Editor *e);
/* Recarga la fuente desde las preferencias (settings.font_path/font_size) y
 * recalcula char_w/line_height. Mantiene la fuente actual si la nueva falla. */
void editor_reload_font(Editor *e);
/* Re-lee el archivo de la pestaña activa decodificándolo con la codificación
 * @p enc (en vez de la autodetectada) y la fija como codificación de la
 * pestaña. */
void editor_reopen_with_encoding(Editor *e, TextEncoding enc);

/* -- Lógica interna (usada entre módulos) --------------------------------- */
void editor_update_lexer(Editor *e, int from_line);
void editor_sync_cursor(Editor *e); /* actualiza cursor_line/col desde buf */
void editor_ensure_visible(Editor *e);
/* Reinicia el timer del parpadeo del cursor (llamar tras cualquier edición o
 * movimiento para que el cursor siempre empiece visible tras una acción). */
void editor_cursor_blink_reset(Editor *e);
size_t editor_pos_from_line_col(Editor *e, int line, int col);

/* -- NUEVO: undo/redo API (usada desde input.c) --------------------------- */
void editor_undo_push_insert(Editor *e, size_t pos, const char *text,
                             size_t len);
void editor_undo_push_delete(Editor *e, size_t pos, const char *text,
                             size_t len);
void editor_undo(Editor *e);
void editor_redo(Editor *e);

/* -- NUEVO: helpers de selección ------------------------------------------- */
/* Devuelve las posiciones lógicas ordenadas del rango seleccionado.
   Retorna 0 si no hay selección activa.                                      */
int editor_sel_range(Editor *e, size_t *from, size_t *to);
void editor_sel_clear(Editor *e);

/* -- Tab management -------------------------------------------------------- */
void editor_tab_new(Editor *e);
void editor_tab_open(Editor *e, const char *path);
void editor_tab_close(Editor *e);
void editor_tab_switch(Editor *e, int i);
void editor_tab_save_state(Editor *e);