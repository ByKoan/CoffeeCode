/**
 * @file lexer.c
 * @brief Tokenizador para el resaltado de sintaxis de C y cache de tokens por
 *        línea, más los resaltadores enchufables (interfaz Highlighter).
 *
 * @note Cómo encaja todo. Este módulo tiene tres capas bien diferenciadas:
 *
 *   1. El TOKENIZADOR (::lexer_tokenize_line). Es una máquina de estados que
 *      recorre una sola línea de texto carácter a carácter y la parte en tokens
 *      (palabra clave, número, string, comentario...). Su único estado que
 *      "sobrevive" a la línea es el flag @c in_block_comment: dado que un
 *      comentario @c /\* ... *\/ puede abarcar varias líneas, el tokenizador
 *      recibe si la línea EMPIEZA dentro de un bloque y devuelve si TERMINA
 *      dentro de uno. Ese bit encadenado es lo que permite resaltar bloques
 *      multilínea sin volver a leer el archivo entero.
 *
 *   2. La CACHE de tokens por línea (::LexerCache y funciones lexer_cache_*).
 *      Tokenizar en cada frame todas las líneas visibles sería caro, así que
 *      se guardan los tokens ya calculados de cada línea y se marcan como
 *      "sucias" (dirty) solo las que cambiaron. El render re-tokeniza
 *      perezosamente solo lo sucio. Como el estado de bloque se encadena, al
 *      ensuciar una línea hay que ensuciar también todas las de abajo.
 *
 *   3. La interfaz HIGHLIGHTER (::Highlighter). Una pequeña tabla de punteros
 *      a función (estilo "OOP en C", como los @c *_ops del kernel de Linux)
 *      que permite enchufar distintos lenguajes detrás de la misma API. Se
 *      elige uno u otro según la extensión del archivo.
 */
#include "lexer/lexer.h"
#include <ctype.h>  /* isalpha, isdigit, isxdigit, isalnum, tolower */
#include <stdlib.h> /* (utilidades estándar) */
#include <string.h> /* strlen, strncmp, strchr, strrchr */

/**
 * @brief Paleta de colores de los tokens (tema oscuro).
 *
 * Indexada por ::LexTokenType: @c TOKEN_COLORS[token.type] da el color RGBA con el
 * que el render pinta ese token. Usa inicializadores designados @c [TOK_x] =
 * ... para que el índice y el valor queden visualmente emparejados y no
 * dependan del orden de declaración del enum.
 */
const Color TOKEN_COLORS[TOK_COUNT] = {
    [TOK_DEFAULT] = {0xCD, 0xC7, 0xBA, 0xFF},      /* blanco cálido   */
    [TOK_KEYWORD] = {0xE0, 0x6C, 0x75, 0xFF},      /* rojo/rosa       */
    [TOK_TYPE] = {0xE5, 0xC0, 0x7B, 0xFF},         /* amarillo        */
    [TOK_COMMENT] = {0x5C, 0x63, 0x70, 0xFF},      /* gris            */
    [TOK_STRING] = {0x98, 0xC3, 0x79, 0xFF},       /* verde           */
    [TOK_NUMBER] = {0xD1, 0x9A, 0x66, 0xFF},       /* naranja         */
    [TOK_PREPROCESSOR] = {0xC6, 0x78, 0xDD, 0xFF}, /* morado          */
    [TOK_OPERATOR] = {0x56, 0xB6, 0xC2, 0xFF},     /* cyan            */
    [TOK_PUNCTUATION] = {0xAB, 0xB2, 0xBF, 0xFF},  /* gris claro      */
};

/**
 * @brief Palabras reservadas de C que se pintan como ::TOK_KEYWORD.
 *
 * Lista terminada en @c NULL (centinela) para poder recorrerla sin pasar la
 * longitud. Incluye además @c NULL/@c true/@c false, que técnicamente son
 * macros/constantes pero se resaltan como palabras clave por comodidad.
 */
static const char *KEYWORDS[] = {
    "auto",   "break",  "case",     "const",    "continue", "default",
    "do",     "else",   "enum",     "extern",   "for",      "goto",
    "if",     "inline", "register", "restrict", "return",   "sizeof",
    "static", "struct", "switch",   "typedef",  "union",    "volatile",
    "while",  "NULL",   "true",     "false",    NULL};
/**
 * @brief Nombres de tipo de C que se pintan como ::TOK_TYPE.
 *
 * También terminada en @c NULL. Cubre los tipos básicos y los típicos de
 * @c <stdint.h>/@c <stddef.h> (size_t, intN_t...). No es exhaustiva: cualquier
 * tipo no listado caerá en ::TOK_DEFAULT (identificador normal).
 */
