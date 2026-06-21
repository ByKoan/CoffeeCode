#pragma once
/**
 * @file app.h
 * @brief Capa de aplicacion multi-ventana: orquesta varias instancias de
 *        ::Editor, una por ventana del SO, compartiendo los recursos comunes.
 *
 * Cada ventana del IDE es un Editor COMPLETO (su propio arbol de dock, splits,
 * flotantes, explorador, panel inferior, navbar y pestanas) que reutiliza TAL
 * CUAL el render (render_frame) y el input (input_handle_event) de la ventana
 * principal.  La App es la DUENA logica del conjunto: mantiene el array de
 * ventanas, cual tiene el foco del teclado, y enruta cada evento de SDL a la
 * instancia de Editor cuya ventana coincide por @c SDL_WindowID.
 *
 * La ventana principal (windows[0]) es la creada con @c editor_init y es la
 * unica que POSEE los recursos compartidos (fuente, host de extensiones,
 * subsistemas SDL/TTF).  Las ventanas secundarias (creadas con
 * @c editor_init_secondary) los REFERENCIAN por puntero y NO los liberan.
 *
 * Invariante de cero regresion: con @c window_count == 1 el bucle, el enrutado y
 * el render son EQUIVALENTES a ejecutar @c editor_run sobre la ventana
 * principal; toda la logica multi-ventana esta gateada por @c window_count > 1.
 */
#include "editor/editor.h"

/** Maximo de ventanas (IDE completos) simultaneas: la principal + secundarias. */
#define APP_MAX_WINDOWS 6

/**
 * @brief Estado de la aplicacion multi-ventana.
 *
 * @c windows[0] es SIEMPRE la ventana principal (duena de los recursos
 * compartidos).  @c windows[1..window_count-1] son secundarias.  @c focused es
 * el indice de la ventana con el foco del teclado (a la que el host de
 * extensiones apunta su buffer activo).  Los Editor se alojan en el heap (son
 * grandes) y la App es duena de esos punteros mientras la ventana viva.
 */
typedef struct App {
    Editor *windows[APP_MAX_WINDOWS]; /**< instancias de Editor (una por ventana) */
    int window_count;                 /**< numero de ventanas vivas (>= 1)        */
    int focused;                      /**< indice de la ventana con foco teclado   */
} App;

/* -- Ciclo de vida -------------------------------------------------------- */

/**
 * @brief Inicializa la App con la ventana PRINCIPAL ya inicializada @p primary.
 *
 * @p primary debe haber pasado por @c editor_init (es duena de los recursos
 * compartidos).  La App toma su puntero como @c windows[0] y queda con una sola
 * ventana enfocada.  No copia el Editor: la App usa el puntero tal cual.
 *
 * @param a       App a inicializar.
 * @param primary Editor principal ya inicializado (no NULL).
 */
void app_init(App *a, Editor *primary);

/**
 * @brief Bucle principal multi-ventana: espera eventos, los enruta por ventana,
 *        autoguarda, sondea LSP/parpadeo y redibuja cada ventana que lo pida.
 *
 * Con una sola ventana es equivalente a @c editor_run sobre la principal.  Con
 * varias, cada evento se entrega a la ventana cuyo @c SDL_WindowID coincide; el
 * CLOSE de una secundaria la fusiona de vuelta a la principal; el QUIT (o el
 * cierre de la principal) termina el bucle.
 *
 * @param a App con al menos la ventana principal.
 */
void app_run(App *a);

/**
 * @brief Libera todas las ventanas secundarias y deja SOLO la principal.
 *
 * No libera la ventana principal (de eso se encarga el llamante con
 * @c editor_free + free): la App solo posee las secundarias que creo.  Tras esto
 * @c window_count == 1.
 *
 * @param a App a vaciar de secundarias.
 */
void app_free(App *a);

/* -- Operaciones multi-ventana (usadas desde input) ----------------------- */

/**
 * @brief Desprende el panel flotante @p fi de la ventana @p e a una VENTANA
 *        NUEVA completa (otro IDE) y le mueve sus pestanas.
 *
 * Crea un Editor secundario (con @c editor_init_secondary) del tamano del
 * flotante, mueve TODAS las pestanas del grupo del flotante a la ventana nueva
 * (a su hoja de dock), elimina el FloatPanel de @p e y enfoca la ventana nueva.
 * No hace nada si no hay sitio para mas ventanas, si @p fi es invalido o si SDL
 * falla creando la ventana (en ese caso el flotante se conserva).
 *
 * @param a  App (debe contener a @p e como una de sus ventanas).
 * @param e  Editor origen del flotante.
 * @param fi Indice del flotante a desprender.
 */
void app_detach_float_to_window(App *a, Editor *e, int fi);

/**
 * @brief Devuelve la App global activa (la que esta corriendo @c app_run), o
 *        NULL si no hay ninguna.
 *
 * Permite que el manejador de input (que recibe solo el @c Editor) llegue a la
 * App para crear/fusionar ventanas sin cambiar la firma de toda la cadena de
 * input.  Lo fija @c app_run al entrar y lo limpia al salir.
 */
App *app_current(void);
