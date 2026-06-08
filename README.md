# CoffeeCode

`CoffeeCode` es un proyecto que busca crear un IDE completamente personalizable en el que el programador pueda crear la experiencia mas agradable y comprometida para crear nuevos productos tecnologicos.

Este IDE esta escrito en su mayor parte en lenguaje ***C*** usando la libreria grafica ***SDL3*** para visualizar la aplicacion.

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
│   ├── font.ttf          ← JetBrains Mono (se copia junto al .exe)
|   └── icono.ico         ← Icono del programa que se aplicara al .exe
└── src/
    ├── main.c            ← punto de entrada
    ├── buffer.c/h        ← Buffer de texto para el editor
    ├── editor.c/h        ← Nucleo del editor
    ├── filetree.c/h      ← Funcionalidades para explorador de archivos
    ├── input.c/h         ← Gestor de entrada
    ├── lexer.c/h         ← Lexer del IDE
    └── render.c/h        ← Renderizador de la aplicacion
```

## TODO (Siguientes Implementaciones)

- [ ] Implementar cuadricula para tener varios archivos en la misma pantalla
- [ ] Implementar una barra de busqueda (visual)
- [ ] Implementacion de idiomas
- [ ] Integrar GIT (Posible solucion via comandos)
- [ ] Soporte de markdown (visual)

## Bug / Fixes (Correciones)

- Texto en la barra de busqueda (CTRL+F) se buguea
- CTRL+A o seleccion de texto con el raton funciona, pero funciona raro
- Boton "nuevo" en archivo no funciona
- Flechas para moverse en el texto no funcionan bien