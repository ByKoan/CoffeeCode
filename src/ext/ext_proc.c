/**
 * @file ext_proc.c
 * @brief Implementacion del facility de subprocesos asincronos del core.
 *
 * Una extension pide al core lanzar un proceso (p.ej. un servidor LSP); el core
 * lo crea con stdin/stdout redirigidos por pipes, arranca un hilo lector por
 * proceso que vuelca el stdout del hijo en una cola GLOBAL protegida por mutex,
 * y despierta el bucle principal con un evento de usuario SDL.  El bucle drena
 * esa cola con @ref ext_proc_pump e invoca los callbacks de la extension en el
 * HILO PRINCIPAL (nunca desde el hilo lector), de modo que la extension no toca
 * la concurrencia.
 *
 * El estado es global al proceso (un IDE = un facility).  Toda la sincronizacion
 * usa SDL (SDL_Thread / SDL_Mutex / eventos de usuario), disponible siempre que
 * SDL este enlazado; las primitivas de hilo no requieren subsistema de video.
 */
#include "ext/ext_proc.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

/* -- Pipes y procesos por plataforma --------------------------------------- */
#if defined(_WIN32)
#include <windows.h>
typedef HANDLE coffee_pipe_t;     /**< extremo de pipe nativo */
typedef HANDLE coffee_prochnd_t;  /**< handle del proceso hijo */
#define COFFEE_PIPE_INVALID NULL
#else
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
typedef int coffee_pipe_t;        /**< descriptor de pipe POSIX */
typedef pid_t coffee_prochnd_t;   /**< pid del hijo */
#define COFFEE_PIPE_INVALID (-1)
#endif

/* ===========================================================================
 *  Estructuras internas
 * =========================================================================== */

/** @brief Un proceso hijo gestionado por el facility. */
struct CoffeeProcImpl {
    coffee_prochnd_t proc;       /**< handle/pid del hijo */
    coffee_pipe_t child_stdin_w; /**< extremo de escritura del stdin del hijo */
    coffee_pipe_t child_stdout_r;/**< extremo de lectura del stdout del hijo */

    SDL_Thread *reader; /**< hilo lector de stdout (NULL si no arranco) */

    /* callbacks de la extension; SOLO se invocan en el hilo principal */
    CoffeeProcDataFn on_data;
    void *on_data_ud;
    CoffeeProcExitFn on_exit;
    void *on_exit_ud;

    int alive;       /**< 1 mientras el hijo no ha terminado de drenarse */
    int exit_code;   /**< codigo de salida capturado por el hilo lector */
    int kill_done;   /**< 1 si ya se libero (evita doble free/join) */
};

/** Tipo de item encolado por el hilo lector hacia el hilo principal. */
typedef enum {
    PROC_ITEM_DATA = 0, /**< chunk de stdout */
    PROC_ITEM_EXIT      /**< el hijo termino (lleva exit_code) */
} ProcItemKind;

/** @brief Un evento de un proceso pendiente de entregar al hilo principal. */
typedef struct {
    CoffeeProc proc;   /**< proceso origen */
    ProcItemKind kind; /**< tipo de item */
    char *bytes;       /**< copia del chunk (solo DATA); heap, lo libera el pump */
    size_t len;        /**< longitud del chunk */
    int exit_code;     /**< codigo de salida (solo EXIT) */
} ProcItem;

/** @brief Un tick periodico registrado por una extension. */
typedef struct {
    CoffeeTickFn fn;
    void *ud;
} ProcTick;

/* ===========================================================================
 *  Estado global del facility
 * =========================================================================== */

static struct {
    int inited;            /**< 1 tras ext_proc_init */
    SDL_Mutex *lock;       /**< protege la cola y la tabla de procesos/ticks */

    /* cola global de items (hilo lector -> hilo principal) */
    ProcItem *queue;
    size_t q_count, q_cap;

    /* tabla de procesos vivos (para shutdown ordenado) */
    CoffeeProc *procs;
    size_t p_count, p_cap;

    /* ticks periodicos */
    ProcTick *ticks;
    size_t t_count, t_cap;

    unsigned int wakeup_event; /**< tipo de evento de usuario SDL (0 = sin SDL) */
} G;

