/**
 * @file ext_host.h
 * @brief Extension host de CoffeeCode: cargador de DLLs + respaldo del CoffeeApi.
 *
 * El host es el nucleo del sistema de extensiones.  Implementa:
 *   - El struct @c CoffeeApi (vtable estable) respaldado por el editor/buffer
 *     activo.  Las extensiones solo ven este contrato; nunca tocan structs
 *     internos del IDE.
 *   - Un @c CoffeeHost opaco que guarda las tablas de comandos, suscripciones a
 *     eventos y servicios, mas un REGISTRO POR-EXTENSION de todo lo que cada
 *     extension registro (para revertirlo limpiamente en el unload).
 *   - El cargador de DLLs (LoadLibrary/dlopen), la resolucion del simbolo de
 *     entrada @c coffee_extension_register y la descarga ordenada.
 *   - La carga de un directorio de extensiones con orden topologico por
 *     dependencias declaradas en el manifiesto @c coffee-extension.toml.
 *
 * Diseno clave: el host trabaja sobre un @c Buffer* (no sobre el @c Editor
 * completo).  Asi las operaciones de buffer funcionan sin SDL y el smoke test
 * headless puede ejercitar el ciclo completo load -> command -> manipular
 * buffer -> unload sin arrancar la UI.  Las acciones de UI (estado, panel de
 * salida, ruta del archivo, acciones del IDE) se delegan a callbacks opcionales
 * que el editor SDL rellena al cablear el host; en el modo headless quedan a
 * NULL y el host las trata como no-ops o stubs que loguean.
 */
#ifndef COFFEE_EXT_HOST_H
#define COFFEE_EXT_HOST_H

#include "buffer/buffer.h"
#include "ext/coffee_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hooks opcionales que el editor SDL provee al host para las acciones
 *        que NO son manipulacion pura de buffer.
 *
 * Todos los punteros pueden ser NULL: en ese caso el host trata la operacion
 * como un stub que se limita a loguear.  Esto permite que el smoke test
 * headless funcione con un @c CoffeeHostBackend que solo aporta el @c Buffer*.
 */
typedef struct CoffeeHostBackend {
    Buffer *buffer; /**< buffer activo respaldado por el host (obligatorio
                         para las operaciones de buffer; puede ser NULL si la
                         extension no lo usa). */
    void *ud;       /**< userdata del editor, pasado de vuelta a los hooks. */

    /* -- UI / feedback (opcionales) -- */
    void (*set_status)(void *ud, const char *msg);
    void (*show_message)(void *ud, const char *title, const char *body);
    void (*output_append)(void *ud, const char *text);
    void (*output_clear)(void *ud);

    /* -- Canales del panel inferior (opcionales) -- */
    int (*register_channel)(void *ud, const char *id, const char *title);
    void (*channel_append)(void *ud, const char *id, const char *text);
    void (*channel_clear)(void *ud, const char *id);
    /** Mensaje de log de una extension (alimenta la pestana "Logs"). */
    void (*log_line)(void *ud, int level, const char *msg);

    /* -- Editor / archivo (opcionales) -- */
    const char *(*current_path)(void *ud);
    void (*open_file)(void *ud, const char *path);
    void (*save_file)(void *ud);
    void (*new_tab)(void *ud);

    /* -- Proyecto / navegacion (opcionales) -- */
    /** Ruta absoluta de la carpeta raiz abierta en el explorador, o NULL. */
    const char *(*workspace_root)(void *ud);
    /** Abre @p path (o cambia a su pestana) y mueve el cursor a (@p line,
     *  @p col) 0-based (col en caracteres), haciendo scroll.  Devuelve 1/0. */
    int (*goto_location)(void *ud, const char *path, int line, int col);

    /* -- Repintado y decoraciones (opcionales) -- */
    void (*request_repaint)(void *ud);

    /* -- Popup de hover con pestanas (opcionales, ABI v7) -- */
    /** Abre el popup de hover en el ancla del ultimo TEXT_HOVER con @p n
     *  pestanas tituladas @p names (contenido inicial vacio). */
    void (*show_hover)(void *ud, const char *const *names, int n);
    /** Fija el contenido de la pestana @p index del popup de hover. */
    void (*set_hover_tab)(void *ud, int index, const char *content);
    /** Cierra el popup de hover. */
    void (*hide_hover)(void *ud);
} CoffeeHostBackend;

