/*
 * CoffeeCode - API de extensiones (ABI C estable).
 *
 * Una extension es una DLL (.dll / .so) que el IDE carga en runtime.  Exporta
 * el simbolo COFFEE_EXTENSION_ENTRY y, al cargarse, recibe el CoffeeApi del IDE
 * para registrar comandos, suscribir eventos, dibujar vistas y manipular el
 * editor.  El core del IDE no depende de ninguna extension; solo provee el
 * cargador y esta API.
 */
#ifndef COFFEE_EXT_H
#define COFFEE_EXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Version del ABI de extensiones.  El IDE rechaza extensiones con un
 *  abi_version mayor al que entiende.  Se sube al romper compatibilidad.
 *
 *  v2: anyadidos register_output_channel / channel_append / channel_clear al
 *      final del CoffeeApi (canales del panel inferior).  Como solo se anyaden
 *      punteros AL FINAL del struct, las extensiones v1 siguen cargando: el IDE
 *      acepta cualquier extension con abi <= COFFEE_ABI_VERSION.
 *
 *  v3: anyadido el facility de subprocesos asincronos (proc_spawn / proc_write /
 *      proc_on_data / proc_on_exit / proc_kill) y el tick por frame
 *      (register_tick), todo AL FINAL del struct.  El core gestiona los hilos:
 *      la extension solo recibe los datos/salida del hijo en el hilo principal.
 *      Las extensiones v1/v2 siguen cargando (solo no ven estas funciones). */
#define COFFEE_ABI_VERSION 3u

/** Handle opaco del IDE.  Las extensiones lo reciben y lo pasan de vuelta a
 *  cada funcion del CoffeeApi.  Su layout es privado al IDE (ABI estable). */
typedef struct CoffeeHost CoffeeHost;

/** Eventos a los que una extension puede suscribirse (subscribe_event). */
typedef enum {
    COFFEE_EVENT_FILE_OPEN = 1, /**< tras abrir archivo; data = const char* path */
    COFFEE_EVENT_FILE_SAVE,     /**< tras guardar;      data = const char* path */
    COFFEE_EVENT_BUFFER_CHANGED,/**< el buffer activo cambio; data = NULL */
    COFFEE_EVENT_CURSOR_MOVED,  /**< cursor movido;     data = NULL */
    COFFEE_EVENT_TAB_SWITCH,    /**< cambio de pestana; data = NULL */
    COFFEE_EVENT_SHUTDOWN       /**< el IDE se cierra; libera recursos aqui */
} CoffeeEventType;

/** Severidad para CoffeeApi::log. */
typedef enum {
    COFFEE_LOG_DEBUG = 0,
    COFFEE_LOG_INFO,
    COFFEE_LOG_WARN,
    COFFEE_LOG_ERROR
} CoffeeLogLevel;

/* ====================  DIBUJO  ==================== */
/* El IDE usa SDL3 internamente, pero NO lo expone: las extensiones dibujan con
 * estas primitivas (ABI estable, independiente del backend grafico).  Una
 * extension no pinta "donde quiere": registra una VISTA (panel/overlay/
 * decoracion) y el IDE la llama con un CoffeePainter recortado a su area. */

typedef struct { int x, y, w, h; } CoffeeRect;          /**< rectangulo en px */
typedef struct { uint8_t r, g, b, a; } CoffeeColor;     /**< color RGBA */

/** Contexto de dibujo opaco; valido SOLO dentro de un CoffeePaintFn. */
typedef struct CoffeePainter CoffeePainter;

/** Primitivas de dibujo.  Se reciben en el paint callback (no se guardan). */
typedef struct CoffeePaint {
    void (*fill_rect)(CoffeePainter *p, CoffeeRect r, CoffeeColor c);
    void (*draw_rect)(CoffeePainter *p, CoffeeRect r, CoffeeColor c); /**< borde */
    void (*draw_line)(CoffeePainter *p, int x0, int y0, int x1, int y1,
                      CoffeeColor c);
    void (*draw_text)(CoffeePainter *p, int x, int y, const char *utf8,
                      CoffeeColor c);
    int (*text_width)(CoffeePainter *p, const char *utf8); /**< ancho en px */
    int (*line_height)(CoffeePainter *p);                  /**< alto de linea */
    void (*set_clip)(CoffeePainter *p, CoffeeRect r); /**< recorte adicional */
    /** Color del tema por nombre ("bg","fg","accent","gutter",...). */
    CoffeeColor (*theme_color)(CoffeePainter *p, const char *role);
} CoffeePaint;

