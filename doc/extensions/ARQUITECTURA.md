# Sistema de extensiones de CoffeeCode (propuesta de diseno)

> Estado: **PROPUESTA** para revisar antes de implementar.  La API esta en
> `include/ext/coffee_ext.h`.

## Principios

1. **Core ligero.**  El IDE no depende de ninguna extension ni de libvesta.
   Solo aporta el *extension host* (cargador de DLLs) + la API
   (`CoffeeApi` en `coffee_ext.h`).  Sin extensiones, CoffeeCode sigue siendo
   el editor SDL3 actual.
2. **Extensiones = DLLs nativas.**  Una extension es un `.dll` / `.so` que
   exporta `coffee_extension_register(CoffeeHost*, const CoffeeApi*)`.  Modelo
   VS Code, pero con binarios nativos (C/C++/Vex/Rust/...).
3. **Marketplace.**  Indice remoto de extensiones; instalar = descargar la DLL
   + su manifiesto al directorio de extensiones del usuario.
4. **libvesta es una extension, no parte del core.**  La extension *vesta*
   enlaza libvesta y aporta: ejecutar/compilar Vex desde el IDE, y el puente
   para escribir **extensiones en Vex**.

## Capas

```
+-----------------------------------------------------------+
|  CoffeeCode core (C11 + SDL3)  -- LIGERO                  |
|   editor / buffer / lexer / render / input               |
|   + Extension Host:                                       |
|       - escanea  <user>/extensions/*/manifest            |
|       - carga la DLL (LoadLibrary/dlopen)                 |
|       - busca "coffee_extension_register" y lo invoca     |
|         con un CoffeeHost* + el CoffeeApi                 |
|       - despacha comandos/eventos a las extensiones       |
+-----------------------------------------------------------+
            |  CoffeeApi (vtable estable)
            v
+--------------------+   +--------------------+   +--------------------+
| ext. nativa C/C++  |   | ext. "vesta" (DLL) |   | otras DLLs...      |
| (git, themes, ...) |   |  enlaza libvesta   |   |                    |
+--------------------+   +----------+---------+   +--------------------+
                                    |  register_native_fn("coffee", ...)
                                    v
                         +-------------------------+
                         | EXTENSIONES EN VEX (.vex)|
                         |  compiladas+ejecutadas   |
                         |  via libvesta; llaman al |
                         |  IDE con extern "coffee" |
                         +-------------------------+
```

## Ciclo de vida de una extension (DLL)

1. Al arrancar (o bajo demanda por *activation event*), el host lee el
   manifiesto, carga la DLL y llama `coffee_extension_register(host, api)`.
2. La extension comprueba `api->abi_version`, y registra:
   - comandos (`register_command`) -> aparecen en la paleta / menus / atajos,
   - suscripciones a eventos (`subscribe_event`),
   - items de menu / atajos.
3. El host despacha: cuando el usuario invoca un comando o ocurre un evento,
   llama al callback de la extension con el `CoffeeHost*`.
4. Al cerrar, el host emite `COFFEE_EVENT_SHUTDOWN` y descarga las DLLs.

## Manifiesto de extension

Cada extension trae un `coffee-extension.toml` (junto a su DLL):

```toml
[extension]
id          = "vesta"
name        = "Vesta / Vex"
version     = "0.1.0"
author      = "author_random"
entry       = "coffee_vesta.dll"        # la DLL a cargar
abi         = 1                          # COFFEE_ABI_VERSION requerido
description = "Compilar y ejecutar Vex; soporte de extensiones en Vex"
# Cuando activar la extension (lazy load):
activation  = ["onCommand:vesta.run", "onLanguage:vex"]
# Otras extensiones de las que depende (se cargan ANTES; se resuelven sus
# servicios via get_service).  El host hace orden topologico.
dependencies = []                        # p.ej. ["coffee.terminal"]
# Servicios que ESTA extension publica para que otras la consuman:
provides     = []                        # p.ej. ["vex.compile"]

[commands]
"vesta.run"        = "Ejecutar Vex"
"vesta.compile"    = "Compilar Vex a .velb"
"vesta.show-ir"    = "Ver IR (SSA) del archivo"

[keybindings]
"vesta.run" = "Ctrl+Shift+R"
```

