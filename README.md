# CoffeeCode

`CoffeeCode` es un proyecto que busca crear un IDE completamente personalizable en el que el programador pueda crear la experiencia mas agradable y comprometida para crear nuevos productos tecnologicos.

Este IDE esta escrito en su mayor parte en lenguaje ***C*** usando la libreria grafica **SDL3*** para visualizar la aplicacion.

## Caracteristicas actuales

- Resaltado de sintaxis para C
- Multiplataforma (Windows + Linux).

## Requisitos

| Herramienta | Versión mínima |
|-------------|------------------------------|
| CMake       | 3.20                         |
| Git         | cualquiera                   |
| C compiler  | GCC 9 / Clang 11 / MSVC 2019 |

> SDL3 y SDL3_ttf se descargan y compilan automáticamente con FetchContent.

---

## Compilar en Linux

```bash
# 1. Clona / descomprime el proyecto
cd ide

# 2. Configura (solo la primera vez)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Compila
cmake --build build -j$(nproc)

# 4. Ejecuta
./build/ide
```

---

## Compilar en Windows

```bash
# 1. Clona / descomprime el proyecto
cd ide

# 2. Configura (solo la primera vez)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Compila
cmake --build build --config Release -j

# 4. Ejecuta
/build/ide.exe
```

---

## Atajos de teclado

| Atajo           | Acción                   |
|-----------------|--------------------------|
| Flechas         | Mover cursor             |
| Inicio / Fin    | Inicio/fin de línea      |
| Re Pág / Av Pág | Scroll rápido            |
| Ctrl+Inicio     | Ir al inicio del archivo |
| Ctrl+Fin        | Ir al final del archivo  |
| Tab             | Indentar (4 espacios)    |
| Enter           | Nueva línea + autoindent |
| Backspace       | Borrar antes del cursor  |
| Supr            | Borrar después del cursor|
| Ctrl+S          | Guardar                  |
| Ctrl+Q          | Salir                    |
| Rueda ratón     | Scroll vertical          |
| Click           | Mover cursor             |

---

## Estructura del proyecto

```bash
ide/
├── CMakeLists.txt
├── assets/
│   └── font.ttf          ← JetBrains Mono (se copia junto al .exe)
└── src/
    ├── main.c            ← punto de entrada
    ├── editor.c/h        ← estado global + main loop
    ├── buffer.c/h        ← gap buffer (estructura de datos del texto)
    ├── lexer.c/h         ← tokenizador C + cache de resaltado
    ├── render.c/h        ← render SDL3 (texto, gutter, cursor, status)
    └── input.c/h         ← gestión de eventos SDL3
```

## TODO (Siguientes Implementaciones)

- [ ] Diálogo nativo de abrir/guardar (SDL_ShowOpenFileDialog en SDL3)
- [ ] Búsqueda y reemplazo (Ctrl+F)
- [ ] Múltiples pestañas / buffers
- [ ] Selección de texto con shift+flechas y Ctrl+C/V
- [ ] Soporte de lenguajes adicionales (Python, Markdown…)
- [ ] Numeración de línea con resaltado de la línea actual

## Bug / Fixes (Correciones)
