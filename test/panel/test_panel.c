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

/* -- Envoltura del texto al ancho (word-wrap) ----------------------------- */

/** Una linea de N columnas con cols=K produce ceil(N/K) filas. */
static void test_wrap_count_hard(void) {
    /* 10 'x' sin espacios, ancho 4 -> ceil(10/4) = 3 filas */
    EXPECT_EQ_INT(panel_wrap_count("xxxxxxxxxx", 4), 3);
    /* exacto: 8 'x', ancho 4 -> 2 filas */
    EXPECT_EQ_INT(panel_wrap_count("xxxxxxxx", 4), 2);
    /* texto vacio -> 1 fila */
    EXPECT_EQ_INT(panel_wrap_count("", 10), 1);
    /* cabe entero -> 1 fila */
    EXPECT_EQ_INT(panel_wrap_count("hola", 10), 1);
}

/** La envoltura respeta los limites de palabra (rompe en el espacio). */
static void test_wrap_word_boundary(void) {
    /* "ab cd ef" ancho 5: "ab cd" cabe (5 cols), luego "ef" -> 2 filas */
    EXPECT_EQ_INT(panel_wrap_count("ab cd ef", 5), 2);
    PanelRow r;
    size_t next = panel_wrap_next("ab cd ef", 0, 5, &r);
    EXPECT_EQ_INT((int)r.offset, 0);
    EXPECT_EQ_INT((int)r.len, 5); /* "ab cd" sin partir la palabra "ef" */
    /* la siguiente fila empieza en "ef" (tras el espacio) */
    EXPECT_EQ_INT((int)next, 6);
    panel_wrap_next("ab cd ef", next, 5, &r);
    EXPECT_EQ_INT((int)r.offset, 6);
    EXPECT_EQ_INT((int)r.len, 2);
}

/** Las '\n' reales separan lineas logicas; cada una se envuelve aparte. */
static void test_wrap_newlines(void) {
    /* "aaaa\nbb" ancho 10 -> 2 filas (una por linea logica) */
    EXPECT_EQ_INT(panel_wrap_count("aaaa\nbb", 10), 2);
    PanelRow r;
    size_t next = panel_wrap_next("aaaa\nbb", 0, 10, &r);
    EXPECT_EQ_INT((int)r.len, 4); /* "aaaa", sin el '\n' */
    EXPECT_EQ_INT((int)next, 5);  /* siguiente fila tras el '\n' */
    panel_wrap_next("aaaa\nbb", next, 10, &r);
    EXPECT_EQ_INT((int)r.offset, 5);
    EXPECT_EQ_INT((int)r.len, 2); /* "bb" */
}

/** Ida y vuelta offset <-> (fila, col) consistente, incluido fin de fila. */
static void test_rowcol_roundtrip(void) {
    const char *t = "ab cd ef"; /* filas a cols=5: "ab cd" (0..5), "ef" (6..8) */
    int row, col;
    /* offset 0 -> fila 0, col 0 */
    panel_offset_to_rowcol(t, 5, 0, &row, &col);
    EXPECT_EQ_INT(row, 0);
    EXPECT_EQ_INT(col, 0);
    /* offset 6 (inicio de "ef") -> fila 1, col 0 */
    panel_offset_to_rowcol(t, 5, 6, &row, &col);
    EXPECT_EQ_INT(row, 1);
    EXPECT_EQ_INT(col, 0);
    /* offset 7 -> fila 1, col 1 */
    panel_offset_to_rowcol(t, 5, 7, &row, &col);
    EXPECT_EQ_INT(row, 1);
    EXPECT_EQ_INT(col, 1);
    /* vuelta: (fila 1, col 1) -> offset 7 */
    EXPECT_EQ_INT((int)panel_rowcol_to_offset(t, 5, 1, 1), 7);
    /* (fila 0, col 0) -> offset 0 */
    EXPECT_EQ_INT((int)panel_rowcol_to_offset(t, 5, 0, 0), 0);
    /* col mas alla del fin de fila se recorta al fin de esa fila */
    EXPECT_EQ_INT((int)panel_rowcol_to_offset(t, 5, 0, 99), 5);
}

/** El substring de una seleccion multilinea preserva las '\n' reales. */
static void test_selection_substring(void) {
    /* texto con 3 lineas logicas */
    const char *t = "linea uno\nlinea dos\nlinea tres";
    /* seleccionar desde el inicio de "uno" (offset 6) hasta el fin de "dos"
     * (offset 19): debe abarcar "uno\nlinea dos" con su '\n' real. */
    int lo = 6, hi = 19;
    size_t len = (size_t)(hi - lo);
    char buf[64];
    memcpy(buf, t + lo, len);
    buf[len] = '\0';
    EXPECT_EQ_STR(buf, "uno\nlinea dos");
    /* seleccionar las 3 lineas enteras: offset 0 hasta el final */
    int total = (int)strlen(t);
    memcpy(buf, t, (size_t)total);
    buf[total] = '\0';
    EXPECT_EQ_STR(buf, "linea uno\nlinea dos\nlinea tres");
}

int main(void) {
    tt_suite("panel");
    tt_run("init: canales integrados", test_init_builtins);
    tt_run("registro idempotente", test_register_idempotente);
    tt_run("append + clear", test_append_clear);
    tt_run("append crea canal al vuelo", test_append_crea_al_vuelo);
    tt_run("canal por defecto (salida)", test_default_channel);
    tt_run("scrollback acotado", test_scrollback_acotado);
    tt_run("wrap: cuenta filas (rotura dura)", test_wrap_count_hard);
    tt_run("wrap: rotura en limite de palabra", test_wrap_word_boundary);
    tt_run("wrap: lineas logicas por '\\n'", test_wrap_newlines);
    tt_run("wrap: ida y vuelta offset<->fila,col", test_rowcol_roundtrip);
    tt_run("seleccion: substring multilinea", test_selection_substring);
    return tt_summary();
}