/**
 * @brief Crea un host respaldado por @p backend.
 *
 * El host COPIA el contenido de @p backend (no guarda el puntero), por lo que
 * el llamante puede pasar una variable de pila.  Devuelve NULL si falla la
 * reserva.
 */
CoffeeHost *ext_host_create(const CoffeeHostBackend *backend);

/** @brief Destruye el host: descarga todas las extensiones y libera recursos. */
void ext_host_destroy(CoffeeHost *host);

/** @brief Devuelve el CoffeeApi del host (la vtable que reciben las DLLs). */
const CoffeeApi *ext_host_api(CoffeeHost *host);

/**
 * @brief Devuelve el ultimo mensaje de error legible del host.
 *
 * Cada camino de fallo de @c ext_host_load / @c ext_host_load_dir /
 * @c ext_host_reload rellena un mensaje con la causa concreta (DLL que no
 * carga con el texto del SO, manifiesto ausente, simbolo de entrada ausente,
 * ABI incompatible, dependencia faltante o ciclo, fallo del registro).  El
 * IDE lo muestra en el panel de salida / barra de estado para que el usuario
 * VEA por que una extension no cargo.
 *
 * La cadena es propiedad del host y valida hasta la siguiente operacion de
 * carga (copiar si se necesita conservar).  Nunca devuelve NULL: si no hubo
 * error, devuelve una cadena vacia.
 */
const char *ext_host_last_error(CoffeeHost *host);

/**
 * @brief Actualiza el buffer activo respaldado por el host.
 *
 * El editor SDL la llama al cambiar de pestana para que las operaciones de
 * buffer del CoffeeApi apunten al buffer correcto.
 */
void ext_host_set_buffer(CoffeeHost *host, Buffer *buffer);

/**
 * @brief Actualiza el userdata del backend del host (el ::Editor al que llegan
 *        los hooks de UI: panel inferior, barra de estado, repintado).
 *
 * En el IDE multi-ventana el host de extensiones es UNICO y compartido por
 * todas las ventanas, pero sus hooks escriben en UN solo Editor (el del
 * @c backend.ud).  Al cambiar el foco de ventana, la capa de aplicacion la
 * llama para que la salida de las extensiones (output_append, channel_append,
 * set_status, log_line, request_repaint) aterrice en la ventana ENFOCADA.
 * Con una sola ventana el userdata es siempre la principal: cero cambio.
 */
void ext_host_set_userdata(CoffeeHost *host, void *ud);

/**
 * @brief Carga UNA extension desde un directorio con su manifiesto.
 *
 * @p dir debe contener @c coffee-extension.toml (con @c id, @c entry, @c abi y
 * opcionalmente @c dependencies) y la DLL referida por @c entry.  El host
 * parsea el manifiesto, carga la DLL, comprueba la ABI, resuelve el simbolo de
 * entrada y lo invoca con (host, api).  Devuelve 0 en exito, !=0 en fallo.
 */
int ext_host_load(CoffeeHost *host, const char *dir);

/**
 * @brief Descarga una extension por id.
 *
 * Invoca @c coffee_extension_unregister (si existe), revierte TODO lo que la
 * extension registro via el registro por-extension (comandos, eventos,
 * servicios) y libera la DLL.  Devuelve 0 en exito, !=0 si no existe.
 */
int ext_host_unload(CoffeeHost *host, const char *id);

/** @brief Recarga (unload + load) una extension por id.  0 = ok. */
int ext_host_reload(CoffeeHost *host, const char *id);

