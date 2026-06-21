/**
 * @file test_layout_persist.c
 * @brief Tests puros de la (de)serializacion de la disposicion del editor.
 *
 * Verifica las funciones puras de session/layout_persist.h:
 *   - layout_serialize + layout_parse: round-trip de una disposicion completa
 *     (pestanas con rutas+grupos, arbol de dock de varios nodos, flotantes y
 *     tamanos de la UI) -> el resultado parseado coincide con el original.
 *   - layout_parse sobre texto vacio/basura -> estado vacio coherente sin
 *     crashear.
 *   - layout_parse descarta un arbol de dock con indices invalidos pero
 *     conserva las pestanas y los tamanos.
 *
 * No depende de SDL ni de la struct Editor: enlaza solo layout_persist.c.
 */
#include "ctests.h"
#include "session/layout_persist.h"

#include <string.h>

/* Construye una disposicion de ejemplo razonablemente rica. */
static void build_sample(LayoutData *d) {
    layout_data_clear(d);

    /* tamanos UI */
    d->ftree_width = 260;
    d->ftree_open = 1;
    d->ext_panel_w = 340;
    d->ext_panel_open = 0;
    d->bottom_panel_h = 180;
    d->bottom_panel_open = 1;

    /* foco */
    d->active_group = 1;
    d->active_tab = 2;

    /* tres pestanas, dos grupos */
    strncpy(d->tabs[0].path, "src/main.c", sizeof(d->tabs[0].path) - 1);
    d->tabs[0].group = 0;
    d->tabs[0].is_group_active = 1;
    strncpy(d->tabs[1].path, "C:/ruta con espacios/a.txt",
            sizeof(d->tabs[1].path) - 1);
    d->tabs[1].group = 0;
    d->tabs[1].is_group_active = 0;
    strncpy(d->tabs[2].path, "include/editor/editor.h",
            sizeof(d->tabs[2].path) - 1);
    d->tabs[2].group = 1;
    d->tabs[2].is_group_active = 1;
    d->tab_count = 3;

    /* arbol de dock: raiz SPLIT vertical con dos hojas (grupos 0 y 1) */
    d->dock_nodes[0].kind = 1; /* SPLIT */
    d->dock_nodes[0].group_id = 0;
    d->dock_nodes[0].orient = 0; /* VERTICAL */
    d->dock_nodes[0].ratio = 0.4f;
    d->dock_nodes[0].child_a = 1;
    d->dock_nodes[0].child_b = 2;
    d->dock_nodes[0].parent = -1;
    d->dock_nodes[1].kind = 0; /* LEAF */
    d->dock_nodes[1].group_id = 0;
    d->dock_nodes[1].child_a = -1;
    d->dock_nodes[1].child_b = -1;
    d->dock_nodes[1].parent = 0;
    d->dock_nodes[2].kind = 0; /* LEAF */
    d->dock_nodes[2].group_id = 1;
    d->dock_nodes[2].child_a = -1;
    d->dock_nodes[2].child_b = -1;
    d->dock_nodes[2].parent = 0;
    d->dock_node_count = 3;
    d->dock_root = 0;
    d->dock_focused_leaf = 2;
    d->dock_leaf_count = 2;

    /* dos flotantes */
    d->floats[0].x = 100;
    d->floats[0].y = 120;
    d->floats[0].w = 480;
    d->floats[0].h = 320;
    d->floats[0].group_id = 2;
    d->floats[1].x = 500;
    d->floats[1].y = 60;
    d->floats[1].w = 300;
    d->floats[1].h = 240;
    d->floats[1].group_id = 3;
    d->float_count = 2;
}

/* -- Round-trip completo --------------------------------------------------- */
static void test_round_trip(void) {
    LayoutData a;
    build_sample(&a);

    char text[8192];
    size_t n = layout_serialize(&a, text, sizeof text);
    EXPECT_TRUE(n > 0); /* algo se escribio */

    LayoutData b;
    int ok = layout_parse(text, &b);
    EXPECT_EQ_INT(ok, 1); /* no vacia, coherente */

    /* tamanos UI */
    EXPECT_EQ_INT(b.ftree_width, 260);
    EXPECT_EQ_INT(b.ftree_open, 1);
    EXPECT_EQ_INT(b.ext_panel_w, 340);
    EXPECT_EQ_INT(b.ext_panel_open, 0);
    EXPECT_EQ_INT(b.bottom_panel_h, 180);
    EXPECT_EQ_INT(b.bottom_panel_open, 1);

    /* foco */
    EXPECT_EQ_INT(b.active_group, 1);
    EXPECT_EQ_INT(b.active_tab, 2);

    /* pestanas */
    EXPECT_EQ_INT(b.tab_count, 3);
    EXPECT_EQ_STR(b.tabs[0].path, "src/main.c");
    EXPECT_EQ_INT(b.tabs[0].group, 0);
    EXPECT_EQ_INT(b.tabs[0].is_group_active, 1);
    EXPECT_EQ_STR(b.tabs[1].path, "C:/ruta con espacios/a.txt"); /* con espacios */
    EXPECT_EQ_INT(b.tabs[1].group, 0);
    EXPECT_EQ_INT(b.tabs[1].is_group_active, 0);
    EXPECT_EQ_STR(b.tabs[2].path, "include/editor/editor.h");
    EXPECT_EQ_INT(b.tabs[2].group, 1);
    EXPECT_EQ_INT(b.tabs[2].is_group_active, 1);

    /* arbol de dock */
    EXPECT_EQ_INT(b.dock_node_count, 3);
    EXPECT_EQ_INT(b.dock_root, 0);
    EXPECT_EQ_INT(b.dock_focused_leaf, 2);
    EXPECT_EQ_INT(b.dock_leaf_count, 2);
    EXPECT_EQ_INT(b.dock_nodes[0].kind, 1);
    EXPECT_EQ_INT(b.dock_nodes[0].orient, 0);
    EXPECT_NEAR(b.dock_nodes[0].ratio, 0.4f, 1e-4);
    EXPECT_EQ_INT(b.dock_nodes[0].child_a, 1);
    EXPECT_EQ_INT(b.dock_nodes[0].child_b, 2);
    EXPECT_EQ_INT(b.dock_nodes[0].parent, -1);
    EXPECT_EQ_INT(b.dock_nodes[1].kind, 0);
    EXPECT_EQ_INT(b.dock_nodes[1].group_id, 0);
    EXPECT_EQ_INT(b.dock_nodes[1].parent, 0);
    EXPECT_EQ_INT(b.dock_nodes[2].group_id, 1);

    /* flotantes */
    EXPECT_EQ_INT(b.float_count, 2);
    EXPECT_EQ_INT(b.floats[0].x, 100);
    EXPECT_EQ_INT(b.floats[0].y, 120);
    EXPECT_EQ_INT(b.floats[0].w, 480);
    EXPECT_EQ_INT(b.floats[0].h, 320);
    EXPECT_EQ_INT(b.floats[0].group_id, 2);
    EXPECT_EQ_INT(b.floats[1].group_id, 3);
}

