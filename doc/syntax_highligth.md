# Resaltado de sintaxis

CoffeeCode resuelve el resaltado en **tres capas**, de menor a mayor calidad,
que conviven sin pisarse entre si. Ninguna de las tres esta cableada al
nucleo del IDE: las tres usan la misma API publica de extensiones
(`include/ext/coffee_ext.h`, ver [extensiones.md](extensiones.md)).

```text
 calidad
   ^
   |   lsp-highlight   -- semantic tokens de un LSP real (async, exacto)
   |   lang-basic       -- keywords/strings/comentarios (sincrono, generico)
   |   comodin "*"      -- strings/numeros/operadores (sincrono, universal)
   +----------------------------------------------------------------> cobertura
       lenguajes con LSP configurado        cualquier archivo
```

1. **`lang_c.c`** (embebido en el propio ejecutable) — el unico lexer que
   trae el core; cubre `.c/.h/.cpp/...`. Es el ejemplo de referencia de como
   se escribe un highlighter sincrono.
2. **`extensions/lang-basic`** — highlighter sincrono generico, **cargado
   desde datos** (`languages.json`), para docenas de lenguajes: keywords,
   tipos, strings, numeros y comentarios. Pinta al instante, sin depender de
   ningun proceso externo.
3. **`extensions/lsp-highlight`** — conecta con el language server real de
   cada lenguaje (clangd, pyright, gopls...), tambien **cargado desde datos**
   (`servers.json`), y pinta con sus *semantic tokens*. Es asincrono (tarda
   lo que tarde el LSP en responder) pero mucho mas preciso: distingue una
   variable de un parametro, un tipo generico, etc.

Cuando el LSP responde, sus `set_tokens` **tienen prioridad automatica**
sobre el resaltado sincrono de la misma linea (lo garantiza el core, ABI
v4): no hay que coordinar nada entre las dos extensiones. Mientras el LSP no
ha respondido (o no esta configurado), `lang-basic` sigue mostrando su base.

---

## 1. El comodin `"*"`: cobertura universal por defecto

El core (`src/ext/ext_host.c`, `host_find_highlighter`) entiende una
extension especial: si un highlighter se registra para `"*"`, se usa como
**ultimo recurso** para cualquier archivo cuya extension no tenga una regla
mas especifica registrada por nadie:

```c
/* Busqueda de highlighter: coincidencia exacta siempre gana; "*" solo se
 * devuelve si NINGUNA regla especifica encaja. */
static HostHighlighter *host_find_highlighter(CoffeeHost *h, const char *ext) {
    ...
    if (host_ext_eq(hl->exts[k], ext)) return hl;         /* exacta: gana ya */
    if (!wildcard && strcmp(hl->exts[k], "*") == 0)
        wildcard = hl;                                     /* candidata */
    ...
    return wildcard;   /* solo si no hubo coincidencia exacta */
}
```

`lang-basic/languages.json` trae una entrada `"exts": ["*"]` al final que
aprovecha esto: da color a strings/numeros/operadores/puntuacion de
**cualquier** archivo, incluso uno con una extension nunca vista (por
ejemplo, un lenguaje inventado por el usuario que aun no tiene entrada
propia ni LSP configurado). No colorea keywords ni comentarios en ese caso
porque no conoce esa sintaxis — adivinarla arriesgaria pintar mal.

---

## 2. `lang-basic`: añadir un lenguaje al resaltado sincrono

No se toca C. Se edita el JSON que vive junto al DLL:

```
extensions/lang-basic/languages.json
```

(en el arbol fuente; CMake lo copia a
`<build>/extensions/lang-basic/languages.json`, que es de donde lo lee la
extension en runtime via `ext_dir(host)`).

Cada entrada:

```json
{
  "id": "mi-lenguaje",
  "exts": [".ml", ".mli"],
  "line_comment": "//",
  "block_comment": true,
  "keywords": ["let", "fun", "match", "if", "else"],
  "types": ["int", "string", "bool"]
}
```

| Campo           | Que hace                                                        |
|-----------------|-------------------------------------------------------------------|
| `id`            | Solo para logs, no afecta al comportamiento.                     |
| `exts`          | Lista de extensiones (con el punto) que activan esta regla.      |
| `line_comment`  | Cadena que abre comentario de linea (`""` si no aplica).         |
| `block_comment` | `true` si soporta `/* ... */`.                                   |
| `keywords`      | Palabras que se pintan como keyword.                              |
| `types`         | Palabras que se pintan como tipo (puede ser `[]`).                |

Guarda el archivo, **reinicia el IDE** (se lee una sola vez, al cargar la
extension) y listo: no hace falta tocar `lang_basic.c` ni recompilar nada.
El tokenizador es unico y generico (strings entre `"`/`'`, numeros,
operadores/puntuacion siempre se detectan igual); lo unico que varia por
lenguaje es la tabla que le pasas.

> Limitacion consciente: es un tokenizador ingenuo linea a linea (sin
> gramatica real), igual que `lang_c.c`. No entiende strings triples de
> Python, *raw strings*, interpolacion, etc. Para eso esta la capa de LSP.

---

## 3. `lsp-highlight`: añadir un language server (lo importante)