/** Donde vive una vista de extension. */
typedef enum {
    COFFEE_VIEW_SIDEBAR = 1, /**< panel lateral (explorador, outline,...) */
    COFFEE_VIEW_PANEL,       /**< panel inferior (salida, terminal, problems) */
    COFFEE_VIEW_OVERLAY,     /**< overlay sobre el editor (minimapa, lens) */
    COFFEE_VIEW_STATUSBAR    /**< segmento en la barra de estado */
} CoffeeViewKind;

/** Callback de pintado de una vista.  @p area = rect asignado por el IDE. */
typedef void (*CoffeePaintFn)(CoffeeHost *host, CoffeePainter *painter,
                              const CoffeePaint *paint, CoffeeRect area,
                              void *userdata);
/** Callback de input dentro de una vista (clic/tecla).  Devuelve 1 si lo
 *  consumio.  @p data depende del tipo de evento (puntero/tecla). */
typedef int (*CoffeeViewInputFn)(CoffeeHost *host, CoffeeEventType ev,
                                 const void *data, void *userdata);

/** Callback de un comando registrado (command palette / atajo / menu). */
typedef void (*CoffeeCommandFn)(CoffeeHost *host, void *userdata);

/** Callback de un evento suscrito.  @p data depende del CoffeeEventType. */
typedef void (*CoffeeEventFn)(CoffeeHost *host, CoffeeEventType ev,
                              const void *data, void *userdata);

/* ====================  SUBPROCESOS ASINCRONOS (ABI v3)  ==================== */
/* El core lanza procesos hijos (p.ej. un servidor LSP) con stdin/stdout por
 * pipes y los lee en hilos propios.  La extension NO maneja hilos: recibe los
 * datos del hijo y su salida SIEMPRE en el hilo principal, via callbacks. */

/** Handle opaco de un proceso hijo lanzado por el core. */
typedef struct CoffeeProcImpl *CoffeeProc;

/** Callback (hilo principal) con un chunk de stdout del hijo. */
typedef void (*CoffeeProcDataFn)(void *userdata, const char *bytes, size_t len);

/** Callback (hilo principal) cuando el hijo termina, con su codigo de salida. */
typedef void (*CoffeeProcExitFn)(void *userdata, int exit_code);

/** Callback periodico (hilo principal), invocado una vez por frame. */
typedef void (*CoffeeTickFn)(void *userdata);

/**
 * @brief API que el IDE expone a las extensiones.
 *
 * Es un struct de punteros a funcion (vtable), con @c abi_version al frente
 * para versionado.  Toda interaccion extension -> IDE pasa por aqui; las
 * extensiones NUNCA tocan structs internos del IDE (Editor/Buffer), solo este
 * contrato.  Asi el IDE puede evolucionar sin romper las extensiones.
 *
 * Convencion de strings: UTF-8, terminadas en NUL.  Los @c const char* que el
 * IDE devuelve son validos hasta la siguiente llamada al CoffeeApi (la
 * extension copia si los necesita despues).
 */
