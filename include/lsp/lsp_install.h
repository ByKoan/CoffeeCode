/**
 * @file lsp_install.h
 * @brief Instalación automática de servidores LSP bajo demanda.
 *
 * Cuando CoffeeCode abre un archivo y no encuentra el servidor LSP
 * correspondiente, este módulo lanza el comando de instalación como proceso
 * hijo no bloqueante y notifica al editor cuando termina.
 *
 * ## Flujo
 *
 *   1. editor_tab_open() llama lsp_start() → falla (servidor no encontrado).
 *   2. Llama lsp_install_async() → lanza el instalador en background.
 *   3. El bucle principal llama lsp_install_poll() cada frame.
 *   4. Cuando el instalador termina, poll() devuelve LSP_INSTALL_DONE.
 *   5. editor_tab_open() reintenta lsp_start().
 *
 * ## Detección de "no instalado"
 *
 * Antes de instalar se comprueba con lsp_server_available() si el ejecutable
 * ya está en el PATH. Si lo está, no se instala nada.
 *
 * ## Seguridad
 *
 * Solo se ejecutan los comandos de instalación definidos en la tabla interna
 * de lsp_install.c. No se ejecuta ningún comando arbitrario del usuario.
 */
#pragma once
#include <stddef.h>

/* ── Estado de una instalación en curso ────────────────────────────────── */
typedef enum {
    LSP_INSTALL_IDLE,      /* sin instalación en curso              */
    LSP_INSTALL_RUNNING,   /* el instalador está corriendo          */
    LSP_INSTALL_DONE,      /* instalación completada con éxito      */
    LSP_INSTALL_FAILED,    /* instalación fallida (ver exit_code)   */
    LSP_INSTALL_NOT_FOUND, /* no hay receta de instalación conocida */
} LspInstallStatus;

/**
 * @brief Estado de una instalación asíncrona.
 *
 * El llamante crea uno de estos en la pila o en el heap y lo pasa a
 * lsp_install_async(). Luego llama lsp_install_poll() en cada frame hasta
 * que el estado deje de ser LSP_INSTALL_RUNNING.
 */
typedef struct {
    LspInstallStatus status;
    char language_id[32];  /* lenguaje que se está instalando      */
    char cmd_display[256]; /* comando legible para mostrar al usuario */
    int  exit_code;        /* código de salida del instalador (cuando DONE/FAILED) */
#if defined(_WIN32) || defined(_WIN64)
    void *hProcess;        /* HANDLE del proceso instalador (Win32) */
#else
    int   pid;             /* PID del proceso instalador (POSIX)    */
#endif
} LspInstallJob;

/* ── API pública ──────────────────────────────────────────────────────── */

/**
 * @brief Comprueba si el ejecutable del servidor LSP está disponible en PATH.
 *
 * @param language_id  Identificador LSP del lenguaje ("c", "python"…).
 * @return 1 si el servidor se puede ejecutar; 0 si no se encuentra.
 */
int lsp_server_available(const char *language_id);

/**
 * @brief Lanza la instalación del servidor LSP en background.
 *
 * No bloquea: el instalador corre como proceso hijo y hay que sondear con
 * lsp_install_poll() hasta que termine.
 *
 * @param job          Estructura de estado que el llamante debe mantener viva
 *                     hasta que el estado sea DONE o FAILED.
 * @param language_id  Lenguaje cuyo servidor instalar.
 * @return LSP_INSTALL_RUNNING si arrancó, LSP_INSTALL_NOT_FOUND si no hay
 *         receta, LSP_INSTALL_FAILED si el proceso no pudo lanzarse.
 */
LspInstallStatus lsp_install_async(LspInstallJob *job, const char *language_id);

/**
 * @brief Sondea el estado de una instalación en curso.
 *
 * Debe llamarse periódicamente (p. ej. una vez por frame) mientras
 * job->status == LSP_INSTALL_RUNNING. Actualiza job->status y job->exit_code
 * cuando el proceso hijo termina.
 *
 * @param job  Trabajo de instalación iniciado con lsp_install_async().
 * @return     El estado actualizado del trabajo.
 */
LspInstallStatus lsp_install_poll(LspInstallJob *job);

/**
 * @brief Devuelve el comando de instalación legible para mostrar al usuario.
 *
 * @param language_id  Identificador LSP del lenguaje.
 * @return Cadena estática con el comando (p. ej. "pip install python-lsp-server")
 *         o NULL si no hay receta conocida.
 */
const char *lsp_install_cmd_display(const char *language_id);
