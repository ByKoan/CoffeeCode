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
#include "panel/panel.h"
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
    int group; /* grupo al que pertenece la pestaña: 0 (por defecto) o 1.
                  Con un solo grupo todas valen 0 y el comportamiento es el de
                  siempre; al dividir el editor, las pestañas se reparten entre
                  el grupo 0 (izquierda) y el grupo 1 (derecha). */
} EditorTab;

/* -- División del editor (árbol de dock) ----------------------------------
 * El área del editor se modela como un árbol de paneles (ver dock/dock.h): N
 * hojas (grupos de pestañas) anidadas en divisiones horizontales y verticales.
 * Cada pestaña pertenece a una hoja por su `group` (== group_id de la hoja).
 * MAX_GROUPS es el tope de hojas simultáneas y debe coincidir con
 * DOCK_MAX_LEAVES. */
#include "dock/dock.h"
#include "dock/float.h"
/* Tope de grupos (hojas del dock + flotantes simultaneos).  Los flotantes
 * tambien reclaman group_id via dock_alloc_group_id (que escanea SOLO las hojas
 * del arbol), asi que el espacio de ids debe cubrir hojas + flotantes a la vez:
 * DOCK_MAX_LEAVES + FLOAT_MAX_PANELS.  group_active_tab[] se indexa por group_id,
 * por lo que su tamano (MAX_GROUPS) debe ser ese total. */
#define MAX_GROUPS (DOCK_MAX_LEAVES + FLOAT_MAX_PANELS)

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
/* Lleva tag (struct Editor) para que otras cabeceras puedan declararla hacia
 * delante sin arrastrar esta (que incluye SDL). */
