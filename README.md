<p align="center">
  <img src="assets/coffe.png" alt="CoffeeCode" width="160">
</p>

<h1 align="center">CoffeeCode</h1>

<p align="center">
  Un IDE ligero y completamente personalizable, escrito en C con SDL3.
</p>

<p align="center">
  <a href="https://github.com/desmonHak/CoffeeCode/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/desmonHak/CoffeeCode/actions/workflows/ci.yml/badge.svg"></a>
  <img alt="Lenguaje C11" src="https://img.shields.io/badge/Lenguaje-C11-00599C?logo=c&logoColor=white">
  <img alt="Plataformas" src="https://img.shields.io/badge/Plataformas-Windows%20%7C%20Linux-2ea44f">
  <img alt="Gráficos SDL3" src="https://img.shields.io/badge/Gr%C3%A1ficos-SDL3-1e90ff">
  <a href="LICENSE"><img alt="Licencia" src="https://img.shields.io/badge/Licencia-No%20comercial-orange"></a>
</p>

---

**CoffeeCode** busca crear un IDE completamente personalizable en el que el programador pueda crear la experiencia más agradable y comprometida para desarrollar nuevos productos tecnológicos. Está escrito en su mayor parte en **C**, usando la librería gráfica **SDL3** para la interfaz.

## Características

- Resaltado de sintaxis para C
- Pestañas con varios archivos abiertos a la vez
- Deshacer / rehacer (undo/redo)
- Explorador de archivos integrado
- Multiplataforma: Windows y Linux

## Compilación

Guía completa en **[doc/how_build.md](doc/how_build.md)**.

- **Windows:** `Compile.bat release` &nbsp;(o `debug` / `native` / `asan` / `test` / `clean`)
- **Linux:** `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`

## Tests

Pruebas unitarias de los módulos puros (estructuras genéricas, gap buffer y
lexer) con la librería [**ctests**](https://github.com/desmonHak/ctests), que se
**descarga sola** vía CMake (FetchContent). No hay que instalar nada.

- **Windows (todos los tests):**

  ```bat
  Compile.bat test
  ```

  Compila solo la batería de tests (sin SDL ni la app) y la ejecuta, mostrando
  el informe completo de ctests (suites, casos, resumen).

- **Linux / CMake manual:**

  ```bash
  cmake -B build-tests -DCOFFEE_BUILD_APP=OFF -DCOFFEE_BUILD_TESTS=ON
  cmake --build build-tests -j
  ctest --test-dir build-tests --output-on-failure
  ```

  Para ver el informe detallado de cada suite, ejecuta los binarios
  directamente (`build-tests/test/test_*`).

`-DCOFFEE_BUILD_APP=OFF` evita descargar y compilar SDL: solo se construye lo
necesario para testear. Cada test es además un test de **CTest**, así que se
integran con CLion / VS Code.

## Documentación

| Documento | Contenido |
|-----------|-----------|
| [Cómo compilar](doc/how_build.md) | Requisitos y compilación en Windows / Linux |
| [Sistema de extensiones](doc/extensiones.md) | ABI C, manifiesto y cómo cualquier lenguaje hace extensiones |
| [Resaltado de sintaxis](doc/resaltado-sintaxis.md) | Resaltado sincrono + via LSP, y cómo añadir un lenguaje o un language server propio |
| [Roadmap](doc/roadmap.md) | Futuras características planificadas |
| [Bugs conocidos](doc/known_issues.md) | Problemas pendientes de corregir |
| [Contribuir](CONTRIBUTING.md) | Flujo de ramas, formato de commits y pull requests |

## Atajos de teclado

| Atajo           | Acción                    |
|-----------------|---------------------------|
| Flechas         | Mover cursor              |
| Inicio / Fin    | Inicio / fin de línea     |
| Re Pág / Av Pág | Scroll rápido             |
| Ctrl+Inicio     | Ir al inicio del archivo  |
| Ctrl+Fin        | Ir al final del archivo   |
| Tab             | Indentar (4 espacios)     |
| Enter           | Nueva línea + autoindent  |
| Backspace       | Borrar antes del cursor   |
| Supr            | Borrar después del cursor |
| Ctrl+S          | Guardar                   |
| Ctrl+Q          | Salir                     |
| Rueda ratón     | Scroll vertical           |
| Click           | Mover cursor              |

## Estructura del proyecto

```text
CoffeeCode/
├-- CMakeLists.txt
├-- Compile.bat            ← build en Windows (modos release/debug/native/asan/clean)
├-- assets/                ← logo e icono
├-- include/<modulo>/      ← cabeceras (una carpeta por modulo)
└-- src/<modulo>/          ← fuentes  (una carpeta por modulo)
```

> Ya no se empaqueta ninguna fuente propia: por defecto la app usa una fuente
> del sistema (detectada en runtime igual que en el selector de
> Preferencias). Puedes elegir cualquier otra fuente instalada desde
> Preferencias, o forzarla con la variable de entorno `COFFEECODE_FONT`.

Módulos: `buffer` (gap buffer de texto), `editor` (núcleo: pestañas, undo/redo), `lexer` (resaltado), `filetree` (explorador), `input` (entrada) y `render` (renderizador). Los includes de módulo usan prefijo de carpeta, p. ej. `#include "editor/editor.h"`.

## Licencia

Uso no comercial, con atribución y propiedad derivada. Consulta [LICENSE](LICENSE). &copy; 2026 Koan (Diego).
