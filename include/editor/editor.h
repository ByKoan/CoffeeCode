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
#include "detached/detached.h"
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
    /* -- Multi-ventana: instancias secundarias --------------------------------
     * Cada ventana del IDE es una instancia COMPLETA de Editor (su propio dock,
     * flotantes, explorador, paneles...).  La ventana PRINCIPAL (creada con
     * editor_init) es DUENA de los recursos compartidos por puntero: la fuente
     * (font), el host de extensiones (ext_host) y los subsistemas globales de
     * SDL/TTF.  Una ventana SECUNDARIA (creada con editor_init_secondary)
     * REFERENCIA esos recursos pero NO los crea ni los destruye: con
     * is_secondary==1, editor_free libera SOLO su window/renderer y sus buffers,
     * dejando intactos font/ext_host/SDL (los libera la principal).  Con
     * is_secondary==0 (caso por defecto, ventana principal) todo es como
     * siempre: cero regresion. */
    int is_secondary;       /* 1 = ventana secundaria; no posee recursos compartidos */
    /* 1 = no persistir la disposicion en editor_free.  Lo activa la capa de
     * aplicacion (app_run) cuando ELLA ya guardo la sesion COMPLETA (principal +
     * secundarias) antes de liberar nada, para que el layout_save de editor_free
     * no sobreescriba el fichero con solo la principal.  Con 0 (init sin App, o
     * fallo de init) editor_free guarda como siempre: cero regresion. */
    int layout_save_suppressed;
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
    int background_view_open; /* 1 = sub-pantalla "Fondos" dentro de preferencias */

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

    /* -- Reordenado/insercion de pestanas sobre una barra ------------------
     * Durante un arrastre (dragging_tab=1), si el cursor cae sobre una BARRA de
     * pestanas (la global, la de una hoja o la de un flotante), se anota aqui el
     * grupo de esa barra y la posicion de insercion (0..n) bajo la X del cursor.
     * Cuando hay objetivo de barra, la barra manda: soltar INSERTA la pestana en
     * esa posicion (reordena si es el mismo grupo, mueve-e-inserta si es otro) y
     * el render dibuja una linea vertical de insercion en lugar del overlay de
     * zona del dock.  tab_reorder_group < 0 = sin objetivo de barra. */
    int tab_reorder_group; /* group_id de la barra bajo el cursor, o -1       */
    int tab_reorder_pos;   /* posicion de insercion (0..n) en ese grupo        */
    int tab_reorder_x;     /* X (px) de la linea de insercion para el render   */
    int tab_reorder_bar_y; /* Y de la barra destino (para la linea de insercion) */

    /* -- Multi-ventana: resaltado de la ventana DESTINO al arrastrar una pestana
     * entre ventanas.  Durante un arrastre cross-window, la capa de aplicacion
     * marca con 1 la ventana bajo el cursor global cuando es DISTINTA de la
     * origen, para que su render dibuje un borde de "soltar aqui".  Se limpia al
     * mover el cursor a otra ventana o al soltar.  Con una sola ventana siempre
     * vale 0: cero regresion. */
    int drag_hover_highlight;

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
    int float_resize_edges;/* mascara FLOAT_EDGE_* de los bordes que se arrastran */
    int float_drag_off_x;  /* desfase X cursor -> esquina del marco al mover   */
    int float_drag_off_y;  /* desfase Y cursor -> esquina del marco al mover   */

    /* -- Re-acople de un flotante arrastrando su barra de titulo ------------
     * Mientras se mueve un flotante (float_drag>=0, float_resizing==0), si el
     * cursor cae sobre una hoja del dock (y no sobre OTRO flotante) se anota
     * aqui el destino de acople: el group_id de la hoja y la zona (DockDropZone)
     * dentro de ella.  El render dibuja la guia y el mouse-up acopla.  Con
     * float_dock_target_group == -1 no hay destino: el flotante solo se mueve. */
    int float_dock_target_group; /* group_id de la hoja destino, o -1            */
    int float_dock_zone;         /* DockDropZone dentro de la hoja destino       */

    /* -- Ventanas desprendidas (un grupo en una ventana REAL del SO) ---------
     * Cada ventana desprendida (::DetachedWindow) saca un grupo de pestanas a su
     * propia ventana del sistema, con su SDL_Window/SDL_Renderer propios.  Sus
     * pestanas siguen en tabs[] (tab.group == group_id).  Con detached_count==0
     * (estado por defecto) NADIE consulta estos campos: el bucle, el input y el
     * render de la ventana principal son EXACTAMENTE los de siempre: cero
     * regresion. */
    DetachedWindow detached[MAX_DETACHED]; /* ventanas desprendidas vivas      */
    int detached_count;                    /* numero de ventanas desprendidas  */
    /* group_id de la ventana (principal o desprendida) que tiene el foco del
     * teclado.  -1 (o cualquier group_id de hoja/flotante) = la principal; el
     * group_id de una ventana desprendida = esa ventana tiene el foco.  Con
     * detached_count==0 no se usa: el teclado va siempre a la principal. */
    int detached_focus_group;

    /* fondo personalizado del editor */
    SDL_Texture *background_texture; /**< Textura de imagen de fondo (NULL si no hay). */
    int background_w;                /**< Ancho original de la imagen de fondo.         */
    int background_h;                /**< Alto original de la imagen de fondo.          */

    /* -- Cache de miniaturas de la galeria de fondos -------------------------
     * Una textura por entrada de settings.background_gallery (en el MISMO
     * orden), construida de forma perezosa al entrar a la sub-pantalla Fondos
     * en modo Imagen.  thumb[i] == NULL = la imagen no se pudo cargar (ruta
     * borrada / formato roto) y el render dibuja un placeholder en su celda.
     * thumb_valid==0 fuerza una reconstruccion la proxima vez que se necesite
     * (al agregar/quitar una imagen o reabrir Ajustes). */
    SDL_Texture *bg_thumb[BG_GALLERY_MAX]; /**< Miniatura de cada entrada (o NULL). */
    int bg_thumb_w[BG_GALLERY_MAX];        /**< Ancho original de cada miniatura.  */
    int bg_thumb_h[BG_GALLERY_MAX];        /**< Alto original de cada miniatura.   */
    int bg_thumb_count;                    /**< Numero de miniaturas construidas.  */
    int bg_thumb_valid;                    /**< 1 = cache al dia; 0 = reconstruir. */
    int bg_gallery_scroll;                 /**< Primera fila visible de la rejilla. */
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