/* -- util: crece un array generico si esta lleno (capacidad x2) ------------- */
static int proc_grow(void **arr, size_t *cap, size_t count, size_t elem) {
    if (count < *cap) return 1;
    size_t ncap = (*cap == 0) ? 8 : (*cap * 2);
    void *np = realloc(*arr, ncap * elem);
    if (!np) return 0;
    *arr = np;
    *cap = ncap;
    return 1;
}

/* ===========================================================================
 *  Inicializacion
 * =========================================================================== */

void ext_proc_init(void) {
    if (G.inited) return;
    memset(&G, 0, sizeof G);
    G.lock = SDL_CreateMutex();
    /* Registrar un tipo de evento de usuario para el wakeup del bucle.  Si SDL
     * no esta listo (no deberia), wakeup_event queda en 0 y el bucle drena por
     * su timeout. */
    Uint32 ev = SDL_RegisterEvents(1);
    G.wakeup_event = (ev == (Uint32)-1) ? 0u : (unsigned int)ev;
    G.inited = 1;
}

unsigned int ext_proc_wakeup_event(void) { return G.wakeup_event; }

/* Despierta el hilo principal empujando el evento de usuario (best-effort). */
static void proc_wakeup_main(void) {
    if (!G.wakeup_event) return;
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = G.wakeup_event;
    SDL_PushEvent(&e); /* si falla (cola llena) el timeout del bucle lo cubre */
}

/* ===========================================================================
 *  Cola global (productor: hilo lector; consumidor: hilo principal)
 * =========================================================================== */

/* Encola un item bajo el mutex y despierta el bucle.  Toma posesion de @p bytes
 * (lo libera el pump).  Devuelve 1 si encolo, 0 si no (sin memoria). */
static int proc_enqueue(CoffeeProc p, ProcItemKind kind, char *bytes, size_t len,
                        int exit_code) {
    SDL_LockMutex(G.lock);
    if (!proc_grow((void **)&G.queue, &G.q_cap, G.q_count, sizeof(ProcItem))) {
        SDL_UnlockMutex(G.lock);
        free(bytes);
        return 0;
    }
    ProcItem *it = &G.queue[G.q_count++];
    it->proc = p;
    it->kind = kind;
    it->bytes = bytes;
    it->len = len;
    it->exit_code = exit_code;
    SDL_UnlockMutex(G.lock);
    proc_wakeup_main();
    return 1;
}

/* ===========================================================================
 *  Hilo lector de stdout (uno por proceso)
 * =========================================================================== */

/* Lee stdout del hijo hasta EOF/error y encola cada chunk.  Al terminar, captura
 * el codigo de salida y encola un item EXIT.  Corre en su propio SDL_Thread. */
static int SDLCALL proc_reader_thread(void *arg) {
    CoffeeProc p = (CoffeeProc)arg;
    char buf[4096];

    for (;;) {
#if defined(_WIN32)
        DWORD n = 0;
        BOOL ok = ReadFile(p->child_stdout_r, buf, (DWORD)sizeof(buf), &n, NULL);
        if (!ok || n == 0) break; /* EOF (pipe cerrado por el hijo) o error */
        size_t got = (size_t)n;
#else
        ssize_t r = read(p->child_stdout_r, buf, sizeof(buf));
        if (r < 0) {
            if (errno == EINTR) continue; /* reintentar tras senal */
            break;
        }
        if (r == 0) break; /* EOF */
        size_t got = (size_t)r;
#endif
        char *copy = (char *)malloc(got);
        if (!copy) continue; /* sin memoria para el chunk: descartar, seguir */
        memcpy(copy, buf, got);
        proc_enqueue(p, PROC_ITEM_DATA, copy, got, 0);
    }

    /* el hijo cerro su stdout: esperar su fin y capturar el codigo de salida */
    int code = 0;
#if defined(_WIN32)
    WaitForSingleObject(p->proc, INFINITE);
    DWORD ec = 0;
    if (GetExitCodeProcess(p->proc, &ec)) code = (int)ec;
#else
    int status = 0;
    while (waitpid(p->proc, &status, 0) < 0 && errno == EINTR) {
        /* reintentar si una senal interrumpio la espera */
    }
    if (WIFEXITED(status)) code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) code = 128 + WTERMSIG(status);
