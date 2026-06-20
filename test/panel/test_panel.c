/**
 * @file test_panel.c
 * @brief Pruebas unitarias del almacen de canales del panel inferior.
 *
 * Ejercita el PanelStore puro (panel/panel.h), que no depende de SDL ni de la
 * struct Editor, asi que corre en headless.  Verifica:
 *   1. Inicializacion: registra los canales integrados (Salida/Logs/Terminal),
 *      el primero es "salida".
 *   2. Registro idempotente de canales.
 *   3. channel_append acumula texto; channel_clear lo vacia.
 *   4. output_append por defecto cae en el canal "salida".
 */
#include "ctests.h"
#include "panel/panel.h"
#include <string.h>

/** Al iniciar hay 3 canales integrados y "salida" es el primero. */
static void test_init_builtins(void) {
    PanelStore s;
    panel_store_init(&s);
    EXPECT_EQ_INT((int)s.count, 3);
    const PanelChannel *c0 = panel_at(&s, 0);
    EXPECT_NOT_NULL(c0);
    EXPECT_EQ_STR(c0->id, "salida");
    EXPECT_EQ_INT(c0->builtin, 1);
    /* el canal por defecto existe y se localiza por id */
    EXPECT_TRUE(panel_find(&s, PANEL_DEFAULT_CHANNEL) >= 0);
    EXPECT_TRUE(panel_find(&s, "logs") >= 0);
    EXPECT_TRUE(panel_find(&s, "terminal") >= 0);
    EXPECT_EQ_INT(panel_find(&s, "inexistente"), -1);
}

/** Registrar un canal nuevo lo anyade; registrar uno existente es idempotente. */
static void test_register_idempotente(void) {
    PanelStore s;
    panel_store_init(&s);
    size_t before = s.count;
    int idx = panel_register(&s, "build", "Compilacion");
    EXPECT_TRUE(idx >= 0);
    EXPECT_EQ_INT((int)s.count, (int)before + 1);
    /* re-registrar el mismo id NO crea otro canal; refresca el titulo */
    int idx2 = panel_register(&s, "build", "Build");
    EXPECT_EQ_INT(idx2, idx);
    EXPECT_EQ_INT((int)s.count, (int)before + 1);
    EXPECT_EQ_STR(panel_at(&s, (size_t)idx)->title, "Build");
}

/** channel_append acumula y channel_clear vacia. */
static void test_append_clear(void) {
    PanelStore s;
    panel_store_init(&s);
    panel_register(&s, "build", "Build");
    panel_append(&s, "build", "linea 1\n");
    panel_append(&s, "build", "linea 2\n");
    int idx = panel_find(&s, "build");
    EXPECT_TRUE(idx >= 0);
    const PanelChannel *c = panel_at(&s, (size_t)idx);
    EXPECT_EQ_STR(c->text, "linea 1\nlinea 2\n");
    panel_clear(&s, "build");
    EXPECT_EQ_STR(panel_at(&s, (size_t)idx)->text, "");
    EXPECT_EQ_INT((int)panel_at(&s, (size_t)idx)->len, 0);
}

/** Anyadir a un canal inexistente lo crea al vuelo. */
static void test_append_crea_al_vuelo(void) {
    PanelStore s;
    panel_store_init(&s);
    size_t before = s.count;
    panel_append(&s, "nuevo", "hola");
    EXPECT_EQ_INT((int)s.count, (int)before + 1);
    int idx = panel_find(&s, "nuevo");
    EXPECT_TRUE(idx >= 0);
    EXPECT_EQ_STR(panel_at(&s, (size_t)idx)->text, "hola");
}

/** output_append (canal por defecto) cae en "salida". */
static void test_default_channel(void) {
    PanelStore s;
    panel_store_init(&s);
    panel_append(&s, PANEL_DEFAULT_CHANNEL, "salida de un compilador\n");
    int idx = panel_find(&s, "salida");
    EXPECT_TRUE(idx >= 0);
    EXPECT_CONTAINS(panel_at(&s, (size_t)idx)->text, "compilador");
}

/** El scrollback nunca crece sin limite: descarta la cabecera mas antigua. */
static void test_scrollback_acotado(void) {
    PanelStore s;
    panel_store_init(&s);
    panel_register(&s, "spam", "Spam");
    /* anyadir mucho mas que la capacidad: el canal queda en su tope */
    char chunk[256];
    memset(chunk, 'x', sizeof(chunk) - 1);
    chunk[sizeof(chunk) - 1] = '\0';
    for (int i = 0; i < 1000; ++i) panel_append(&s, "spam", chunk);
    int idx = panel_find(&s, "spam");
    const PanelChannel *c = panel_at(&s, (size_t)idx);
    EXPECT_TRUE(c->len <= PANEL_CHAN_CAP - 1);
    EXPECT_EQ_INT((int)c->text[c->len], 0); /* siempre null-terminado */
}

int main(void) {
    tt_suite("panel");
    tt_run("init: canales integrados", test_init_builtins);
    tt_run("registro idempotente", test_register_idempotente);
    tt_run("append + clear", test_append_clear);
    tt_run("append crea canal al vuelo", test_append_crea_al_vuelo);
    tt_run("canal por defecto (salida)", test_default_channel);
    tt_run("scrollback acotado", test_scrollback_acotado);
    return tt_summary();
}
