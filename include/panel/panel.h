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

/** Numero maximo de tramos de color (spans) que un canal guarda a la vez. */
#define PANEL_MAX_SPANS 512

/** Id del canal por defecto al que cae output_append / output_clear. */
#define PANEL_DEFAULT_CHANNEL "salida"

/* -- Color del texto (secuencias ANSI SGR) --------------------------------
 *
 * La salida de programas trae codigos ANSI tipo "\x1b[31m" (rojo) o
 * "\x1b[0m" (reset).  El panel los INTERPRETA al anyadir texto: separa los
 * bytes de escape (que NO se guardan ni ocupan columnas) del texto VISIBLE
 * (que sigue siendo @c PanelChannel::text), y registra el color en una lista
 * paralela de tramos (::PanelColorSpan) sobre offsets del texto visible.  Asi
 * la envoltura, la seleccion y la copia operan igual sobre texto plano.
 */

/** Indice de color "por defecto" (usa el color de texto normal del panel). */
#define PANEL_COL_DEFAULT 0xFF
/** Bit de @c PanelColorSpan::flags: el tramo se dibuja en negrita/intenso. */
#define PANEL_SGR_BOLD 0x01
/** Bit de @c PanelColorSpan::flags: @c fg es un color RGB directo, no indice. */
#define PANEL_SGR_FG_RGB 0x02
/** Bit de @c PanelColorSpan::flags: @c bg es un color RGB directo, no indice. */
#define PANEL_SGR_BG_RGB 0x04

/**
 * @brief Un tramo de color [start, end) sobre offsets del texto VISIBLE.
 *
 * @c fg / @c bg son indices de la paleta ANSI de 16 colores (0..15) o
 * ::PANEL_COL_DEFAULT, salvo que el flag RGB correspondiente este activo, en
 * cuyo caso el indice se interpreta como un color empaquetado.  Para soportar
 * 256/truecolor sin inflar el span, el RGB se guarda aparte (ver
 * PanelChannel::span_rgb) y aqui solo va su indice en esa tabla.
 */
typedef struct {
    size_t start;     /**< primer byte del tramo en @c text */
    size_t end;       /**< byte siguiente al fin del tramo (exclusivo) */
    unsigned char fg; /**< indice de color de texto (o PANEL_COL_DEFAULT) */
    unsigned char bg; /**< indice de color de fondo (o PANEL_COL_DEFAULT) */
    unsigned char flags; /**< combinacion de PANEL_SGR_* */
} PanelColorSpan;

/** Estado SGR vigente de un canal (continuo entre llamadas a append). */
typedef struct {
    unsigned char fg;    /**< color de texto actual (indice o PANEL_COL_DEFAULT) */
    unsigned char bg;    /**< color de fondo actual */
    unsigned char flags; /**< PANEL_SGR_* activos (bold, RGB) */
} PanelSgrState;

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
    /* -- color del texto (ANSI SGR) -- */
    PanelColorSpan spans[PANEL_MAX_SPANS]; /**< tramos de color sobre @c text */
    size_t span_count;                     /**< spans en uso (<= PANEL_MAX_SPANS) */
    PanelSgrState sgr;     /**< estado SGR vigente (continuo entre appends) */
    /* tabla de colores RGB para 256/truecolor referenciados por los spans */
    unsigned int span_rgb[PANEL_MAX_SPANS]; /**< 0x00RRGGBB por indice */
    size_t span_rgb_count;                  /**< colores RGB registrados */
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

/* -- Paleta y consulta de color ANSI --------------------------------------
 *
 * Helpers PUROS (sin SDL) que el render usa para colorear cada sub-tramo de
 * una fila visual.  La paleta ANSI de 16 colores vive en panel_core.c en un
 * unico sitio; estos helpers la exponen como bytes RGB para que el render los
 * empaquete en su propio tipo de color.
 */

/**
 * @brief Devuelve el color RGB de un indice de la paleta ANSI de 16 colores.
 *
 * @param idx  Indice 0..15 (0-7 estandar, 8-15 brillante).  Fuera de rango se
 *             recorta.
 * @param[out] r Componente rojo (0..255).  Puede ser NULL.
 * @param[out] g Componente verde.  Puede ser NULL.
 * @param[out] b Componente azul.  Puede ser NULL.
 */
void panel_ansi_rgb(int idx, unsigned char *r, unsigned char *g,
                    unsigned char *b);

/**
 * @brief Resuelve el color de primer plano de un span a RGB (0..255).
 *
 * Maneja los tres casos: indice de paleta (con bold -> variante brillante),
 * color RGB directo (256/truecolor) y PANEL_COL_DEFAULT (devuelve 0 en
 * @p is_default para que el llamante use su color de texto por defecto).
 *
 * @param c    Canal (aporta la tabla span_rgb para colores 256/truecolor).
 * @param sp   Span a resolver.
 * @param[out] r,g,b Componentes RGB resultantes (validos solo si is_default==0).
 * @param[out] is_default 1 si el span usa el color por defecto del panel.
 */
void panel_span_fg(const PanelChannel *c, const PanelColorSpan *sp,
                   unsigned char *r, unsigned char *g, unsigned char *b,
                   int *is_default);

/**
 * @brief Como ::panel_span_fg pero para el color de fondo del span.
 *
 * @param[out] is_default 1 si el span no fija fondo (usa el del panel).
 */
void panel_span_bg(const PanelChannel *c, const PanelColorSpan *sp,
                   unsigned char *r, unsigned char *g, unsigned char *b,
                   int *is_default);

/**
 * @brief Devuelve el span de color que cubre el byte-offset @p off, o NULL.
 *
 * Busca el primer span [start,end) que contiene @p off.  Los spans estan en
 * orden de aparicion (no solapados), asi que una busqueda lineal desde una
 * pista (@p hint) recorre el texto en O(n) total al dibujar una fila.
 *
 * @param c    Canal.
 * @param off  Byte-offset en @c text.
 * @param hint Indice de span por el que empezar a buscar (0 la primera vez;
 *             el resultado previo acelera llamadas consecutivas).  Puede ser
 *             NULL.
 * @return Span que cubre @p off, o NULL si ningun span lo cubre (color por
 *         defecto).
 */
const PanelColorSpan *panel_span_at(const PanelChannel *c, size_t off,
                                    size_t *hint);