#endif
    p->exit_code = code;
    proc_enqueue(p, PROC_ITEM_EXIT, NULL, 0, code);
    return 0;
}

/* ===========================================================================
 *  Spawn
 * =========================================================================== */

/* Registra @p p en la tabla global de procesos vivos. */
static void proc_track(CoffeeProc p) {
    SDL_LockMutex(G.lock);
    if (proc_grow((void **)&G.procs, &G.p_cap, G.p_count, sizeof(CoffeeProc)))
        G.procs[G.p_count++] = p;
    SDL_UnlockMutex(G.lock);
}

/* Quita @p p de la tabla global (swap-remove). */
static void proc_untrack(CoffeeProc p) {
    SDL_LockMutex(G.lock);
    for (size_t i = 0; i < G.p_count; ++i) {
        if (G.procs[i] == p) {
            G.procs[i] = G.procs[--G.p_count];
            break;
        }
    }
    SDL_UnlockMutex(G.lock);
}

#if defined(_WIN32)
/* Construye la linea de comando Win32 citando cada argumento que lo necesite.
 * Devuelve un buffer heap (el llamante lo libera) o NULL sin memoria. */
static char *win_build_cmdline(const char *exe, const char *const *argv,
                               int argc) {
    /* estimacion holgada del tamano: cada arg puede duplicarse por escapes */
    size_t cap = strlen(exe) * 2 + 4;
    for (int i = 0; i < argc; ++i) cap += (argv[i] ? strlen(argv[i]) : 0) * 2 + 4;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    size_t pos = 0;

    /* recorre exe (argv[0]) y luego cada argumento; cita el token entre comillas
     * si contiene espacios/tabs/comillas y escapa las comillas internas. */
    for (int i = -1; i < argc; ++i) {
        const char *a = (i < 0) ? exe : (argv[i] ? argv[i] : "");
        if (pos > 0) out[pos++] = ' ';
        int needs_quote = (a[0] == '\0');
        for (const char *c = a; *c; ++c)
            if (*c == ' ' || *c == '\t' || *c == '"') { needs_quote = 1; break; }
        if (needs_quote) out[pos++] = '"';
        for (const char *c = a; *c; ++c) {
            if (*c == '"') out[pos++] = '\\'; /* escapar comilla interna */
            out[pos++] = *c;
        }
        if (needs_quote) out[pos++] = '"';
    }
    out[pos] = '\0';
    return out;
}
#endif

CoffeeProc ext_proc_spawn(const char *exe, const char *const *argv, int argc) {
    if (!G.inited) ext_proc_init();
    if (!exe || !exe[0]) return NULL;

    CoffeeProc p = (CoffeeProc)calloc(1, sizeof(struct CoffeeProcImpl));
    if (!p) return NULL;
    p->child_stdin_w = COFFEE_PIPE_INVALID;
    p->child_stdout_r = COFFEE_PIPE_INVALID;

#if defined(_WIN32)
    /* Pipes: el padre escribe en stdin_w (el hijo lee de stdin_r); el hijo
     * escribe en stdout_w (el padre lee de stdout_r).  Los extremos que hereda
     * el hijo deben ser heredables; los que se queda el padre, NO. */
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;

    HANDLE stdin_r = NULL, stdin_w = NULL, stdout_r = NULL, stdout_w = NULL;
    if (!CreatePipe(&stdin_r, &stdin_w, &sa, 0) ||
        !CreatePipe(&stdout_r, &stdout_w, &sa, 0)) {
        if (stdin_r) CloseHandle(stdin_r);
        if (stdin_w) CloseHandle(stdin_w);
        if (stdout_r) CloseHandle(stdout_r);
        if (stdout_w) CloseHandle(stdout_w);
        free(p);
        return NULL;
    }
    /* el padre no debe heredar SUS extremos (stdin_w / stdout_r) */
    SetHandleInformation(stdin_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_r, HANDLE_FLAG_INHERIT, 0);

    char *cmdline = win_build_cmdline(exe, argv, argc);
    if (!cmdline) {
        CloseHandle(stdin_r); CloseHandle(stdin_w);
        CloseHandle(stdout_r); CloseHandle(stdout_w);
        free(p);
        return NULL;
    }

    STARTUPINFOA si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdin_r;
    si.hStdOutput = stdout_w;
    si.hStdError = stdout_w; /* stderr del hijo al mismo pipe que stdout */

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmdline);
    /* el padre cierra los extremos que ya heredo el hijo */
    CloseHandle(stdin_r);
    CloseHandle(stdout_w);
    if (!ok) {
        CloseHandle(stdin_w);
        CloseHandle(stdout_r);
        free(p);
        return NULL;
    }
    CloseHandle(pi.hThread); /* no usamos el hilo principal del hijo */
    p->proc = pi.hProcess;
    p->child_stdin_w = stdin_w;
    p->child_stdout_r = stdout_r;
