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
    int prev_focused;                 /**< ventana enfocada ANTES de la actual     */
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
 *        autoguarda, parpadeo del cursor y redibuja cada ventana que lo pida.
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

/**
 * @brief Guarda la disposicion de la SESION COMPLETA (ventana principal +
 *        secundarias) en el fichero de disposicion.
 *
 * Recorre todas las ventanas vivas, captura la disposicion de cada una (con la
 * geometria de su ventana del SO) y las escribe como una sesion multi-ventana.
 * Llamada por @c app_run al terminar, ANTES de liberar las secundarias, para no
 * perder su estado.  Tras escribir, marca la principal para que su @c editor_free
 * no vuelva a guardar (evita sobreescribir la sesion con solo la principal).
 *
 * @param a App con las ventanas vivas (no NULL).
 */
void app_layout_save(App *a);

/**
 * @brief Restaura la SESION COMPLETA: la principal (ya restaurada por
 *        @c editor_init) mas las ventanas SECUNDARIAS guardadas.
 *
 * Recrea cada ventana secundaria (con @c editor_init_secondary), la coloca en su
 * posicion+tamano de pantalla y le aplica su disposicion (pestanas, dock,
 * flotantes).  Las secundarias sin pestanas validas (archivos borrados) se
 * omiten.  Robusto: ante una sesion antigua de una sola ventana o un fichero sin
 * secundarias, no crea ninguna y todo queda como antes.
 *
 * @param a App con la principal ya en @c windows[0] (no NULL).
 */
void app_layout_restore(App *a);

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
 * @brief Indice de la ventana de la App cuyo rect de PANTALLA contiene el punto
 *        global (@p gx,@p gy), o -1 si el punto no cae sobre ninguna ventana
 *        (escritorio).
 *
 * Calcula el rect de cada ventana con @c SDL_GetWindowPosition + win_w/h y
 * delega la geometria pura en @c win_at_point (probada en headless).  La ventana
 * ENFOCADA se prueba primero para que, en caso de solape, gane la que esta al
 * frente.
 *
 * @param a  App con las ventanas vivas.
 * @param gx X global del cursor (px de pantalla).
 * @param gy Y global del cursor (px de pantalla).
 * @return Indice de ventana en @c windows[], o -1 si ninguna.
 */
int app_window_at_global(App *a, int gx, int gy);

/**
 * @brief Resuelve el SOLTAR de una pestana arrastrada teniendo en cuenta TODAS
 *        las ventanas: la mueve a otra ventana o crea una nueva (tear-off).
 *
 * Toma la posicion GLOBAL del cursor y pregunta @c app_window_at_global:
 *  - misma ventana origen @p src  -> devuelve 0 (el llamante hace el drop local).
 *  - otra ventana existente       -> mueve la pestana @p tab de @p src a esa
 *                                    ventana (en la hoja/zona bajo el cursor),
 *                                    cierra @p src si era secundaria y quedo
 *                                    vacia, y devuelve 1.
 *  - fuera de toda ventana         -> crea una ventana NUEVA en la posicion del
 *                                    cursor con la pestana movida (tear-off),
 *                                    cierra @p src si procede, y devuelve 1.
 *
 * @param a   App en ejecucion (no NULL).
 * @param src Editor origen de la pestana (una de las ventanas de @p a).
 * @param tab Indice GLOBAL de la pestana arrastrada en @p src.
 * @return 1 si el drop fue cross-window/tear-off (ya gestionado); 0 si es en la
 *         misma ventana origen (el llamante debe aplicar el drop local).
 */
int app_drop_tab_cross_window(App *a, Editor *src, int tab);

/**
 * @brief Devuelve la App global activa (la que esta corriendo @c app_run), o
 *        NULL si no hay ninguna.
 *
 * Permite que el manejador de input (que recibe solo el @c Editor) llegue a la
 * App para crear/fusionar ventanas sin cambiar la firma de toda la cadena de
 * input.  Lo fija @c app_run al entrar y lo limpia al salir.
 */
App *app_current(void);

/* -- Terminal integrada en el panel inferior --------------------------------
 *
 * La terminal se incrusta en la pestaña «Terminal» del panel inferior: el proceso
 * hijo (cmd.exe / bash) escribe en el canal «terminal» del PanelStore y el usuario
 * teclea en una línea de input que se envía al proceso al pulsar Enter.
 *
 * Ciclo de vida:
 *   1. term_start  — lanza el proceso hijo y abre las tuberías.
 *   2. term_pump   — llamar cada frame (desde app_run) para leer stdout/stderr.
 *   3. term_send   — enviar una línea de texto al stdin del proceso.
 *   4. term_stop   — cierra tuberías y espera al proceso.
 *
 * El Editor almacena el estado opaco en term_proc (handle de proceso) y los
 * descriptores de lectura/escritura en term_read_fd / term_write_fd.
 * En Windows se usan HANDLE anonimizados en un puntero void*.
 */

/**
 * @brief Lanza el shell integrado en la ruta @p path y conecta las tuberías.
 *
 * @param e    Editor cuyo panel «terminal» recibirá la salida.
 * @param path Directorio de trabajo inicial (NULL → home del usuario).
 * @return 1 si el proceso se inició, 0 si falló.
 */
int term_start(Editor *e, const char *path);

/**
 * @brief Lee toda la salida pendiente del proceso hijo y la vuelca en el canal.
 *
 * No bloquea; debe llamarse cada frame.  Devuelve el número de bytes leídos
 * (útil para saber si hay que repintar).
 *
 * @param e Editor con una terminal activa.
 * @return Bytes recibidos (>= 0).
 */
int term_pump(Editor *e);

/**
 * @brief Envía la cadena @p text al stdin del proceso hijo.
 *
 * @param e    Editor con una terminal activa.
 * @param text Texto a enviar (no NULL).
 */
void term_send(Editor *e, const char *text);

/**
 * @brief Cierra la terminal integrada y libera sus recursos.
 *
 * @param e Editor con (o sin) terminal activa; no hace nada si ya está cerrada.
 */
void term_stop(Editor *e);
