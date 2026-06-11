/**
 * @file lsp.h
 * @brief Cliente LSP (Language Server Protocol) para CoffeeCode.
 *
 * Lanza un servidor LSP como proceso hijo y se comunica con él mediante
 * JSON-RPC sobre sus stdin/stdout (el transporte estándar que usan clangd,
 * pylsp, rust-analyzer, etc.).
 *
 * ## Flujo de uso
 *
 *   1. lsp_start()        — lanza el proceso del servidor.
 *   2. lsp_open()         — envía textDocument/didOpen con el contenido inicial.
 *   3. lsp_change()       — envía textDocument/didChange tras cada edición.
 *   4. lsp_tokens_full()  — solicita textDocument/semanticTokens/full y espera
 *                           la respuesta (bloqueante con timeout).
 *   5. lsp_close()        — envía textDocument/didClose.
 *   6. lsp_stop()         — mata el proceso y libera recursos.
 *
 * ## Tokens semánticos
 *
 * El servidor devuelve un array compacto de enteros (deltaLine, deltaStart,
 * length, tokenType, tokenModifiers). Esta implementación los expande en
 * ::LspToken y los pone a disposición del render a través de ::LspTokenCache.
 *
 * ## Compatibilidad de plataforma
 *
 * - Linux/macOS: pipes POSIX (pipe2 / fork / exec).
 * - Windows:     pipes anónimas Win32 (CreatePipe / CreateProcess).
 * El código condicional está en lsp.c; esta cabecera es portable.
 *
 * ## Seguridad de hilos
 *
 * La cache de tokens se escribe desde el hilo de E/S LSP y se lee desde el
 * hilo de render. Se protege con un mutex SDL (SDL_Mutex).
 */
#pragma once

#include "lexer/lexer.h" /* LexTokenType, LineTokens */
#include <stddef.h>
#include <stdint.h>

/* ── Configuración ─────────────────────────────────────────────────────────── */

/** Tiempo máximo (ms) que lsp_tokens_full() espera la respuesta del servidor. */
#define LSP_REQUEST_TIMEOUT_MS 2000

/** Tamaño del buffer de lectura de la respuesta JSON del servidor (bytes). */
#define LSP_READ_BUFFER_SIZE (1024 * 256) /* 256 KB */

/** Versión del documento que se envía al servidor (se incrementa en cada
 * cambio). */
#define LSP_DOC_VERSION_INIT 1

/* ── Tipos de token semántico LSP ─────────────────────────────────────────── */

/**
 * @brief Categorías de token semántico definidas por LSP.
 *
 * Subconjunto de la lista estándar de LSP que se mapea a ::LexTokenType para el
 * render. Los valores numéricos coinciden con el índice en la lista de
 * "legendTokenTypes" que negocia el servidor durante la inicialización.
 * lsp_map_semantic_type() convierte LspSemanticType → LexTokenType.
 */
typedef enum {
    LSP_TOK_NAMESPACE = 0,
    LSP_TOK_TYPE,
    LSP_TOK_CLASS,
    LSP_TOK_ENUM,
    LSP_TOK_INTERFACE,
    LSP_TOK_STRUCT,
    LSP_TOK_TYPE_PARAMETER,
    LSP_TOK_PARAMETER,
    LSP_TOK_VARIABLE,
    LSP_TOK_PROPERTY,
    LSP_TOK_ENUM_MEMBER,
    LSP_TOK_EVENT,
    LSP_TOK_FUNCTION,
    LSP_TOK_METHOD,
    LSP_TOK_MACRO,
    LSP_TOK_KEYWORD,
    LSP_TOK_MODIFIER,
    LSP_TOK_COMMENT,
    LSP_TOK_STRING,
    LSP_TOK_NUMBER,
    LSP_TOK_REGEXP,
    LSP_TOK_OPERATOR,
    LSP_TOK_DECORATOR,
    LSP_TOK_COUNT /* centinela */
} LspSemanticType;

/* ── Token semántico expandido ─────────────────────────────────────────────── */