/* Tareas periodicas de un Editor que se ejecutan una vez por iteracion del bucle
 * (independientes de eventos): autoguardado y parpadeo del cursor.  La extrae
 * el bucle multi-ventana (app_run) para
 * correrlas por CADA ventana; editor_run la usa para la suya. */
void editor_frame_tasks(Editor *e);

/* Inicializa @p e como ventana SECUNDARIA de @p primary: crea su PROPIO
 * SDL_Window/SDL_Renderer del tamano (@p w,@p h) y COMPARTE por puntero los
 * recursos de @p primary (fuente, ext_host, metricas de fuente y tema).  El
 * editor secundario arranca SIN pestanas (dock de una hoja vacia), listo para
 * recibir pestanas movidas desde otra ventana.  Marca @c is_secondary=1 para que
 * @c editor_free NO libere los recursos compartidos (de los que la principal
 * sigue siendo duena) ni cierre SDL/TTF.  Devuelve 1 si todo fue bien; 0 si SDL
 * fallo creando la ventana/renderer (en ese caso @p e queda sin usar). */
int editor_init_secondary(Editor *e, Editor *primary, int w, int h);

/* Mueve TODAS las pestanas (structs ::EditorTab) del grupo @p src_group del
 * editor @p src al editor @p dst (a su hoja de dock con foco), preservando su
 * almacenamiento (buf/lex/undo): es una copia superficial del struct + limpieza
 * del slot origen, sin re-cargar del disco.  Repara en @p src los indices
 * guardados (active_tab + group_active_tab[]) y colapsa su hoja origen si queda
 * vacia; en @p dst enfoca la hoja destino con la pestana activa del grupo origen
 * como activa.  Lo usa el desprender (a una ventana nueva).  No hace nada si el
 * grupo origen esta vacio o no hay hoja destino en @p dst. */
void editor_transfer_group(Editor *src, int src_group, Editor *dst);