/**
 * @brief Escanea @p extensions_root, lee los manifiestos de cada subdirectorio,
 *        ordena topologicamente por dependencias y carga en orden.
 *
 * Una dependencia se carga ANTES que quien la declara.  Un ciclo de
 * dependencias es un error (se reporta y se aborta la carga del conjunto).
 * Devuelve el numero de extensiones cargadas con exito (>=0), o <0 en error
 * grave (p.ej. ciclo).
 */
int ext_host_load_dir(CoffeeHost *host, const char *extensions_root);

/** @brief Invoca un comando registrado por id.  0 = invocado, !=0 = no existe. */
int ext_host_run_command(CoffeeHost *host, const char *command_id);

/** @brief Notifica a los suscriptores de @p event con @p data. */
void ext_host_emit(CoffeeHost *host, CoffeeEventType event, const void *data);

/** @brief 1 si la extension @p id esta cargada y activa, 0 si no. */
int ext_host_has(CoffeeHost *host, const char *id);

/**
 * @brief Registra una extension NATIVA embebida (sin DLL) en la lista del host.
 *
 * Para lenguajes/funciones compiladas dentro del ejecutable (p.ej. el resaltador
 * de C base).  Crea una entrada visible en el panel de extensiones con la misma
 * metadata que una DLL, pero marcada como builtin: no se puede descargar ni
 * recargar (su codigo vive en el .exe).  Cualquier puntero puede ser NULL salvo
 * @p id.
 *
 * @return 0 ok, -1 args invalidos, -2 ya existe ese id, -3 sin memoria.
 */
int ext_host_register_builtin(CoffeeHost *host, const char *id, const char *name,
                              const char *version, const char *author,
                              const char *description);

/* ===========================================================================
 *  Introspeccion: listar las extensiones cargadas para el panel de
 *  extensiones del IDE (el "marketplace de cargadas").
 * =========================================================================== */

/** @brief Numero de slots de extension del host (incluye slots inactivos). */
size_t ext_host_count(CoffeeHost *host);

/**
 * @brief Devuelve los datos de la extension del slot @p idx.
 *
 * Pensada para el panel de extensiones: rellena los punteros de salida (los que
 * no sean NULL) con el id, nombre legible, directorio y estado de la extension
 * en el slot @p idx.  Las cadenas devueltas son propiedad del host y validas
 * mientras la extension siga cargada (copiar si se necesitan luego).
 *
 * @param host   Host.
 * @param idx    Indice de slot en [0, ext_host_count).
 * @param[out] id     Id del manifiesto (o NULL si el slot esta libre).
 * @param[out] name   Nombre legible del manifiesto (cae al id si no se dio).
 * @param[out] dir    Directorio de la extension (NULL si es nativa embebida).
 * @param[out] active 1 si la extension esta activa, 0 si el slot esta libre.
 * @return 1 si @p idx es un slot valido, 0 si esta fuera de rango.
 */
int ext_host_info(CoffeeHost *host, size_t idx, const char **id,
                  const char **name, const char **dir, int *active);

/**
 * @brief Metadata extra de la extension del slot @p idx (para el panel).
 *
 * Rellena los punteros de salida no-NULL con version, autor, descripcion y si la
 * extension es nativa embebida (builtin).  Cualquiera puede quedar NULL si el
 * manifiesto no lo declaro.  Cadenas propiedad del host.
 *
 * @return 1 si @p idx es valido, 0 si esta fuera de rango.
 */
int ext_host_info_meta(CoffeeHost *host, size_t idx, const char **version,
                       const char **author, const char **description,
                       int *is_builtin);

/* ===========================================================================
 *  Vistas registradas: el render del IDE las dibuja.
 * =========================================================================== */

