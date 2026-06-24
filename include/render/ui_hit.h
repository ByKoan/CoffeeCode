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
    UI_PREF_RESET,      /**< Botón "Restablecer valores por defecto". */
    UI_PREF_AUTOSAVE,   /**< Toggle de autoguardado.                  */
    UI_PREF_LINENUM,    /**< Toggle de números de línea.              */
    UI_PREF_HLLINE,     /**< Toggle de resaltar la línea activa.      */
    UI_PREF_SHORTCUTS,  /**< Toggle de la barra de atajos.            */
    UI_PREF_THEME,      /**< Selector cíclico de tema.                */
    UI_PREF_FONT,       /**< Selector cíclico de fuente.              */
    UI_PREF_TABW_DEC,   /**< Stepper ancho de tab: −.                 */
    UI_PREF_TABW_INC,   /**< Stepper ancho de tab: +.                 */
    UI_PREF_FONTSZ_DEC, /**< Stepper tamaño de fuente: −.             */
    UI_PREF_FONTSZ_INC, /**< Stepper tamaño de fuente: +.             */
    UI_PREF_FONT_LIST,  /**< Área de la lista de fuentes (para la rueda). */
    UI_PREF_BG,         /**< Fila "Fondo" de Apariencia: abre la sub-pantalla Fondos. */
    /* Sub-pantalla "Fondos" (dentro de Apariencia) */
    UI_BG_BACK,         /**< Boton "< Volver" de la sub-pantalla Fondos.  */
    UI_BG_MODE,         /**< Selector ciclico de modo (Ninguno/Imagen/Color). */
    UI_BG_PICK,         /**< Boton "Seleccionar imagen..." (modo Imagen).  */
    UI_BG_SCALE,        /**< Selector ciclico de escalado (modo Imagen).   */
    UI_BG_OPACITY_DEC,  /**< Stepper opacidad: menos.                      */
    UI_BG_OPACITY_INC,  /**< Stepper opacidad: mas.                        */
    UI_BG_R_DEC,        /**< Stepper componente rojo del color: menos.     */
    UI_BG_R_INC,        /**< Stepper componente rojo del color: mas.       */
    UI_BG_G_DEC,        /**< Stepper componente verde del color: menos.    */
    UI_BG_G_INC,        /**< Stepper componente verde del color: mas.      */
    UI_BG_B_DEC,        /**< Stepper componente azul del color: menos.     */
    UI_BG_B_INC,        /**< Stepper componente azul del color: mas.        */
    UI_BG_ADD,          /**< Boton "Anyadir imagen..." de la galeria.       */
    UI_BG_GALLERY,      /**< Area de la rejilla de miniaturas (para la rueda). */
    UI_STATUS_ENC,      /**< Codificación en la barra de estado (clic).  */
    UI_ENC_MODE_REOPEN, /**< Botón "Reabrir con" del popup.             */
    UI_ENC_MODE_SAVE,   /**< Botón "Guardar como" del popup.            */
    UI_ENC_LIST,        /**< Área de la lista de codificaciones.        */
    /* Panel de extensiones */
    UI_EXT_TOGGLE,      /**< Botón abrir/cerrar el panel de extensiones. */
    UI_EXT_INSTALL,     /**< Botón "Instalar extension" (desde carpeta). */
    UI_EXT_PANEL,       /**< Marco del panel (consume clics dentro).     */
    /* Panel inferior (Salida/Logs/Terminal) */
    UI_BOTTOM_TOGGLE,   /**< Botón abrir/cerrar el panel inferior.       */
    UI_BOTTOM_PANEL,    /**< Marco del panel inferior (consume clics + foco). */
    UI_BOTTOM_BODY,     /**< Area de texto del canal activo (rueda + sel). */
    /* División del editor (split panes) */
    UI_SPLIT_V,         /**< Botón dividir el editor en vertical (lado a lado). */
    UI_SPLIT_H,         /**< Botón dividir el editor en horizontal (arriba/abajo). */
    /* Opciones de la vista godbolt del hover (toggles genericos ui_toggle). */
    UI_HOVER_OPT_ARROWS, /**< Toggle flechas de salto.            */
    UI_HOVER_OPT_FRAME,  /**< Toggle banda del stack frame.       */
    UI_HOVER_OPT_NOTES,  /**< Toggle anotaciones del desensamblado. */
    UI_HOVER_OPT_IR,     /**< Selector ciclico del modo de correlacion IR. */
    /* Terminal del sistema */
    UI_TERMINAL_OPEN,    /**< Botón "Abrir terminal del sistema".  */
    UI_ID_COUNT
} UiId;

/** Familias de controles indexados (listas). */
typedef enum {
    UI_LIST_TAB = 0,   /**< Pestañas de la barra superior.   */
    UI_LIST_TAB_CLOSE, /**< Botón "x" de cada pestaña.       */
    UI_LIST_TREE_ROW,  /**< Filas del explorador de archivos.*/
    UI_LIST_MENU_ITEM, /**< Items del menú "Archivo".        */
    UI_LIST_PREF_FONT, /**< Filas de la lista de fuentes.    */
    UI_LIST_ENC,       /**< Filas del selector de codificación. */
    UI_LIST_EXT_RELOAD, /**< Botón "recargar" de cada extension. */
    UI_LIST_EXT_UNLOAD, /**< Botón "descargar" de cada extension. */
    UI_LIST_BOTTOM_TAB, /**< Pestañas del panel inferior (por índice de canal). */
    UI_LIST_SPLIT_NEW,  /**< Botón "+" de nueva pestaña de cada grupo (split),
                           indexado por número de grupo (0 o 1).            */
    UI_LIST_BG_THUMB,     /**< Celda (miniatura) de la galeria de fondos, por indice. */
    UI_LIST_BG_THUMB_DEL, /**< Boton "x" de quitar de cada celda, por indice.        */
    UI_LIST_HOVER_TAB,    /**< Pestana del popup de hover, por indice.               */
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

/**
 * @brief Recupera el rectángulo registrado del control de lista @p list con
 *        índice @p idx en el frame actual.
 *
 * Lo usa el render del arrastre de pestañas para situar el "fantasma" del título
 * sobre la pestaña agarrada.  Si no hay tal entrada este frame, devuelve 0 y no
 * toca @p out.
 *
 * @param u    Registro de hit-test.
 * @param list Familia de la lista.
 * @param idx  Índice buscado.
 * @param[out] out Rectángulo encontrado.
 * @return 1 si se encontró, 0 si no.
 */
int ui_get_idx(const UiRegistry *u, UiList list, int idx, Rect *out);
