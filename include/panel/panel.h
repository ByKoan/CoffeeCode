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

/* -- Envoltura del texto al ancho (word-wrap) -----------------------------
 *
 * El cuerpo del panel inferior muestra el texto envuelto al ancho del
 * contenedor: cada LINEA LOGICA (separada por '\n') que excede @c cols columnas
 * se parte en varias FILAS VISUALES.  Estas funciones son PURAS (sin SDL): el
 * render y el input comparten EXACTAMENTE el mismo layout, de modo que el clic,
 * el resaltado y el scroll cuadran con lo dibujado, y todo se prueba en headless.
 *
 * UTF-8: el ancho se mide en BYTES (no en celdas de display).  El texto del
 * panel es casi siempre ASCII, donde byte == columna.  Las roturas por ancho
 * nunca parten una secuencia multibyte por la mitad (se respetan los bytes de
 * continuacion 0x80..0xBF), pero un caracter de doble ancho cuenta como sus
 * bytes, no como 2 columnas.  Documentado como limitacion conocida.
 */

/**
 * @brief Una fila visual: un tramo [offset, len) del texto que cabe en el ancho.
 *
 * @c offset y @c len son indices/longitudes en BYTES dentro del @c text del
 * canal.  Una fila NUNCA incluye el '\n' que la termina (si lo hay): el salto de
 * linea logico vive entre el final de una fila y el inicio de la siguiente.
 */
typedef struct {
    size_t offset; /**< byte donde empieza la fila dentro de @c text */
    size_t len;    /**< bytes de la fila (sin contar el '\n' final) */
} PanelRow;

/**
 * @brief Numero de filas visuales en que @p text se envuelve a @p cols columnas.
 *
 * Equivale a iterar ::panel_wrap_next hasta el final y contar.  Una linea
 * logica vacia cuenta como 1 fila; un texto vacio cuenta como 1 fila.
 *
 * @param text Texto del canal (null-terminado).
 * @param cols Ancho en columnas (>=1; valores <1 se tratan como 1).
 * @return Total de filas visuales (>=1).
 */
int panel_wrap_count(const char *text, int cols);

/**
 * @brief Calcula la siguiente fila visual a partir del byte @p start.
 *
 * Rompe preferentemente en el ultimo espacio que cabe dentro de @p cols; si una
 * "palabra" no cabe entera, rompe por caracter (respetando limites UTF-8).  El
 * '\n' real termina la fila sin formar parte de ella.
 *
 * @param text  Texto del canal (null-terminado).
 * @param start Byte desde el que empezar (debe ser 0 o el resultado de una
 *              llamada previa).
 * @param cols  Ancho en columnas (>=1).
 * @param[out] row Fila resultante ([offset,len) en bytes).
 * @return Byte de inicio de la SIGUIENTE fila, o (size_t)-1 si @p start ya esta
 *         al final del texto (no hay mas filas).
 */
size_t panel_wrap_next(const char *text, size_t start, int cols, PanelRow *row);

/**
 * @brief Mapea una posicion visual (fila, columna) a un byte-offset del texto.
 *
 * Inversa de ::panel_offset_to_rowcol.  Recorta la fila al rango valido y la
 * columna al final de su fila.  Util para traducir un clic (ya convertido a
 * fila/col por el input) a un offset de seleccion.
 *
 * @param text Texto del canal.
 * @param cols Ancho en columnas (>=1).
 * @param row  Fila visual (0-based).  Se recorta a [0, total_filas-1].
 * @param col  Columna dentro de la fila (0-based).  Se recorta al fin de fila.
 * @return Byte-offset correspondiente dentro de @c text.
 */
size_t panel_rowcol_to_offset(const char *text, int cols, int row, int col);

/**
 * @brief Mapea un byte-offset del texto a su posicion visual (fila, columna).
 *
 * Inversa de ::panel_rowcol_to_offset.  Si @p offset cae en el limite entre dos
 * filas envueltas por ancho, se asigna al INICIO de la fila siguiente (col 0),
 * coherente con el avance de ::panel_wrap_next.
 *
 * @param text Texto del canal.
 * @param cols Ancho en columnas (>=1).
 * @param offset Byte-offset (se recorta a [0, strlen(text)]).
 * @param[out] row Fila visual (0-based).  Puede ser NULL.
 * @param[out] col Columna dentro de la fila (0-based).  Puede ser NULL.
 */
void panel_offset_to_rowcol(const char *text, int cols, size_t offset, int *row,
                            int *col);
