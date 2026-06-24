/**
 * @file test_float_dock.c
 * @brief Pruebas de la logica PURA del re-acople de un flotante al dock.
 *
 * El re-acople por arrastre de la barra de titulo de un flotante se apoya en
 * dos piezas PURAS (sin SDL ni struct Editor), que aqui se verifican en
 * headless:
 *
 *   1. La DETECCION del destino: dado el rect de la hoja del dock bajo el
 *      cursor, dock_drop_zone clasifica el punto en CENTER o una banda de borde.
 *      Es exactamente lo que editor_float_dock_target usa para decidir adonde
 *      cae el flotante.
 *   2. El ACOPLE: el caso CENTER mueve TODAS las pestanas del grupo del flotante
 *      al grupo destino; el caso de borde crea una hoja nueva con esas pestanas
 *      dividiendo la hoja destino (dock_split_leaf_side).  Aqui se simula el
 *      movimiento de pestanas sobre un array plano (mismo bucle que
 *      editor_float_dock_to) y se ejercita la creacion de la hoja por borde.
 *
 * No se enlaza la struct Editor (necesita SDL); se reproduce solo la logica de
 * membresia y la mutacion del arbol, que es donde estan las invariantes.
 */
#include "ctests.h"
#include "dock/dock.h"

/* -- 1. Deteccion del destino: dock_drop_zone sobre la hoja --------------- */

static void test_detect_zone(void) {
    DockRect leaf = {0, 0, 400, 300};

    /* el centro de la hoja -> mover al grupo (CENTER) */
    EXPECT_EQ_INT((int)dock_drop_zone(leaf, 200, 150), (int)DOCK_DZ_CENTER);

    /* las bandas de borde -> dividir hacia ese lado */
    EXPECT_EQ_INT((int)dock_drop_zone(leaf, 5, 150), (int)DOCK_DZ_LEFT);
    EXPECT_EQ_INT((int)dock_drop_zone(leaf, 395, 150), (int)DOCK_DZ_RIGHT);
    EXPECT_EQ_INT((int)dock_drop_zone(leaf, 200, 5), (int)DOCK_DZ_TOP);
    EXPECT_EQ_INT((int)dock_drop_zone(leaf, 200, 295), (int)DOCK_DZ_BOTTOM);

    /* fuera del rect -> sin zona (el flotante solo se mueve, no se acopla) */
    EXPECT_EQ_INT((int)dock_drop_zone(leaf, 500, 150), (int)DOCK_DZ_NONE);
    EXPECT_EQ_INT((int)dock_drop_zone(leaf, 200, 400), (int)DOCK_DZ_NONE);
}

/* -- 2. Acople CENTER: mover TODAS las pestanas del grupo al destino ------- */

/* Modelo minimo de pestana: solo su grupo (la membresia es lo que cambia). */
#define TEST_MAX_TABS 16

/* Reproduce el bucle central de editor_float_dock_to para el caso CENTER:
 * reasigna al grupo destino toda pestana cuyo grupo sea el del flotante. */
static int move_group_center(int *tab_group, int n, int float_group,
                             int dst_group) {
    int moved = 0;
    for (int i = 0; i < n; i++)
        if (tab_group[i] == float_group) {
            tab_group[i] = dst_group;
            moved++;
        }
    return moved;
}

static void test_center_moves_all_tabs(void) {
    /* grupo 0 = hoja del dock destino; grupo 5 = flotante con 3 pestanas */
    int tab_group[TEST_MAX_TABS] = {0, 5, 0, 5, 5};
    int n = 5;

    int moved = move_group_center(tab_group, n, /*float_group*/ 5, /*dst*/ 0);
    EXPECT_EQ_INT(moved, 3); /* las 3 pestanas del flotante se mueven */

    /* tras el acople CENTER ninguna pestana queda en el grupo del flotante */
    int still = 0;
    for (int i = 0; i < n; i++)
        if (tab_group[i] == 5) still++;
    EXPECT_EQ_INT(still, 0);

    /* y todas las del flotante acabaron en el grupo destino */
    int in_dst = 0;
    for (int i = 0; i < n; i++)
        if (tab_group[i] == 0) in_dst++;
    EXPECT_EQ_INT(in_dst, 5); /* 2 que ya estaban + 3 movidas */
}

static void test_center_noop_when_empty(void) {
    /* flotante sin pestanas (grupo 7 no aparece): no se mueve nada */
    int tab_group[TEST_MAX_TABS] = {0, 0, 1};
    int n = 3;
    int moved = move_group_center(tab_group, n, /*float_group*/ 7, /*dst*/ 0);
    EXPECT_EQ_INT(moved, 0);
}

/* -- 3. Acople por borde: dividir la hoja destino crea una hoja nueva ------ */

static void test_border_split_creates_leaf(void) {
    DockTree t;
    dock_init_single(&t, 0); /* una sola hoja (grupo 0) */
    EXPECT_EQ_INT(t.leaf_count, 1);

    int target_leaf = dock_leaf_by_group(&t, 0);
    EXPECT_TRUE(target_leaf != DOCK_NONE);

    /* zona RIGHT -> split vertical, hoja nueva a la derecha (no es la primera) */
    int new_group = dock_alloc_group_id(&t);
    EXPECT_TRUE(new_group != DOCK_NONE);
    int new_leaf = dock_split_leaf_side(&t, target_leaf, DOCK_VERTICAL, new_group,
                                        /*new_first*/ 0);
    EXPECT_TRUE(new_leaf != DOCK_NONE);
    EXPECT_EQ_INT(t.leaf_count, 2); /* ahora hay dos hojas */

    /* la hoja nueva existe con el group_id asignado */
    EXPECT_TRUE(dock_leaf_by_group(&t, new_group) != DOCK_NONE);

    /* el reparto vertical pone la hoja original a la izquierda y la nueva a la
     * derecha dentro del area */
    DockRect area = {0, 0, 800, 600};
    DockLeafRect rects[DOCK_MAX_LEAVES];
    int n = dock_compute_leaf_rects(&t, area, rects, DOCK_MAX_LEAVES);
    EXPECT_EQ_INT(n, 2);
    int gx0 = -1, gxnew = -1;
    for (int i = 0; i < n; i++) {
        if (rects[i].group_id == 0) gx0 = rects[i].rect.x;
        if (rects[i].group_id == new_group) gxnew = rects[i].rect.x;
    }
    EXPECT_TRUE(gx0 >= 0 && gxnew >= 0);
    EXPECT_TRUE(gxnew > gx0); /* la hoja nueva queda a la derecha */
}

int main(void) {
    tt_suite("float_dock");
    tt_run("deteccion de zona de acople sobre la hoja", test_detect_zone);
    tt_run("CENTER mueve todas las pestanas del flotante", test_center_moves_all_tabs);
    tt_run("CENTER sin pestanas es no-op", test_center_noop_when_empty);
    tt_run("borde divide la hoja destino y crea hoja nueva", test_border_split_creates_leaf);
    return tt_summary();
}
