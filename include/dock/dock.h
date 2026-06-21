#pragma once
/**
 * @file dock.h
 * @brief Arbol de "dock" del area del editor: N paneles con divisiones
 *        anidadas horizontales y verticales.
 *
 * Generaliza el antiguo split de dos grupos lado a lado a un ARBOL recursivo:
 * cada NODO es o bien una DIVISION (SPLIT: una orientacion H/V, un ratio en
 * [0,1] y dos hijos) o bien una HOJA (LEAF: un grupo de pestanas identificado
 * por @c group_id).  Una sola hoja = el editor sin dividir (comportamiento de
 * siempre, cero regresion).  Dos hojas con un split vertical = el MVP previo.
 * El arbol admite N hojas anidadas en cualquier combinacion de H y V.
 *
 * Diseno hardware-amigable y serializable: los nodos viven en un POOL contiguo
 * (array fijo + indices, NUNCA punteros).  Un indice -1 marca "sin nodo".  Asi
 * no hay fragmentacion ni punteros colgantes al copiar/guardar el estado, y el
 * recorrido del arbol es un walk de memoria contigua.
 *
 * El nucleo son funciones PURAS sobre enteros y la struct ::DockTree: no
 * dependen de SDL ni de la struct Editor, asi que se prueban en headless (ver
 * test/dock/test_dock.c).  El render y el input las alimentan con la geometria
 * del area del editor y enrutan por los rects que devuelven.
 */
#include <stddef.h>

/* -- Limites del pool ----------------------------------------------------- */
/* Maximo de HOJAS (grupos de pestanas) simultaneas.  Cada split intermedio
 * consume tambien un nodo, asi que el pool total se dimensiona para N hojas
 * mas sus N-1 splits internos. */
#define DOCK_MAX_LEAVES 8
#define DOCK_MAX_NODES (2 * DOCK_MAX_LEAVES) /* hojas + splits, con holgura */

/* Indice "nulo" en el pool (sin hijo / sin nodo). */
#define DOCK_NONE (-1)

/* Ancho/alto minimo (px) que conserva cada hoja al repartir un split, para que
 * ningun panel colapse al arrastrar un divisor. */
#define DOCK_LEAF_MIN 120

/* Anchura (px) de la franja agarrable a cada lado de un divisor interno. */
#define DOCK_DIVIDER_GRAB 5

/** Tipo de nodo del arbol de dock. */
typedef enum {
    DOCK_LEAF = 0, /**< hoja: un grupo de pestanas (group_id)              */
    DOCK_SPLIT     /**< division: orientacion + ratio + dos hijos          */
} DockNodeKind;

/**
 * @brief Zona de "drop" dentro del rectangulo de una hoja al arrastrar.
 *
 * Al arrastrar una pestana sobre una hoja, la posicion del cursor dentro de su
 * rect decide la accion: el centro mueve la pestana a ESE grupo; las bandas de
 * los bordes dividen la hoja creando una hoja nueva a ese lado (LEFT/RIGHT en
 * vertical, TOP/BOTTOM en horizontal).
 */
typedef enum {
    DOCK_DZ_NONE = 0, /**< fuera del rect: ninguna accion              */
    DOCK_DZ_CENTER,   /**< centro: mover la pestana a este grupo        */
    DOCK_DZ_LEFT,     /**< banda izquierda: dividir V, hoja nueva a la izquierda */
    DOCK_DZ_RIGHT,    /**< banda derecha: dividir V, hoja nueva a la derecha    */
    DOCK_DZ_TOP,      /**< banda superior: dividir H, hoja nueva arriba         */
    DOCK_DZ_BOTTOM    /**< banda inferior: dividir H, hoja nueva abajo          */
} DockDropZone;

/* Fraccion del ancho/alto del rect que ocupa cada banda de borde al calcular la
 * zona de drop; el resto (centro) es DOCK_DZ_CENTER. */
#define DOCK_DROP_BAND 0.25f

/** Orientacion de una division. */
typedef enum {
    DOCK_VERTICAL = 0,  /**< divisor vertical: hijos lado a lado (reparte ancho) */
    DOCK_HORIZONTAL     /**< divisor horizontal: hijos apilados (reparte alto)   */
} DockOrient;

/**
 * @brief Un nodo del arbol de dock (hoja o division), en el pool contiguo.
 *
 * Cuando @c kind==DOCK_LEAF solo @c group_id es relevante.  Cuando
 * @c kind==DOCK_SPLIT solo @c orient/@c ratio/@c child_a/@c child_b lo son.
 */
typedef struct {
    DockNodeKind kind;  /**< LEAF o SPLIT                                   */
    /* -- LEAF -- */
    int group_id;       /**< id del grupo de pestanas de esta hoja          */
    /* -- SPLIT -- */
    DockOrient orient;  /**< orientacion de la division                     */
    float ratio;        /**< fraccion [0,1] del espacio para child_a        */
    int child_a;        /**< indice del primer hijo (izquierda/arriba)      */
    int child_b;        /**< indice del segundo hijo (derecha/abajo)        */
    int parent;         /**< indice del padre, o DOCK_NONE en la raiz       */
} DockNode;