#else
    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
        if (in_pipe[0] >= 0) close(in_pipe[0]);
        if (in_pipe[1] >= 0) close(in_pipe[1]);
        if (out_pipe[0] >= 0) close(out_pipe[0]);
        if (out_pipe[1] >= 0) close(out_pipe[1]);
        free(p);
        return NULL;
    }

    /* redirecciones del hijo: stdin <- in_pipe[0], stdout/stderr -> out_pipe[1] */
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in_pipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&fa, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, out_pipe[1], STDERR_FILENO);
    /* cerrar en el hijo los extremos del padre y los duplicados originales */
    posix_spawn_file_actions_addclose(&fa, in_pipe[1]);
    posix_spawn_file_actions_addclose(&fa, out_pipe[0]);
    posix_spawn_file_actions_addclose(&fa, in_pipe[0]);
    posix_spawn_file_actions_addclose(&fa, out_pipe[1]);

    /* construir argv del hijo: [exe, argv..., NULL] */
    int total = argc + 2;
    char **child_argv = (char **)malloc((size_t)total * sizeof(char *));
    if (!child_argv) {
        posix_spawn_file_actions_destroy(&fa);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        free(p);
        return NULL;
    }
    child_argv[0] = (char *)exe;
    for (int i = 0; i < argc; ++i) child_argv[i + 1] = (char *)(argv ? argv[i] : NULL);
    child_argv[total - 1] = NULL;

    pid_t pid = 0;
    int rc = posix_spawnp(&pid, exe, &fa, NULL, child_argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    free(child_argv);
    /* el padre cierra los extremos que usa el hijo */
    close(in_pipe[0]);
    close(out_pipe[1]);
    if (rc != 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        free(p);
        return NULL;
    }
    p->proc = pid;
    p->child_stdin_w = in_pipe[1];
    p->child_stdout_r = out_pipe[0];
#endif

    p->alive = 1;
    proc_track(p);

    /* arrancar el hilo lector ya con el proceso registrado */
    p->reader = SDL_CreateThread(proc_reader_thread, "coffee-proc-reader", p);
    if (!p->reader) {
        /* sin hilo lector no hay forma de drenar el hijo: matarlo y limpiar */
        ext_proc_kill(p);
        return NULL;
    }
    return p;
}

/* ===========================================================================
 *  Escritura a stdin del hijo
 * =========================================================================== */

