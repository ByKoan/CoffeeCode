# Sistema de extensiones de CoffeeCode

CoffeeCode es extensible mediante **DLLs cargadas en runtime** (`.dll` en
Windows, `.so` en Linux/macOS).  El nucleo del IDE no depende de ninguna
extension: solo provee un cargador y una **API en C de ABI estable**
(`include/ext/coffee_ext.h`).  Cualquier lenguaje capaz de producir una
biblioteca dinamica con simbolos C (`extern "C"`) puede escribir una extension:
C, C++, Rust, Zig, D, Go (`-buildmode=c-shared`), Nim, o el propio Vesta
compilado a nativo (ver [extension_vesta.md](extension_vesta.md)).

> El resaltado de C/C++ del propio editor ES una extension (embebida en el
> ejecutable, pero usa exactamente la misma API que las DLLs externas).  Eso
> garantiza que la API es suficiente para cosas reales.

---

## 1. Como descubre y carga el IDE las extensiones

Al arrancar, el IDE escanea la carpeta `extensions/` **junto al ejecutable**
(`<dir_del_exe>/extensions/`).  Cada **subcarpeta** es una extension y debe
contener:

```text
extensions/
  mi-extension/
    coffee-extension.toml      <- manifiesto (obligatorio)
    mi_extension.dll           <- la biblioteca (nombre = campo `entry`)
  otra-extension/
    coffee-extension.toml
    otra.so
```

Para cada subcarpeta el IDE:

1. Lee `coffee-extension.toml`.
2. Comprueba que `abi <= COFFEE_ABI_VERSION` (si la extension pide un ABI mas
   nuevo que el del IDE, se rechaza).
3. Carga la DLL (`LoadLibrary` / `dlopen`).
4. Busca el simbolo `coffee_extension_register` y lo invoca.
5. Si la extension declara `dependencies`, las resuelve antes (las que faltan
   abortan la carga con un error claro).

Las extensiones tambien se pueden cargar/descargar/recargar **en caliente**
desde otra extension via `load_extension` / `unload_extension` /
`reload_extension` (util durante el desarrollo).

### El manifiesto `coffee-extension.toml`

```toml
[extension]
id           = "mi-extension"        # identificador unico (obligatorio)
name         = "Mi Extension"         # nombre legible
version      = "0.1.0"
author       = "tu-nombre"
entry        = "mi_extension.dll"     # nombre del fichero de la DLL (obligatorio)
abi          = 7                      # version de ABI contra la que compilaste
description  = "Que hace la extension"
dependencies = []                     # ids de otras extensiones requeridas
provides     = ["mi.svc.algo"]        # servicios que publica (opcional)
```

Solo `id` y `entry` son estrictamente obligatorios.  El sufijo de la DLL se
puede dejar al sistema de build (en el ejemplo `vesta-lsp` se usa
`coffee_vesta_lsp@COFFEE_EXT_DLL_SUFFIX@`, que CMake sustituye por `.dll`/`.so`
segun la plataforma).

---

## 2. Ciclo de vida de una extension

Toda extension exporta un punto de entrada y, opcionalmente, uno de salida:

```c
#include "coffee_ext.h"

/* Llamado una vez al cargar.  Devuelve 0 = ok, !=0 = fallo (se descarga). */
COFFEE_EXTENSION_EXPORT
int coffee_extension_register(CoffeeHost *host, const CoffeeApi *api) {
    /* Aqui registras comandos, eventos, vistas, resaltadores, etc. */
    api->register_command(host, "miext.hola", "Mi Extension: Hola",
                          cmd_hola, /*userdata*/ NULL);
    return 0;
}

/* OPCIONAL.  Llamado antes de descargar: libera lo que alocaste por tu cuenta.
 * El host ya quita automaticamente comandos/vistas/decoraciones/servicios/
 * eventos que registraste, asi que aqui solo va memoria/hilos/handles propios. */
COFFEE_EXTENSION_EXPORT
void coffee_extension_unregister(CoffeeHost *host) { /* ... */ }
```