/**
 * @brief Arbol de dock completo: pool de nodos + raiz + hoja con foco.
 *
 * @c leaf_count == 1 (estado por defecto) significa una unica hoja a pantalla
 * completa: el editor se comporta EXACTAMENTE como sin dividir.
 */
typedef struct {
    DockNode nodes[DOCK_MAX_NODES]; /**< pool contiguo de nodos             */
    int node_count;                 /**< nodos usados del pool              */
    int root;                       /**< indice de la raiz (DOCK_NONE vacio) */
    int focused_leaf;               /**< indice del nodo hoja con el foco    */
    int leaf_count;                 /**< numero de hojas vivas en el arbol   */
} DockTree;

/** Rectangulo en pixeles (independiente de SDL/Editor). */
typedef struct {
    int x, y, w, h;
} DockRect;

/** Rect de una hoja: su group_id y su rectangulo en pantalla. */
typedef struct {
    int group_id; /**< id del grupo de la hoja            */
    int node;     /**< indice del nodo hoja en el pool    */
    DockRect rect; /**< area asignada a la hoja (px)       */
} DockLeafRect;

/* ===========================================================================
 *  Construccion y mutacion del arbol (puras: solo tocan la DockTree)
 * =========================================================================== */

/**
 * @brief Reinicia el arbol a una sola hoja con @p group_id (estado por defecto).
 *
 * Tras esto @c leaf_count==1 y el unico nodo es la raiz LEAF enfocada: el editor
 * se comporta como sin dividir.
 *
 * @param t        Arbol a inicializar.
 * @param group_id Id del grupo de la unica hoja.
 */
void dock_init_single(DockTree *t, int group_id);

/**
 * @brief Divide la hoja @p leaf en la orientacion @p orient, creando una hoja
 *        nueva con @p new_group_id como hermana.
 *
 * La hoja @p leaf se convierte (en su sitio) en un SPLIT con dos hijos: la hoja
 * original (que conserva su group_id) y una hoja nueva con @p new_group_id.  El
 * ratio inicial es 0.5 (mitades).  La hoja nueva queda como child_b (derecha en
 * V, abajo en H).
 *
 * @param t            Arbol.
 * @param leaf         Indice del nodo hoja a dividir.
 * @param orient       Orientacion del nuevo split (V o H).
 * @param new_group_id Id del grupo de la hoja nueva.
 * @return El indice del nodo hoja NUEVO, o DOCK_NONE si no se pudo dividir
 *         (nodo no es hoja, sin sitio en el pool, o tope de hojas alcanzado).
 */
int dock_split_leaf(DockTree *t, int leaf, DockOrient orient, int new_group_id);

/**
 * @brief Como ::dock_split_leaf pero eligiendo en que lado queda la hoja nueva.
 *
 * ::dock_split_leaf siempre coloca la hoja nueva como child_b (derecha en V,
 * abajo en H).  Esta variante permite colocarla como child_a (izquierda/arriba)
 * cuando @p new_first es 1, util al soltar una pestana sobre la banda
 * izquierda/superior de una hoja.  Con @p new_first==0 el comportamiento es
 * identico a ::dock_split_leaf.
 *
 * @param t            Arbol.
 * @param leaf         Indice del nodo hoja a dividir.
 * @param orient       Orientacion del nuevo split (V o H).
 * @param new_group_id Id del grupo de la hoja nueva.
 * @param new_first    1 = la hoja nueva es child_a (izquierda/arriba); 0 = child_b.
 * @return El indice del nodo hoja NUEVO, o DOCK_NONE si no se pudo dividir.
 */
int dock_split_leaf_side(DockTree *t, int leaf, DockOrient orient,
                         int new_group_id, int new_first);

/**
 * @brief Elimina la hoja @p leaf y colapsa su split: el hermano ocupa el sitio.
 *
 * Si la hoja es la raiz (unica hoja) no hace nada.  En otro caso, su nodo padre
 * (un SPLIT) se sustituye por el hermano de @p leaf, de modo que el hermano
 * hereda el espacio que ocupaba el split.  Compacta indices invalidados.
 *
 * @param t    Arbol.
 * @param leaf Indice del nodo hoja a eliminar.
 * @return 1 si se elimino, 0 si no (era la unica hoja o indice invalido).
 */
int dock_remove_leaf(DockTree *t, int leaf);

/**
 * @brief Devuelve el indice del nodo hoja cuyo group_id es @p group_id.
 *
 * @param t        Arbol.
 * @param group_id Id de grupo a buscar.
 * @return Indice del nodo hoja, o DOCK_NONE si ningun nodo lo tiene.
 */
