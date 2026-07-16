/**
 * @file lang_basic.c
 * @brief Resaltado SINCRONO generico (keywords/tipos/strings/comentarios/
 *        numeros), INDEPENDIENTE de cualquier LSP, para cuantos lenguajes
 *        traiga declarados "languages.json" -- SIN recompilar.
 *
 * lang_c.c (embebido en el core) es el unico lexer sincrono que trae el IDE;
 * cualquier otro lenguaje se queda sin colorear hasta que un LSP responda
 * (lsp-highlight), y eso depende de tener el binario del servidor instalado.
 * Esta extension cubre ese hueco de forma DINAMICA: en vez de una tabla en C
 * (un lenguaje = tocar codigo + recompilar), lee "languages.json" -- el mismo
 * fichero de datos que se instala junto al DLL -- y registra un highlighter
 * por cada entrada.  El tokenizador (basic_highlight) es UNO SOLO y generico:
 * strings/numeros/comentarios/operadores son universales; lo unico que varia
 * por lenguaje son sus listas de keywords/tipos y su sintaxis de comentario,
 * que es justo lo que trae el JSON.  Anyadir un lenguaje nuevo = anyadir un
 * objeto al JSON y reiniciar el IDE; cero cambios en este archivo.
 *
 * Eficiencia:
 *   - El JSON se parsea UNA sola vez, al cargar la extension (no en cada
 *     repintado ni en cada tecla).
 *   - Las listas de keywords/tipos se indexan en arrays NULL-terminados ya
 *     resueltos (punteros directos al arbol cJSON, sin copias) para que
 *     in_word_list() sea una simple pasada lineal por linea.
 *   - basic_highlight() tokeniza la linea en UNA pasada (O(n) en bytes de la
 *     linea), igual que lang_c.c; no hay backtracking ni reanalisis.
 *
 * Universalidad: languages.json trae una entrada final con "exts": ["*"],
 * que el core (host_find_highlighter, src/ext/ext_host.c) trata como
 * comodin de PRIORIDAD MINIMA: se usa solo cuando ninguna regla especifica
 * coincide.  Asi CUALQUIER extension no listada explicitamente -- incluida
 * la de un lenguaje inventado por el usuario -- recibe igualmente un
 * resaltado basico (strings/numeros/operadores) en vez de texto plano.
 *
 * Prioridad: si "lsp-highlight" tambien esta cargada y su servidor conecta,
 * sus semantic tokens (set_tokens) tienen prioridad automatica sobre este
 * resaltador sincrono (lo garantiza el core, ABI v4): este archivo da la
 * base inmediata y el LSP, cuando puede, la afina.
 */
#include "ext/coffee_ext.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Decodificador UTF-8 minimo y AUTONOMO (copia deliberada, no un #include de
 * utf8/utf8.h): esa implementacion se compila DENTRO del ejecutable del IDE y
 * en Windows no se exporta a las DLLs de extension (ld: "undefined reference
 * to utf8_decode" al enlazar). Solo necesitamos saber cuantos BYTES ocupa el
 * codepoint que arranca en s[0], para convertir offsets de byte a columnas. */
static int lb_utf8_len(const char *s, int maxlen) {
    if (maxlen <= 0) return 0;
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return 1;
    int n;
    if ((c & 0xE0) == 0xC0) n = 2;
    else if ((c & 0xF0) == 0xE0) n = 3;
    else if ((c & 0xF8) == 0xF0) n = 4;
    else return 1;
    return (n <= maxlen) ? n : 1;
}

/* -------------------------------------------------------------------------
 * Lenguaje cargado desde JSON (ver languages.json).  keywords/types apuntan
 * directamente a las cadenas del arbol cJSON (vivo mientras dure la ext.),
 * NULL-terminados para que in_word_list() no necesite contar nada.
 * ---------------------------------------------------------------------- */
typedef struct {
    char id[64];
    const char **exts;      /* NULL-terminado */
    int n_exts;
    const char **keywords;  /* NULL-terminado */
    const char **types;     /* NULL-terminado; NULL si el lenguaje no aplica */
    char line_comment[4];   /* "" si no tiene, p.ej. JSON */
    int block_comments;     /* 1 si soporta /* ... *\/ */
} LoadedLang;

static LoadedLang *g_langs = NULL;
static int g_n_langs = 0;
static cJSON *g_root = NULL; /* mantiene vivos los char* de arriba */

/* Paleta fija (misma que usa lsp-highlight para que, al llegar semantic
 * tokens de un LSP, el cambio de color no "salte"). */
static const CoffeeColor COL_DEFAULT = {212, 212, 212, 255};
static const CoffeeColor COL_KEYWORD = {197, 134, 192, 255};
static const CoffeeColor COL_TYPE    = {78, 201, 176, 255};
static const CoffeeColor COL_STRING  = {206, 145, 120, 255};
static const CoffeeColor COL_NUMBER  = {181, 206, 168, 255};
static const CoffeeColor COL_COMMENT = {106, 153, 85, 255};
static const CoffeeColor COL_OPERATOR= {212, 212, 212, 255};
static const CoffeeColor COL_PUNCT   = {212, 212, 212, 255};

/* -------------------------------------------------------------------------
 * Carga de languages.json (una vez, en el registro de la extension)
 * ---------------------------------------------------------------------- */

/* Vuelca un array JSON de strings en un array de punteros NULL-terminado que
 * apunta DIRECTAMENTE a las cadenas del nodo cJSON (root debe seguir vivo). */
static const char **json_str_array(cJSON *arr, int *out_n) {
    int n = (arr && cJSON_IsArray(arr)) ? cJSON_GetArraySize(arr) : 0;
    const char **out = (const char **)malloc(sizeof(char *) * (size_t)(n + 1));
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        out[i] = cJSON_IsString(it) ? it->valuestring : "";
    }
    out[n] = NULL;
    if (out_n) *out_n = n;
    return out;
}