typedef struct CoffeeApi {
    uint32_t abi_version; /**< = COFFEE_ABI_VERSION */

    /* ---- Registro de capacidades ---- */
    /** Registra un comando invocable (id unico tipo "editor.format"). 0 = ok. */
    int (*register_command)(CoffeeHost *h, const char *id, const char *title,
                            CoffeeCommandFn fn, void *userdata);
    /** Suscribe un callback a un tipo de evento. 0 = ok. */
    int (*subscribe_event)(CoffeeHost *h, CoffeeEventType ev, CoffeeEventFn fn,
                           void *userdata);
    /** Anyade una entrada de menu que dispara un comando por id. */
    int (*add_menu_item)(CoffeeHost *h, const char *menu_path,
                         const char *command_id);
    /** Asocia un atajo (p.ej. "Ctrl+Shift+R") a un comando por id. */
    int (*bind_key)(CoffeeHost *h, const char *keychord, const char *command_id);

    /* ---- Editor / buffer ACTIVO ---- */
    size_t (*buffer_length)(CoffeeHost *h); /**< nº de caracteres del buffer */
    /** Copia [from,to) a @p out (hasta @p cap bytes). Devuelve bytes escritos. */
    size_t (*buffer_get_text)(CoffeeHost *h, size_t from, size_t to, char *out,
                              size_t cap);
    void (*buffer_insert)(CoffeeHost *h, const char *utf8); /**< en el cursor */
    void (*buffer_replace)(CoffeeHost *h, size_t from, size_t to,
                           const char *utf8);
    size_t (*cursor_pos)(CoffeeHost *h);
    void (*set_cursor)(CoffeeHost *h, size_t pos);
    /** Rango seleccionado en *from/*to. Devuelve 1 si hay seleccion, 0 si no. */
    int (*selection)(CoffeeHost *h, size_t *from, size_t *to);
    /** Ruta del archivo del tab activo (o NULL si sin guardar). */
    const char *(*current_path)(CoffeeHost *h);

    /* ---- Acciones del IDE ---- */
    void (*open_file)(CoffeeHost *h, const char *path);
    void (*save_file)(CoffeeHost *h);
    void (*new_tab)(CoffeeHost *h);

    /* ---- UI / feedback ---- */
    void (*set_status)(CoffeeHost *h, const char *msg); /**< barra de estado */
    void (*show_message)(CoffeeHost *h, const char *title, const char *body);
    void (*log)(CoffeeHost *h, CoffeeLogLevel level, const char *msg);
    /** Panel de salida: anyade/limpia texto en el canal por defecto "salida"
     *  del panel inferior.  Equivalen a channel_append/channel_clear con
     *  id="salida"; se conservan por compatibilidad ABI v1. */
    void (*output_append)(CoffeeHost *h, const char *text);
    void (*output_clear)(CoffeeHost *h);

    /* ---- Dibujo: vistas y decoraciones ---- */
    /** Registra una vista que la extension dibuja (panel/overlay/statusbar).
     *  El IDE llama @p paint cuando hay que redibujarla.  @p input puede ser
     *  NULL si la vista no recibe input.  Devuelve un id de vista (>=0) o <0. */
    int (*register_view)(CoffeeHost *h, const char *id, CoffeeViewKind kind,
                         const char *title, CoffeePaintFn paint,
                         CoffeeViewInputFn input, void *userdata);
    /** Quita una vista registrada. */
    void (*remove_view)(CoffeeHost *h, const char *id);
    /** Pide al IDE redibujar (tras cambiar el estado de una vista). */
    void (*request_repaint)(CoffeeHost *h);
    /** Decoracion: resalta el fondo de una linea del editor (0 = quitar). */
    int (*set_line_background)(CoffeeHost *h, size_t line, CoffeeColor bg);
    /** Decoracion: icono/marcador en el gutter de una linea. */
    int (*set_gutter_marker)(CoffeeHost *h, size_t line, const char *glyph,
                             CoffeeColor color);
    /** Texto inline al final de una linea (estilo "code lens" / hints). */
    int (*set_inline_hint)(CoffeeHost *h, size_t line, const char *text,
                           CoffeeColor color);
    /** Limpia TODAS las decoraciones que puso esta extension. */
    void (*clear_decorations)(CoffeeHost *h);

    /* ---- Inter-extension: servicios y dependencias ---- */
    /** Publica un servicio (struct de funciones/datos) bajo un nombre, para
     *  que OTRAS extensiones lo consuman.  La extension dueña define el
     *  contrato (un struct en un header compartido). */
    int (*register_service)(CoffeeHost *h, const char *name, void *iface);
    /** Obtiene el servicio publicado por otra extension (NULL si no existe).
     *  Asi una extension puede depender de otra (p.ej. un compilador expone un
     *  servicio "lang.compile" que un linter consume). */
    void *(*get_service)(CoffeeHost *h, const char *name);
    /** True si una extension (por id) esta cargada y activa. */
    int (*has_extension)(CoffeeHost *h, const char *id);
    /** Invoca un comando registrado por id (encadenar extensiones). */
    int (*run_command)(CoffeeHost *h, const char *command_id);

    /* ---- Carga / descarga dinamica (runtime) ---- */
    /** Carga una extension desde un directorio (con su manifiesto). 0 = ok. */
    int (*load_extension)(CoffeeHost *h, const char *dir);
    /** Descarga una extension por id: emite su deactivate, quita sus comandos/
     *  vistas/decoraciones/servicios y libera la DLL. */
    int (*unload_extension)(CoffeeHost *h, const char *id);
    /** Recarga (unload + load) -- util para desarrollo de extensiones. */
    int (*reload_extension)(CoffeeHost *h, const char *id);

    /* ---- Config / almacenamiento por-extension ---- */
    const char *(*get_config)(CoffeeHost *h, const char *key);
    void (*set_config)(CoffeeHost *h, const char *key, const char *value);
    /** Directorio de datos privado de la extension (para caches, etc.). */
    const char *(*ext_dir)(CoffeeHost *h);

    /* ---- Funciones nativas con nombre (para lenguajes embebidos) ---- */
    /**
     * Registra una funcion nativa accesible por nombre (lib:name).  La usa una
     * extension que embeba un interprete/compilador para exponer este CoffeeApi
     * a su lenguaje embebido: cada funcion del host se registra aqui y el script
     * la invoca por (lib, name).  Asi una extension escrita en un lenguaje de
     * scripting puede registrar comandos / manipular el editor igual que una DLL.
     */
    int (*register_native_fn)(CoffeeHost *h, const char *lib, const char *name,
                              void *fnptr);

    /* ---- Canales del panel inferior (ABI v2) ----
     * NOTA ABI: estos punteros se anyaden AL FINAL del struct (no se reordena
     * nada de arriba), de modo que las extensiones v1 -compiladas contra el
     * layout anterior- siguen siendo compatibles.  ESCRIBIR en el panel se hace
     * con channel_append; la ENTRADA interactiva (terminal) queda como trabajo
     * FUTURO (la pestana Terminal es hoy un placeholder). */

    /** Registra una pestana (canal) en el panel inferior por @p id, con titulo
     *  @p title.  Idempotente: si el canal ya existe, refresca su titulo.
     *  Devuelve 0 si el canal quedo registrado, !=0 en error. */
    int (*register_output_channel)(CoffeeHost *h, const char *id,
                                   const char *title);
    /** Anyade @p text al canal @p id del panel inferior (lo crea si no existe). */
    void (*channel_append)(CoffeeHost *h, const char *id, const char *text);
    /** Vacia el scrollback del canal @p id. */
    void (*channel_clear)(CoffeeHost *h, const char *id);

    /* ---- Subprocesos asincronos + tick por frame (ABI v3) ----
     * NOTA ABI: punteros anyadidos AL FINAL del struct; las extensiones v1/v2
     * compiladas contra el layout anterior siguen siendo compatibles.  El core
     * crea/gestiona los hilos; los callbacks corren en el HILO PRINCIPAL. */

    /** Lanza @p exe con @p argc argumentos (@p argv, sin contar argv[0], que el
     *  core fija al propio @p exe).  stdin/stdout del hijo quedan redirigidos por
     *  pipes.  Devuelve el handle del proceso, o NULL si fallo. */
    CoffeeProc (*proc_spawn)(CoffeeHost *h, const char *exe,
                             const char *const *argv, int argc);
    /** Escribe @p len bytes a stdin del hijo.  Devuelve bytes escritos o -1. */
    int (*proc_write)(CoffeeHost *h, CoffeeProc p, const void *bytes, size_t len);
    /** Registra el callback de datos de stdout (invocado en el hilo principal). */
    void (*proc_on_data)(CoffeeHost *h, CoffeeProc p, CoffeeProcDataFn cb,
                         void *userdata);
    /** Registra el callback de fin del hijo (invocado en el hilo principal). */
    void (*proc_on_exit)(CoffeeHost *h, CoffeeProc p, CoffeeProcExitFn cb,
                         void *userdata);
    /** Termina el hijo y libera sus recursos (hilo lector, pipes, handle). */
    void (*proc_kill)(CoffeeHost *h, CoffeeProc p);

    /** Registra un callback periodico llamado UNA vez por frame (hilo
     *  principal), para trabajo periodico de la extension.  Soporta varios. */
    void (*register_tick)(CoffeeHost *h, CoffeeTickFn cb, void *userdata);

    /* ---- Proyecto / navegacion (anyadidos AL FINAL, sigue ABI v3) ----
     * NOTA ABI: punteros anyadidos al final del struct; las extensiones
     * compiladas contra el layout anterior siguen siendo compatibles (no ven
     * estas funciones, pero cargan). */

    /** Ruta absoluta de la carpeta raiz del proyecto abierta en el explorador
     *  (la del arbol de archivos / Ctrl+K), o NULL si no hay carpeta abierta
     *  (en ese caso la extension cae al directorio del archivo activo).  El
     *  puntero es estable: valido hasta el siguiente cambio de carpeta. */
    const char *(*workspace_root)(CoffeeHost *h);

    /** Abre @p path (o cambia a su pestana si ya esta abierto) y mueve el cursor
     *  a (@p line, @p col) 0-based, donde @p col cuenta CARACTERES (codepoints,
     *  convencion LSP), haciendo scroll para que quede visible.  Recorta valores
     *  fuera de rango.  Devuelve 1 si el archivo se abrio, 0 si no.  Lo usa, por
     *  ejemplo, go-to-definition de un servidor LSP. */
    int (*goto_location)(CoffeeHost *h, const char *path, int line, int col);
} CoffeeApi;