/** @brief Datos de una vista registrada por una extension (solo lectura). */
typedef struct CoffeeHostView {
    const char *id;            /**< id de la vista */
    const char *title;         /**< titulo legible */
    CoffeeViewKind kind;       /**< sidebar/panel/overlay/statusbar */
    CoffeePaintFn paint;       /**< callback de pintado */
    CoffeeViewInputFn input;   /**< callback de input (puede ser NULL) */
    void *userdata;            /**< userdata de los callbacks */
} CoffeeHostView;

/** @brief Numero de vistas registradas y vivas. */
size_t ext_host_view_count(CoffeeHost *host);

/**
 * @brief Copia los datos de la vista @p idx en @p out.
 * @return 1 si @p idx es valido y la vista esta viva, 0 si no.
 */
int ext_host_view_at(CoffeeHost *host, size_t idx, CoffeeHostView *out);

/* ===========================================================================
 *  Decoraciones del editor (por-BUFFER): el render del IDE las consulta.
 * ---------------------------------------------------------------------------
 *  Las extensiones ponen decoraciones (fondo de linea, marcador de gutter) via
 *  CoffeeApi::set_line_background / set_gutter_marker sobre el BUFFER ACTIVO.
 *  El host las guarda asociadas a ese Buffer*, de modo que cada archivo/pestana
 *  tiene las SUYAS y al cambiar de pestana se muestran las del archivo activo.
 *  El render consulta estos accesores pasando su buffer "en vivo" (e->buf).
 * =========================================================================== */

/**
 * @brief Color de fondo decorado para la linea @p line del buffer @p buffer.
 *
 * @param[out] out_color Color de fondo (4 bytes RGBA) si hay decoracion.
 * @return 1 si la linea tiene fondo decorado (rellena @p out_color), 0 si no.
 */
int ext_host_line_background(CoffeeHost *host, const Buffer *buffer, size_t line,
                             CoffeeColor *out_color);

/**
 * @brief Marcador de gutter de la linea @p line del buffer @p buffer.
 *
 * @param[out] out_glyph Puntero al glifo (cadena UTF-8 propiedad del host,
 *                       valida hasta clear/unload).  Puede ser NULL.
 * @param[out] out_color Color del marcador.  Puede ser NULL.
 * @return 1 si la linea tiene marcador (rellena las salidas no NULL), 0 si no.
 */
int ext_host_gutter_marker(CoffeeHost *host, const Buffer *buffer, size_t line,
                           const char **out_glyph, CoffeeColor *out_color);

/** @brief Un subrayado de rango (squiggle) recuperado del host. */
typedef struct CoffeeUnderline {
    uint32_t start_col;  /**< columna inicial (codepoints) */
    uint32_t end_col;    /**< columna final exclusiva (codepoints) */
    CoffeeColor color;   /**< color del trazo (RGBA) */
} CoffeeUnderline;

/**
 * @brief Recolecta los subrayados de rango de la linea @p line del buffer.
 *
 * Pensada para el render: una linea puede tener VARIOS subrayados (varios
 * diagnosticos).  Rellena @p out con hasta @p max y devuelve cuantos.
 *
 * @return Numero de subrayados escritos en @p out (0 si la linea no tiene).
 */
int ext_host_range_underlines(CoffeeHost *host, const Buffer *buffer, size_t line,
                              CoffeeUnderline *out, int max);

/**
 * @brief Texto fantasma (inline hint) al final de la linea @p line del buffer.
 *
 * Lo pone una extension con @c set_inline_hint para mostrar valores calculados
 * (p.ej. el resultado de @c sizeof<T> en compile-time) en color tenue, sin
 * modificar el texto del documento.
 *
 * @param[out] out_text  Texto del hint (propiedad del host).  Puede ser NULL.
 * @param[out] out_color Color del hint.  Puede ser NULL.
 * @return 1 si la linea tiene un inline hint, 0 si no.
 */
int ext_host_inline_hint(CoffeeHost *host, const Buffer *buffer, size_t line,
                         const char **out_text, CoffeeColor *out_color);

