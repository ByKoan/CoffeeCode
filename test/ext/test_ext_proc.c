/**
 * @file test_ext_proc.c
 * @brief Smoke test HEADLESS del facility de subprocesos asincronos del core.
 *
 * Ejercita la capa interna ext_proc_* SIN GUI (no abre ventana): lanza un
 * proceso conocido y portable, drena la cola con ext_proc_pump en un bucle y
 * verifica que:
 *   1. el callback de datos recibio la salida del hijo ("coffee_proc_ok");
 *   2. el callback de fin disparo con codigo 0;
 *   3. tras ext_proc_kill sobre otro proceso no quedan recursos colgados
 *      (el shutdown del facility completa sin bloquearse).
 *
 * El facility usa SDL para hilos/mutex/eventos: las primitivas de hilo no
 * requieren subsistema de video, por eso el test corre headless.  El evento de
 * wakeup necesita el subsistema de eventos, asi que inicializamos SDL_EVENTS;
 * si no estuviera disponible, el drenado seguiria por el bucle de polling.
 */
#include "ctests.h"
#include "ext/ext_proc.h"

#include <SDL3/SDL.h>
#include <string.h>

/** Acumulador del callback de datos (hilo principal). */
typedef struct {
    char buf[4096];
    size_t len;
    int exited;
    int exit_code;
} Collector;

static void on_data(void *ud, const char *bytes, size_t len) {
    Collector *c = (Collector *)ud;
    if (c->len + len < sizeof(c->buf)) {
        memcpy(c->buf + c->len, bytes, len);
        c->len += len;
        c->buf[c->len] = '\0';
    }
}

static void on_exit(void *ud, int code) {
    Collector *c = (Collector *)ud;
    c->exited = 1;
    c->exit_code = code;
}

/* Bombea la cola hasta que el hijo termina o se agota el tiempo de espera. */
static void pump_until_exit(Collector *c, int timeout_ms) {
    int waited = 0;
    while (!c->exited && waited < timeout_ms) {
        if (ext_proc_pump() == 0) {
            SDL_Delay(5);
            waited += 5;
        }
    }
}

/**
 * @brief Lanza el proceso de prueba portable y devuelve el comando + args.
 *
 * Windows: "cmd /c echo coffee_proc_ok".
 * POSIX:   "/bin/echo coffee_proc_ok".
 */
static CoffeeProc spawn_echo(void) {
#if defined(_WIN32)
    const char *argv[] = {"/c", "echo", "coffee_proc_ok"};
    return ext_proc_spawn("cmd", argv, 3);
#else
    const char *argv[] = {"coffee_proc_ok"};
    return ext_proc_spawn("/bin/echo", argv, 1);
#endif
}

/**
 * @brief Spawn -> recibe stdout via on_data -> recibe fin via on_exit (code 0).
 */
static void test_spawn_echo_data_exit(void) {
    ext_proc_init();

    Collector c;
    memset(&c, 0, sizeof c);

    CoffeeProc p = spawn_echo();
    EXPECT_NOT_NULL(p);
    if (!p) return;

    ext_proc_on_data(p, on_data, &c);
    ext_proc_on_exit(p, on_exit, &c);

    pump_until_exit(&c, 5000);

    /* on_data recibio la salida del hijo (substring "coffee_proc_ok") */
    EXPECT_TRUE(strstr(c.buf, "coffee_proc_ok") != NULL);
    /* on_exit disparo con codigo 0 */
    EXPECT_TRUE(c.exited);
    EXPECT_EQ_INT(c.exit_code, 0);

    /* el proceso ya se libero al entregar EXIT; el shutdown no debe colgar */
    ext_proc_shutdown();
}

/**
 * @brief ext_proc_kill sobre un proceso vivo libera sin colgarse.
 *
 * Lanza el echo y lo mata de inmediato (puede o no haber emitido datos aun);
 * lo importante es que kill hace join del hilo lector + cierra pipes sin
 * bloquear, y que el shutdown posterior tampoco se cuelga.
 */
static void test_kill_no_leak(void) {
    ext_proc_init();

    Collector c;
    memset(&c, 0, sizeof c);

    CoffeeProc p = spawn_echo();
    EXPECT_NOT_NULL(p);
    if (!p) {
        ext_proc_shutdown();
        return;
    }
    ext_proc_on_data(p, on_data, &c);
    ext_proc_on_exit(p, on_exit, &c);

    /* matar de inmediato: kill debe completar (join + cierre) sin colgarse */
    ext_proc_kill(p);

    /* drenar lo que pudiera haber quedado encolado: tras kill los items de ese
     * proceso se descartan, asi que no debe entregarse on_exit. */
    ext_proc_pump();

    /* shutdown sin procesos vivos: completa limpio */
    ext_proc_shutdown();
    EXPECT_TRUE(1); /* llegar aqui sin bloquear ya es el exito */
}

/**
 * @brief Los ticks registrados se ejecutan en ext_proc_run_ticks.
 */
static int g_tick_calls = 0;
static void on_tick(void *ud) {
    (void)ud;
    g_tick_calls++;
}

static void test_ticks(void) {
    ext_proc_init();
    g_tick_calls = 0;

    ext_proc_register_tick(on_tick, NULL);
    ext_proc_run_ticks();
    ext_proc_run_ticks();
    EXPECT_EQ_INT(g_tick_calls, 2);

    /* quitar el tick: deja de ejecutarse */
    ext_proc_unregister_tick(on_tick, NULL);
    ext_proc_run_ticks();
    EXPECT_EQ_INT(g_tick_calls, 2);

    ext_proc_shutdown();
}

int main(void) {
    /* Subsistema de eventos para el wakeup del facility (sin video: headless). */
    SDL_Init(SDL_INIT_EVENTS);

    tt_suite("ext_proc");
    tt_run("spawn echo -> on_data recibe stdout + on_exit con code 0",
           test_spawn_echo_data_exit);
    tt_run("kill libera sin colgarse (join + cierre de pipes)",
           test_kill_no_leak);
    tt_run("ticks por frame se ejecutan y se pueden quitar", test_ticks);
    int rc = tt_summary();

    SDL_Quit();
    return rc;
}