static int load_languages(CoffeeHost *host, const CoffeeApi *api) {
    const char *dir = api->ext_dir(host);
    if (!dir) {
        api->log(host, COFFEE_LOG_ERROR, "lang-basic: ext_dir() vacio");
        return 0;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/languages.json", dir);

    FILE *f = fopen(path, "rb");
    if (!f) {
        char msg[1100];
        snprintf(msg, sizeof msg, "lang-basic: no se pudo abrir '%s'", path);
        api->log(host, COFFEE_LOG_ERROR, msg);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    g_root = cJSON_Parse(buf);
    free(buf);
    if (!g_root || !cJSON_IsArray(g_root)) {
        api->log(host, COFFEE_LOG_ERROR, "lang-basic: languages.json invalido");
        if (g_root) cJSON_Delete(g_root);
        g_root = NULL;
        return 0;
    }

    g_n_langs = cJSON_GetArraySize(g_root);
    g_langs = (LoadedLang *)calloc((size_t)g_n_langs, sizeof(LoadedLang));
    if (!g_langs) { g_n_langs = 0; return 0; }

    for (int i = 0; i < g_n_langs; i++) {
        cJSON *o = cJSON_GetArrayItem(g_root, i);
        LoadedLang *L = &g_langs[i];

        cJSON *id = cJSON_GetObjectItemCaseSensitive(o, "id");
        snprintf(L->id, sizeof L->id, "%s",
                 cJSON_IsString(id) ? id->valuestring : "?");

        L->exts = json_str_array(cJSON_GetObjectItemCaseSensitive(o, "exts"),
                                 &L->n_exts);
        L->keywords =
            json_str_array(cJSON_GetObjectItemCaseSensitive(o, "keywords"), NULL);

        cJSON *types = cJSON_GetObjectItemCaseSensitive(o, "types");
        L->types = (types && cJSON_IsArray(types) && cJSON_GetArraySize(types) > 0)
                       ? json_str_array(types, NULL)
                       : NULL;

        cJSON *lc = cJSON_GetObjectItemCaseSensitive(o, "line_comment");
        snprintf(L->line_comment, sizeof L->line_comment, "%s",
                 cJSON_IsString(lc) ? lc->valuestring : "");

        cJSON *bc = cJSON_GetObjectItemCaseSensitive(o, "block_comment");
        L->block_comments = cJSON_IsTrue(bc);
    }
    return 1;
}

static void free_languages(void) {
    for (int i = 0; i < g_n_langs; i++) {
        free((void *)g_langs[i].exts);
        free((void *)g_langs[i].keywords);
        free((void *)g_langs[i].types);
    }
    free(g_langs);
    g_langs = NULL;
    g_n_langs = 0;
    if (g_root) cJSON_Delete(g_root);
    g_root = NULL;
}

/* -------------------------------------------------------------------------
 * Utilidades de tokenizado (identicas en espiritu a lang_c.c)
 * ---------------------------------------------------------------------- */
static int in_word_list(const char *const *list, const char *w, int len) {
    if (!list) return 0;
    for (int i = 0; list[i]; i++)
        if ((int)strlen(list[i]) == len && strncmp(w, list[i], (size_t)len) == 0)
            return 1;
    return 0;
}

static uint32_t cols_upto(const char *text, int len, int byte_off) {
    if (byte_off > len) byte_off = len;
    uint32_t cols = 0;
    int b = 0;
    while (b < byte_off) {
        int n = lb_utf8_len(text + b, len - b);
        if (n <= 0) n = 1;
        b += n;
        cols++;
    }
    return cols;
}

/* Consume el comentario de bloque /* ... *\/ ; devuelve 1 si sigue abierto. */
static int scan_block_comment_end(const char *text, int len, int *i) {
    while (*i < len) {
        if (*i + 1 < len && text[*i] == '*' && text[*i + 1] == '/') {
            *i += 2;
            return 0;
        }
        (*i)++;
    }
    return 1;
}

/* -------------------------------------------------------------------------
 * El resaltador (::CoffeeHighlightFn), parametrizado por LoadedLang via ud.
 * UN SOLO tokenizador para todos los lenguajes del JSON: una pasada O(n).
 * ---------------------------------------------------------------------- */
static int basic_highlight(void *ud, const char *line_utf8, int line_len,
                           int in_block_comment, CoffeeSpan *out, int max_out,
                           int *out_block) {
    const LoadedLang *r = (const LoadedLang *)ud;
    if (out_block) *out_block = 0;
    if (!r || !line_utf8) return 0;

    int len = line_len, i = 0, n_out = 0;
    int in_block = r->block_comments ? in_block_comment : 0;
    size_t lc_len = strlen(r->line_comment);

#define EMIT(byte_col_, byte_len_, color_)                                   \
    do {                                                                     \
        if (out && n_out < max_out && (byte_len_) > 0) {                    \
            uint32_t c0 = cols_upto(line_utf8, len, (byte_col_));           \
            uint32_t c1 = cols_upto(line_utf8, len, (byte_col_) + (byte_len_)); \
            out[n_out].start_col = c0;                                      \
            out[n_out].len = (c1 >= c0) ? (c1 - c0) : 0;                    \
            out[n_out].color = (color_);                                    \
            n_out++;                                                        \
        }                                                                    \
    } while (0)

    while (i < len) {
        if (in_block) {
            int start = i;
            in_block = scan_block_comment_end(line_utf8, len, &i);
            EMIT(start, i - start, COL_COMMENT);
            continue;
        }

        char c = line_utf8[i];

        if (r->block_comments && c == '/' && i + 1 < len &&
            line_utf8[i + 1] == '*') {
            int start = i;
            i += 2;
            in_block = scan_block_comment_end(line_utf8, len, &i);
            EMIT(start, i - start, COL_COMMENT);
            continue;
        }

        if (lc_len && (size_t)(len - i) >= lc_len &&
            strncmp(line_utf8 + i, r->line_comment, lc_len) == 0) {
            EMIT(i, len - i, COL_COMMENT);
            i = len;
            continue;
        }

        if (c == '"' || c == '\'') {
            char delim = c;
            int start = i++;
            while (i < len) {
                if (line_utf8[i] == '\\') { i += 2; continue; }
                if (line_utf8[i] == delim) { i++; break; }
                i++;
            }
            EMIT(start, i - start, COL_STRING);
            continue;
        }

        if (isdigit((unsigned char)c) ||
            (c == '.' && i + 1 < len && isdigit((unsigned char)line_utf8[i + 1]))) {
            int start = i;
            while (i < len &&
                   (isalnum((unsigned char)line_utf8[i]) || line_utf8[i] == '.' ||
                    line_utf8[i] == '_'))
                i++;
            EMIT(start, i - start, COL_NUMBER);
            continue;
        }

        if (isalpha((unsigned char)c) || c == '_') {
            int start = i;
            while (i < len &&
                   (isalnum((unsigned char)line_utf8[i]) || line_utf8[i] == '_'))
                i++;
            int wlen = i - start;
            CoffeeColor col = COL_DEFAULT;
            if (in_word_list(r->keywords, line_utf8 + start, wlen))
                col = COL_KEYWORD;
            else if (in_word_list(r->types, line_utf8 + start, wlen))
                col = COL_TYPE;
            EMIT(start, wlen, col);
            continue;
        }

        if (strchr("+-*/%=<>&|^!~?:", c)) { EMIT(i, 1, COL_OPERATOR); i++; continue; }
        if (strchr("(){}[];,.", c))       { EMIT(i, 1, COL_PUNCT); i++; continue; }

        i++; /* espacios / continuacion UTF-8: sin emitir */
    }

#undef EMIT
    if (out_block) *out_block = in_block;
    return n_out;
}

/* -------------------------------------------------------------------------
 * Entrada / salida de la extension
 * ---------------------------------------------------------------------- */
COFFEE_EXTENSION_EXPORT
int coffee_extension_register(CoffeeHost *host, const CoffeeApi *api) {
    if (!api->register_highlighter) {
        api->log(host, COFFEE_LOG_ERROR, "lang-basic: host sin ABI v4 (resaltado)");
        return 1;
    }
    if (!load_languages(host, api)) return 1;

    for (int i = 0; i < g_n_langs; i++)
        api->register_highlighter(host, g_langs[i].exts, g_langs[i].n_exts,
                                  basic_highlight, &g_langs[i]);

    char msg[128];
    snprintf(msg, sizeof msg,
             "lang-basic: resaltado inmediato listo (%d lenguajes desde languages.json)",
             g_n_langs);
    api->log(host, COFFEE_LOG_INFO, msg);
    return 0;
}

COFFEE_EXTENSION_EXPORT
void coffee_extension_unregister(CoffeeHost *host) {
    (void)host;
    free_languages();
}
