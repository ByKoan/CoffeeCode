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
| `-DCOFFEE_NATIVE=ON`   | Compila con `-march=native` (optimizado para la CPU local, no portable) |
| `-DCOFFEE_SANITIZE=ON` | Activa ASan/UBSan en builds Debug (requiere un toolchain con el runtime; TDM-GCC/MinGW no lo trae) |

> `compile_commands.json` se genera siempre, para que CLion / VS Code / clangd
> indexen el proyecto con precisión.