- `COFFEE_EXTENSION_EXPORT` se expande a `__declspec(dllexport)` (Windows) o
  `__attribute__((visibility("default")))` (POSIX).
- El nombre del simbolo de entrada esta fijado por `COFFEE_EXTENSION_ENTRY`
  (`"coffee_extension_register"`).
- El `CoffeeHost *` es un handle **opaco**: nunca se desreferencia, solo se pasa
  de vuelta a cada funcion del `CoffeeApi`.

---

## 3. Que puede hacer una extension (el `CoffeeApi`)

El `CoffeeApi` es una **vtable** (struct de punteros a funcion) con
`abi_version` al frente.  TODA interaccion extension -> IDE pasa por aqui; las
extensiones NUNCA tocan structs internos del IDE (`Editor`/`Buffer`).  Asi el
IDE puede evolucionar sin romper extensiones: los campos nuevos se anyaden
**siempre al final** del struct, de modo que una extension compilada contra un
ABI viejo sigue cargando (solo no ve las funciones nuevas).

Resumen de capacidades por area:

| Area | Funciones | Para que |
| :--- | :--- | :--- |
| **Comandos** | `register_command`, `run_command`, `add_menu_item`, `bind_key` | Acciones invocables desde paleta, menu o atajo |
| **Eventos** | `subscribe_event` | Reaccionar a abrir/guardar/editar/mover cursor/cambiar pestana/cerrar/hover |
| **Buffer activo** | `buffer_length`, `buffer_get_text`, `buffer_insert`, `buffer_replace`, `cursor_pos`, `set_cursor`, `selection`, `current_path` | Leer y modificar el texto del editor |
| **Acciones IDE** | `open_file`, `save_file`, `new_tab`, `goto_location`, `workspace_root` | Navegacion y ficheros |
| **UI / feedback** | `set_status`, `show_message`, `log`, `output_append`, `register_output_channel`, `channel_append` | Mensajes y paneles de salida |
| **Dibujo** | `register_view` (+ `CoffeePaint`), `remove_view`, `request_repaint` | Paneles laterales, paneles inferiores, overlays, segmentos de barra de estado |
| **Decoraciones** | `set_line_background`, `set_gutter_marker`, `set_inline_hint`, `set_inline_hint_at`, `set_range_underline`, `clear_decorations`, `clear_inline_hints` | Fondos de linea, marcadores de gutter, hints inline, subrayados (diagnosticos) |
| **Resaltado** | `register_highlighter` (sincrono), `set_tokens` / `clear_tokens` (asincrono) | Coloreado de sintaxis y semantic tokens |
| **Subprocesos** | `proc_spawn`, `proc_write`, `proc_on_data`, `proc_on_exit`, `proc_kill`, `register_tick` | Lanzar procesos hijo (p.ej. un servidor LSP) sin gestionar hilos |
| **Hover con pestanas** | `show_hover`, `set_hover_tab`, `hide_hover` | Popup de informacion al posar el raton (doc/IR/bytecode/JIT/AOT...) |
| **Inter-extension** | `register_service`, `get_service`, `has_extension` | Una extension expone un servicio que otra consume |
| **Config / datos** | `get_config`, `set_config`, `ext_dir` | Preferencias y directorio privado de la extension |
| **Lenguajes embebidos** | `register_native_fn` | Exponer el `CoffeeApi` a un interprete/compilador embebido (ver seccion 5) |

El **dibujo** sigue un modelo de capacidades: una extension no pinta "donde
quiere", sino que registra una **vista** (`COFFEE_VIEW_SIDEBAR` / `PANEL` /
`OVERLAY` / `STATUSBAR`) y el IDE la invoca con un `CoffeePainter` recortado a
su area.  Las primitivas (`fill_rect`, `draw_text`, `theme_color`, ...) son
independientes del backend grafico (el IDE usa SDL3 internamente pero NO lo
expone).

---

## 4. Ejemplo minimo (C)