static const char *TYPES[] = {
    "char",     "double",    "float",    "int",      "long",    "short",
    "signed",   "unsigned",  "void",     "bool",     "size_t",  "ptrdiff_t",
    "intptr_t", "uintptr_t", "int8_t",   "int16_t",  "int32_t", "int64_t",
    "uint8_t",  "uint16_t",  "uint32_t", "uint64_t", "FILE",    NULL};

/**
 * @brief ¿Está la palabra @p w (de @p len caracteres) en la lista @p list?
 *
 * La palabra @p w NO está terminada en @c '\0' (apunta dentro de la línea), por
 * eso se compara por longitud exacta + @c strncmp en vez de @c strcmp: primero
 * se descartan las entradas de longitud distinta y luego se comparan los bytes.
 *
 * @param list Array de cadenas terminado en @c NULL donde buscar.
 * @param w    Inicio de la palabra a buscar (no necesariamente terminada en
 * nul).
 * @param len  Número de caracteres de @p w a considerar.
 * @return 1 si la palabra coincide con alguna entrada; 0 si no.
 */
static int in_word_list(const char *const *list, const char *w, int len) {
    for (int i = 0; list[i]; i++)
        /* misma longitud Y mismos bytes: descarta rápido por longitud */
        if ((int)strlen(list[i]) == len &&
            strncmp(w, list[i], (size_t)len) == 0)
            return 1;
    return 0;
}

/**
 * @brief Avanza @p *i hasta pasar el cierre @c *\/ de un comentario de bloque.
 *
 * Recorre la línea desde @p *i buscando la secuencia de dos caracteres @c '*'
 * seguido de @c '/'. Si la encuentra, deja @p *i justo después del cierre y
 * devuelve 0 (bloque cerrado). Si llega al final de la línea sin verla, deja
 * @p *i en @p len y devuelve 1 (el bloque continúa en la línea siguiente). Ese
 * valor de retorno es el que propaga el flag @c in_block_comment entre líneas.
 *
 * @param text Texto de la línea.
 * @param len  Longitud de la línea.
 * @param[in,out] i Posición de lectura; se avanza in situ.
 * @return 1 si el bloque sigue abierto al acabar la línea; 0 si se cerró.
 */
static int scan_to_comment_end(const char *text, int len, int *i) {
    while (*i < len) {
        /* '*' seguido de '/' (con cuidado de no leer fuera con el +1): cierre
         */
        if (*i + 1 < len && text[*i] == '*' && text[*i + 1] == '/') {
            *i += 2;  /* consumir los dos caracteres del cierre */
            return 0; /* comentario cerrado */
        }
        (*i)++;
    }
    return 1; /* sigue abierto al final de la línea */
}

/**
 * @brief Tokeniza una línea de C en @p out (el corazón de la máquina de
 * estados).
 *
 * Recorre @p text de izquierda a derecha con un índice @p i. En cada vuelta del
 * bucle mira el carácter actual y decide qué tipo de token empieza ahí; consume
 * todos los caracteres de ese token de una sentada (avanzando @p i hasta su
 * final) y emite una entrada con ::PUSH. El orden de los @c if es la PRIORIDAD
 * de reconocimiento; a grandes rasgos:
 *
 *   - Si venimos dentro de un bloque de comentario abierto en una línea previa
 *     (@p in_block_comment), todo cuenta como comentario hasta el cierre.
 *   - @c '#' al estilo de C: directiva de preprocesador hasta fin de línea.
 *   - @c "//" comentario de línea; @c "/\*" inicio de comentario de bloque.
 *   - @c '"' o @c '\'': string/carácter, respetando los escapes con @c '\\'.
 *   - Dígito (o @c '.' seguido de dígito): número (hex, decimal, con sufijos).
 *   - Letra o @c '_': identificador, que luego se clasifica en
 * keyword/tipo/normal.
 *   - Operadores y signos de puntuación de un carácter.
 *   - Cualquier otra cosa (espacios, etc.) se salta sin emitir token.
 *
 * El único estado que cruza el límite de la línea es @p in_block_comment, que
 * se recibe como parámetro y se devuelve actualizado; así un @c /\* abierto
 * aquí "tiñe" de comentario las líneas siguientes hasta encontrar su @c *\/.
 *
 * @param text             Texto de la línea (sin el @c '\n' final).
 * @param len              Longitud de la línea en caracteres.
 * @param[out] out         Destino de los tokens; se vacía (count=0) al entrar.
 * @param in_block_comment 1 si la línea EMPIEZA dentro de un bloque de
 * comentario.
 * @return 1 si la línea TERMINA aún dentro de un bloque de comentario; 0 si no.
 */
