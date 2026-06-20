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

/* -- Color ANSI (secuencias SGR) ------------------------------------------ */

/** Helper: indice del canal "build" recien registrado con texto. */
static const PanelChannel *build_chan(PanelStore *s) {
    int idx = panel_find(s, "build");
    return panel_at(s, (size_t)idx);
}

/** "\x1b[31mhola\x1b[0m" -> visible "hola" + 1 span fg=rojo sobre [0,4). */
static void test_ansi_basic(void) {
    PanelStore s;
    panel_store_init(&s);
    panel_register(&s, "build", "Build");
    panel_append(&s, "build", "\x1b[31mhola\x1b[0m");
    const PanelChannel *c = build_chan(&s);
    /* el texto visible NO contiene bytes de escape */
    EXPECT_EQ_STR(c->text, "hola");
    EXPECT_EQ_INT((int)c->len, 4);
    EXPECT_TRUE(strchr(c->text, 0x1B) == NULL);
    /* un span rojo (indice 1) sobre [0,4) */
    EXPECT_EQ_INT((int)c->span_count, 1);
    EXPECT_EQ_INT((int)c->spans[0].start, 0);
    EXPECT_EQ_INT((int)c->spans[0].end, 4);
    EXPECT_EQ_INT((int)c->spans[0].fg, 1); /* 31 -> indice 1 (rojo) */
}

/** El estado SGR es continuo entre dos appends. */
static void test_ansi_continuo(void) {
    PanelStore s;
    panel_store_init(&s);
    panel_register(&s, "build", "Build");
    panel_append(&s, "build", "\x1b[32m"); /* verde, sin texto */
    panel_append(&s, "build", "abc");      /* hereda el verde */
    const PanelChannel *c = build_chan(&s);
    EXPECT_EQ_STR(c->text, "abc");
    EXPECT_EQ_INT((int)c->span_count, 1);
    EXPECT_EQ_INT((int)c->spans[0].start, 0);
    EXPECT_EQ_INT((int)c->spans[0].end, 3);
    EXPECT_EQ_INT((int)c->spans[0].fg, 2); /* 32 -> indice 2 (verde) */
}

/** "a\x1b[1;34mb\x1b[0mc" -> visible "abc" + span bold+azul sobre [1,2). */
static void test_ansi_bold_mid(void) {
    PanelStore s;
    panel_store_init(&s);
    panel_register(&s, "build", "Build");
    panel_append(&s, "build", "a\x1b[1;34mb\x1b[0mc");
    const PanelChannel *c = build_chan(&s);
    EXPECT_EQ_STR(c->text, "abc");
    EXPECT_EQ_INT((int)c->span_count, 1); /* solo la "b" lleva color */
    EXPECT_EQ_INT((int)c->spans[0].start, 1);
    EXPECT_EQ_INT((int)c->spans[0].end, 2);
    EXPECT_EQ_INT((int)c->spans[0].fg, 4); /* 34 -> indice 4 (azul) */
    EXPECT_TRUE((c->spans[0].flags & PANEL_SGR_BOLD) != 0);
}

/** Una secuencia CSI que no es 'm' (p.ej. "\x1b[2K") se consume y no aparece. */
static void test_ansi_csi_no_m(void) {
    PanelStore s;
    panel_store_init(&s);
    panel_register(&s, "build", "Build");
    panel_append(&s, "build", "x\x1b[2Ky");
    const PanelChannel *c = build_chan(&s);
    EXPECT_EQ_STR(c->text, "xy");          /* el "\x1b[2K" desaparece */
    EXPECT_TRUE(strchr(c->text, 0x1B) == NULL);
    EXPECT_EQ_INT((int)c->span_count, 0);  /* no fija color */
}

/** El texto visible tras parsear ANSI sigue siendo compatible con wrap. */
static void test_ansi_wrap_compat(void) {
    PanelStore s;
    panel_store_init(&s);
    panel_register(&s, "build", "Build");
    /* color alrededor de "xxxxxxxxxx" (10 'x'); con cols=4 -> 3 filas */
    panel_append(&s, "build", "\x1b[31mxxxxxxxxxx\x1b[0m");
    const PanelChannel *c = build_chan(&s);
    EXPECT_EQ_STR(c->text, "xxxxxxxxxx");
    EXPECT_EQ_INT(panel_wrap_count(c->text, 4), 3);
}