/** @brief Un inline hint recuperado del host (texto fantasma). */
typedef struct CoffeeInlineHint {
    const char *text;  /**< texto del hint (no se libera; vive en el host) */
    CoffeeColor color; /**< color del texto */
    uint32_t col;      /**< columna de inserción (codepoints); UINT32_MAX = al
                            final de la linea (tras el codigo, ante el //) */
} CoffeeInlineHint;

/**
 * @brief Recolecta TODOS los inline hints de la linea @p line del buffer.
 *
 * Incluye tanto el hint "al final" (set_inline_hint, col==UINT32_MAX) como los
 * de columna intermedia (set_inline_hint_at).  Hasta @p max hints.
 *
 * @return Numero de hints escritos en @p out (0 si la linea no tiene).
 */
int ext_host_inline_hints(CoffeeHost *host, const Buffer *buffer, size_t line,
                          CoffeeInlineHint *out, int max);

/**
 * @brief Descarta TODAS las decoraciones asociadas a @p buffer.
 *
 * El editor la llama cuando un Buffer deja de ser valido (al cerrar una
 * pestana, antes de liberar/reusar su struct) para que ninguna decoracion
 * quede colgando de una direccion reciclada.  Sin coste si el buffer no tenia
 * decoraciones.  No-op si @p host o @p buffer son NULL.
 */
void ext_host_drop_buffer(CoffeeHost *host, const Buffer *buffer);

/* ===========================================================================
 *  Resaltado de sintaxis (ABI v4): registro de resaltadores + tramos pushed.
 * ---------------------------------------------------------------------------
 *  El core ya no trae resaltador propio.  Las extensiones registran un
 *  resaltador sincrono por extension de archivo (register_highlighter) y/o
 *  empujan tramos por linea sobre el buffer activo (set_tokens / clear_tokens).
 *  El render del IDE consulta estos accesores para colorear cada linea.
 * =========================================================================== */

/**
 * @brief 1 si hay un resaltador sincrono registrado para la extension de @p path.
 *
 * Compara la extension de @p path (case-insensitive) con las registradas.  El
 * render lo usa para decidir si re-tokenizar una linea con un resaltador o
 * dibujarla en texto plano.  @p path puede ser NULL/sin extension (devuelve 0).
 */
int ext_host_has_highlighter(CoffeeHost *host, const char *path);

/**
 * @brief Resalta UNA linea con el resaltador registrado para la extension de
 *        @p path.
 *
 * Resuelve el resaltador por la extension de @p path y lo invoca con la linea.
 * Escribe hasta @p max_out tramos en @p out (en COLUMNAS DE CARACTER) y devuelve
 * cuantos escribio, o -1 si no hay resaltador para esa extension (el render cae a
 * texto plano).  @p in_block / @p out_block encadenan el estado de comentario de
 * bloque multilinea (out_block puede ser NULL).
 */
int ext_host_highlight_line(CoffeeHost *host, const char *path,
                            const char *line_utf8, int line_len, int in_block,
                            CoffeeSpan *out, int max_out, int *out_block);

/**
 * @brief Tramos pushed (set_tokens) de la linea @p line del buffer @p buffer.
 *
 * Copia hasta @p max_out tramos en @p out y devuelve cuantos hay (>=0), o -1 si
 * esa linea no tiene tramos pushed (el render cae al resaltador sincrono).  Una
 * linea con 0 tramos pushed explicitos (set_tokens count=0) devuelve 0.
 */
int ext_host_line_tokens(CoffeeHost *host, const Buffer *buffer, size_t line,
                         CoffeeSpan *out, int max_out);

/**
 * @brief 1 si el buffer @p buffer tiene ALGUN tramo pushed (set_tokens).
 *
 * Permite al render saber rapido si debe consultar ext_host_line_tokens por
 * linea, sin coste cuando ninguna extension empujo tramos a ese buffer.
 */
int ext_host_has_pushed_tokens(CoffeeHost *host, const Buffer *buffer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* COFFEE_EXT_HOST_H */