static int lexer_tokenize_line(const char *text, int len, LineTokens *out,
                               int in_block_comment) {
    out->count = 0; /* empezar con la línea sin tokens */
    int i = 0;      /* índice de lectura dentro de text */

/* Emite un token (col, len, type) en out, sin desbordar el array fijo.
 * Es una macro (no función) para no pasar 'out' en cada llamada y porque
 * captura directamente las variables locales del bucle. */
#define PUSH(col_, len_, type_)                                                \
    do {                                                                       \
        /* si la línea es enorme, sobran tokens */                             \
        if (out->count < MAX_TOKENS_PER_LINE) {                                \
            out->tokens[out->count].col = (col_);                              \
            out->tokens[out->count].len = (len_);                              \
            out->tokens[out->count].type = (type_);                            \
            out->count++;                                                      \
        }                                                                      \
    } while (0)

    while (i < len) {
        /* continuación de un bloque de comentario abierto en líneas previas:
         * todo lo que haya hasta el cierre (o el fin de línea) es comentario.
         */
        if (in_block_comment) {
            int start = i;
            /* avanza i y actualiza estado */
            in_block_comment = scan_to_comment_end(text, len, &i);
            PUSH(start, i - start, TOK_COMMENT);
            continue;
        }

        char c = text[i]; /* carácter que decide qué token empieza aquí */

        if (c == '#') { /* preprocesador: hasta el fin de línea */
            PUSH(i, len - i, TOK_PREPROCESSOR);
            i = len; /* consumir el resto de la línea de golpe */
            continue;
        }

        if (c == '/' && i + 1 < len &&
            text[i + 1] == '/') { /* comentario de línea */
            PUSH(i, len - i, TOK_COMMENT);
            i = len; /* "//" se come todo lo que queda */
            continue;
        }

        if (c == '/' && i + 1 < len &&
            text[i + 1] == '*') { /* inicio de bloque */
            int start = i;
            i += 2; // saltar el apertura de bloque
            /* ¿se cierra en esta línea? */
            in_block_comment = scan_to_comment_end(text, len, &i);
            PUSH(start, i - start, TOK_COMMENT);
            continue;
        }

        if (c == '"' || c == '\'') { /* string o carácter */
            char delim = c; /* el mismo carácter que abre debe cerrar */
            /* recordar inicio y pasar tras la comilla de apertura */
            int start = i++;
            while (i < len) {
                if (text[i] ==
                    '\\') { /* escape: '\' protege al siguiente carácter */
                    i += 2; /* saltar la barra Y el carácter escapado */
                    continue;
                }
                if (text[i] == delim) { /* comilla de cierre sin escapar */
                    i++; /* incluir la comilla de cierre en el token */
                    break;
                }
                i++;
            }
            /* nota: si no aparece el cierre, el token llega hasta el fin de
             * línea */
            PUSH(start, i - start, TOK_STRING);
            continue;
        }

        if (isdigit((unsigned char)c) ||
            (c == '.' && i + 1 < len &&
             isdigit((unsigned char)text[i + 1]))) { /* número */
            int start = i;
            if (c == '0' && i + 1 < len &&
                (text[i + 1] == 'x' || text[i + 1] == 'X')) { /* hex */
                i += 2; /* saltar el prefijo "0x"/"0X" */
                while (i < len && isxdigit((unsigned char)text[i]))
                    i++; /* dígitos hexadecimales 0-9 a-f A-F */
            } else {
                /* decimal/flotante: dígitos, punto, exponente (e/E) y sufijos
                 * de tipo (f/F float, u/U unsigned, l/L long), todos juntos. */
                while (i < len &&
                       (isdigit((unsigned char)text[i]) || text[i] == '.' ||
                        text[i] == 'e' || text[i] == 'E' || text[i] == 'f' ||
                        text[i] == 'F' || text[i] == 'u' || text[i] == 'U' ||
                        text[i] == 'l' || text[i] == 'L'))
                    i++;
            }
            PUSH(start, i - start, TOK_NUMBER);
            continue;
        }

        if (isalpha((unsigned char)c) ||
            c == '_') { /* identificador / palabra clave / tipo */
            int start = i;
            /* un identificador es [A-Za-z_][A-Za-z0-9_]*: consumir su cola */
            while (i < len &&
                   (isalnum((unsigned char)text[i]) || text[i] == '_'))
                i++;
            int wlen = i - start;      /* longitud de la palabra leída */
            LexTokenType t = TOK_DEFAULT; /* por defecto: identificador normal */
            /* clasificar la palabra: primero keyword, si no tipo, si no normal
             */
            if (in_word_list(KEYWORDS, text + start, wlen))
                t = TOK_KEYWORD;
            else if (in_word_list(TYPES, text + start, wlen))
                t = TOK_TYPE;
            PUSH(start, wlen, t);
            continue;
        }

        if (strchr("+-*/%=<>&|^!~?:", c)) { /* operador (un carácter) */
            PUSH(i, 1, TOK_OPERATOR);
            i++;
            continue;
        }

        if (strchr("(){}[];,.", c)) { /* puntuación (un carácter) */
            PUSH(i, 1, TOK_PUNCTUATION);
            i++;
            continue;
        }

        i++; /* resto (espacios, etc.): avanzar sin emitir token */
    }

#undef PUSH
    return in_block_comment; /* propagar el estado de bloque a la línea
                                siguiente */
}

