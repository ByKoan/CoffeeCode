/**
 * @file lsp.c
 * @brief Implementación del cliente LSP para CoffeeCode.
 *
 * ## Arquitectura
 *
 *   ┌──────────────┐   JSON-RPC/stdin   ┌──────────────┐
 *   │  CoffeeCode  │ ─────────────────► │  LSP server  │
 *   │  (lsp.c)     │ ◄───────────────── │  (clangd…)   │
 *   └──────────────┘   JSON-RPC/stdout  └──────────────┘
 *
 * La comunicación usa el framing estándar LSP:
 *
 *   Content-Length: <N>\r\n\r\n<JSON de N bytes>
 *
 * ## Sincronización de tokens
 *
 * lsp_tokens_full() envía la petición y luego lee respuestas del pipe hasta
 * encontrar la que tiene el id correcto o agotar el timeout. Esto es bloqueante
 * pero rápido en la práctica (los servidores responden en < 100 ms para archivos
 * medianos). Para un diseño completamente asíncrono se usaría un hilo de lectura
 * dedicado, lo cual queda pendiente como mejora futura.
 *
 * ## Compatibilidad
 *
 * - POSIX (Linux/macOS): pipe2() + fork() + execvp().
 * - Win32: CreatePipe() + CreateProcess().
 */
#include "lsp/lsp.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SDL_Mutex — incluimos solo lo estrictamente necesario */
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_timer.h> /* SDL_GetTicks */

/* ── Plataforma ──────────────────────────────────────────────────────────── */
#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#define PLATFORM_POSIX 1
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* Helpers internos */

/** Escribe exactamente @p n bytes desde @p buf al pipe del servidor. */
static int pipe_write_all(LspProcess *p, const char *buf, size_t n) {
    if (!p->alive) return 0;
#ifdef PLATFORM_WINDOWS
    DWORD written;
    return WriteFile(p->hStdin, buf, (DWORD)n, &written, NULL) &&
           written == (DWORD)n;
#else
    size_t total = 0;
    while (total < n) {
        ssize_t r = write(p->stdin_fd, buf + total, n - total);
        if (r <= 0) {
            p->alive = 0;
            return 0;
        }
        total += (size_t)r;
    }
    return 1;
#endif
}

/** Lee hasta @p max bytes del pipe del servidor en @p buf. Devuelve bytes leídos. */
static int pipe_read(LspProcess *p, char *buf, int max) {
    if (!p->alive || max <= 0) return 0;
#ifdef PLATFORM_WINDOWS
    DWORD avail = 0;
    if (!PeekNamedPipe(p->hStdout, NULL, 0, NULL, &avail, NULL) || avail == 0)
        return 0;
    DWORD rd = 0;
    ReadFile(p->hStdout, buf, (DWORD)(avail < (DWORD)max ? avail : (DWORD)max),
             &rd, NULL);
    return (int)rd;
#else
    /* Lectura no bloqueante */
    int flags = fcntl(p->stdout_fd, F_GETFL, 0);
    fcntl(p->stdout_fd, F_SETFL, flags | O_NONBLOCK);
    ssize_t r = read(p->stdout_fd, buf, (size_t)max);
    fcntl(p->stdout_fd, F_SETFL, flags); /* restaurar */
    if (r < 0) return 0;
    return (int)r;
#endif
}

/* Framing LSP */

/**
 * @brief Envía un mensaje JSON-RPC con el framing LSP.
 *
 * @param p    Proceso del servidor.
 * @param json Cuerpo JSON (cadena terminada en '\0').
 * @return 1 si se envió bien; 0 si hubo error.
 */
static int lsp_send(LspProcess *p, const char *json) {
    int len = (int)strlen(json);
    char header[64];
    int hlen = snprintf(header, sizeof(header), "Content-Length: %d\r\n\r\n", len);
    return pipe_write_all(p, header, (size_t)hlen) &&
           pipe_write_all(p, json, (size_t)len);
}