/* -- Robustez: texto vacio ------------------------------------------------- */
static void test_parse_vacio(void) {
    LayoutData d;
    int ok = layout_parse("", &d);
    EXPECT_EQ_INT(ok, 0);          /* vacio -> 0 */
    EXPECT_EQ_INT(d.tab_count, 0); /* estado limpio */
    EXPECT_EQ_INT(d.dock_node_count, 0);
    EXPECT_EQ_INT(d.float_count, 0);
    EXPECT_EQ_INT(d.dock_root, -1);
}

/* -- Robustez: texto basura ------------------------------------------------ */
static void test_parse_basura(void) {
    LayoutData d;
    const char *garbage =
        "esto no es un layout\n@@@###\n12345\nui\nnode foo bar\n";
    int ok = layout_parse(garbage, &d);
    /* No debe crashear; al no haber campos validos, queda vacio. */
    EXPECT_EQ_INT(ok, 0);
    EXPECT_EQ_INT(d.tab_count, 0);
    EXPECT_EQ_INT(d.dock_node_count, 0);
    EXPECT_EQ_INT(d.float_count, 0);
}

/* -- Robustez: NULL -------------------------------------------------------- */
static void test_parse_null(void) {
    LayoutData d;
    int ok = layout_parse(NULL, &d);
    EXPECT_EQ_INT(ok, 0);
    EXPECT_EQ_INT(d.tab_count, 0); /* layout_data_clear corrio igualmente */
}

/* -- Arbol de dock con indices invalidos: se descarta, pestanas se quedan --- */
static void test_dock_invalido_descarta_solo_arbol(void) {
    const char *text =
        "ui ftree_width 200\n"
        "tab 0 1 src/x.c\n"
        "dock 2 0 0 2\n"
        "node 1 0 0 0.5 9 9 -1\n" /* hijos 9,9 fuera de rango (count=2) */
        "node 0 0 0 0.5 -1 -1 0\n";
    LayoutData d;
    int ok = layout_parse(text, &d);
    EXPECT_EQ_INT(ok, 1); /* hubo pestana valida */
    /* el arbol incoherente se descarto */
    EXPECT_EQ_INT(d.dock_node_count, 0);
    EXPECT_EQ_INT(d.dock_root, -1);
    /* pero la pestana y los tamanos se conservaron */
    EXPECT_EQ_INT(d.tab_count, 1);
    EXPECT_EQ_STR(d.tabs[0].path, "src/x.c");
    EXPECT_EQ_INT(d.ftree_width, 200);
}

/* -- Pestana sin ruta no se serializa (tab_count solo cuenta con ruta) ------ */
static void test_tab_sin_ruta_no_round_trip(void) {
    LayoutData a;
    layout_data_clear(&a);
    /* una pestana valida + un slot sin ruta no contado en tab_count */
    strncpy(a.tabs[0].path, "a.c", sizeof(a.tabs[0].path) - 1);
    a.tabs[0].group = 0;
    a.tabs[0].is_group_active = 1;
    a.tab_count = 1;

    char text[2048];
    size_t n = layout_serialize(&a, text, sizeof text);
    EXPECT_TRUE(n > 0);

    LayoutData b;
    layout_parse(text, &b);
    EXPECT_EQ_INT(b.tab_count, 1);
    EXPECT_EQ_STR(b.tabs[0].path, "a.c");
}

int main(void) {
    tt_suite("layout_persist");
    tt_run("round-trip completo", test_round_trip);
    tt_run("parse texto vacio", test_parse_vacio);
    tt_run("parse texto basura", test_parse_basura);
    tt_run("parse NULL", test_parse_null);
    tt_run("dock invalido se descarta y conserva pestanas",
           test_dock_invalido_descarta_solo_arbol);
    tt_run("pestana con ruta round-trip", test_tab_sin_ruta_no_round_trip);
    return tt_summary();
}