## Dibujo (las extensiones pintan)

El IDE usa SDL3 internamente pero NO lo expone (ABI estable, sin acoplar las
extensiones a SDL).  Una extension no pinta libremente la pantalla: registra
una **vista** y el IDE la llama con un `CoffeePainter` recortado a su area.

- **Vistas** (`register_view`): panel lateral, panel inferior, overlay sobre el
  editor, o segmento de la barra de estado.  El IDE asigna el rect, gestiona el
  layout y llama al `paint` callback con primitivas (`fill_rect`, `draw_text`,
  `draw_line`, `text_width`, `line_height`, `set_clip`, `theme_color`) +
  opcionalmente un `input` callback (clic/tecla dentro de la vista).
- **Decoraciones del editor**: `set_line_background`, `set_gutter_marker`
  (iconos/breakpoints), `set_inline_hint` (texto al final de linea, estilo code
  lens / type hints).  `clear_decorations` limpia las de esa extension.
- **Repintado**: `request_repaint` cuando la extension cambia su estado.

Ejemplos que esto habilita: minimapa, panel de salida de "Ejecutar Vex",
problems/diagnostics, breakpoints, git blame inline, outline, terminal.

## Inter-extension (se ven entre ellas)

Algunas extensiones dependen de otras.  Dos mecanismos:

1. **Servicios** (`register_service` / `get_service`): una extension publica un
   struct de funciones bajo un nombre; otra lo obtiene y lo usa.  El contrato
   del servicio vive en un header compartido.  Ej.: la extension *vesta* publica
   `"vex.compile"` (un `struct VexCompileService { int (*compile)(...); }`) y un
   linter de Vex lo consume sin enlazar libvesta el mismo.
2. **Dependencias declaradas** (manifiesto `dependencies`): el host carga las
   deps ANTES (orden topologico) y garantiza que sus servicios esten
   disponibles.  `has_extension(id)` y `run_command(id)` permiten encadenar.

## Carga / descarga dinamica

Las extensiones se cargan, descargan y recargan en runtime (no solo al
arrancar): `load_extension(dir)`, `unload_extension(id)`, `reload_extension(id)`.

- Al **descargar**: el host invoca el `coffee_extension_unregister` opcional de
  la DLL, y AUTOMATICAMENTE quita todo lo que esa extension registro (comandos,
  vistas, decoraciones, servicios, suscripciones a eventos) -- el host lleva un
  registro por-extension de cada cosa para poder revertirla limpiamente.  Luego
  descarga la DLL (FreeLibrary/dlclose).
- **Recargar** = descargar + cargar; clave para desarrollar extensiones sin
  reiniciar el IDE.
- El **marketplace** usa esto: instalar = bajar + `load_extension`;
  desinstalar = `unload_extension` + borrar; actualizar = `reload_extension`.

## Marketplace

- **Indice remoto** (`marketplace.json`): lista de extensiones publicadas
  (id, version, descripcion, autor, URL de descarga, hash, abi).
- **Instalar**: el IDE descarga el zip/DLL + manifiesto a
  `<user-data>/extensions/<id>/`, verifica hash/abi, y lo carga.
- **Seguridad**: las DLLs corren con los permisos del IDE.  v1 = confianza por
  firma/hash del indice (igual que el package manager de Vesta).  Sandbox real
  (capabilities) = trabajo futuro.

## Seguridad: confianza (como todo IDE)

Las extensiones del IDE se basan en **confianza**, igual que VS Code, JetBrains,
Vim, etc.  Un DLL nativo (C/C++/Rust/Go/...) cargado in-process es codigo maquina
con acceso TOTAL al proceso: NO se puede sandboxear ni limitar por capabilities
desde el host (cualquier check a nivel de API se salta llamando syscalls).  Por
eso la seguridad NO es una promesa universal del host, sino una propiedad del
*tier* en que corre la extension:

| Tier | Lenguajes | Aislamiento | Modelo |
|------|-----------|-------------|--------|
| **Nativo in-process** (default) | C/C++/Rust/Go/... | ninguno | **confianza**: firma/verificacion del publicador; el usuario instala bajo su responsabilidad |
| **Nativo out-of-process** (opcional, futuro) | los mismos, en proceso aparte | OS (Job Objects / AppContainer / seccomp) | para nativas NO confiables; aislan de verdad, con coste de IPC |
| **Vex** | Vex (corre en libvesta/VM) | el VM enforce caps | unico tier sandboxeable in-process -> via "segura por defecto" para extensiones de la comunidad |

Lo que el host SI aporta (language-agnostic):
- **Crash-guard**: envuelve las llamadas a extensiones (SEH en Windows /
  try-catch) para contener *crashes accidentales* -> un bug de una extension no
  tumba el IDE (pero NO contiene comportamiento malicioso; eso es confianza).
- **Firma/verificacion** de extensiones del marketplace (base de la confianza).
- **Process isolation** como opcion para nativas no confiables (tier 2).

Encaje con Vex: las community-extensions no confiables se escriben en **Vex**
(sandboxeadas por el VM); las nativas son de confianza/firmadas.  No se asume
que toda extension use Vesta -- es solo el tier "seguro" opcional.

## libvesta como extension + extensiones en Vex

La extension **vesta** (`coffee_vesta.dll`) enlaza `libvesta` (la C-ABI del
compilador+VM) y, en su `coffee_extension_register`:

1. Registra comandos del IDE:
   - `vesta.run`  -> toma el buffer activo, `vesta_eval(...)`, vuelca la salida
     y el exit-code al panel (`output_append`).
   - `vesta.compile` -> `vesta_compile(...)` a `.velb`.
   - `vesta.show-ir` -> `vesta_compile_to_ir(...)` a un tab nuevo.
   - (futuro) diagnostics/LSP via los artefactos de libvesta.
2. **Puente Vex -> IDE**: registra cada funcion del `CoffeeApi` como funcion
   nativa nombrada (`api->register_native_fn(h, "coffee", "register_command",
   &bridge_register_command)`, etc.).  Asi un programa Vex puede:

   ```vex
   extern "coffee" {
       fn register_command(u8* id, u8* title, u64 cb) -> i32;
       fn buffer_insert(u8* utf8) -> void;
       fn set_status(u8* msg) -> void;
   }
   i32 on_hello(u64 host) { set_status(str_cstr("Hola desde una extension Vex")); return 0; }
   i32 main() {
       register_command(str_cstr("demo.hello"), str_cstr("Hola Vex"),
                        as_native_callback(on_hello));
       return 0;
   }
   ```

3. **Extension escrita en Vex**: su manifiesto declara `entry = "ext.vex"`
   (en vez de un .dll).  La extension vesta detecta el `.vex`, lo compila con
   `vesta_compile`, lo ejecuta con `vesta_run`, y el programa Vex usa
   `extern "coffee"` para registrarse.  -> El IDE usa NUESTRO lenguaje para
   sus plugins, estilo VS Code con JS pero con Vex.

## Hacia un sistema profesional (piezas pendientes)

Lo de arriba es la base.  Para un sistema completo estilo VS Code faltan
(priorizadas):

1. **Tareas async / off-UI-thread.**  Compilar, LSP, git, etc. sin congelar la
   UI: API de tasks (lanzar trabajo en un worker) + progreso + cancelacion.  Los
   callbacks del resultado vuelven al hilo de UI.  *Critico.*
2. **Edits con undo integrado.**  `begin_edit`/`end_edit` para que una operacion
   multi-edit de una extension sea UN solo paso de undo (no romper el historial
   del editor).