/**
 * @brief Lee un mensaje JSON-RPC del servidor con timeout.
 *
 * Acumula bytes hasta leer un mensaje completo (Content-Length completo) o
 * hasta agotar timeout_ms milisegundos. El resultado se escribe en @p out_buf
 * (debe tener al menos LSP_READ_BUFFER_SIZE bytes) y se apunta con @p out_json
 * al inicio del cuerpo JSON.
 *
 * @param p          Proceso del servidor.
 * @param out_buf    Buffer de trabajo (LSP_READ_BUFFER_SIZE bytes).
 * @param out_json   [out] Apunta al cuerpo JSON dentro de out_buf.
 * @param timeout_ms Tiempo máximo de espera en milisegundos.
 * @return Longitud del cuerpo JSON, o 0 si timeout/error.
 */
static int lsp_recv(LspProcess *p, char *out_buf, char **out_json,
                    int timeout_ms) {
    Uint64 deadline = SDL_GetTicks() + (Uint64)timeout_ms;
    int filled = 0;
    int capacity = LSP_READ_BUFFER_SIZE - 1;

    while (SDL_GetTicks() < deadline) {
        int r = pipe_read(p, out_buf + filled, capacity - filled);
        if (r > 0) {
            filled += r;
            out_buf[filled] = '\0';

            /* Buscar el separador de cabecera */
            char *sep = strstr(out_buf, "\r\n\r\n");
            if (!sep) continue;

            int content_length = 0;
            if (sscanf(out_buf, "Content-Length: %d", &content_length) != 1)
                continue;

            char *body = sep + 4;
            int body_available = filled - (int)(body - out_buf);
            if (body_available < content_length) continue; /* aún incompleto */

            body[content_length] = '\0';
            *out_json = body;
            return content_length;
        }
        SDL_Delay(5); /* ceder CPU mientras esperamos */
    }
    return 0; /* timeout */
}

/* JSON mínimo */
/*
 * No usamos una librería JSON externa para no añadir dependencias al proyecto.
 * Implementamos solo lo que necesitamos: escribir objetos JSON y extraer campos
 * simples de la respuesta (enteros y arrays de enteros).
 */

/** Escapa una cadena para JSON (solo los caracteres imprescindibles). */
static void json_escape(const char *src, char *dst, int dst_size) {
    int i = 0, j = 0;
    dst_size--; /* reservar '\0' */
    while (src[i] && j < dst_size) {
        unsigned char c = (unsigned char)src[i++];
        if (c == '"' && j + 1 < dst_size) {
            dst[j++] = '\\';
            dst[j++] = '"';
        } else if (c == '\\' && j + 1 < dst_size) {
            dst[j++] = '\\';
            dst[j++] = '\\';
        } else if (c == '\n' && j + 1 < dst_size) {
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (c == '\r' && j + 1 < dst_size) {
            dst[j++] = '\\';
            dst[j++] = 'r';
        } else if (c == '\t' && j + 1 < dst_size) {
            dst[j++] = '\\';
            dst[j++] = 't';
        } else {
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
}

/**
 * @brief Extrae el valor entero de un campo JSON por nombre.
 *
 * Búsqueda naïve pero suficiente para los mensajes que emite el servidor.
 * Solo funciona con valores escalares enteros en el nivel superior del objeto.
 *
 * @param json  Cadena JSON.
 * @param key   Nombre del campo (sin comillas).
 * @param out   Valor entero extraído.
 * @return 1 si se encontró; 0 si no.
 */
static int json_get_int(const char *json, const char *key, int *out) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return 0;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':' || *p == ' ') p++;
    if (!isdigit((unsigned char)*p) && *p != '-') return 0;
    *out = atoi(p);
    return 1;
}

/**
 * @brief Localiza el array "data" dentro de la respuesta semanticTokens.
 *
 * La respuesta tiene la forma: {"result":{"data":[n,n,n,...]}}
 * Devuelve un puntero al primer '[' o NULL si no se encontró.
 */
static const char *json_find_data_array(const char *json) {
    const char *p = strstr(json, "\"data\"");
    if (!p) return NULL;
    p = strchr(p, '[');
    return p;
}

/**
 * @brief Extrae los enteros de un array JSON compacto.
 *
 * Llena @p out con hasta @p max enteros leídos del array que empieza en @p arr.
 * @return Número de enteros leídos.
 */
static int json_read_int_array(const char *arr, int *out, int max) {
    int n = 0;
    const char *p = arr;
    if (*p == '[') p++;
    while (*p && *p != ']' && n < max) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']' || !*p) break;
        if (isdigit((unsigned char)*p) || *p == '-') {
            out[n++] = atoi(p);
            while (*p && *p != ',' && *p != ']') p++;
        } else {
            p++;
        }
    }
    return n;
}

