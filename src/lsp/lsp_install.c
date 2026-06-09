/**
 * @file lsp_install.c
 * @brief Instalación automática de servidores LSP bajo demanda.
 *
 * Cada receta define:
 *   - check_cmd:   ejecutable a buscar en PATH para saber si ya está instalado.
 *   - install_cmd: comando de instalación (se pasa a sh/cmd).
 *   - display:     texto legible para mostrar al usuario.
 *
 * La detección de "disponible" usa `which`/`where` en POSIX/Win32 en vez de
 * intentar ejecutar el servidor directamente, para no lanzar procesos LSP
 * innecesarios.
 */
#include "lsp/lsp_install.h"
#include "lsp/lsp.h" /* lsp_server_cmd_for_language */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#define PLATFORM_POSIX 1
#include <fcntl.h> /* open, O_WRONLY */
#include <sys/wait.h>
#include <unistd.h>
#endif

/* Tabla de recetas
 *
 * check_cmd:    ejecutable principal a buscar con which/where.
 * install_argv: argumentos para sh -c / cmd /C (el instalador real).
 * display:      texto para mostrar al usuario en la UI.
 */
typedef struct {
    const char *language_id;
    const char *check_cmd;    /* ejecutable a buscar en PATH           */
    const char *install_sh;   /* comando para Linux/macOS (sh -c "...")*/
    const char *install_cmd;  /* comando para Windows   (cmd /C "...") */
    const char *display;      /* texto legible                         */
} InstallRecipe;

static const InstallRecipe RECIPES[] = {
    {
        "c", "clangd",
        "apt-get install -y clangd 2>/dev/null || "
        "brew install llvm 2>/dev/null || "
        "dnf install -y clang-tools-extra 2>/dev/null || "
        "pacman -S --noconfirm clang 2>/dev/null",
        "winget install LLVM.LLVM",
        "apt install clangd  /  brew install llvm  /  winget install LLVM.LLVM"
    },
    {
        "cpp", "clangd",
        "apt-get install -y clangd 2>/dev/null || "
        "brew install llvm 2>/dev/null || "
        "dnf install -y clang-tools-extra 2>/dev/null || "
        "pacman -S --noconfirm clang 2>/dev/null",
        "winget install LLVM.LLVM",
        "apt install clangd  /  brew install llvm  /  winget install LLVM.LLVM"
    },
    {
        "python", "pylsp",
        "pip install python-lsp-server",
        "pip install python-lsp-server",
        "pip install python-lsp-server"
    },
    {
        "rust", "rust-analyzer",
        "rustup component add rust-analyzer",
        "rustup component add rust-analyzer",
        "rustup component add rust-analyzer"
    },
    {
        "go", "gopls",
        "go install golang.org/x/tools/gopls@latest",
        "go install golang.org/x/tools/gopls@latest",
        "go install golang.org/x/tools/gopls@latest"
    },
    {
        "javascript", "typescript-language-server",
        "npm install -g typescript-language-server typescript",
        "npm install -g typescript-language-server typescript",
        "npm install -g typescript-language-server typescript"
    },
    {
        "typescript", "typescript-language-server",
        "npm install -g typescript-language-server typescript",
        "npm install -g typescript-language-server typescript",
        "npm install -g typescript-language-server typescript"
    },
    {
        "lua", "lua-language-server",
        "brew install lua-language-server 2>/dev/null || "
        "apt-get install -y lua-language-server 2>/dev/null",
        "winget install lua-language-server",
        "brew install lua-language-server  /  winget install lua-language-server"
    },
    {
        "ruby", "solargraph",
        "gem install solargraph",
        "gem install solargraph",
        "gem install solargraph"
    },
    {
        "shellscript", "bash-language-server",
        "npm install -g bash-language-server",
        "npm install -g bash-language-server",
        "npm install -g bash-language-server"
    },
    { NULL, NULL, NULL, NULL, NULL } /* centinela */
};

/* Búsqueda de receta */

static const InstallRecipe *find_recipe(const char *language_id) {
    if (!language_id) return NULL;
    for (int i = 0; RECIPES[i].language_id; i++)
        if (strcmp(RECIPES[i].language_id, language_id) == 0)
            return &RECIPES[i];
    return NULL;
}