/**
 * @brief Un token semántico en coordenadas absolutas (línea/columna).
 *
 * El servidor LSP entrega tokens en formato "delta" compacto; lsp.c los expande
 * a coordenadas absolutas antes de almacenarlos en ::LspTokenCache.
 */
typedef struct {
    int line;            /**< Línea (0-based). */
    int col;             /**< Columna de inicio (0-based). */
    int len;             /**< Longitud en caracteres. */
    LspSemanticType type; /**< Categoría semántica raw del servidor. */
} LspToken;

/* ── Cache de tokens semánticos ─────────────────────────────────────────────  */

/**
 * @brief Tabla de tokens semánticos por línea, actualizada asíncronamente.
 *
 * El hilo de E/S LSP escribe en esta cache cada vez que recibe una respuesta
 * semanticTokens/full. El render la lee en cada frame para colorear el texto.
 * El acceso concurrente se serializa con ::mutex.
 */
typedef struct {
    LspToken *tokens; /**< Array plano de tokens (malloc'd). */
    int count;        /**< Número de tokens válidos. */
    int version;      /**< Versión del documento al que corresponden. */
    void *mutex;      /**< SDL_Mutex* — opaco para no incluir SDL aquí. */
    int ready;        /**< 1 si hay tokens válidos para usar. */
} LspTokenCache;

/* ── Proceso hijo del servidor LSP ────────────────────────────────────────── */

/**
 * @brief Handles del proceso hijo del servidor LSP.
 *
 * Abstrae las diferencias entre POSIX y Win32. Solo lsp.c accede a los campos
 * internos; el resto del código usa ::LspClient.
 */
typedef struct {
#if defined(_WIN32) || defined(_WIN64)
    void *hProcess;  /**< HANDLE del proceso (Win32). */
    void *hStdin;    /**< Extremo de escritura del pipe stdin del servidor. */
    void *hStdout;   /**< Extremo de lectura del pipe stdout del servidor. */
#else
    int pid;         /**< PID del proceso hijo (POSIX). */
    int stdin_fd;    /**< fd de escritura → stdin del servidor. */
    int stdout_fd;   /**< fd de lectura ← stdout del servidor. */
#endif
    int alive;       /**< 1 si el proceso sigue en marcha. */
} LspProcess;

/* ── Cliente LSP ─────────────────────────────────────────────────────────── */

/**
 * @brief Estado completo del cliente LSP para un archivo.
 *
 * Cada pestaña (::EditorTab) puede tener su propio ::LspClient, lo que permite
 * que distintos archivos hablen con distintos servidores (clangd para .c/.cpp,
 * pylsp para .py, etc.).
 */
typedef struct {
    LspProcess proc;          /**< Proceso del servidor LSP. */
    LspTokenCache cache;      /**< Cache de tokens semánticos. */
    char uri[512];            /**< URI del documento ("file:///ruta/al/archivo"). */
    char language_id[32];     /**< Identificador de lenguaje LSP ("c", "python"…). */
    int doc_version;          /**< Versión del documento (se incrementa en didChange). */
    int next_request_id;      /**< Contador de IDs para las peticiones JSON-RPC. */
    int initialized;          /**< 1 tras recibir la respuesta de "initialize". */
    /* Mapa de tipos de token negociado con el servidor */
    char token_types[LSP_TOK_COUNT][64]; /**< Names from legendTokenTypes */
    int  token_type_count;               /**< Cuántos entradas son válidas */
} LspClient;

/* ── API pública ──────────────────────────────────────────────────────────── */

/**
 * @brief Lanza el servidor LSP y realiza el handshake initialize/initialized.
 *
 * @param client  Cliente a inicializar (memoria ya reservada por el llamador).
 * @param cmd     Comando del servidor (p. ej. "clangd", "pylsp").
 * @param uri     URI del workspace raíz (p. ej. "file:///home/user/proyecto").
 * @return 1 si el servidor arrancó y respondió; 0 si hubo error.
 */
