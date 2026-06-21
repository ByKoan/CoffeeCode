#pragma once
/**
 * @file layout_persist.h
 * @brief Persistencia de la disposicion del editor entre sesiones (layout):
 *        pestanas abiertas, arbol de paneles del dock, paneles flotantes y
 *        tamanos de los paneles de la UI.
 *
 * El modulo se parte en dos capas:
 *
 *   1. Una struct POD intermedia ::LayoutData que captura SOLO lo persistible
 *      del estado del editor, mas dos funciones PURAS ::layout_serialize y
 *      ::layout_parse que la convierten a/desde texto.  Esta capa NO depende de
 *      SDL ni de la struct Editor, asi que se prueba en headless
 *      (ver test/session/test_layout_persist.c).
 *
 *   2. El wireado al Editor (rellenar ::LayoutData desde el estado vivo al
 *      guardar; reconstruir el estado vivo desde ::LayoutData al cargar) vive en
 *      layout_persist_editor.c, que SI incluye editor.h.
 *
 * El formato de texto es linea-a-linea, "clave valor" o registros prefijados,
 * facil de parsear en C sin librerias externas (NO es JSON).  Un fichero
 * corrupto o parcial nunca debe crashear: ::layout_parse valida cada campo y
 * devuelve 0 ante datos inverosimiles, y el llamante cae al comportamiento por
 * defecto.
 */
#include <stddef.h>

/* -- Limites del POD -------------------------------------------------------
 * Replican los topes del editor (MAX_TABS, DOCK_MAX_NODES, DOCK_MAX_LEAVES,
 * FLOAT_MAX_PANELS) pero SIN incluir sus cabeceras, para que esta capa quede
 * libre de SDL/Editor.  Si alguno de esos topes cambia, ajustar aqui tambien
 * (un static_assert en el wireado verifica que no se queden cortos). */
#define LAYOUT_MAX_TABS 16   /**< == MAX_TABS                          */
#define LAYOUT_MAX_NODES 16  /**< == DOCK_MAX_NODES (2*DOCK_MAX_LEAVES) */
#define LAYOUT_MAX_LEAVES 8  /**< == DOCK_MAX_LEAVES                    */
#define LAYOUT_MAX_FLOATS 8  /**< == FLOAT_MAX_PANELS                   */
#define LAYOUT_PATH_MAX 512  /**< == sizeof(EditorTab::filepath)        */

/**
 * @brief Una pestana persistida: su ruta de archivo y su grupo.
 *
 * Solo se guardan pestanas con @c path no vacio (las "Sin titulo" sin ruta no
 * se persisten).  @c is_group_active marca si esta pestana es la activa de su
 * grupo (para restaurar @c group_active_tab[]).
 */
typedef struct {
    char path[LAYOUT_PATH_MAX]; /**< ruta del archivo (no vacia)        */
    int group;                  /**< group_id al que pertenece          */
    int is_group_active;        /**< 1 si es la pestana activa del grupo */
} LayoutTab;

/**
 * @brief Un nodo del arbol de dock, volcado tal cual (espejo de DockNode).
 *
 * Se serializa el pool entero de nodos por indices, igual que vive en memoria,
 * para reconstruirlo bit a bit sin reinterpretar la topologia.
 */
typedef struct {
    int kind;     /**< 0 = LEAF, 1 = SPLIT                       */
    int group_id; /**< LEAF: id del grupo de pestanas            */
    int orient;   /**< SPLIT: 0 = VERTICAL, 1 = HORIZONTAL        */
    float ratio;  /**< SPLIT: fraccion [0,1] para child_a         */
    int child_a;  /**< SPLIT: indice del primer hijo (o -1)       */
    int child_b;  /**< SPLIT: indice del segundo hijo (o -1)      */
    int parent;   /**< indice del padre (o -1 en la raiz)         */
} LayoutDockNode;

/**
 * @brief Un panel flotante persistido: su rectangulo y su grupo.
 */
typedef struct {
    int x, y, w, h; /**< marco completo del flotante (px)  */
    int group_id;   /**< id del grupo de pestanas          */
} LayoutFloat;

/**
 * @brief Disposicion completa del editor, lista para serializar/parsear.
 *
 * Captura los tamanos de la UI, las pestanas abiertas con ruta, el pool de
 * nodos del dock, los flotantes y el foco (grupo activo + pestana activa
 * global).  Es POD pura: no posee recursos dinamicos.
 */