Esta es la capa que responde a *"tengo mi propio lenguaje con mi propio
LSP"*. Se edita, igual que antes, un JSON junto al DLL:

```
extensions/lsp-highlight/servers.json
```

Cada entrada:

```json
{
  "id": "mi-lenguaje",
  "exts": [".ml", ".mli"],
  "server_exe": "mi-lenguaje-lsp",
  "args": ["--stdio"],
  "language_id": "milenguaje"
}
```

| Campo         | Que hace                                                              |
|---------------|--------------------------------------------------------------------------|
| `id`          | Solo para logs.                                                          |
| `exts`        | Extensiones de archivo que arrancan/usan este servidor.                  |
| `server_exe`  | Ejecutable del LSP. Debe estar en `PATH`, o dale una ruta absoluta.       |
| `args`        | Argumentos con los que se lanza (sin `argv[0]`). La mayoria de LSP piden `--stdio`. |
| `language_id` | El `languageId` que se manda en `textDocument/didOpen`. Lo define tu propio servidor. |

Requisitos del lado del servidor: que hable **LSP estandar por stdio** y
soporte como minimo:

- `initialize` / `initialized`
- `textDocument/didOpen`, `textDocument/didChange` (full sync)
- `textDocument/semanticTokens/full`, con su `semanticTokensProvider.legend`
  en la respuesta de `initialize`

Eso es exactamente lo que implementa cualquier LSP "de verdad" — incluido
uno que te hayas escrito tu mismo para un lenguaje inventado (con librerias
como `pygls` en Python, `tower-lsp` en Rust, `vscode-languageserver` en
Node, etc.). CoffeeCode no le pide nada especifico del IDE: es el mismo
protocolo que usa VS Code.

Guarda el archivo, reinicia el IDE. La primera vez que abras un archivo con
esa extension:

1. Se lanza `server_exe` (perezosamente; un proceso por lenguaje, reusado
   mientras la extension este cargada).
2. Se le manda `initialize` con la raiz del workspace.
3. Al quedar listo, se manda `didOpen` con el buffer completo y se piden
   `semanticTokens/full`.
4. La respuesta se decodifica y se pinta con `set_tokens`, linea a linea.

### Colores por tipo de token

Los nombres de tipo de token (`keyword`, `type`, `string`, `function`...)
vienen de la *legend* que devuelve el propio LSP en `initialize`, asi que
cada servidor puede usar los suyos: `lsp_highlight.c` los resuelve por
**nombre** (`color_for_type()`), no por indice fijo. Si tu servidor usa un
tipo que no reconoce (por ejemplo algo muy especifico de tu lenguaje), cae
en el color por defecto. Para darle un color propio, añade un caso en
`color_for_type()` (esa parte si es codigo, no datos — es la paleta visual,
no la lista de lenguajes).

### Eficiencia: por que no se dispara en cada tecla

`BUFFER_CHANGED` llega en cada pulsacion, pero la extension **no** manda
`didChange`/pide tokens ahi mismo: solo marca la sesion como "sucia" con un
contador de frames (`DEBOUNCE_FRAMES`, ~20 frames ≈ 0.3 s). Un
`register_tick` decrementa ese contador cada frame; solo cuando llega a 0
(el usuario dejo de teclear un instante) se sincroniza de verdad con el LSP
y se piden tokens nuevos. Si sigues escribiendo, el contador se resetea y no
se manda nada — evita saturar el servidor y evita parpadeos de resaltado en
mitad de una palabra.

Ademas, la respuesta de `semanticTokens/full` se descarta si mientras tanto
cambiaste de pestana (se compara la ruta activa contra la ruta que origino
la peticion), asi que nunca se pinta un archivo con los tokens de otro.

### Que pasa si el servidor no esta instalado

Si `server_exe` no se encuentra, `proc_spawn` falla, se registra un
`COFFEE_LOG_WARN` (visible en el panel/canal de logs del IDE) y esa entrada
queda inactiva — el resto de `servers.json` sigue funcionando con
normalidad, y `lang-basic` sigue dando resaltado basico para ese lenguaje
mientras tanto.

---

## 4. Resumen: añadir un lenguaje nuevo, paso a paso

Para un lenguaje que **ya tiene** un LSP publico (Python, Go, Rust...):

1. añade una entrada a `extensions/lsp-highlight/servers.json` con su
   ejecutable y extensiones.
2. (Opcional) añade tambien una entrada a
   `extensions/lang-basic/languages.json` para tener color inmediato antes
   de que el LSP conecte, o cuando el usuario no lo tenga instalado.
3. Reinicia el IDE.

Para un lenguaje **inventado por ti**, con tu propio LSP:

1. Escribe el servidor (cualquier libreria LSP del ecosistema que prefieras)
   e implementa como minimo `initialize` + `semanticTokens/full`.
2. Una entrada en `servers.json` apuntando a tu binario.
3. (Opcional) una entrada en `languages.json` con las keywords de tu
   lenguaje, para tener algo pintado incluso sin el LSP corriendo.

En ningun caso hace falta tocar `lsp_highlight.c`, `lang_basic.c` ni el
core del IDE — ambas listas son datos, no codigo.
