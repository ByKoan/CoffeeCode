#pragma once
/**
 * @file ui_hit.h
 * @brief Registro de "hit-test" de la UI inmediata: comparte la geometría de
 * los componentes entre el render (que la escribe) y el input (que la lee).
 *
 * En un render inmediato la posición de cada control se calcula al dibujar. En
 * vez de guardarla en campos sueltos del editor y recalcular el click en input,
 * cada componente "registra" su rectángulo bajo un identificador (::UiId) en el
 * frame actual; input pregunta luego por ese mismo id. Así la posición y la
 * detección de click salen siempre de la MISMA fuente.
 *
 * Cabecera deliberadamente independiente de @c Editor (solo tipos básicos) para
 * poder incrustar ::UiRegistry dentro del struct Editor sin ciclos de include.
 */
#include <stddef.h>

/** Rectángulo en píxeles de pantalla. */
typedef struct {
    int x, y, w, h;
} Rect;

/** ¿Está el punto (@p px, @p py) dentro del rectángulo @p r? */
static inline int rect_has(Rect r, int px, int py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

/**
 * @brief Identificadores de los controles "únicos" (uno por frame) de la UI.
 *
 * Los controles que aparecen en lista (pestañas, filas del explorador...) usan
 * en cambio el registro indexado (::ui_put_idx / ::ui_hit_idx) con su índice.
 */
typedef enum {
    UI_ID_NONE = 0,
    UI_TOGGLE_TREE,  /**< Botón abrir/cerrar el panel del explorador. */
    UI_BTN_FILE,     /**< Botón "Archivo" de la barra de navegación.  */
    UI_TAB_NEW,      /**< Botón "+" para nueva pestaña.               */
    UI_FIND_PREV,    /**< Botón coincidencia anterior.                */
    UI_FIND_NEXT,    /**< Botón coincidencia siguiente.               */
    UI_FIND_REPLACE, /**< Botón "Reemplazar".                         */
    UI_FIND_QUERY,   /**< Campo de texto "buscar".                    */
    UI_FIND_REPL,    /**< Campo de texto "reemplazar".                */
    UI_FIND_BAR,  /**< Marco de la barra de búsqueda (consume clics dentro). */
    UI_SCROLLBAR, /**< Pulgar de la barra de scroll vertical.      */
    /* Pantalla de preferencias */
    UI_PREF_BACK,       /**< Botón "← Volver" de preferencias.        */
    UI_PREF_AUTOSAVE,   /**< Toggle de autoguardado.                  */
    UI_PREF_LINENUM,    /**< Toggle de números de línea.              */
    UI_PREF_THEME,      /**< Selector cíclico de tema.                */
    UI_PREF_FONT,       /**< Selector cíclico de fuente.              */
    UI_PREF_TABW_DEC,   /**< Stepper ancho de tab: −.                 */
    UI_PREF_TABW_INC,   /**< Stepper ancho de tab: +.                 */
    UI_PREF_FONTSZ_DEC, /**< Stepper tamaño de fuente: −.             */
    UI_PREF_FONTSZ_INC, /**< Stepper tamaño de fuente: +.             */
    UI_PREF_FONT_LIST,  /**< Área de la lista de fuentes (para la rueda). */
    UI_ID_COUNT
} UiId;

/** Familias de controles indexados (listas). */
typedef enum {
    UI_LIST_TAB = 0,   /**< Pestañas de la barra superior.   */
    UI_LIST_TAB_CLOSE, /**< Botón "x" de cada pestaña.       */
    UI_LIST_TREE_ROW,  /**< Filas del explorador de archivos.*/
    UI_LIST_MENU_ITEM, /**< Items del menú "Archivo".        */
    UI_LIST_PREF_FONT, /**< Filas de la lista de fuentes.    */
    UI_LIST_COUNT
} UiList;

/** Tope de entradas indexadas registradas por frame. */
#define UI_MAX_INDEXED 512

/** Una entrada indexada: familia + índice + rectángulo. */
typedef struct {
    UiList list;
    int idx;
    Rect r;
} UiIndexed;

/**
 * @brief Geometría de los controles registrada durante el último frame.
 *
 * Vive dentro del ::Editor. Se vacía con ::ui_reset al empezar a dibujar el
 * frame y se rellena a medida que se dibujan los controles.
 */
typedef struct {
    Rect single[UI_ID_COUNT]; /**< Rect de cada control único (w<=0 = sin
                                 registrar). */
    UiIndexed indexed[UI_MAX_INDEXED]; /**< Controles en lista. */
    int indexed_count; /**< Nº de entradas indexadas usadas este frame. */
} UiRegistry;

/** Vacía el registro (al empezar a dibujar un frame). */
void ui_reset(UiRegistry *u);

/** Registra el rect @p r del control único @p id. */
void ui_put(UiRegistry *u, UiId id, Rect r);

/** Registra el rect @p r del control de lista @p list con índice @p idx. */
void ui_put_idx(UiRegistry *u, UiList list, int idx, Rect r);

/** ¿El punto (@p mx, @p my) cae sobre el control único @p id? */
int ui_hit(const UiRegistry *u, UiId id, int mx, int my);

/**
 * @brief Índice del control de lista @p list bajo (@p mx, @p my).
 * @return El índice registrado, o -1 si el punto no cae sobre ninguno.
 */
int ui_hit_idx(const UiRegistry *u, UiList list, int mx, int my);