typedef struct Editor {
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

    /* -- división del editor (árbol de dock) --------------------------------
     * Con dock.leaf_count==1 (estado por defecto) el editor se comporta
     * EXACTAMENTE como antes: una única hoja a pantalla completa.  Con varias
     * hojas, el área del editor se reparte según el árbol (divisiones H/V
     * anidadas); cada hoja muestra solo SUS pestañas (las que tienen
     * tab.group == group_id de la hoja) y su pestaña activa.  La hoja enfocada
     * (active_group == group_id de la hoja con foco) recibe el teclado y la
     * edición: e->buf y los escalares de vista reflejan SIEMPRE la pestaña
     * activa de la hoja con foco, así todo el código existente sigue operando
     * sobre ella.  El árbol vive en `dock`. */
    DockTree dock;                  /* árbol de paneles del editor           */
    int active_group;               /* group_id de la hoja con el foco       */
    int group_active_tab[MAX_GROUPS]; /* índice GLOBAL en tabs[] de la pestaña
                                         activa de cada grupo (por group_id)  */

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
    int ext_panel_scroll;     /* scroll vertical del panel de extensiones */
    int ext_install_mode;     /* 1 = el proximo dialogo de carpeta instala una
                                 extension (ext_host_load) en lugar de abrir el
                                 explorador (ftree_load) */
    int ext_panel_w;          /* ancho actual del panel de extensiones (px),
                                 redimensionable arrastrando su borde izquierdo */

    /* -- Panel inferior con pestanas (Salida/Logs/Terminal + canales de las
     * extensiones).  El texto de cada canal vive en `panels` (almacen puro);
     * aqui van los escalares de la UI del panel. */
    PanelStore panels;        /* almacen de canales de texto (scrollback) */
    int bottom_panel_open;    /* 1 = panel inferior visible */
    int bottom_panel_h;       /* alto actual del panel inferior (px),
                                 redimensionable arrastrando su borde superior */
    int bottom_active_chan;   /* indice del canal/pestana activo */
    int bottom_focused;       /* 1 = el panel inferior tiene el foco (Ctrl+C
                                 copia su canal activo) */
    /* seleccion de texto con el raton dentro del panel inferior.  Se modela
     * como un rango [anchor, caret] de BYTE-OFFSETS dentro del `text` del canal
     * activo (NO indices de linea), para poder seleccionar caracteres arbitrarios
     * a traves de varias filas como en una terminal.  -1 = sin seleccion; ademas
     * anchor==caret => seleccion vacia (no se resalta nada). */
    int bottom_sel_anchor;    /* byte-offset ancla (extremo fijo) */
    int bottom_sel_caret;     /* byte-offset caret (extremo movil) */
    int bottom_sel_active;    /* 1 = hay seleccion viva en el panel inferior */
    int bottom_selecting;     /* 1 = arrastrando para seleccionar */

    /* -- Divisores arrastrables entre regiones (ver layout/layout.h) ------
     * Estado del arrastre del borde de un panel para redimensionarlo. */
    int dragging_divider; /* LayoutDivider en curso, o DIVIDER_NONE (-1) */
    int hovered_divider;  /* LayoutDivider bajo el cursor, o DIVIDER_NONE  */
    /* Nodo SPLIT del árbol de dock que se está arrastrando (DIVIDER_DOCK), o
     * DOCK_NONE.  Su orientación decide el cursor de redimensión. */
    int dock_drag_split;  /* índice del SPLIT en arrastre, o DOCK_NONE      */
    int dock_drag_orient; /* DockOrient del SPLIT en arrastre (cursor EW/NS) */

    /* -- Arrastre de una pestana para reorganizar el editor (drag-to-dock) --
     * Al pulsar sobre el titulo de una pestana se registra un CANDIDATO a
     * arrastre (drag_tab >= 0) sin empezar aun: si el cursor se mueve mas que
     * el umbral con el boton pulsado, dragging_tab pasa a 1.  Soltar mueve la
     * pestana al grupo bajo el cursor (zona centro) o divide la hoja destino
     * (zonas de borde).  Soltar sin superar el umbral = click normal. */
    int drag_tab;         /* indice GLOBAL en tabs[] del candidato, o -1     */
    int drag_from_group;  /* group_id de la hoja origen de la pestana         */
    int dragging_tab;     /* 1 = arrastre en curso (umbral superado)          */
    int drag_start_x;     /* X del raton al pulsar (para el umbral)           */
    int drag_start_y;     /* Y del raton al pulsar                            */
    int drag_mx, drag_my; /* posicion actual del raton durante el arrastre   */

    /* -- Override transitorio del área de contenido del editor --------------
     * Cuando el editor está dividido, el render dibuja CADA hoja haciendo su
     * pestaña activa la activa temporalmente y fijando aquí el sub-rectángulo
     * de la hoja; el dibujante de contenido (y el mapeo píxel->columna del
     * input) leen este override en vez del área global de la ventana.  Con
     * pane_active==0 (caso de 1 hoja) NADIE consulta estos campos y la
     * geometría es la de siempre: cero regresión.  El left/top/width/height
     * acotan el área ÚTIL del editor de esa hoja (su contenido, ya bajo su
     * propia barra de pestañas).  pane_top es relevante con divisiones
     * horizontales (hojas apiladas a distinta Y). */
    int pane_active;                       /* 1 = usar el override de abajo   */
    int pane_left, pane_top;               /* origen del área del panel (px)  */
    int pane_width, pane_height;           /* tamaño del área del panel (px)  */

    /* -- Paneles flotantes (overlay dentro de la ventana) -------------------
     * Cada flotante es un grupo de pestanas libre dibujado ENCIMA del arbol de
     * dock (ver dock/float.h).  El z-order es el orden del array: floats[0] es
     * el de mas atras, floats[float_count-1] el de mas al frente.  Con
     * float_count==0 (estado por defecto) NADIE consulta estos campos y todo se
     * comporta EXACTAMENTE como antes: cero regresion. */
    FloatPanel floats[FLOAT_MAX_PANELS]; /* paneles flotantes (z-order ascendente) */
    int float_count;                     /* numero de flotantes vivos             */

    /* -- Arrastre de un flotante (mover / redimensionar) --------------------
     * float_drag >= 0 indica el indice del flotante en arrastre; el modo (mover
     * por la barra de titulo o redimensionar por la esquina) lo distingue
     * float_resizing.  drag_off_x/y guardan el desfase cursor->esquina al iniciar
     * el movimiento para que el flotante no "salte" bajo el cursor. */
    int float_drag;        /* indice del flotante en arrastre, o -1            */
    int float_resizing;    /* 1 = redimensionando; 0 = moviendo                */
    int float_drag_off_x;  /* desfase X cursor -> esquina del marco al mover   */
    int float_drag_off_y;  /* desfase Y cursor -> esquina del marco al mover   */
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

/* -- División del editor (árbol de dock) ----------------------------------- */
/* Enfoca el grupo (hoja) @p g: guarda la vista actual en su pestaña, fija
 * active_group=g y carga el estado de la pestaña activa de ese grupo (de modo
 * que e->buf y los escalares de vista pasen a reflejar la hoja enfocada). No
 * hace nada si @p g no corresponde a ninguna hoja o ya es el grupo activo. */
void editor_focus_group(Editor *e, int g);
/* Divide la hoja enfocada en la orientación @p orient (DOCK_VERTICAL = lado a
 * lado, DOCK_HORIZONTAL = arriba/abajo), creando una hoja nueva como hermana.
 * Mueve la pestaña activa a la hoja nueva y la enfoca; si solo hay una pestaña,
 * crea una nueva vacía para no dejar la hoja original sin contenido.  No hace
 * nada si no hay pestañas o se alcanzó el tope de hojas. */
void editor_split_dir(Editor *e, DockOrient orient);
/* Atajo: divide la hoja enfocada en vertical (compatibilidad con el binding
 * Ctrl+\ original). */
void editor_split(Editor *e);

/* Suelta la pestana de indice GLOBAL @p tab en la hoja @p target_group segun la
 * zona @p zone (ver DockDropZone): DOCK_DZ_CENTER la mueve a ese grupo;
 * DOCK_DZ_LEFT/RIGHT/TOP/BOTTOM dividen esa hoja creando una hoja nueva a ese
 * lado y mueven la pestana ahi.  Si la hoja origen se queda sin pestanas, se
 * colapsa (el hermano hereda el espacio).  Repara foco, pestana activa y estado.
 * No hace nada en zonas/objetivos invalidos o si el movimiento es un no-op. */
void editor_tab_drop(Editor *e, int tab, int target_group, int zone);

/* Calcula la hoja y la zona de drop bajo el cursor (@p mx,@p my) para el
 * arrastre de pestanas.  Recorre las hojas del arbol (vale tambien con una sola
 * hoja, para poder dividir arrastrando sobre un borde).  Devuelve 1 si el cursor
 * cae sobre alguna hoja: rellena @p out_group (group_id), @p out_zone
 * (DockDropZone) y @p out_rect (rect de la hoja, opcional, puede ser NULL).
 * Devuelve 0 si el cursor no cae sobre ninguna hoja. */
int editor_drag_target(Editor *e, int mx, int my, int *out_group, int *out_zone,
                       DockRect *out_rect);

/* Devuelve el área del editor (px) que el árbol de dock reparte entre las
 * hojas: entre el explorador (izquierda) y el panel de extensiones (derecha),
 * y entre la navbar (arriba) y el panel inferior / barra de estado / atajos
 * (abajo).  Incluye la franja de la barra de pestañas de cada hoja (la barra
 * vive en el borde superior del rect de la hoja).  Es la MISMA geometría que
 * usa render e input, para que el dibujado y el enrutado de clics coincidan. */
DockRect editor_dock_area(Editor *e);

/* Enlaza SOLO los punteros y escalares "en vivo" del editor a la pestaña de
 * índice global @p idx, SIN recargar del disco ni tocar la cache del lexer. Es
 * un cambio de vista barato para que el RENDER dibuje el contenido de un grupo
 * no enfocado: tras dibujarlo, el render vuelve a enlazar la pestaña del grupo
 * con foco. No usar para cambiar el foco real (eso es editor_focus_group). */
void editor_render_bind_tab(Editor *e, int idx);

/* Indice GLOBAL valido de la pestana activa del grupo @p g, o -1 si el grupo
 * esta vacio.  Devuelve el indice registrado solo si esa pestana sigue viva y
 * pertenece a @p g; en otro caso la primera del grupo, o -1.  El render lo usa
 * como guarda para no dibujar el buffer de un grupo ajeno (ver
 * editor/tab_membership.h).  No muta el editor. */
int editor_group_valid_active_tab(Editor *e, int g);

/* -- Paneles flotantes (ver dock/float.h) --------------------------------- */
/* Limites validos (px) en los que un flotante puede moverse/redimensionarse:
 * toda la ventana bajo la navbar (para que la barra de titulo no tape la navbar)
 * y sobre la barra de estado.  La MISMA geometria la usan render e input. */
Rect editor_float_bounds(Editor *e);

/* Desprende la pestana de indice GLOBAL @p tab a un panel flotante nuevo cuyo
 * marco se centra en (@p cx,@p cy) (recortado a editor_float_bounds).  La pestana
 * se mueve a un group_id nuevo; si su hoja de dock origen queda vacia, se
 * colapsa.  El flotante queda al frente y enfocado.  No hace nada si no hay sitio
 * para mas flotantes/ids o el indice es invalido. */
void editor_float_detach_tab(Editor *e, int tab, int cx, int cy);

/* Trae el flotante de indice @p fi al frente del z-order (lo dibuja/consulta el
 * ultimo) y enfoca su grupo.  No hace nada si @p fi es invalido. */
void editor_float_focus(Editor *e, int fi);

/* Cierra el flotante de indice @p fi: cierra todas SUS pestanas.  Tras esto el
 * flotante desaparece del array (z-order compactado).  Si eran las ultimas
 * pestanas del editor, queda la pantalla de bienvenida. */
void editor_float_close(Editor *e, int fi);

/* Acopla el flotante de indice @p fi de vuelta al arbol de dock: mueve TODAS sus
 * pestanas a la hoja enfocada del dock (zona centro) y elimina el flotante.  La
 * pestana activa del flotante queda como activa en el destino.  No hace nada si
 * @p fi es invalido. */
void editor_float_dock(Editor *e, int fi);

/* Retira del array cualquier panel flotante que se haya quedado sin pestanas
 * (p.ej. tras arrastrar su ultima pestana al dock).  Compacta el z-order. */
void editor_float_gc_empty(Editor *e);