/* ── Cache de tokens por línea ──────────────────────────────────────────────
 */

/**
 * @brief Inicializa la cache con @p line_count líneas, todas marcadas sucias.
 *
 * Reserva dos vectores paralelos indexados por número de línea: @c lines guarda
 * los tokens ya calculados de cada línea y @c dirty marca con 1 las líneas que
 * aún deben (re)tokenizarse. Al arrancar nada está calculado todavía, así que
 * todas se dejan sucias para que el primer render las procese.
 *
 * @param lc         Cache a inicializar.
 * @param line_count Número de líneas iniciales (se fuerza a >= 1).
 * @return 1 si las reservas de memoria fueron bien; 0 si alguna falló.
 */
int lexer_cache_init(LexerCache *lc, int line_count) {
    if (line_count < 1) line_count = 1; /* al menos una línea (archivo vacío) */
    vec_init(&lc->lines, sizeof(LineTokens)); /* Vec<LineTokens> */
    vec_init(&lc->dirty, sizeof(int));        /* Vec<int> paralelo */
    /* vec_resize pone a cero las entradas nuevas => LineTokens.count = 0 */
    if (!vec_resize(&lc->lines, (size_t)line_count)) return 0;
    if (!vec_resize(&lc->dirty, (size_t)line_count)) return 0;
    for (int i = 0; i < line_count; i++)
        *(int *)vec_at(&lc->dirty, (size_t)i) = 1; /* todas sucias de inicio */
    return 1;
}

/** @brief Libera los dos vectores internos de la cache. @param lc Cache a
 * liberar. */
void lexer_cache_free(LexerCache *lc) {
    vec_free(&lc->lines);
    vec_free(&lc->dirty);
}

/**
 * @brief Ajusta la cache a @p new_count líneas (las nuevas quedan sucias).
 *
 * Se llama tras una edición que cambió el número de líneas. Si el tamaño no
 * cambió no hace nada. Al crecer, las entradas nuevas se ponen a cero (tokens
 * vacíos) y se marcan sucias para que se tokenicen; al encoger simplemente se
 * recortan los vectores.
 *
 * @param lc        Cache a redimensionar.
 * @param new_count Nuevo número de líneas (se fuerza a >= 1).
 */
void lexer_cache_resize(LexerCache *lc, int new_count) {
    if (new_count < 1) new_count = 1;
    int old = (int)lc->lines.len; /* tamaño anterior */
    if (new_count == old) return; /* nada que hacer si no cambió */

    /* nuevas LineTokens a cero (count=0) */
    vec_resize(&lc->lines, (size_t)new_count);
    vec_resize(&lc->dirty, (size_t)new_count);
    for (int i = old; i < new_count; i++)
        /* ensuciar solo las añadidas */
        *(int *)vec_at(&lc->dirty, (size_t)i) = 1;
}

/**
 * @brief Marca como sucias todas las líneas desde @p from_line hasta el final.
 *
 * Ensucia "hacia abajo" y no solo la línea editada porque el estado de bloque
 * de comentario se encadena: abrir o cerrar un @c /\* ... *\/ en @p from_line
 * puede cambiar cómo se tokenizan todas las líneas siguientes. Re-tokenizar de
 * más es seguro; re-tokenizar de menos dejaría resaltado obsoleto.
 *
 * @param lc        Cache a ensuciar.
 * @param from_line Primera línea (inclusive) a marcar como pendiente.
 */