/**
 * @brief Firma del punto de entrada que TODA extension DLL debe exportar.
 *
 * El IDE carga la DLL (LoadLibrary/dlopen), busca el simbolo
 * COFFEE_EXTENSION_ENTRY y lo invoca con el host + la API.  La extension
 * registra sus comandos/eventos y devuelve 0 (ok) o !=0 (fallo -> se descarga).
 *
 *   COFFEE_EXTENSION_EXPORT int coffee_extension_register(
 *       CoffeeHost *host, const CoffeeApi *api) { ... }
 */
typedef int (*CoffeeExtensionRegisterFn)(CoffeeHost *host, const CoffeeApi *api);

/** Nombre del simbolo de entrada que el IDE busca en cada DLL de extension. */
#define COFFEE_EXTENSION_ENTRY "coffee_extension_register"

/**
 * @brief Punto de salida OPCIONAL de una extension (para descarga limpia).
 *
 * El IDE lo invoca (si existe) antes de unload_extension: la extension libera
 * sus recursos (hilos, memoria, handles).  El host YA quita automaticamente
 * los comandos/vistas/decoraciones/servicios/eventos registrados por esa
 * extension, asi que aqui solo va lo que la extension alocó por su cuenta.
 *
 *   COFFEE_EXTENSION_EXPORT void coffee_extension_unregister(CoffeeHost *h) {}
 */
typedef void (*CoffeeExtensionUnregisterFn)(CoffeeHost *host);

/** Nombre del simbolo de desactivacion (opcional) en la DLL de extension. */
#define COFFEE_EXTENSION_DEACTIVATE "coffee_extension_unregister"

/** Macro de export para el .dll/.so de una extension. */
#if defined(_WIN32)
#define COFFEE_EXTENSION_EXPORT __declspec(dllexport)
#else
#define COFFEE_EXTENSION_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* COFFEE_EXT_H */