typedef struct {
    /* -- tamanos de la UI -- */
    int ftree_width;        /**< ancho del explorador (px)           */
    int ftree_open;         /**< 1 = explorador visible              */
    int ext_panel_w;        /**< ancho del panel de extensiones (px)  */
    int ext_panel_open;     /**< 1 = panel de extensiones visible     */
    int bottom_panel_h;     /**< alto del panel inferior (px)         */
    int bottom_panel_open;  /**< 1 = panel inferior visible           */

    /* -- pestanas -- */
    LayoutTab tabs[LAYOUT_MAX_TABS]; /**< pestanas con ruta            */
    int tab_count;                   /**< numero de pestanas guardadas */

    /* -- arbol de dock -- */
    LayoutDockNode dock_nodes[LAYOUT_MAX_NODES]; /**< pool de nodos    */
    int dock_node_count;  /**< nodos usados del pool                   */
    int dock_root;        /**< indice de la raiz (-1 = vacio)          */
    int dock_focused_leaf;/**< indice del nodo hoja con foco            */
    int dock_leaf_count;  /**< numero de hojas vivas                    */

    /* -- flotantes -- */
    LayoutFloat floats[LAYOUT_MAX_FLOATS]; /**< paneles flotantes      */
    int float_count;                       /**< numero de flotantes     */

    /* -- foco -- */
    int active_group; /**< group_id de la hoja/flotante con foco        */
    int active_tab;   /**< indice GLOBAL en tabs[] de la pestana activa  */
} LayoutData;

/**
 * @brief Pone @p d a un estado "vacio" coherente (sin pestanas ni flotantes).
 *
 * Tras esto, @c tab_count, @c dock_node_count y @c float_count valen 0,
 * @c dock_root vale -1 y los tamanos quedan a 0 (el llamante decide si usa
 * defaults).  Util como base antes de rellenar y como salida de un parse fallido.
 *
 * @param d Struct a inicializar (no nula).
 */
void layout_data_clear(LayoutData *d);

/**
 * @brief Serializa @p d a texto en @p out (terminado en NUL).
 *
 * El formato es linea-a-linea, ASCII puro, parseable por ::layout_parse.
 * Funcion PURA: no toca disco ni globals.
 *
 * @param d   Disposicion a serializar.
 * @param out Buffer de salida.
 * @param cap Capacidad de @p out en bytes (incluye el NUL).
 * @return Numero de bytes escritos (sin contar el NUL), o 0 si no cabe.
 */
size_t layout_serialize(const LayoutData *d, char *out, size_t cap);

/**
 * @brief Parsea el texto @p text rellenando @p out.
 *
 * Tolera lineas desconocidas, en blanco y campos ausentes (quedan en su valor
 * por defecto).  Valida rangos e indices: ante datos inverosimiles (conteos
 * fuera de rango, indices que desbordan el pool) deja @p out en un estado vacio
 * coherente y devuelve 0.  Funcion PURA.
 *
 * @param text Texto a parsear (terminado en NUL).
 * @param out  Disposicion de salida.
 * @return 1 si se parseo una disposicion no vacia y coherente; 0 si vacia o
 *         invalida (en cuyo caso @p out queda limpio via ::layout_data_clear).
 */
int layout_parse(const char *text, LayoutData *out);

/* ===========================================================================
 *  Wireado al Editor (implementado en layout_persist_editor.c, con SDL/Editor)
 * =========================================================================== */

/* Declaracion adelantada: el wireado conoce la struct Editor; la capa pura no. */
struct Editor;

/**
 * @brief Vuelca la disposicion viva del editor al fichero @c layout.txt.
 *
 * Si no hay nada interesante que guardar (sin pestanas con ruta, sin dividir y
 * sin flotantes) no escribe nada.  Pensado para llamarse al cerrar el editor.
 *
 * @param e Editor (solo lectura).
 */
void layout_save(const struct Editor *e);

/**
 * @brief Reconstruye la disposicion del editor desde @c layout.txt.
 *
 * Reabre las pestanas guardadas (saltando archivos inexistentes), reconstruye
 * el arbol de dock y los flotantes, fija el foco y los tamanos de la UI, y
 * valida las invariantes.  Si el fichero no existe, esta corrupto o la
 * restauracion queda vacia, NO toca el editor y devuelve 0 (el llamante deja el
 * arranque por defecto).
 *
 * @param e Editor a reconstruir.
 * @return 1 si se restauro una disposicion con al menos una pestana; 0 si no.
 */
int layout_restore(struct Editor *e);