int dock_leaf_by_group(const DockTree *t, int group_id);

/**
 * @brief Asigna el primer group_id libre (no usado por ninguna hoja) en [0,N).
 *
 * Util al dividir: la hoja nueva necesita un group_id que no choque con los
 * existentes.
 *
 * @param t Arbol.
 * @return Un group_id sin usar en [0, DOCK_MAX_LEAVES), o DOCK_NONE si todos
 *         estan ocupados.
 */
int dock_alloc_group_id(const DockTree *t);

/* ===========================================================================
 *  Layout (puro: reparte un area entre las hojas segun el arbol)
 * =========================================================================== */

/**
 * @brief Computa el rectangulo de CADA hoja del arbol dentro de @p area.
 *
 * Recorre el arbol: un SPLIT vertical reparte el ANCHO de su area entre
 * child_a/child_b segun el ratio (con un minimo ::DOCK_LEAF_MIN por hijo); uno
 * horizontal reparte el ALTO.  Las hojas reciben el area resultante.  Con una
 * sola hoja, su rect es @p area completa (cero regresion).
 *
 * @param t          Arbol.
 * @param area       Area total a repartir (px).
 * @param out_rects  Buffer de salida (al menos ::DOCK_MAX_LEAVES entradas).
 * @param max_out    Capacidad de @p out_rects.
 * @return El numero de hojas escritas en @p out_rects.
 */
int dock_compute_leaf_rects(const DockTree *t, DockRect area,
                            DockLeafRect *out_rects, int max_out);

/**
 * @brief Reparte el area de un SPLIT entre sus dos hijos (child_a, child_b).
 *
 * Aplica el mismo reparto que ::dock_compute_leaf_rects para un solo nodo
 * SPLIT: util para que el render situe el divisor o recursar manualmente sin
 * reimplementar la matematica.  Si @p node no es un SPLIT, devuelve @p area en
 * ambos rects.
 *
 * @param t      Arbol.
 * @param node   Indice del nodo SPLIT.
 * @param area   Area del nodo.
 * @param[out] ra Area del primer hijo (izquierda/arriba).
 * @param[out] rb Area del segundo hijo (derecha/abajo).
 */
void dock_child_areas(const DockTree *t, int node, DockRect area, DockRect *ra,
                      DockRect *rb);

/**
 * @brief Hit-test de los divisores internos del arbol bajo el cursor.
 *
 * Recorre los SPLIT del arbol y comprueba si (@p mx,@p my) cae sobre la franja
 * agarrable (+/-::DOCK_DIVIDER_GRAB) del borde que separa sus dos hijos, dentro
 * del area del split.
 *
 * @param t          Arbol.
 * @param area       Area total del editor (la misma que en dock_compute_leaf_rects).
 * @param mx         X del raton (px).
 * @param my         Y del raton (px).
 * @param[out] orient Orientacion del divisor encontrado (si retorna != DOCK_NONE).
 * @return Indice del nodo SPLIT bajo el cursor, o DOCK_NONE.
 */
int dock_hit_divider(const DockTree *t, DockRect area, int mx, int my,
                     DockOrient *orient);

/**
 * @brief Ajusta el ratio del SPLIT @p split segun la posicion del cursor.
 *
 * Para un split vertical usa @p mx; para uno horizontal usa @p my.  El ratio se
 * recorta para que ambos hijos conserven al menos ::DOCK_LEAF_MIN px en el area
 * del split (calculada recorriendo el arbol desde la raiz).
 *
 * @param t     Arbol a modificar.
 * @param area  Area total del editor.
 * @param split Indice del nodo SPLIT a ajustar.
 * @param mx    X del raton (px).
 * @param my    Y del raton (px).
 */
void dock_apply_divider(DockTree *t, DockRect area, int split, int mx, int my);

/**
 * @brief Clasifica el punto (@p mx,@p my) dentro del rect @p leaf_rect en una
 *        ::DockDropZone (centro o banda de borde) para el arrastre de pestanas.
 *
 * Las bandas de cada borde ocupan ::DOCK_DROP_BAND del ancho (LEFT/RIGHT) o del
 * alto (TOP/BOTTOM) del rect; el resto interior es ::DOCK_DZ_CENTER.  Cuando el
 * punto se acerca a una esquina gana la banda mas cercana a su borde (la de
 * menor profundidad relativa).  Un punto fuera del rect devuelve ::DOCK_DZ_NONE.
 *
 * Funcion PURA: no toca el arbol, solo geometria.  Se prueba en headless.
 *
 * @param leaf_rect Rectangulo de la hoja destino (px).
 * @param mx        X del cursor (px).
 * @param my        Y del cursor (px).
 * @return La zona de drop bajo el cursor.
 */
DockDropZone dock_drop_zone(DockRect leaf_rect, int mx, int my);