/* Construcción de URIs y mensajes JSON-RPC */

/* path_to_uri: definida como static inline en include/lsp/lsp.h */

/* Lanzamiento del proceso */

#ifdef PLATFORM_POSIX
static int launch_posix(LspProcess *p, const char *cmd) {
    int to_child[2], from_child[2];
    if (pipe(to_child) < 0 || pipe(from_child) < 0) return 0;

    pid_t pid = fork();
    if (pid < 0) return 0;

    if (pid == 0) {
        /* Proceso hijo: conectar pipes a stdin/stdout */
        close(to_child[1]);
        close(from_child[0]);
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]);
        close(from_child[1]);
        /* Redirigir stderr a /dev/null para no contaminar el stdout */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        /* Construir argv: cmd puede ser "clangd --some-flag" */
        char cmd_copy[256];
        strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
        cmd_copy[sizeof(cmd_copy) - 1] = '\0';
        char *argv[16];
        int argc = 0;
        char *tok = strtok(cmd_copy, " ");
        while (tok && argc < 15) {
            argv[argc++] = tok;
            tok = strtok(NULL, " ");
        }
        argv[argc] = NULL;
        execvp(argv[0], argv);
        _exit(1); /* exec falló */
    }

    /* Proceso padre */
    close(to_child[0]);
    close(from_child[1]);
    p->pid = (int)pid;
    p->stdin_fd = to_child[1];
    p->stdout_fd = from_child[0];
    p->alive = 1;
    return 1;
}
#endif

