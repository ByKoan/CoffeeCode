/**
 * @file app_terminal.c
 * @brief Terminal integrada en el panel inferior del IDE.
 *
 * Lanza un proceso hijo (cmd.exe en Windows, bash en Linux/macOS) con tuberías
 * anónimas conectadas a su stdin/stdout/stderr.  La salida del proceso se
 * vuelca en el canal «terminal» del PanelStore y el usuario puede escribir
 * comandos en la línea de input del panel.
 *
 * Plataformas:
 *   - Windows : CreatePipe + CreateProcess con handles heredables.
 *   - POSIX   : pipe(2) + fork(2) + execvp(3); las lecturas son no bloqueantes
 *               (O_NONBLOCK) para que term_pump() no detenga el hilo principal.
 */

#include "app/app.h"
#include "panel/panel.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Implementación Windows */
#ifdef _WIN32
#include <windows.h>

/** Inicializa los campos de terminal del editor a «sin terminal». */
static void term_reset(Editor *e) {
    e->term_proc  = NULL;
    e->term_read  = NULL;
    e->term_write = NULL;
    e->term_input_len = 0;
    e->term_input[0]  = '\0';
}

int term_start(Editor *e, const char *path) {
    if (e->term_proc) term_stop(e); /* cerrar la anterior si la hubiera */

    /* Tuberías: stdout/stderr del hijo → nos llega por hRead */
    HANDLE hReadStdout, hWriteStdout;
    HANDLE hReadStdin,  hWriteStdin;

    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE}; /* heredable */

    if (!CreatePipe(&hReadStdout, &hWriteStdout, &sa, 0)) return 0;
    SetHandleInformation(hReadStdout, HANDLE_FLAG_INHERIT, 0); /* no hereda el lado lectura */

    if (!CreatePipe(&hReadStdin, &hWriteStdin, &sa, 0)) {
        CloseHandle(hReadStdout); CloseHandle(hWriteStdout);
        return 0;
    }
    SetHandleInformation(hWriteStdin, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput   = hReadStdin;
    si.hStdOutput  = hWriteStdout;
    si.hStdError   = hWriteStdout; /* stderr al mismo pipe */

    /* Directorio de trabajo */
    const char *work = (path && path[0]) ? path : NULL;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    /* cmd.exe /Q suprime el eco del comando en la salida */
    if (!CreateProcessA(NULL, "cmd.exe /Q", NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, work, &si, &pi)) {
        CloseHandle(hReadStdout); CloseHandle(hWriteStdout);
        CloseHandle(hReadStdin);  CloseHandle(hWriteStdin);
        return 0;
    }

    /* Cerramos los handles que el hijo usa; el padre solo necesita los suyos */
    CloseHandle(hWriteStdout);
    CloseHandle(hReadStdin);
    CloseHandle(pi.hThread);

    e->term_proc  = pi.hProcess;
    e->term_read  = hReadStdout;
    e->term_write = hWriteStdin;
    e->term_input_len = 0;
    e->term_input[0]  = '\0';

    panel_clear(&e->panels, "terminal");
    panel_append(&e->panels, "terminal", "Terminal conectada.\r\n");
    return 1;
}

int term_pump(Editor *e) {
    if (!e->term_proc) return 0;

    /* Verificar si el proceso sigue vivo */
    DWORD exit_code;
    if (GetExitCodeProcess(e->term_proc, &exit_code) &&
        exit_code != STILL_ACTIVE) {
        panel_append(&e->panels, "terminal", "\r\n[Proceso terminado]\r\n");
        term_stop(e);
        return 0;
    }

    DWORD avail = 0;
    if (!PeekNamedPipe(e->term_read, NULL, 0, NULL, &avail, NULL) || avail == 0)
        return 0;

    char buf[4096];
    DWORD read_bytes = 0;
    DWORD to_read = avail < sizeof(buf) - 1 ? avail : sizeof(buf) - 1;
    if (!ReadFile(e->term_read, buf, to_read, &read_bytes, NULL) || read_bytes == 0)
        return 0;

    buf[read_bytes] = '\0';
    panel_append(&e->panels, "terminal", buf);
    return (int)read_bytes;
}

void term_send(Editor *e, const char *text) {
    if (!e->term_proc || !text) return;
    DWORD written;
    WriteFile(e->term_write, text, (DWORD)strlen(text), &written, NULL);
}