int lsp_start(LspClient *client, const char *cmd, const char *workspace_uri);

/**
 * @brief Envía textDocument/didOpen con el contenido completo del archivo.
 *
 * @param client      Cliente ya iniciado con lsp_start().
 * @param file_path   Ruta absoluta del archivo en disco.
 * @param language_id Identificador LSP del lenguaje ("c", "cpp", "python"…).
 * @param text        Contenido completo del archivo (UTF-8).
 */
void lsp_open(LspClient *client, const char *file_path,
              const char *language_id, const char *text);

/**
 * @brief Envía textDocument/didChange con el nuevo contenido completo.
 *
 * Usa el método de sincronización "full" (envía todo el texto cada vez) para
 * simplificar la implementación. Para archivos grandes se puede cambiar a
 * "incremental" en una versión futura.
 *
 * @param client  Cliente con el documento ya abierto.
 * @param text    Nuevo contenido completo del archivo.
 */
void lsp_change(LspClient *client, const char *text);

/**
 * @brief Solicita los tokens semánticos del documento completo.
 *
 * Envía textDocument/semanticTokens/full y espera la respuesta hasta
 * LSP_REQUEST_TIMEOUT_MS ms. Actualiza client->cache con los tokens recibidos.
 *
 * @param client  Cliente con el documento ya abierto.
 * @return 1 si se recibieron tokens; 0 si hubo timeout o error.
 */
int lsp_tokens_full(LspClient *client);

/**
 * @brief Envía textDocument/didClose.
 * @param client  Cliente con el documento abierto.
 */
void lsp_close(LspClient *client);

/**
 * @brief Envía shutdown + exit y mata el proceso del servidor.
 * @param client  Cliente a detener.
 */
void lsp_stop(LspClient *client);

/**
 * @brief Rellena un LineTokens con los tokens semánticos de la línea @p line.
 *
 * Traduce los ::LspToken de la cache al formato ::LineTokens que usa el render,
 * usando lsp_map_semantic_type() para convertir tipos semánticos a ::LexTokenType.
 *
 * @param client  Cliente con cache actualizada.
 * @param line    Número de línea (0-based).
 * @param out     Destino de los tokens de render.
 */
void lsp_fill_line_tokens(const LspClient *client, int line, LineTokens *out);

/**
 * @brief Convierte un tipo semántico LSP al LexTokenType del render.
 *
 * Mapeo conservador: los tipos sin equivalente directo caen en ::TOK_DEFAULT.
 *
 * @param t Tipo semántico del servidor.
 * @return  LexTokenType correspondiente.
 */
LexTokenType lsp_map_semantic_type(LspSemanticType t);

/**
 * @brief Devuelve el language_id LSP estándar para la extensión de @p path.
 *
 * @param path  Ruta del archivo (puede ser NULL).
 * @return      Cadena estática con el language_id ("c", "cpp", "python", etc.)
 *              o NULL si la extensión no está mapeada.
 */
const char *lsp_language_id_for_path(const char *path);

/**
 * @brief Devuelve el comando del servidor LSP para el language_id dado.
 *
 * @param language_id  Identificador LSP ("c", "cpp", "python"…).
 * @return             Cadena estática con el comando (p. ej. "clangd") o NULL.
 */
const char *lsp_server_cmd_for_language(const char *language_id);

/**
 * @brief Convierte una ruta absoluta del sistema de archivos a una URI
 *        "file:///" válida para JSON-RPC.
 *
 * En Windows sustituye las barras invertidas por '/' para evitar secuencias
 * de escape inválidas en JSON.
 */
#include <stdio.h>
static inline void path_to_uri(const char *path, char *uri, int uri_size) {
#ifdef PLATFORM_WINDOWS
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (int i = 0; tmp[i]; i++)
        if (tmp[i] == '\\') tmp[i] = '/';
    snprintf(uri, uri_size, "file:///%s", tmp[0] == '/' ? tmp + 1 : tmp);
#else
    snprintf(uri, uri_size, "file://%s", path);
#endif
}