#ifdef PLATFORM_WINDOWS
static int launch_windows(LspProcess *p, const char *cmd) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE hChildIn_R, hChildIn_W, hChildOut_R, hChildOut_W;
    if (!CreatePipe(&hChildIn_R, &hChildIn_W, &sa, 0)) return 0;
    if (!CreatePipe(&hChildOut_R, &hChildOut_W, &sa, 0)) return 0;
    SetHandleInformation(hChildIn_W, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hChildOut_R, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.hStdInput = hChildIn_R;
    si.hStdOutput = hChildOut_W;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi = {0};
    char cmd_copy[512];
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
    if (!CreateProcessA(NULL, cmd_copy, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        return 0;

    CloseHandle(hChildIn_R);
    CloseHandle(hChildOut_W);
    CloseHandle(pi.hThread);
    p->hProcess = pi.hProcess;
    p->hStdin = hChildIn_W;
    p->hStdout = hChildOut_R;
    p->alive = 1;
    return 1;
}
#endif

/* Inicialización LSP */

/**
 * @brief Envía la petición "initialize" y espera la respuesta.
 *
 * También extrae la lista legendTokenTypes del resultado para saber qué
 * índice corresponde a cada tipo semántico.
 */
static int lsp_do_initialize(LspClient *c, const char *workspace_uri) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{"
             "\"jsonrpc\":\"2.0\","
             "\"id\":%d,"
             "\"method\":\"initialize\","
             "\"params\":{"
             "  \"processId\":null,"
             "  \"rootUri\":\"%s\","
             "  \"capabilities\":{"
             "    \"textDocument\":{"
             "      \"semanticTokens\":{"
             "        \"requests\":{\"full\":true},"
             "        \"tokenTypes\":["
             "          \"namespace\",\"type\",\"class\",\"enum\",\"interface\","
             "          \"struct\",\"typeParameter\",\"parameter\",\"variable\","
             "          \"property\",\"enumMember\",\"event\",\"function\","
             "          \"method\",\"macro\",\"keyword\",\"modifier\","
             "          \"comment\",\"string\",\"number\",\"regexp\","
             "          \"operator\",\"decorator\""
             "        ],"
             "        \"tokenModifiers\":[],"
             "        \"formats\":[\"relative\"],"
             "        \"multilineTokenSupport\":false"
             "      }"
             "    }"
             "  }"
             "}"
             "}",
             c->next_request_id++, workspace_uri);

    if (!lsp_send(&c->proc, json)) return 0;

    char *buf = (char *)malloc(LSP_READ_BUFFER_SIZE);
    if (!buf) return 0;
    char *body = NULL;
    int len = lsp_recv(&c->proc, buf, &body, LSP_REQUEST_TIMEOUT_MS);
    int ok = (len > 0 && strstr(body, "\"result\"") != NULL);
    free(buf);

    if (ok) {
        /* Enviar "initialized" notification (no espera respuesta) */
        const char *notif =
            "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}";
        lsp_send(&c->proc, notif);
        c->initialized = 1;
    }
    return ok;
}

/* API pública */

int lsp_start(LspClient *client, const char *cmd, const char *workspace_uri) {
    memset(client, 0, sizeof(*client));
    client->next_request_id = 1;
    client->doc_version = LSP_DOC_VERSION_INIT;

    /* Inicializar cache */
    client->cache.mutex = (void *)SDL_CreateMutex();
    if (!client->cache.mutex) return 0;

    /* Lanzar proceso */
#ifdef PLATFORM_POSIX
    if (!launch_posix(&client->proc, cmd)) return 0;
#else
    if (!launch_windows(&client->proc, cmd)) return 0;
#endif

    /* Pequeña pausa para que el servidor arranque */
    SDL_Delay(100);

    /* Handshake initialize */
    if (!lsp_do_initialize(client, workspace_uri)) {
        lsp_stop(client);
        return 0;
    }
    return 1;
}

void lsp_open(LspClient *client, const char *file_path,
              const char *language_id, const char *text) {
    if (!client->initialized) return;

    path_to_uri(file_path, client->uri, sizeof(client->uri));
    strncpy(client->language_id, language_id, sizeof(client->language_id) - 1);

    /* Escapar el texto del documento */
    int text_len = (int)strlen(text);
    char *escaped = (char *)malloc((size_t)(text_len * 2 + 1));
    if (!escaped) return;
    json_escape(text, escaped, text_len * 2 + 1);

    int json_size = text_len * 2 + 512;
    char *json = (char *)malloc((size_t)json_size);
    if (!json) {
        free(escaped);
        return;
    }
    snprintf(json, (size_t)json_size,
             "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\","
             "\"params\":{\"textDocument\":{"
             "\"uri\":\"%s\","
             "\"languageId\":\"%s\","
             "\"version\":%d,"
             "\"text\":\"%s\"}}}",
             client->uri, client->language_id, client->doc_version, escaped);

    lsp_send(&client->proc, json);
    free(json);
    free(escaped);
}

void lsp_change(LspClient *client, const char *text) {
    if (!client->initialized) return;
    client->doc_version++;

    int text_len = (int)strlen(text);
    char *escaped = (char *)malloc((size_t)(text_len * 2 + 1));
    if (!escaped) return;
    json_escape(text, escaped, text_len * 2 + 1);

    int json_size = text_len * 2 + 512;
    char *json = (char *)malloc((size_t)json_size);
    if (!json) {
        free(escaped);
        return;
    }
    snprintf(json, (size_t)json_size,
             "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\","
             "\"params\":{\"textDocument\":{\"uri\":\"%s\",\"version\":%d},"
             "\"contentChanges\":[{\"text\":\"%s\"}]}}",
             client->uri, client->doc_version, escaped);

    lsp_send(&client->proc, json);
    free(json);
    free(escaped);
}

int lsp_tokens_full(LspClient *client) {
    if (!client->initialized) return 0;

    int req_id = client->next_request_id++;
    char json[512];
    snprintf(json, sizeof(json),
             "{\"jsonrpc\":\"2.0\",\"id\":%d,"
             "\"method\":\"textDocument/semanticTokens/full\","
             "\"params\":{\"textDocument\":{\"uri\":\"%s\"}}}",
             req_id, client->uri);

    if (!lsp_send(&client->proc, json)) return 0;

    /* Leer respuestas hasta encontrar la que tiene nuestro id */
    char *buf = (char *)malloc(LSP_READ_BUFFER_SIZE);
    if (!buf) return 0;

    int found = 0;
    Uint64 deadline = SDL_GetTicks() + LSP_REQUEST_TIMEOUT_MS;

    while (!found && SDL_GetTicks() < deadline) {
        char *body = NULL;
        int len = lsp_recv(&client->proc, buf, &body, 200);
        if (len <= 0) continue;

        /* Verificar que es la respuesta a nuestro id */
        int resp_id = -1;
        if (!json_get_int(body, "id", &resp_id) || resp_id != req_id) continue;

        /* Extraer el array "data" */
        const char *data_arr = json_find_data_array(body);
        if (!data_arr) {
            found = 1; /* respuesta sin tokens (archivo vacío o error) */
            break;
        }

        /* El array puede tener muchos enteros: calcular un tope conservador */
        int max_ints = LSP_READ_BUFFER_SIZE / 4;
        int *raw = (int *)malloc((size_t)max_ints * sizeof(int));
        if (!raw) break;

        int count = json_read_int_array(data_arr, raw, max_ints);

        /* Expandir el formato delta → coordenadas absolutas */
        int token_count = count / 5;
        LspToken *tokens = (LspToken *)malloc((size_t)token_count * sizeof(LspToken));
        if (tokens) {
            int abs_line = 0, abs_start = 0;
            for (int i = 0; i < token_count; i++) {
                int delta_line  = raw[i * 5 + 0];
                int delta_start = raw[i * 5 + 1];
                int length      = raw[i * 5 + 2];
                int token_type  = raw[i * 5 + 3];
                /* raw[i*5+4] son modificadores, no los usamos aún */

                if (delta_line != 0) {
                    abs_line  += delta_line;
                    abs_start  = delta_start;
                } else {
                    abs_start += delta_start;
                }

                tokens[i].line = abs_line;
                tokens[i].col  = abs_start;
                tokens[i].len  = length;
                tokens[i].type = (token_type < LSP_TOK_COUNT)
                                      ? (LspSemanticType)token_type
                                      : LSP_TOK_VARIABLE;
            }

            /* Actualizar cache con mutex */
            SDL_LockMutex((SDL_Mutex *)client->cache.mutex);
            free(client->cache.tokens);
            client->cache.tokens  = tokens;
            client->cache.count   = token_count;
            client->cache.version = client->doc_version;
            client->cache.ready   = 1;
            SDL_UnlockMutex((SDL_Mutex *)client->cache.mutex);
            found = 1;
        }
        free(raw);
    }

    free(buf);
    return found;
}

void lsp_close(LspClient *client) {
    if (!client->initialized) return;
    char json[512];
    snprintf(json, sizeof(json),
             "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\","
             "\"params\":{\"textDocument\":{\"uri\":\"%s\"}}}",
             client->uri);
    lsp_send(&client->proc, json);
}

void lsp_stop(LspClient *client) {
    if (client->proc.alive) {
        /* shutdown + exit */
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"shutdown\","
                 "\"params\":null}",
                 client->next_request_id++);
        lsp_send(&client->proc, json);
        SDL_Delay(200);
        const char *exit_notif =
            "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
        lsp_send(&client->proc, exit_notif);

#ifdef PLATFORM_POSIX
        close(client->proc.stdin_fd);
        close(client->proc.stdout_fd);
        waitpid(client->proc.pid, NULL, WNOHANG);
        kill(client->proc.pid, SIGTERM);
#else
        CloseHandle(client->proc.hStdin);
        CloseHandle(client->proc.hStdout);
        TerminateProcess(client->proc.hProcess, 0);
        CloseHandle(client->proc.hProcess);
#endif
        client->proc.alive = 0;
    }

    if (client->cache.mutex) {
        SDL_DestroyMutex((SDL_Mutex *)client->cache.mutex);
        client->cache.mutex = NULL;
    }
    free(client->cache.tokens);
    client->cache.tokens = NULL;
    client->cache.count = 0;
    client->cache.ready = 0;
    client->initialized = 0;
}