/* Mueve UNA pestana (struct ::EditorTab, con su buf/lex/undo) de indice
 * GLOBAL @p tab del editor @p src al editor @p dst, decidiendo la hoja y la zona
 * de drop a partir de las coordenadas LOCALES (@p dst_mx,@p dst_my) en la ventana
 * receptora (igual que editor_tab_drop pero entre ventanas distintas).  Es una
 * copia superficial del struct + limpieza del slot origen (sin recargar del
 * disco).  Zona CENTER: la inserta en esa hoja; zona de borde: divide la hoja
 * destino y la coloca en la hoja nueva.  Repara @p src (colapsa su hoja si quedo
 * vacia y re-enfoca, o deja la bienvenida si se quedo sin pestanas) y enfoca
 * @p dst sobre la pestana movida.  No hace nada si @p src==@p dst, el indice es
 * invalido o @p dst no tiene sitio (MAX_TABS).  Lo usa el arrastre de una pestana
 * a OTRA ventana. */
void editor_transfer_tab(Editor *src, int tab, Editor *dst, int dst_mx,
                         int dst_my);

/* Mueve TODAS las pestanas de @p src (de cualquiera de sus grupos) a la hoja de
 * dock con foco de @p dst, aplanandolas en ese grupo.  Es la fusion de una
 * ventana secundaria de vuelta a la principal al cerrarla: tras esto @p src queda
 * sin pestanas (tab_count==0) y sus recursos los libera @c editor_free.  La
 * pestana activa de @p src queda como activa en @p dst.  No hace nada si @p src
 * no tiene pestanas o @p dst no tiene hoja destino. */
void editor_merge_all(Editor *src, Editor *dst);
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

/* Reordena/inserta la pestana global @p tab para que quede en la posicion
 * @p insert_pos DENTRO de @p target_group, reasignando su grupo si difiere.
 * Reordena la tabla de pestanas y remapea los indices guardados (active_tab +
 * group_active_tab[]) para que sigan apuntando a las MISMAS pestanas logicas.
 * Si la hoja origen se queda vacia, la colapsa (el hermano hereda).  Enfoca el
 * grupo destino con la pestana movida como activa.  Es la accion de soltar una
 * pestana arrastrada SOBRE una barra de pestanas. */
void editor_tab_reorder(Editor *e, int tab, int target_group, int insert_pos);

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

/* Acopla el flotante de indice @p fi a una hoja CONCRETA del dock (no a la
 * enfocada): mueve TODAS sus pestanas al grupo @p target_group cuando @p zone es
 * DOCK_DZ_CENTER, o divide esa hoja en la direccion de la zona de borde
 * (LEFT/RIGHT/TOP/BOTTOM) creando una hoja nueva con las pestanas del flotante.
 * Tras esto el flotante desaparece y la hoja resultante queda enfocada.  Lo usa
 * el re-acople por arrastre de la barra de titulo.  No hace nada si @p fi,
 * @p target_group o @p zone son invalidos. */
void editor_float_dock_to(Editor *e, int fi, int target_group, int zone);

/* Detecta el destino de re-acople de un flotante cuyo cursor esta en (@p mx,
 * @p my): si el punto cae sobre una hoja del dock y NO sobre otro flotante,
 * escribe en @p out_group el group_id de la hoja, en @p out_zone la DockDropZone
 * y devuelve 1.  Devuelve 0 (sin destino) si el cursor esta fuera del dock o
 * sobre cualquier flotante distinto del que se arrastra (@p drag_fi).  Funcion
 * de apoyo del arrastre de la barra de titulo. */
int editor_float_dock_target(Editor *e, int drag_fi, int mx, int my,
                             int *out_group, int *out_zone);

/* Retira del array cualquier panel flotante que se haya quedado sin pestanas
 * (p.ej. tras arrastrar su ultima pestana al dock).  Compacta el z-order. */
void editor_float_gc_empty(Editor *e);

/* Da el foco de edicion al grupo @p group de una ventana desprendida (que NO es
 * una hoja del dock): fija active_group/active_tab a su pestana activa valida y
 * carga su estado (e->buf y escalares de vista pasan a reflejar esa pestana).
 * No hace nada si el grupo esta vacio.  Es el analogo de editor_focus_group para
 * grupos que viven fuera del arbol de dock. */