void lexer_cache_dirty(LexerCache *lc, int from_line) {
    int n = (int)lc->dirty.len;
    for (int i = from_line; i < n; i++)
        *(int *)vec_at(&lc->dirty, (size_t)i) = 1;
}

/* ── Interfaz Highlighter (resaltadores enchufables) ────────────────────────
 */

/**
 * @brief Resaltador de C: delega en el tokenizador de arriba.
 *
 * Adaptador que ajusta ::lexer_tokenize_line a la firma de la interfaz
 * ::Highlighter. El parámetro @p self (el "this" de este objeto-en-C) no se usa
 * porque el resaltador de C no guarda estado propio; se ignora con @c (void).
 *
 * @param self     El propio resaltador (no usado).
 * @param text     Texto de la línea.
 * @param len      Longitud de la línea.
 * @param[out] out Destino de los tokens.
 * @param in_block 1 si la línea empieza dentro de un bloque de comentario.
 * @return 1 si la línea termina dentro de un bloque de comentario.
 */
static int hl_c_tokenize(const Highlighter *self, const char *text, int len,
                         LineTokens *out, int in_block) {
    (void)self;
    return lexer_tokenize_line(text, len, out, in_block);
}
/** Instancia del resaltador de C (nombre + función de tokenizado). */
const Highlighter highlighter_c = {"C", hl_c_tokenize};

/**
 * @brief Resaltador nulo: texto plano, sin tokens (render usa el color por
 * defecto).
 *
 * Para archivos cuyo lenguaje no conocemos. No produce tokens (count=0) y nunca
 * entra en estado de bloque, así que el render pinta todo con ::TOK_DEFAULT.
 *
 * @param self     El propio resaltador (no usado).
 * @param text     Texto de la línea (no usado).
 * @param len      Longitud de la línea (no usado).
 * @param[out] out Destino: se deja vacío.
 * @param in_block Estado de bloque entrante (no usado).
 * @return 0 siempre (nunca queda un bloque abierto).
 */
static int hl_none_tokenize(const Highlighter *self, const char *text, int len,
                            LineTokens *out, int in_block) {
    (void)self;
    (void)text;
    (void)len;
    (void)in_block;
    out->count = 0;
    return 0;
}
/** Instancia del resaltador de texto plano. */
const Highlighter highlighter_none = {"texto", hl_none_tokenize};

/**
 * @brief Comparación de extensión case-insensitive (sin depender de SDL/POSIX).
 *
 * Compara dos cadenas carácter a carácter en minúsculas. Devuelve verdadero
 * solo si tienen la misma longitud y los mismos caracteres ignorando mayúsculas
 * (la comprobación @c *a==*b al final exige que ambas acaben a la vez en @c
 * '\0').
 *
 * @param a Primera cadena (p. ej. la extensión del archivo).
 * @param b Segunda cadena (p. ej. una extensión conocida).
 * @return 1 si son iguales ignorando mayúsculas/minúsculas; 0 si no.
 */
static int ext_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b; /* iguales solo si ambas terminaron al mismo tiempo */
}

/**
 * @brief Resaltador por defecto cuando no hay más información.
 * @return Puntero al resaltador de C (el lenguaje principal del editor).
 */
const Highlighter *highlighter_default(void) {
    return &highlighter_c;
}

/**
 * @brief Selecciona el resaltador adecuado según la extensión de @p path.
 *
 * Lógica de selección:
 *   - Ruta vacía/@c NULL → resaltador de C (asume código nuevo sin guardar).
 *   - Sin punto en el nombre (no hay extensión) → texto plano.
 *   - Extensión en la lista de C/C++ (.c, .h, .cpp...) → resaltador de C.
 *   - Cualquier otra extensión → texto plano.
 *
 * @param path Ruta del archivo (puede ser @c NULL o vacía).
 * @return Puntero al ::Highlighter elegido (nunca @c NULL).
 */
const Highlighter *highlighter_for_path(const char *path) {
    if (!path || !path[0]) return &highlighter_c; /* sin nombre: asumir C */
    const char *dot = strrchr(path, '.'); /* última '.' = inicio de extensión */
    if (!dot) return &highlighter_none;   /* sin extensión: texto plano */

    /* extensiones reconocidas como C/C++ (incluye el punto) */
    static const char *c_exts[] = {".c",   ".h",  ".cpp", ".cc", ".cxx",
                                   ".hpp", ".hh", ".hxx", NULL};
    for (int i = 0; c_exts[i]; i++)
        if (ext_eq(dot, c_exts[i])) return &highlighter_c;
    return &highlighter_none; /* extensión desconocida: texto plano */
}