/* Consulta de la cache */

void lsp_fill_line_tokens(const LspClient *client, int line, LineTokens *out) {
    out->count = 0;
    if (!client || !client->cache.ready) return;

    SDL_LockMutex((SDL_Mutex *)client->cache.mutex);
    const LspToken *toks = client->cache.tokens;
    int n = client->cache.count;

    for (int i = 0; i < n && out->count < MAX_TOKENS_PER_LINE; i++) {
        if (toks[i].line != line) continue;
        Token *t = &out->tokens[out->count++];
        t->col  = toks[i].col;
        t->len  = toks[i].len;
        t->type = lsp_map_semantic_type(toks[i].type);
    }
    SDL_UnlockMutex((SDL_Mutex *)client->cache.mutex);
}

/* Mapeo de tipos */

LexTokenType lsp_map_semantic_type(LspSemanticType t) {
    switch (t) {
    case LSP_TOK_KEYWORD:
    case LSP_TOK_MODIFIER:
        return TOK_KEYWORD;
    case LSP_TOK_TYPE:
    case LSP_TOK_CLASS:
    case LSP_TOK_STRUCT:
    case LSP_TOK_ENUM:
    case LSP_TOK_INTERFACE:
    case LSP_TOK_TYPE_PARAMETER:
        return TOK_TYPE;
    case LSP_TOK_COMMENT:
        return TOK_COMMENT;
    case LSP_TOK_STRING:
        return TOK_STRING;
    case LSP_TOK_NUMBER:
    case LSP_TOK_REGEXP:
        return TOK_NUMBER;
    case LSP_TOK_OPERATOR:
        return TOK_OPERATOR;
    case LSP_TOK_MACRO:
        return TOK_PREPROCESSOR;
    case LSP_TOK_FUNCTION:
    case LSP_TOK_METHOD:
    case LSP_TOK_NAMESPACE:
    case LSP_TOK_PROPERTY:
    case LSP_TOK_ENUM_MEMBER:
    case LSP_TOK_EVENT:
    case LSP_TOK_DECORATOR:
    case LSP_TOK_PARAMETER:
    case LSP_TOK_VARIABLE:
    default:
        return TOK_DEFAULT;
    }
}