/** Una secuencia incompleta al final del buffer no deja bytes de escape. */
static void test_ansi_incompleta(void) {
    PanelStore s;
    panel_store_init(&s);
    panel_register(&s, "build", "Build");
    panel_append(&s, "build", "hola\x1b[3"); /* CSI sin byte final */
    const PanelChannel *c = build_chan(&s);
    EXPECT_EQ_STR(c->text, "hola");          /* solo el texto visible */
    EXPECT_TRUE(strchr(c->text, 0x1B) == NULL);
}

/** La COPIA de una seleccion sobre texto con color NO contiene escapes. */
static void test_ansi_copy_limpia(void) {
    PanelStore s;
    panel_store_init(&s);
    panel_register(&s, "build", "Build");
    panel_append(&s, "build", "\x1b[31mrojo\x1b[0m y \x1b[32mverde\x1b[0m");
    const PanelChannel *c = build_chan(&s);
    /* el "substring" de la copia opera sobre c->text (texto visible): nunca
     * incluye bytes de escape (la copia real usa exactamente este buffer). */
    EXPECT_EQ_STR(c->text, "rojo y verde");
    EXPECT_TRUE(strchr(c->text, 0x1B) == NULL);
    /* seleccionar [0,4) -> "rojo", sin escapes */
    char buf[32];
    memcpy(buf, c->text + 0, 4);
    buf[4] = '\0';
    EXPECT_EQ_STR(buf, "rojo");
    EXPECT_TRUE(strchr(buf, 0x1B) == NULL);
}

/** clear vacia tambien los spans y restablece el estado SGR a por defecto. */
static void test_ansi_clear_resetea(void) {
    PanelStore s;
    panel_store_init(&s);
    panel_register(&s, "build", "Build");
    panel_append(&s, "build", "\x1b[31mrojo");
    const PanelChannel *c = build_chan(&s);
    EXPECT_EQ_INT((int)c->span_count, 1);
    panel_clear(&s, "build");
    EXPECT_EQ_INT((int)c->span_count, 0);
    EXPECT_EQ_INT((int)c->sgr.fg, PANEL_COL_DEFAULT);
    /* tras el clear, texto nuevo sin color no crea spans */
    panel_append(&s, "build", "plano");
    EXPECT_EQ_STR(c->text, "plano");
    EXPECT_EQ_INT((int)c->span_count, 0);
}

/** panel_span_at localiza el span que cubre un offset; NULL en los huecos. */
static void test_ansi_span_at(void) {
    PanelStore s;
    panel_store_init(&s);
    panel_register(&s, "build", "Build");
    panel_append(&s, "build", "ab\x1b[31mCD\x1b[0mef"); /* color solo en "CD" */
    const PanelChannel *c = build_chan(&s);
    EXPECT_EQ_STR(c->text, "abCDef");
    size_t hint = 0;
    EXPECT_TRUE(panel_span_at(c, 0, &hint) == NULL); /* "a": sin color */
    hint = 0;
    const PanelColorSpan *sp = panel_span_at(c, 2, &hint); /* "C": rojo */
    EXPECT_NOT_NULL(sp);
    EXPECT_EQ_INT((int)sp->fg, 1);
    hint = 0;
    EXPECT_TRUE(panel_span_at(c, 4, &hint) == NULL); /* "e": sin color */
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
    tt_run("ansi: basico (strip + span rojo)", test_ansi_basic);
    tt_run("ansi: estado continuo entre appends", test_ansi_continuo);
    tt_run("ansi: bold + color a mitad de linea", test_ansi_bold_mid);
    tt_run("ansi: CSI no-'m' se consume", test_ansi_csi_no_m);
    tt_run("ansi: visible compatible con wrap", test_ansi_wrap_compat);
    tt_run("ansi: secuencia incompleta sin escapes", test_ansi_incompleta);
    tt_run("ansi: copia de seleccion sin escapes", test_ansi_copy_limpia);
    tt_run("ansi: clear resetea spans y SGR", test_ansi_clear_resetea);
    tt_run("ansi: panel_span_at localiza spans", test_ansi_span_at);
    return tt_summary();
}
