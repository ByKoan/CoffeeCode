#pragma once
/**
 * @file panel.h
 * @brief Almacen de canales de texto del panel inferior (pestanas Salida/Logs/
 *        Terminal y las que registren las extensiones).
 *
 * El panel inferior del IDE muestra varios CANALES en pestanas: cada canal es
 * un buffer de texto (scrollback) con un id estable y un titulo legible.  Las
 * extensiones escriben en ellos via el CoffeeApi (output_append cae en el canal
 * por defecto "salida"; channel_append escribe en cualquier canal por id).
 *
 * Este modulo es el ALMACEN puro de esos canales: solo manipula texto e indices,
 * NO depende de SDL ni de la struct Editor, asi que se compila y prueba en
 * headless (ver test/panel/test_panel.c).  El render del panel y su input viven
 * aparte, sobre el Editor, y consultan este almacen.
 *
 * Cada canal tiene un scrollback con tope (ring por bytes): cuando el texto
 * acumulado supera la capacidad, se descarta la cabecera mas antigua, de modo
 * que el canal nunca crece sin limite.
 */

#include <stddef.h>

/** Numero maximo de canales que el panel puede contener a la vez. */
#define PANEL_MAX_CHANNELS 16
/** Longitud maxima (bytes) del id de un canal, incluyendo el NUL. */
#define PANEL_ID_MAX 32
/** Longitud maxima (bytes) del titulo de un canal, incluyendo el NUL. */
#define PANEL_TITLE_MAX 48
/** Capacidad (bytes) del scrollback de cada canal (texto + NUL). */
#define PANEL_CHAN_CAP 8192

/** Id del canal por defecto al que cae output_append / output_clear. */
#define PANEL_DEFAULT_CHANNEL "salida"

/**
 * @brief Un canal del panel: id estable + titulo + scrollback acotado.
 */
typedef struct {
    char id[PANEL_ID_MAX];       /**< id estable para localizar el canal */
    char title[PANEL_TITLE_MAX]; /**< titulo legible mostrado en la pestana */
    char text[PANEL_CHAN_CAP];   /**< scrollback (siempre null-terminado) */
    size_t len;                  /**< bytes usados de @c text (sin el NUL) */
    int scroll;                  /**< primera linea visible del canal (>=0) */
    int builtin;                 /**< 1 si es un canal integrado del IDE */
} PanelChannel;

/**
 * @brief Almacen de los canales del panel inferior.
 *
 * Se incrusta por valor en el Editor.  Inicializar con ::panel_store_init antes
 * de usar.
 */
typedef struct {
    PanelChannel chans[PANEL_MAX_CHANNELS]; /**< canales registrados */
    size_t count;                           /**< numero de canales en uso */
} PanelStore;

/**
 * @brief Inicializa el almacen y registra los canales integrados del IDE
 *        (Salida, Logs, Terminal).
 *
 * Tras esta llamada hay al menos un canal ("salida") y el primero queda como
 * canal por defecto.  Idempotente: vuelve a dejar el almacen en su estado
 * inicial si se llama de nuevo.
 *
 * @param s Almacen a inicializar.
 */
void panel_store_init(PanelStore *s);

/**
 * @brief Devuelve el indice del canal con id @p id, o -1 si no existe.
 *
 * @param s  Almacen.
 * @param id Id del canal a buscar.
 * @return Indice en [0, count) o -1.
 */
int panel_find(const PanelStore *s, const char *id);

/**
 * @brief Registra un canal (pestana) con @p id y @p title.
 *
 * Idempotente: si ya existe un canal con ese id, solo actualiza su titulo (si
 * @p title no es NULL) y devuelve su indice.  Devuelve -1 si no hay sitio o los
 * argumentos no son validos.
 *
 * @param s     Almacen.
 * @param id    Id del canal (no NULL, no vacio).
 * @param title Titulo legible (puede ser NULL: se usa el id).
 * @return Indice del canal o -1.
 */
int panel_register(PanelStore *s, const char *id, const char *title);

/**
 * @brief Anyade @p text al final del canal @p id (lo crea si no existe).
 *
 * Si el scrollback supera su capacidad, descarta la cabecera mas antigua.  El
 * canal queda siempre null-terminado.
 *
 * @param s    Almacen.
 * @param id   Id del canal destino.
 * @param text Texto a anyadir (UTF-8, null-terminado).
 */
void panel_append(PanelStore *s, const char *id, const char *text);

/**
 * @brief Vacia el scrollback del canal @p id (no lo elimina).
 *
 * @param s  Almacen.
 * @param id Id del canal a vaciar.
 */
void panel_clear(PanelStore *s, const char *id);

/**
 * @brief Devuelve el canal por indice, o NULL si @p idx esta fuera de rango.
 *
 * @param s   Almacen.
 * @param idx Indice en [0, count).
 * @return Puntero al canal (valido mientras el almacen viva) o NULL.
 */
const PanelChannel *panel_at(const PanelStore *s, size_t idx);