int ext_proc_write(CoffeeProc p, const void *bytes, size_t len) {
    if (!p || p->child_stdin_w == COFFEE_PIPE_INVALID) return -1;
    if (!bytes || len == 0) return 0;
    const char *src = (const char *)bytes;
    size_t off = 0;
    while (off < len) {
#if defined(_WIN32)
        DWORD n = 0;
        if (!WriteFile(p->child_stdin_w, src + off, (DWORD)(len - off), &n, NULL))
            return -1;
        if (n == 0) return -1;
        off += (size_t)n;
#else
        ssize_t n = write(p->child_stdin_w, src + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
#endif
    }
    return (int)off;
}

/* ===========================================================================
 *  Registro de callbacks
 * =========================================================================== */

void ext_proc_on_data(CoffeeProc p, CoffeeProcDataFn cb, void *ud) {
    if (!p) return;
    SDL_LockMutex(G.lock);
    p->on_data = cb;
    p->on_data_ud = ud;
    SDL_UnlockMutex(G.lock);
}

void ext_proc_on_exit(CoffeeProc p, CoffeeProcExitFn cb, void *ud) {
    if (!p) return;
    SDL_LockMutex(G.lock);
    p->on_exit = cb;
    p->on_exit_ud = ud;
    SDL_UnlockMutex(G.lock);
}

/* ===========================================================================
 *  Kill / liberacion
 * =========================================================================== */

/* Libera los recursos del proceso: mata el hijo, hace join del hilo lector,
 * cierra los pipes y libera la struct.  Idempotente via kill_done. */
static void proc_dispose(CoffeeProc p) {
    if (!p || p->kill_done) return;
    p->kill_done = 1;

    /* terminar el hijo si sigue vivo; al cerrarse su stdout el hilo lector
     * sale del bucle de lectura y luego del waitpid/WaitForSingleObject. */
#if defined(_WIN32)
    if (p->proc) TerminateProcess(p->proc, 1);
#else
    if (p->proc > 0) kill(p->proc, SIGKILL);
#endif

    /* cerrar el stdin del padre: ayuda a que el hijo note EOF en su entrada */
#if defined(_WIN32)
    if (p->child_stdin_w != COFFEE_PIPE_INVALID) {
        CloseHandle(p->child_stdin_w);
        p->child_stdin_w = COFFEE_PIPE_INVALID;
    }
#else
    if (p->child_stdin_w != COFFEE_PIPE_INVALID) {
        close(p->child_stdin_w);
        p->child_stdin_w = COFFEE_PIPE_INVALID;
    }
#endif

    /* join del hilo lector ANTES de cerrar el stdout_r que el lee */
    if (p->reader) {
        SDL_WaitThread(p->reader, NULL);
        p->reader = NULL;
    }

#if defined(_WIN32)
    if (p->child_stdout_r != COFFEE_PIPE_INVALID) {
        CloseHandle(p->child_stdout_r);
        p->child_stdout_r = COFFEE_PIPE_INVALID;
    }
    if (p->proc) {
        CloseHandle(p->proc);
        p->proc = NULL;
    }
#else
    if (p->child_stdout_r != COFFEE_PIPE_INVALID) {
        close(p->child_stdout_r);
        p->child_stdout_r = COFFEE_PIPE_INVALID;
    }
#endif

    free(p);
}

/* Descarta de la cola global los items que apuntan a @p p (tras liberarlo no se
 * pueden entregar).  Debe llamarse con el mutex tomado. */
static void proc_drop_queued_locked(CoffeeProc p) {
    for (size_t i = 0; i < G.q_count;) {
        if (G.queue[i].proc == p) {
            free(G.queue[i].bytes);
            G.queue[i] = G.queue[--G.q_count]; /* swap-remove */
        } else {
            ++i;
        }
    }
}

void ext_proc_kill(CoffeeProc p) {
    if (!p) return;
    proc_untrack(p);
    SDL_LockMutex(G.lock);
    proc_drop_queued_locked(p);
    SDL_UnlockMutex(G.lock);
    proc_dispose(p);
}

/* ===========================================================================
 *  Ticks periodicos
 * =========================================================================== */

void ext_proc_register_tick(CoffeeTickFn cb, void *ud) {
    if (!cb) return;
    if (!G.inited) ext_proc_init();
    SDL_LockMutex(G.lock);
    if (proc_grow((void **)&G.ticks, &G.t_cap, G.t_count, sizeof(ProcTick))) {
        G.ticks[G.t_count].fn = cb;
        G.ticks[G.t_count].ud = ud;
        G.t_count++;
    }
    SDL_UnlockMutex(G.lock);
}

void ext_proc_unregister_tick(CoffeeTickFn cb, void *ud) {
    if (!cb || !G.inited) return;
    SDL_LockMutex(G.lock);
    for (size_t i = 0; i < G.t_count;) {
        if (G.ticks[i].fn == cb && G.ticks[i].ud == ud)
            G.ticks[i] = G.ticks[--G.t_count];
        else
            ++i;
    }
    SDL_UnlockMutex(G.lock);
}

void ext_proc_run_ticks(void) {
    if (!G.inited) return;
    /* copiar los ticks bajo el mutex y ejecutarlos fuera de el (un tick podria
     * registrar/quitar ticks o tocar el facility). */
    SDL_LockMutex(G.lock);
    size_t n = G.t_count;
    if (n == 0) {
        SDL_UnlockMutex(G.lock);
        return;
    }
    ProcTick *snap = (ProcTick *)malloc(n * sizeof(ProcTick));
    if (!snap) {
        SDL_UnlockMutex(G.lock);
        return;
    }
    memcpy(snap, G.ticks, n * sizeof(ProcTick));
    SDL_UnlockMutex(G.lock);

    for (size_t i = 0; i < n; ++i)
        if (snap[i].fn) snap[i].fn(snap[i].ud);
    free(snap);
}

/* ===========================================================================
 *  Drenado de la cola en el hilo principal
 * =========================================================================== */

int ext_proc_pump(void) {
    if (!G.inited) return 0;

    /* sacar TODOS los items bajo el mutex a un snapshot local; procesarlos
     * fuera del mutex para no invocar callbacks de la extension con el lock
     * tomado (podrian re-entrar al facility). */
    SDL_LockMutex(G.lock);
    size_t n = G.q_count;
    if (n == 0) {
        SDL_UnlockMutex(G.lock);
        return 0;
    }
    ProcItem *snap = (ProcItem *)malloc(n * sizeof(ProcItem));
    if (!snap) {
        SDL_UnlockMutex(G.lock);
        return 0;
    }
    memcpy(snap, G.queue, n * sizeof(ProcItem));
    G.q_count = 0; /* los bytes ahora son propiedad del snapshot */
    SDL_UnlockMutex(G.lock);

    int processed = 0;
    for (size_t i = 0; i < n; ++i) {
        ProcItem *it = &snap[i];
        CoffeeProc p = it->proc;
        if (it->kind == PROC_ITEM_DATA) {
            if (p && p->on_data && !p->kill_done)
                p->on_data(p->on_data_ud, it->bytes, it->len);
            free(it->bytes);
        } else { /* PROC_ITEM_EXIT */
            if (p && !p->kill_done) {
                if (p->on_exit) p->on_exit(p->on_exit_ud, it->exit_code);
                p->alive = 0;
                /* el hijo termino por si mismo: liberar sus recursos (join del
                 * hilo lector, cerrar pipes).  No quedan mas items suyos porque
                 * EXIT es el ultimo que el hilo lector encola. */
                proc_untrack(p);
                proc_dispose(p);
            }
        }
        processed++;
    }
    free(snap);
    return processed;
}

/* ===========================================================================
 *  Apagado del facility
 * =========================================================================== */

void ext_proc_shutdown(void) {
    if (!G.inited) return;

    /* tomar una copia de los procesos vivos y liberarlos fuera del mutex
     * (proc_dispose hace join, que no debe correr con el lock tomado). */
    SDL_LockMutex(G.lock);
    size_t n = G.p_count;
    CoffeeProc *snap = NULL;
    if (n > 0) {
        snap = (CoffeeProc *)malloc(n * sizeof(CoffeeProc));
        if (snap) memcpy(snap, G.procs, n * sizeof(CoffeeProc));
    }
    G.p_count = 0;
    /* descartar la cola pendiente (sus procesos van a morir) */
    for (size_t i = 0; i < G.q_count; ++i) free(G.queue[i].bytes);
    G.q_count = 0;
    SDL_UnlockMutex(G.lock);

    if (snap) {
        for (size_t i = 0; i < n; ++i) proc_dispose(snap[i]);
        free(snap);
    }

    SDL_LockMutex(G.lock);
    free(G.queue);
    free(G.procs);
    free(G.ticks);
    G.queue = NULL; G.q_count = G.q_cap = 0;
    G.procs = NULL; G.p_count = G.p_cap = 0;
    G.ticks = NULL; G.t_count = G.t_cap = 0;
    SDL_UnlockMutex(G.lock);

    SDL_DestroyMutex(G.lock);
    G.lock = NULL;
    G.inited = 0;
    G.wakeup_event = 0;
}
