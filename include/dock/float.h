#pragma once
/**
 * @file float.h
 * @brief Paneles flotantes del editor: grupos de pestanas libres dibujados como
 *        overlay ENCIMA del arbol de dock, con barra de titulo, contenido,
 *        redimension, cierre, foco y vuelta a acoplar.
 *
 * Un panel flotante (::FloatPanel) es un grupo de pestanas (un group_id) que NO
 * vive en el arbol de dock sino "suelto" sobre la ventana, en un rectangulo
 * arbitrario.  Sus pestanas siguen el mismo modelo que las hojas del dock: una
 * pestana pertenece al flotante si @c tab.group == @c float.group_id.  Cada
 * flotante muestra su tira de pestanas y el contenido de su pestana activa
 * (reusando @c group_active_tab[group_id]).
 *
 * Este modulo expone SOLO geometria pura: clasificar un punto en una region del
 * flotante (titulo, pestanas, contenido, esquina de redimension, botones) y
 * recortar (clamp) el movimiento/redimension dentro de la ventana.  No depende
 * de SDL ni de la struct Editor, asi que se prueba en headless (ver
 * test/dock/test_float.c).  El render y el input lo alimentan con la geometria
 * y enrutan por la region que devuelve.
 */
#include "render/ui_hit.h" /* Rect + rect_has (independientes de SDL/Editor) */

/* -- Limites ---------------------------------------------------------------- */
/** Maximo de paneles flotantes simultaneos. */
#define FLOAT_MAX_PANELS 8

/* Alto (px) de la barra de titulo de un flotante (la franja que lo mueve). */
#define FLOAT_TITLEBAR_H 24
/* Lado (px) del cuadrado pulsable del boton de cerrar / acoplar de la barra. */
#define FLOAT_BTN_SZ 18
/* Lado (px) de la esquina inferior-derecha que redimensiona el flotante. */
#define FLOAT_RESIZE_SZ 14
/* Tamano por defecto al desprender una pestana a un flotante nuevo (px). */
#define FLOAT_DEFAULT_W 480
#define FLOAT_DEFAULT_H 320
/* Tamano minimo de un flotante al redimensionar (px). */
#define FLOAT_MIN_W 220
#define FLOAT_MIN_H 120

/**
 * @brief Un panel flotante: su rectangulo en pantalla y el grupo de pestanas que
 *        contiene.
 *
 * @c rect es el marco COMPLETO (incluye barra de titulo y tira de pestanas).
 * @c group_id identifica el grupo de pestanas (las que tienen tab.group ==
 * group_id).  El z-order lo lleva el contenedor (el ultimo del array es el que
 * esta al frente).
 */
typedef struct {
    Rect rect;    /**< marco completo del flotante (px)        */
    int group_id; /**< id del grupo de pestanas del flotante    */
} FloatPanel;

/**
 * @brief Region de un flotante bajo un punto, para enrutar el clic/arrastre.
 */
typedef enum {
    FLOAT_HIT_NONE = 0, /**< fuera del flotante: ninguna region        */
    FLOAT_HIT_TITLEBAR, /**< barra de titulo: mover + traer al frente   */
    FLOAT_HIT_CLOSE,    /**< boton "x": cerrar el flotante              */
    FLOAT_HIT_DOCK,     /**< boton acoplar: devolver al arbol de dock   */
    FLOAT_HIT_TABBAR,   /**< tira de pestanas: cambiar/cerrar/+ pestana */
    FLOAT_HIT_RESIZE,   /**< esquina inferior-derecha: redimensionar    */
    FLOAT_HIT_CONTENT   /**< cuerpo: enfocar + editar                   */
} FloatHit;

/* ===========================================================================
 *  Geometria pura (sin SDL/Editor): se prueba en headless
 * =========================================================================== */

/**
 * @brief Rectangulo de la barra de titulo (franja superior del marco).
 * @param p Panel flotante. @return Rect de la barra de titulo (px).
 */
Rect float_titlebar_rect(const FloatPanel *p);

/**
 * @brief Rectangulo del boton de cerrar ("x"), pegado al borde derecho de la
 *        barra de titulo.
 * @param p Panel flotante. @return Rect del boton de cerrar (px).
 */
Rect float_close_rect(const FloatPanel *p);

/**
 * @brief Rectangulo del boton de acoplar, a la izquierda del de cerrar.
 * @param p Panel flotante. @return Rect del boton de acoplar (px).
 */
Rect float_dock_rect(const FloatPanel *p);

/**
 * @brief Rectangulo de la tira de pestanas (bajo la barra de titulo).
 * @param p Panel flotante. @return Rect de la tira de pestanas (px).
 */
Rect float_tabbar_rect(const FloatPanel *p);

/**
 * @brief Rectangulo del area de CONTENIDO (bajo titulo + tira de pestanas).
 * @param p Panel flotante. @return Rect del contenido (px).
 */
Rect float_content_rect(const FloatPanel *p);

/**
 * @brief Rectangulo de la esquina inferior-derecha de redimension.
 * @param p Panel flotante. @return Rect de la esquina de resize (px).
 */
Rect float_resize_rect(const FloatPanel *p);

/**
 * @brief Clasifica el punto (@p mx,@p my) en una ::FloatHit dentro del flotante.
 *
 * Orden de prioridad de las regiones que se solapan: la esquina de resize gana a
 * la tira de pestanas y al contenido; los botones de la barra de titulo ganan a
 * la propia barra.  Un punto fuera del marco devuelve ::FLOAT_HIT_NONE.
 *
 * Funcion PURA: solo geometria.  Se prueba en headless.
 *
 * @param p  Panel flotante.
 * @param mx X del cursor (px).
 * @param my Y del cursor (px).
 * @return La region bajo el cursor.
 */
FloatHit float_hit_test(const FloatPanel *p, int mx, int my);

/**
 * @brief Mueve el flotante a la esquina (@p new_x,@p new_y) recortando el marco
 *        para que NUNCA salga de @p bounds (mantiene el tamano).
 *
 * Si @p bounds es mas pequeno que el flotante, lo alinea al borde superior-
 * izquierdo de @p bounds (prioriza que el titulo quede accesible).
 *
 * @param rect   Rect actual del flotante (de el toma el tamano).
 * @param new_x  X deseada de la esquina superior-izquierda (px).
 * @param new_y  Y deseada (px).
 * @param bounds Limites validos (area de la ventana usable).
 * @return El nuevo rect recortado dentro de @p bounds.
 */
Rect float_clamp_move(Rect rect, int new_x, int new_y, Rect bounds);

/**
 * @brief Redimensiona el flotante a (@p new_w,@p new_h) acotando al minimo y a
 *        que el borde inferior-derecho no salga de @p bounds (mantiene origen).
 *
 * @param rect   Rect actual (de el toma el origen x,y).
 * @param new_w  Ancho deseado (px).
 * @param new_h  Alto deseado (px).
 * @param bounds Limites validos (area de la ventana usable).
 * @return El nuevo rect con el tamano recortado.
 */
Rect float_clamp_resize(Rect rect, int new_w, int new_h, Rect bounds);