void editor_focus_detached_group(Editor *e, int group);

/* Mueve TODAS las pestanas del grupo @p group a la hoja del dock con foco y la
 * enfoca con la pestana activa del grupo origen como activa.  No toca recursos
 * de SDL: solo reasigna tab.group y repara foco/pestana activa/estado.  Lo usa el
 * cierre de una ventana desprendida para re-acoplar su grupo a la principal.  No
 * hace nada si no hay pestanas en @p group o no hay hoja de dock destino. */
void editor_detached_reattach_group(Editor *e, int group);

/* -- Ventanas desprendidas (ventana REAL del SO; ver detached/detached.h) --- */
/* Promueve el panel flotante de indice @p fi a una ventana desprendida del SO:
 * crea su SDL_Window/SDL_Renderer del tamano del flotante, le pasa su group_id y
 * ELIMINA el FloatPanel in-window (sus pestanas siguen en tabs[] con el mismo
 * grupo).  No hace nada si @p fi es invalido, si no hay sitio para mas ventanas
 * desprendidas (detached_count >= MAX_DETACHED) o si SDL falla creando la
 * ventana/renderer (en ese caso el flotante se conserva). */
void editor_detach_float(Editor *e, int fi);

/* Indice de la ventana desprendida cuyo SDL_Window tiene el @p window_id dado, o
 * -1 si ninguna (p.ej. el evento es de la ventana principal).  Es el enrutado de
 * eventos multi-ventana.  Funcion barata (compara hasta MAX_DETACHED ids). */
int editor_detached_by_window_id(Editor *e, unsigned int window_id);

/* Dibuja TODAS las ventanas desprendidas (su tira de pestanas + el contenido de
 * su pestana activa) en sus renderers propios, presentando cada una.  Salva y
 * restaura el renderer/tamano/bind de la ventana principal alrededor de cada
 * una, de modo que el estado del Editor queda como estaba al entrar.  No hace
 * nada si detached_count==0: cero regresion. */
void editor_render_detached(Editor *e);

/* Maneja un evento de SDL @p ev que pertenece a la ventana desprendida de indice
 * @p di (ya resuelto por editor_detached_by_window_id): foco de su grupo, clic
 * que coloca el cursor en su pestana activa, teclado que edita esa pestana,
 * rueda que hace scroll, resize que actualiza su win_w/h y CLOSE_REQUESTED que la
 * cierra re-acoplando su grupo a la principal.  @p ev es un SDL_Event* (void*
 * para no acoplar este header a SDL). */
void editor_detached_handle_event(Editor *e, int di, void *ev);

/* Cierra la ventana desprendida de indice @p di: re-acopla su grupo de pestanas
 * a la ventana principal (a la hoja del dock con foco) y destruye su
 * renderer+window.  No deja pestanas huerfanas ni grupos colgantes.  No hace nada
 * si @p di es invalido. */
void editor_detached_close(Editor *e, int di);

/* -- Fondo personalizado --------------------------------------------------- */
/**
 * @brief Carga una imagen como textura de fondo del editor.
 *
 * Si @p path está vacío o es NULL, libera el fondo actual. La imagen se carga
 * con SDL_image y se sube a GPU como textura para un renderizado eficiente.
 *
 * @param e    Editor destino.
 * @param path Ruta a la imagen (PNG, JPG, BMP, etc.). "" o NULL limpia el fondo.
 * @return 1 si se cargó (o limpió) exitosamente; 0 en caso de error.
 */
int editor_load_background(Editor *e, const char *path);

/**
 * @brief (Re)construye la cache de miniaturas de la galeria de fondos.
 *
 * Libera las miniaturas previas y carga una textura por cada ruta de
 * @c e->settings.background_gallery.  Las imagenes que no carguen quedan con
 * miniatura NULL (el render dibuja un placeholder).  Marca @c bg_thumb_valid=1.
 * Solo hace trabajo si la cache esta invalidada; llamarla a menudo es barato.
 */
void editor_bg_thumbs_build(Editor *e);

/** Libera todas las miniaturas de la galeria y deja la cache invalidada. */
void editor_bg_thumbs_free(Editor *e);

/** Invalida la cache de miniaturas (se reconstruira al pintar la galeria). */
void editor_bg_thumbs_invalidate(Editor *e);
