# Cómo compilar CoffeeCode

> SDL3, SDL3_ttf y Freetype se descargan y compilan automáticamente con
> FetchContent la primera vez. No necesitas instalarlos a mano.

## Requisitos

| Herramienta  | Versión mínima               |
|--------------|------------------------------|
| CMake        | 3.24                         |
| Git          | cualquiera                   |
| Compilador C | GCC 9 / Clang 11 / MSVC 2019 |

## Linux

```bash
cd CoffeeCode
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/CoffeeCode
```

## Windows

### Opción A — `Compile.bat` (recomendada)

El script gestiona los modos de compilación. Cada modo usa su propio directorio
`build\<modo>`, así que cambiar de modo no obliga a recompilar las dependencias.

| Comando | Modo |
|---------|------|
| `Compile.bat` o `Compile.bat release` | Optimizado y portable (`-O3` + LTO) |
| `Compile.bat debug`  | Depuración completa (`-g3 -O0`, con consola) |
| `Compile.bat native` | Optimizado para tu CPU (`-march=native`) |
| `Compile.bat asan`   | Debug + AddressSanitizer/UBSan |
| `Compile.bat test`   | Compila y ejecuta los tests unitarios (ver «Tests») |
| `Compile.bat clean`  | Borra todo `build/` |

El binario queda en `build\<modo>\CoffeeCode.exe`.

### Opción B — CMake manual

```bat
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
:: binario en build\CoffeeCode.exe
```

## Opciones de CMake

| Opción | Efecto |
|--------|--------|
| `-DCOFFEE_NATIVE=ON` | Compila con `-march=native` (optimizado para la CPU local, no portable) |
| `-DCOFFEE_SANITIZE=ON` | Activa ASan/UBSan en builds Debug (requiere un toolchain con el runtime; TDM-GCC/MinGW no lo trae) |
| `-DCOFFEE_BUILD_TESTS=ON` | Compila la batería de tests (ON por defecto) |
| `-DCOFFEE_BUILD_APP=OFF` | NO compila la app ni SDL: solo los tests (build de test mucho más rápido) |

> `compile_commands.json` se genera siempre, para que CLion / VS Code / clangd
> indexen el proyecto con precisión.

## Tests

Las pruebas unitarias cubren los módulos **sin dependencias de SDL** (las
estructuras genéricas de `src/structs`, el gap buffer de `src/buffer` y el lexer
de `src/lexer`). Usan la librería [**ctests**](https://github.com/desmonHak/ctests),
que CMake **descarga automáticamente** con FetchContent (no hay que instalarla).

### Tests en Windows

```bat
Compile.bat test
```

Configura con `-DCOFFEE_BUILD_APP=OFF` (no descarga SDL), compila los tests en
`build\test\` y ejecuta cada binario mostrando el informe de ctests (suites,
casos y resumen). Devuelve un código de error distinto de cero si algún test
falla.

### Tests en Linux / CMake manual

```bash
# Solo los tests (sin SDL ni la app):
cmake -B build-tests -DCOFFEE_BUILD_APP=OFF -DCOFFEE_BUILD_TESTS=ON
cmake --build build-tests -j$(nproc)

# Opción 1: resumen agregado con CTest
ctest --test-dir build-tests --output-on-failure

# Opción 2: informe detallado por suite (ejecuta los binarios directamente)
for t in build-tests/test/test_*; do "$t"; done
```

Cada test también está registrado en **CTest**, así que CLion y VS Code los
descubren y permiten lanzarlos desde su interfaz. Para añadir un test nuevo,
crea `test/<módulo>/test_*.c` y regístralo en `test/CMakeLists.txt`.