3. **Modelo de documentos + workspace.**  Acceso a buffers no activos, URI +
   `languageId` por documento, file-system watcher, abrir/listar/observar la
   carpeta del proyecto.
4. **Language Features API** (base del LSP): proveedores de completion / hover /
   definition / diagnostics / formatting / code-actions.  Clave para extensiones
   de lenguaje (Vex el primero, via libvesta).
5. **Contribuciones declarativas** en el manifiesto: comandos, menus,
   keybindings (con *when* contexts), settings-schema, lenguajes, temas,
   snippets -- el IDE los cablea ANTES de activar la extension (permite lazy).
6. **Activation events** (`onCommand`/`onLanguage`/`onStartup`) -> carga lazy.
7. **Estado persistente** (global + por-workspace) y **dependencias con semver**.
8. **UI interactiva**: quick-pick (paleta), input box, notificaciones con
   acciones; **event bus** custom entre extensiones (ademas de servicios).
9. **Marketplace pro**: versiones/updates/changelog, resolucion automatica de
   dependencias al instalar, install local/offline, firma.
10. **SDK / scaffolding**: plantilla + headers + build para crear una extension
    en C/C++/**Vex** en minutos; output channel por extension; API stability
    tiers (stable vs proposed); i18n.

Minimos para "funcional y profesional": 1, 2, 4 y 5 (el resto es incremental).

## Plan de implementacion (incrementos)

- **E1 - Extension host (core):** cargador de DLLs + `CoffeeApi` (comandos,
  eventos, editor/buffer) + **registro POR-EXTENSION** de todo lo que registra
  (para descarga limpia) + carga/descarga/recarga dinamica + resolucion de
  dependencias (orden topologico) + servicios (`register/get_service`).  Una
  extension de ejemplo en C.  (El dibujo de vistas/decoraciones puede ir aqui o
  en E1.b segun encaje con el render actual.)
- **E1.b - Dibujo:** `register_view` + `CoffeePainter` sobre el render SDL3 +
  decoraciones del editor (fondo de linea, gutter, hints).  Extension de
  ejemplo: un panel lateral o un minimapa simple.
- **E2 - Extension vesta (DLL):** `coffee_vesta.dll` que enlaza libvesta +
  comandos `vesta.run`/`compile`/`show-ir` + panel de salida.
- **E3 - Extensiones en Vex:** puente `register_native_fn` -> `extern "coffee"`
  + carga de manifiestos con `entry = *.vex` (compilar+ejecutar via libvesta).
  Extension de ejemplo escrita en Vex.
- **E4 - Contribuciones + activation events + async:** manifiesto declarativo
  (commands/menus/keybindings/settings/languages), carga lazy por activation
  events, API de tasks async + progreso/cancelacion, edits con undo integrado.
- **E5 - Marketplace:** indice remoto + instalar/actualizar/desinstalar +
  resolucion de dependencias + firma/verificacion.
- **E6 - Language Features API:** completion/hover/diagnostics/definition/
  formatting/code-actions; primera implementacion = Vex via la extension vesta
  (la base del LSP).
- **E7 - SDK + DX:** plantillas de extension (C/C++/Vex), output channel,
  process isolation opcional (tier nativo no confiable), API stability tiers.
- **E8+ (futuro):** resaltado de sintaxis Vex en el lexer del IDE, temas/iconos
  de extension, i18n, terminal integrado.

## Decisiones abiertas (para cerrar antes de E1)

1. **Carga**: lazy por *activation events* (como VS Code) o *eager* (cargar
   todas al arrancar).  Propuesta: empezar eager (simple) y anyadir lazy en E4.
2. **Hilos**: las extensiones corren en el hilo de UI (simple) o en un worker.
   Propuesta: hilo de UI en v1; los comandos largos (ejecutar Vex) que no
   bloqueen via callback async mas adelante.
3. **Formato de manifiesto**: TOML (como el resto del ecosistema Vesta) o JSON.
   Propuesta: TOML.
4. **Directorio de extensiones**: `<user-data>/CoffeeCode/extensions/`.
