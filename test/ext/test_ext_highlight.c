/**
 * @file test_ext_highlight.c
 * @brief Test HEADLESS del resaltado controlado por extensiones (ABI v4).
 *
 * Verifica, sin DLLs ni SDL de video, el sistema de resaltado del host:
 *   1. register_highlighter por extension de archivo + resolucion por path
 *      (ext_host_has_highlighter / ext_host_highlight_line), encadenando el
 *      estado de comentario de bloque entre lineas.
 *   2. El almacen de tramos PUSHED por-buffer (set_tokens / clear_tokens):
 *      prioridad por linea, copia del contenido, limpieza por count=0 y al
 *      soltar el buffer (ext_host_drop_buffer).
 *
 * Registra funciones de prueba en proceso via la vtable del CoffeeApi (igual que
 * hace el lenguaje C embebido), sin cargar ninguna DLL.
 */
#include "ctests.h"
#include "ext/ext_host.h"

#include <string.h>

/* Resaltador de prueba: marca toda la linea como UN tramo rojo, y propaga un
 * estado de bloque ficticio (si la linea contiene '{' abre bloque; si contiene
 * '}' lo cierra) para verificar el encadenado in_block/out_block. */
static int fake_hl(void *ud, const char *line, int len, int in_block,
                   CoffeeSpan *out, int max_out, int *out_block) {
    (void)ud;
    int blk = in_block;
    for (int i = 0; i < len; i++) {
        if (line[i] == '{') blk = 1;
        else if (line[i] == '}') blk = 0;
    }
    if (out_block) *out_block = blk;
    if (out && max_out > 0 && len > 0) {
        out[0].start_col = 0;
        out[0].len = (uint32_t)len; /* test ASCII: columnas == bytes */
        out[0].color.r = 0xFF;
        out[0].color.g = 0x00;
        out[0].color.b = 0x00;
        out[0].color.a = 0xFF;
        return 1;
    }
    return 0;
}

/** register_highlighter + resolucion por extension + estado de bloque. */
static void test_register_y_resolver(void) {
    Buffer b;
    EXPECT_TRUE(buf_init(&b));
    CoffeeHostBackend backend;
    memset(&backend, 0, sizeof backend);
    backend.buffer = &b;
    CoffeeHost *host = ext_host_create(&backend);
    EXPECT_NOT_NULL(host);

    /* sin registrar: no hay resaltador para .foo */
    EXPECT_FALSE(ext_host_has_highlighter(host, "x.foo"));

    const CoffeeApi *api = ext_host_api(host);
    EXPECT_NOT_NULL(api);
    EXPECT_NOT_NULL(api->register_highlighter);
    const char *exts[] = {".foo", ".bar"};
    EXPECT_EQ_INT(api->register_highlighter(host, exts, 2, fake_hl, NULL), 0);

    /* ahora si resuelve para .foo y .bar (case-insensitive), no para .txt */
    EXPECT_TRUE(ext_host_has_highlighter(host, "main.foo"));
    EXPECT_TRUE(ext_host_has_highlighter(host, "MAIN.FOO")); /* case-insensitive */
    EXPECT_TRUE(ext_host_has_highlighter(host, "x.bar"));
    EXPECT_FALSE(ext_host_has_highlighter(host, "notas.txt"));
    EXPECT_FALSE(ext_host_has_highlighter(host, "sinpunto"));

    /* resaltar una linea: un tramo rojo que cubre toda la linea */
    CoffeeSpan out[8];
    int blk = 0;
    int n = ext_host_highlight_line(host, "x.foo", "abc{", 4, 0, out, 8, &blk);
    EXPECT_EQ_INT(n, 1);
    EXPECT_EQ_INT((int)out[0].start_col, 0);
    EXPECT_EQ_INT((int)out[0].len, 4);
    EXPECT_EQ_INT((int)out[0].color.r, 0xFF);
    EXPECT_EQ_INT(blk, 1); /* '{' abrio bloque */

    /* la siguiente linea entra con in_block=1 y lo cierra con '}' */
    n = ext_host_highlight_line(host, "x.foo", "}", 1, 1, out, 8, &blk);
    EXPECT_EQ_INT(blk, 0);

    /* path sin resaltador: -1 (el render cae a texto plano) */
    EXPECT_EQ_INT(ext_host_highlight_line(host, "x.txt", "abc", 3, 0, out, 8,
                                          &blk),
                  -1);

    ext_host_destroy(host);
    buf_free(&b);
}

