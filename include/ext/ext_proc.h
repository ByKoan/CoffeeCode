/**
 * @file ext_proc.h
 * @brief Facility de subprocesos asincronos del core para las extensiones.
 *
 * Las extensiones NO manejan hilos: el core lanza el proceso hijo, redirige su
 * stdin/stdout por pipes, lo lee en un hilo dedicado y entrega los datos y la
 * salida del hijo a la extension SIEMPRE en el hilo principal (via una cola
 * global drenada una vez por frame).  Es el cimiento para que una extension
 * actue de cliente de un servidor externo (p.ej. un servidor LSP) sin tocar
 * la concurrencia.
 *
 * Modelo de hilos:
 *   - El hilo principal (el del bucle de la app) crea/escribe/mata procesos y
 *     recibe los callbacks (on_data / on_exit).
 *   - Un hilo lector POR proceso lee su stdout y encola los chunks en una cola
 *     global protegida por mutex; tras encolar despierta el bucle principal con
 *     un evento de usuario SDL para que no espere el timeout completo.
 *   - @ref ext_proc_pump drena la cola en el hilo principal e invoca los
 *     callbacks; @ref ext_proc_run_ticks corre los callbacks periodicos.
 *
 * El estado es GLOBAL al proceso (un IDE = un facility): no depende del host
 * concreto, lo que evita tener que enhebrar punteros por toda la API.
 */
#ifndef COFFEE_EXT_PROC_H
#define COFFEE_EXT_PROC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Handle opaco de un proceso hijo lanzado por el facility. */
typedef struct CoffeeProcImpl *CoffeeProc;

/** Callback (hilo principal) con un chunk de stdout del hijo. */
typedef void (*CoffeeProcDataFn)(void *ud, const char *bytes, size_t len);

/** Callback (hilo principal) cuando el hijo termina, con su codigo de salida. */
typedef void (*CoffeeProcExitFn)(void *ud, int exit_code);

/** Callback periodico (hilo principal), invocado una vez por frame. */
typedef void (*CoffeeTickFn)(void *ud);

/**
 * @brief Inicializa el facility (idempotente).
 *
 * Registra el evento de usuario SDL para el wakeup del bucle y prepara las
 * estructuras globales.  El bucle de la app la llama una vez al arrancar; si
 * SDL no esta listo, el facility sigue funcionando (solo no habra wakeup por
 * evento y el drenado depende del timeout del bucle).
 */
void ext_proc_init(void);

/**
 * @brief Lanza un proceso con stdin/stdout redirigidos por pipes.
 *
 * @param exe  Ruta del ejecutable.
 * @param argv Argumentos (argv[0..argc-1]); puede ser NULL si argc==0.  El
 *             ejecutable se antepone automaticamente como argv[0] del hijo.
 * @param argc Numero de elementos de @p argv.
 * @return Handle del proceso, o NULL si fallo el lanzamiento.
 */
CoffeeProc ext_proc_spawn(const char *exe, const char *const *argv, int argc);

/**
 * @brief Escribe @p len bytes a stdin del hijo (escritura completa en bucle).
 * @return Bytes escritos, o -1 en error / handle invalido.
 */
int ext_proc_write(CoffeeProc p, const void *bytes, size_t len);

/** @brief Registra el callback de datos de stdout (invocado en hilo principal). */
void ext_proc_on_data(CoffeeProc p, CoffeeProcDataFn cb, void *ud);

/** @brief Registra el callback de fin del hijo (invocado en hilo principal). */
void ext_proc_on_exit(CoffeeProc p, CoffeeProcExitFn cb, void *ud);

/**
 * @brief Mata el hijo y libera sus recursos (hilo lector, pipes, handle).
 *
 * Es seguro llamarla mas de una vez sobre el mismo handle; tras ella el handle
 * deja de ser valido para el llamante.
 */
void ext_proc_kill(CoffeeProc p);

/** @brief Registra un callback periodico llamado una vez por frame. */
void ext_proc_register_tick(CoffeeTickFn cb, void *ud);

/** @brief Quita un tick registrado (por callback+userdata).  No falla si no esta. */
void ext_proc_unregister_tick(CoffeeTickFn cb, void *ud);

/**
 * @brief Drena la cola global en el HILO PRINCIPAL e invoca on_data/on_exit.
 *
 * Saca los items bajo el mutex y los procesa fuera de el.  Llamala una vez por
 * iteracion del bucle de la app.  @return numero de items procesados (>0 indica
 * que hubo actividad y conviene repintar).
 */
int ext_proc_pump(void);

/** @brief Ejecuta todos los ticks registrados (una vez por frame). */
void ext_proc_run_ticks(void);

/**
 * @brief Tipo del evento de usuario SDL que el hilo lector usa para despertar
 *        el bucle, o 0 si aun no se registro.  Lo consume el bucle para saber
 *        que un evento es un wakeup del facility (no hace falta procesarlo mas
 *        alla de drenar con ext_proc_pump).
 */
unsigned int ext_proc_wakeup_event(void);

/**
 * @brief Termina todos los procesos vivos y libera el facility.
 *
 * El bucle de la app la llama al cerrar: mata cada hijo, hace join de los hilos
 * lectores y cierra los handles.  Sin procesos huerfanos tras esto.
 */
void ext_proc_shutdown(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* COFFEE_EXT_PROC_H */