```c
#include "coffee_ext.h"

/* La api se guarda en una global al registrar, para usarla en los callbacks. */
static const CoffeeApi *g_api;

static void cmd_mayus(CoffeeHost *h, void *ud) {
    (void)ud;
    size_t from, to;
    if (!g_api->selection(h, &from, &to))   /* sin seleccion -> nada que hacer */
        return;
    char buf[4096];
    size_t n = g_api->buffer_get_text(h, from, to, buf, sizeof buf);
    for (size_t i = 0; i < n; i++)
        if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32; /* a MAYUSCULAS */
    g_api->buffer_replace(h, from, to, buf);
}

COFFEE_EXTENSION_EXPORT
int coffee_extension_register(CoffeeHost *host, const CoffeeApi *api) {
    g_api = api;
    api->register_command(host, "texto.mayusculas", "Texto: A MAYUSCULAS",
                          cmd_mayus, NULL);
    api->bind_key(host, "Ctrl+Shift+U", "texto.mayusculas");
    api->log(host, COFFEE_LOG_INFO, "extension de texto cargada");
    return 0;
}
```

Build (MinGW, ejemplo):

```bash
gcc -shared -I<coffee>/include/ext -o texto.dll texto.c
# colocar en: <dir_del_exe>/extensions/texto/texto.dll
#           + <dir_del_exe>/extensions/texto/coffee-extension.toml
```

Cualquier lenguaje con FFI a C produce este mismo `.dll`/`.so`: basta exportar
`coffee_extension_register` con la firma `(CoffeeHost*, const CoffeeApi*) -> int`
y respetar el layout de `CoffeeApi` (incluir `coffee_ext.h` o replicar el
struct).

---

## 5. Extensiones en lenguajes embebidos (sin DLL nativa)

`register_native_fn(host, lib, name, fnptr)` permite que una extension que
**embebe un interprete o compilador** exponga el `CoffeeApi` a su lenguaje:
cada funcion del host se registra por `(lib, name)` y el script la invoca por
ese nombre.  Asi una extension escrita en un lenguaje de scripting (o en uno
con su propia VM) puede registrar comandos y manipular el editor igual que una
DLL nativa, sin compilar a `.dll`: la DLL puente registra las funciones del
`CoffeeApi` como funciones nativas con nombre, y el codigo del lenguaje
embebido las invoca por ese nombre.

---

## 6. Estabilidad del ABI (resumen)

- `COFFEE_ABI_VERSION` (hoy **7**) se incrementa al ampliar la API.
- Las funciones nuevas se anyaden **al final** de `CoffeeApi`; nunca se reordena
  ni se cambia la firma de las existentes.
- El IDE acepta cualquier extension con `abi <= COFFEE_ABI_VERSION`.
- Una extension compilada contra un ABI viejo sigue cargando; simplemente no ve
  los campos nuevos (que quedan fuera de su copia del struct).

Historial de versiones de ABI (extracto de `coffee_ext.h`):

- **v2**: canales del panel inferior (`register_output_channel` / `channel_*`).
- **v3**: subprocesos asincronos (`proc_*`) + `register_tick` + proyecto
  (`workspace_root`, `goto_location`).
- **v4**: resaltado controlado por extensiones (`register_highlighter`,
  `set_tokens` / `clear_tokens`).
- **v5**: `set_range_underline` (subrayado ondulado para diagnosticos).
- **v6**: `set_inline_hint_at` / `clear_inline_hints` (hints en columna).
- **v7**: hover con pestanas (`show_hover` / `set_hover_tab` / `hide_hover`) y
  evento `COFFEE_EVENT_TEXT_HOVER`.

---

## 7. Referencia

- Header de la API: [`include/ext/coffee_ext.h`](../include/ext/coffee_ext.h)
  (documentado con Doxygen, fuente de verdad del contrato).
- Implementacion del cargador/host: `src/ext/ext_host.c`.
- Extension de ejemplo real: `extensions/vesta-lsp/` (cliente LSP que lanza un
  servidor con `proc_spawn`, empuja diagnosticos con `set_range_underline`,
  semantic tokens con `set_tokens`, y un hover godbolt con `show_hover`).