/** set_tokens / clear_tokens: almacen por-buffer, copia y limpieza. */
static void test_tokens_pushed(void) {
    Buffer b;
    EXPECT_TRUE(buf_init(&b));
    CoffeeHostBackend backend;
    memset(&backend, 0, sizeof backend);
    backend.buffer = &b;
    CoffeeHost *host = ext_host_create(&backend);
    EXPECT_NOT_NULL(host);
    const CoffeeApi *api = ext_host_api(host);

    /* sin pushear nada: el buffer no tiene tramos */
    EXPECT_FALSE(ext_host_has_pushed_tokens(host, &b));
    EXPECT_EQ_INT(ext_host_line_tokens(host, &b, 0, NULL, 0), -1);

    /* pushear 2 tramos a la linea 3 */
    CoffeeSpan in[2];
    in[0].start_col = 0; in[0].len = 4;
    in[0].color.r = 1; in[0].color.g = 2; in[0].color.b = 3; in[0].color.a = 4;
    in[1].start_col = 5; in[1].len = 2;
    in[1].color.r = 9; in[1].color.g = 8; in[1].color.b = 7; in[1].color.a = 6;
    api->set_tokens(host, 3, in, 2);

    EXPECT_TRUE(ext_host_has_pushed_tokens(host, &b));
    /* la linea 3 tiene 2 tramos; la 0 no tiene (devuelve -1) */
    EXPECT_EQ_INT(ext_host_line_tokens(host, &b, 0, NULL, 0), -1);
    CoffeeSpan out[8];
    int n = ext_host_line_tokens(host, &b, 3, out, 8);
    EXPECT_EQ_INT(n, 2);
    EXPECT_EQ_INT((int)out[0].len, 4);
    EXPECT_EQ_INT((int)out[0].color.a, 4);
    EXPECT_EQ_INT((int)out[1].start_col, 5);
    EXPECT_EQ_INT((int)out[1].color.r, 9);

    /* es una COPIA: mutar el origen no cambia lo guardado */
    in[0].len = 999;
    n = ext_host_line_tokens(host, &b, 3, out, 8);
    EXPECT_EQ_INT((int)out[0].len, 4);

    /* set_tokens count=0 limpia esa linea */
    api->set_tokens(host, 3, NULL, 0);
    EXPECT_EQ_INT(ext_host_line_tokens(host, &b, 3, out, 8), -1);
    EXPECT_FALSE(ext_host_has_pushed_tokens(host, &b));

    /* re-pushear y limpiar todo con clear_tokens */
    api->set_tokens(host, 1, in, 1);
    api->set_tokens(host, 2, in, 1);
    EXPECT_TRUE(ext_host_has_pushed_tokens(host, &b));
    api->clear_tokens(host);
    EXPECT_FALSE(ext_host_has_pushed_tokens(host, &b));

    /* drop_buffer tambien limpia los tramos pushed del buffer */
    api->set_tokens(host, 0, in, 1);
    EXPECT_TRUE(ext_host_has_pushed_tokens(host, &b));
    ext_host_drop_buffer(host, &b);
    EXPECT_FALSE(ext_host_has_pushed_tokens(host, &b));

    ext_host_destroy(host);
    buf_free(&b);
}

int main(void) {
    tt_suite("ext_highlight");
    tt_run("register_highlighter + resolver por extension", test_register_y_resolver);
    tt_run("set_tokens / clear_tokens por buffer", test_tokens_pushed);
    return tt_summary();
}