/* Detección de ejecutable en PATH */

int lsp_server_available(const char *language_id) {
    const char *cmd = lsp_server_cmd_for_language(language_id);
    if (!cmd) return 0;

    /* Extraer solo el nombre del ejecutable (primer token antes del espacio) */
    char exe[128];
    strncpy(exe, cmd, sizeof(exe) - 1);
    exe[sizeof(exe) - 1] = '\0';
    char *sp = strchr(exe, ' ');
    if (sp) *sp = '\0';

#ifdef PLATFORM_WINDOWS
    /* En Windows: where.exe devuelve 0 si lo encuentra */
    char check[256];
    snprintf(check, sizeof(check), "where \"%s\" >NUL 2>&1", exe);
    return (system(check) == 0);
#else
    /* En POSIX: which devuelve 0 si lo encuentra */
    char check[256];
    snprintf(check, sizeof(check), "which \"%s\" >/dev/null 2>&1", exe);
    return (system(check) == 0);
#endif
}

const char *lsp_install_cmd_display(const char *language_id) {
    const InstallRecipe *r = find_recipe(language_id);
    return r ? r->display : NULL;
}

/* Lanzamiento asíncrono */

#ifdef PLATFORM_POSIX
static LspInstallStatus launch_posix(LspInstallJob *job,
                                     const char *install_sh) {
    pid_t pid = fork();
    if (pid < 0) return LSP_INSTALL_FAILED;
    if (pid == 0) {
        /* hijo: redirigir stdout/stderr a /dev/null para no contaminar */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execl("/bin/sh", "sh", "-c", install_sh, NULL);
        _exit(1);
    }
    job->pid = (int)pid;
    return LSP_INSTALL_RUNNING;
}
#endif

#ifdef PLATFORM_WINDOWS
static LspInstallStatus launch_windows(LspInstallJob *job,
                                       const char *install_cmd) {
    char full_cmd[512];
    snprintf(full_cmd, sizeof(full_cmd), "cmd /C \"%s\" >NUL 2>&1", install_cmd);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    if (!CreateProcessA(NULL, full_cmd, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return LSP_INSTALL_FAILED;

    CloseHandle(pi.hThread);
    job->hProcess = pi.hProcess;
    return LSP_INSTALL_RUNNING;
}
#endif

LspInstallStatus lsp_install_async(LspInstallJob *job,
                                   const char *language_id) {
    memset(job, 0, sizeof(*job));
    strncpy(job->language_id, language_id, sizeof(job->language_id) - 1);
    job->status = LSP_INSTALL_IDLE;

    const InstallRecipe *r = find_recipe(language_id);
    if (!r) {
        job->status = LSP_INSTALL_NOT_FOUND;
        return LSP_INSTALL_NOT_FOUND;
    }

    strncpy(job->cmd_display, r->display, sizeof(job->cmd_display) - 1);

#ifdef PLATFORM_POSIX
    job->status = launch_posix(job, r->install_sh);
#else
    job->status = launch_windows(job, r->install_cmd);
#endif
    return job->status;
}

/* Sondeo del proceso hijo */

LspInstallStatus lsp_install_poll(LspInstallJob *job) {
    if (job->status != LSP_INSTALL_RUNNING) return job->status;

#ifdef PLATFORM_POSIX
    int wstatus = 0;
    pid_t r = waitpid((pid_t)job->pid, &wstatus, WNOHANG);
    if (r == 0) return LSP_INSTALL_RUNNING; /* aún corriendo */
    if (r < 0) {
        job->status = LSP_INSTALL_FAILED;
        return job->status;
    }
    job->exit_code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 1;
    job->status = (job->exit_code == 0) ? LSP_INSTALL_DONE : LSP_INSTALL_FAILED;
#else
    DWORD result = WaitForSingleObject(job->hProcess, 0);
    if (result == WAIT_TIMEOUT) return LSP_INSTALL_RUNNING;
    DWORD exit_code = 1;
    GetExitCodeProcess(job->hProcess, &exit_code);
    CloseHandle(job->hProcess);
    job->hProcess = NULL;
    job->exit_code = (int)exit_code;
    job->status = (exit_code == 0) ? LSP_INSTALL_DONE : LSP_INSTALL_FAILED;
#endif
    return job->status;
}