/* Utilidades de mapeo lenguaje/servidor */

const char *lsp_language_id_for_path(const char *path) {
    if (!path) return NULL;
    const char *dot = strrchr(path, '.');
    if (!dot) return NULL;
    dot++; /* saltar el punto */

    /* C / C++ */
    if (strcmp(dot, "c") == 0 || strcmp(dot, "h") == 0)   return "c";
    if (strcmp(dot, "cpp") == 0 || strcmp(dot, "cc") == 0 ||
        strcmp(dot, "cxx") == 0 || strcmp(dot, "hpp") == 0 ||
        strcmp(dot, "hh") == 0  || strcmp(dot, "hxx") == 0) return "cpp";
    /* Python */
    if (strcmp(dot, "py") == 0)  return "python";
    /* Rust */
    if (strcmp(dot, "rs") == 0)  return "rust";
    /* JavaScript / TypeScript */
    if (strcmp(dot, "js") == 0) return "javascript";
    if (strcmp(dot, "ts") == 0) return "typescript";
    /* Go */
    if (strcmp(dot, "go") == 0) return "go";
    /* Java */
    if (strcmp(dot, "java") == 0) return "java";
    /* Ruby */
    if (strcmp(dot, "rb") == 0)  return "ruby";
    /* Lua */
    if (strcmp(dot, "lua") == 0) return "lua";
    /* Shell */
    if (strcmp(dot, "sh") == 0) return "shellscript";

    return NULL;
}

const char *lsp_server_cmd_for_language(const char *language_id) {
    if (!language_id) return NULL;
    if (strcmp(language_id, "c") == 0 || strcmp(language_id, "cpp") == 0)
        return "clangd";
    if (strcmp(language_id, "python") == 0) return "pylsp";
    if (strcmp(language_id, "rust") == 0)   return "rust-analyzer";
    if (strcmp(language_id, "javascript") == 0 ||
        strcmp(language_id, "typescript") == 0)
        return "typescript-language-server --stdio";
    if (strcmp(language_id, "go") == 0)   return "gopls";
    if (strcmp(language_id, "java") == 0) return "jdtls";
    if (strcmp(language_id, "ruby") == 0) return "solargraph stdio";
    if (strcmp(language_id, "lua") == 0)  return "lua-language-server";
    return NULL;
}
