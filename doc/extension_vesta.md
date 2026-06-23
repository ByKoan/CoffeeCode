# Usar la extension de Vesta (Vex)

La extension **Vesta LSP** (`extensions/vesta-lsp/`) da soporte de lenguaje de
primera clase para ficheros `.vex` dentro de CoffeeCode: diagnosticos de
compilacion en vivo, resaltado semantico, navegacion, hints inline y un
inspector tipo godbolt (bytecode / IR / JIT / AOT).  Es un cliente del language
server de Vesta (`vesta_lsp`), que la extension lanza como subproceso.

Este documento explica **que necesitas y como ponerla en marcha**.  Para el
sistema de extensiones en general, ver [extensiones.md](extensiones.md).

---

## 1. Requisitos

1. **La extension** instalada en `extensions/vesta-lsp/` junto al ejecutable del
   IDE (su `coffee-extension.toml` + la DLL).  Viene incluida en las
   distribuciones de CoffeeCode.
2. **El servidor `vesta_lsp`** (`vesta_lsp.exe` en Windows, `vesta_lsp` en
   Linux/macOS).  La extension lo **descubre** automaticamente y, si no lo
   encuentra, ofrece **instalarlo** (ver seccion 3).

No hay que configurar nada a mano en el caso normal: si tienes VestaVM
instalado, la extension encuentra el servidor sola.

---

## 2. Como localiza la extension el servidor

Al activarse, busca el ejecutable en este orden (la primera coincidencia gana):

1. Config `server_path` (si apunta a un fichero existente).
2. Variable de entorno `VESTA_LSP_PATH` (fichero directo o carpeta que lo
   contenga).
3. Variable de entorno `VESTA_HOME` (y `VESTA_HOME/bin`).
4. El `PATH` del sistema.
5. Instalaciones estandar:
   - **Windows**: `%ProgramFiles%\VestaVM`, `C:\Program Files\VestaVM`,
     `%LOCALAPPDATA%\VestaVM` (y sus subcarpetas `bin` / `build`),
     `%APPDATA%\VestaVM`, `%USERPROFILE%\VestaVM`.
   - **Linux/macOS**: `/usr/local/bin`, `/usr/local/share/vesta`,
     `~/.local/share/vesta` (y sus subcarpetas `bin` / `build`).

Cuando lo encuentra, guarda la ruta en la config `server_path` para arrancar mas
rapido la proxima vez.

---

## 3. Instalacion automatica del servidor

Si no hay ningun `vesta_lsp` en el sistema, la extension puede instalarlo:

1. Intenta descargar un binario ya compilado desde las releases de GitHub del
   repositorio oficial.
2. Si no hay binario disponible, hace `git clone --depth 1` del repositorio (con
   submodulos) y lo compila con CMake.

Para compilar desde fuente necesitas tener en el `PATH`: **git**, **cmake** y un
**compilador C/C++** (MinGW/GCC o Clang).  El progreso aparece en el panel
inferior, canal **vesta-lsp**.

Puedes cambiar el origen con estas claves de config o variables de entorno:

| Config | Variable de entorno | Para que |
| :--- | :--- | :--- |
| `server_path` | `VESTA_LSP_PATH` | Ruta exacta al ejecutable (salta el descubrimiento) |
| `vesta_repo` | `VESTA_REPO` | Repositorio del que clonar/descargar |
| `vesta_branch` | `VESTA_BRANCH` | Rama a usar |
| `download_url` | `download_url` | URL directa de un binario ya compilado |

---

## 4. Instalacion manual del servidor (compilar desde fuente)

Si prefieres instalar `vesta_lsp` tu mismo (o la instalacion automatica no
puede compilar en tu equipo), estos son los pasos.  El servidor forma parte del
repositorio de VestaVM:

- **Repositorio**: <https://github.com/desmonHak/VM>
- **Rama recomendada**: `feature` (ultimos cambios y submodulos validos).

### Requisitos

- **git**, **CMake** (>= 3.5) y un **compilador C++17**: MinGW/GCC o Clang en
  Windows, GCC/Clang en Linux/macOS.
- **OpenSSL 3.x** y **SQLite3** (SQLite va incluido en el repo).
  - Linux (Debian/Ubuntu): `sudo apt install libssl-dev libsqlite3-dev`.
  - Windows: usa binarios precompilados de OpenSSL.