void term_stop(Editor *e) {
    if (e->term_proc) {
        TerminateProcess(e->term_proc, 0);
        CloseHandle(e->term_proc);
        e->term_proc = NULL;
    }
    if (e->term_read)  { CloseHandle(e->term_read);  e->term_read  = NULL; }
    if (e->term_write) { CloseHandle(e->term_write); e->term_write = NULL; }
    e->term_input_len = 0;
    e->term_input[0]  = '\0';
}

/* Implementación POSIX (Linux / macOS) */
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

static void term_reset(Editor *e) {
    e->term_pid      = -1;
    e->term_read_fd  = -1;
    e->term_write_fd = -1;
    e->term_input_len = 0;
    e->term_input[0]  = '\0';
}

int term_start(Editor *e, const char *path) {
    if (e->term_pid != -1) term_stop(e);

    /* pipe stdout_pipe: [0]=lectura padre, [1]=escritura hijo */
    int stdout_pipe[2], stdin_pipe[2];
    if (pipe(stdout_pipe) != 0) return 0;
    if (pipe(stdin_pipe)  != 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return 0;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stdin_pipe[0]);  close(stdin_pipe[1]);
        return 0;
    }

    if (pid == 0) {
        /* Proceso hijo */
        /* Redirigir stdin desde el pipe */
        dup2(stdin_pipe[0],  STDIN_FILENO);
        /* Redirigir stdout y stderr al pipe del padre */
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);

        /* Cerrar todos los fds heredados que ya no necesitamos */
        close(stdin_pipe[0]);  close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);

        /* Cambiar al directorio de trabajo */
        if (path && path[0]) chdir(path);

        /* Ejecutar shell interactivo */
        const char *shell = getenv("SHELL");
        if (!shell || shell[0] == '\0') shell = "/bin/bash";

        char *args[] = { (char*)shell, NULL };
        execvp(shell, args);

        /* Si execvp falla, intentar bash directamente */
        char *fallback[] = { "/bin/bash", NULL };
        execvp("/bin/bash", fallback);
        _exit(1);
    }

    /* Proceso padre */
    close(stdin_pipe[0]);   /* el hijo lee de aquí; el padre no lo necesita */
    close(stdout_pipe[1]);  /* el hijo escribe aquí; el padre no lo necesita */

    /* Hacer la lectura del stdout del hijo no bloqueante */
    int flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

    e->term_pid      = pid;
    e->term_read_fd  = stdout_pipe[0];
    e->term_write_fd = stdin_pipe[1];
    e->term_input_len = 0;
    e->term_input[0]  = '\0';

    panel_clear(&e->panels, "terminal");
    panel_append(&e->panels, "terminal", "Terminal conectada.\r\n");
    return 1;
}

int term_pump(Editor *e) {
    if (e->term_pid == -1) return 0;

    /* Verificar si el proceso hijo terminó sin bloquearnos */
    int status;
    pid_t result = waitpid(e->term_pid, &status, WNOHANG);
    if (result == e->term_pid) {
        panel_append(&e->panels, "terminal", "\r\n[Proceso terminado]\r\n");
        term_stop(e);
        return 0;
    }

    /* Leer toda la salida pendiente (no bloqueante gracias a O_NONBLOCK) */
    char buf[4096];
    int total = 0;
    ssize_t n;
    while ((n = read(e->term_read_fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        panel_append(&e->panels, "terminal", buf);
        total += (int)n;
    }
    return total;
}

void term_send(Editor *e, const char *text) {
    if (e->term_pid == -1 || !text) return;
    size_t len = strlen(text);
    while (len > 0) {
        ssize_t n = write(e->term_write_fd, text, len);
        if (n <= 0) break;
        text += n;
        len  -= (size_t)n;
    }
}

void term_stop(Editor *e) {
    if (e->term_pid != -1) {
        kill(e->term_pid, SIGTERM);
        waitpid(e->term_pid, NULL, 0);
        e->term_pid = -1;
    }
    if (e->term_read_fd  != -1) { close(e->term_read_fd);  e->term_read_fd  = -1; }
    if (e->term_write_fd != -1) { close(e->term_write_fd); e->term_write_fd = -1; }
    e->term_input_len = 0;
    e->term_input[0]  = '\0';
}

#endif /* _WIN32 */