- **Keystone** y **Capstone** vienen como submodulos del repo (por eso el clon
  usa `--recurse-submodules`).

### Pasos

```sh
# 1. Clonar el repositorio con submodulos
git clone --depth 1 --recurse-submodules --branch feature \
    https://github.com/desmonHak/VM vesta-src

# 2. Configurar (Release)
#    En Windows con MinGW anyade:  -G "MinGW Makefiles"
cmake -S vesta-src -B vesta-build -DCMAKE_BUILD_TYPE=Release

# 3. Compilar SOLO el servidor LSP
cmake --build vesta-build --target vesta_lsp
```

El binario resultante (`vesta_lsp.exe` en Windows, `vesta_lsp` en Linux/macOS)
queda en la carpeta de build (`vesta-build`).

### Que haga la extension lo encuentre

Coloca el binario en una de las rutas de la seccion 2, o apunta a el
directamente con cualquiera de estas opciones (la extension las prueba en ese
orden):

- Config `server_path` = ruta completa al ejecutable.
- Variable de entorno `VESTA_LSP_PATH` = ruta al ejecutable o a su carpeta.
- Variable de entorno `VESTA_HOME` = carpeta de VestaVM (busca tambien en
  `VESTA_HOME/bin`).
- Copiarlo a una instalacion estandar (p.ej. `C:\Program Files\VestaVM\` o
  `/usr/local/bin/`), o dejarlo accesible desde el `PATH`.

Tras encontrarlo, la extension guarda la ruta en la config `server_path`.

---

## 5. Que aporta al editar `.vex`

Con la extension activa y el servidor listo (la barra de estado muestra
`vesta-lsp: servidor listo`), al abrir un `.vex` obtienes:

- **Diagnosticos en vivo**: errores y avisos del compilador subrayados en el
  codigo, con un resumen en el canal **vesta-problems** del panel inferior.
- **Resaltado semantico**: coloreado por significado (tipos, funciones,
  variables, etc.), no solo por palabras clave.
- **Ir a definicion** (go-to-definition).
- **Hints inline**: nombres de parametros antes de cada argumento y valores
  `comptime` calculados, mostrados sobre el codigo.
- **Hover de inspeccion**: al posar el raton sobre un simbolo, un popup con
  pestanas (documentacion/firma, IR, bytecode, codigo nativo JIT/AOT) con
  correlacion entre fuente, IR y ensamblador.

---

## 6. Comandos del inspector

La extension registra estos comandos (paleta de comandos y menu
**Vesta/Inspector**).  Operan sobre el `.vex` activo:

| Comando | Atajo | Que muestra |
| :--- | :--- | :--- |
| Vesta: ver bytecode .vel | `Ctrl+Shift+B` | El bytecode `.vel` generado |
| Vesta: ver IR (post-opt) | | La representacion intermedia tras optimizar |
| Vesta: ver codigo nativo del JIT | | El x86-64 que emite el JIT |
| Vesta: ver codigo nativo del AOT | | El x86-64 que emite el AOT |
| Vesta: compatibilidad AOT | | Si el codigo es compilable a nativo |
| Vesta: complejidad (Big-O) | | Analisis de complejidad |
| Vesta: ver diagrama | | Diagrama del flujo (mermaid) |
| Vesta: funciones | | Listado de funciones |
| Vesta: expansion de macros | | El codigo tras expandir las macros |
| Vesta: valores comptime | | Los valores calculados en tiempo de compilacion |

La salida de cada inspeccion aparece en el canal **vesta-inspector** del panel
inferior.

---

## 7. Resolucion de problemas

- **"el activo no es .vex"**: el inspector solo opera sobre ficheros `.vex`.
- **"servidor no listo"**: espera a que la barra de estado indique
  `vesta-lsp: servidor listo`; revisa el canal **vesta-lsp** del panel inferior.
- **No arranca el servidor**: comprueba que `vesta_lsp` existe en alguna de las
  rutas de la seccion 2, o fija `server_path` / `VESTA_LSP_PATH` a su ubicacion.
- **Falla la instalacion automatica**: asegurate de tener `git`, `cmake` y un
  compilador C/C++ en el `PATH`; el detalle del fallo sale en el canal
  **vesta-lsp**.

---

## 8. Referencias

- Carpeta de la extension: `extensions/vesta-lsp/`.
- API de extensiones de CoffeeCode: [extensiones.md](extensiones.md).
