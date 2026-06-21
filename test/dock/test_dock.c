/**
 * @file test_dock.c
 * @brief Pruebas unitarias de la logica PURA del arbol de dock.
 *
 * Ejercita solo las funciones puras de dock.h (sin SDL ni struct Editor), asi
 * que corre en headless.  Verifica:
 *   1. Una sola hoja ocupa el area completa (cero regresion).
 *   2. Split vertical reparte el ancho por ratio; horizontal reparte el alto.
 *   3. Anidamiento 2x2: cuatro hojas con sus rects correctos.
 *   4. Clamp del ratio: ninguna hoja baja del minimo.
 *   5. dock_hit_divider acierta el divisor correcto y su orientacion.
 *   6. dock_remove_leaf colapsa el split y el hermano ocupa el sitio.
 */
#include "ctests.h"
#include "dock/dock.h"

/* -- Una sola hoja: area completa ------------------------------------------ */

static void test_single_leaf(void) {
    DockTree t;
    dock_init_single(&t, 0);
    EXPECT_EQ_INT(t.leaf_count, 1);

    DockRect area = {10, 20, 800, 600};
    DockLeafRect rects[DOCK_MAX_LEAVES];
    int n = dock_compute_leaf_rects(&t, area, rects, DOCK_MAX_LEAVES);
    EXPECT_EQ_INT(n, 1);
    EXPECT_EQ_INT(rects[0].group_id, 0);
    EXPECT_EQ_INT(rects[0].rect.x, 10);
    EXPECT_EQ_INT(rects[0].rect.y, 20);
    EXPECT_EQ_INT(rects[0].rect.w, 800);
    EXPECT_EQ_INT(rects[0].rect.h, 600);
}

/* -- Split vertical: mitades por ancho ------------------------------------- */

static void test_split_vertical(void) {
    DockTree t;
    dock_init_single(&t, 0);
    int nl = dock_split_leaf(&t, t.root, DOCK_VERTICAL, 1);
    EXPECT_TRUE(nl != DOCK_NONE);
    EXPECT_EQ_INT(t.leaf_count, 2);

    DockRect area = {0, 0, 800, 600};
    DockLeafRect rects[DOCK_MAX_LEAVES];
    int n = dock_compute_leaf_rects(&t, area, rects, DOCK_MAX_LEAVES);
    EXPECT_EQ_INT(n, 2);
    /* child_a (group 0) a la izquierda, child_b (group 1) a la derecha */
    EXPECT_EQ_INT(rects[0].group_id, 0);
    EXPECT_EQ_INT(rects[0].rect.x, 0);
    EXPECT_EQ_INT(rects[0].rect.w, 400);
    EXPECT_EQ_INT(rects[0].rect.h, 600); /* alto completo */
    EXPECT_EQ_INT(rects[1].group_id, 1);
    EXPECT_EQ_INT(rects[1].rect.x, 400);
    EXPECT_EQ_INT(rects[1].rect.w, 400);
}

/* -- Split horizontal: mitades por alto ------------------------------------ */

static void test_split_horizontal(void) {
    DockTree t;
    dock_init_single(&t, 0);
    dock_split_leaf(&t, t.root, DOCK_HORIZONTAL, 1);

    DockRect area = {0, 0, 800, 600};
    DockLeafRect rects[DOCK_MAX_LEAVES];
    int n = dock_compute_leaf_rects(&t, area, rects, DOCK_MAX_LEAVES);
    EXPECT_EQ_INT(n, 2);
    /* child_a arriba, child_b abajo */
    EXPECT_EQ_INT(rects[0].rect.y, 0);
    EXPECT_EQ_INT(rects[0].rect.h, 300);
    EXPECT_EQ_INT(rects[0].rect.w, 800); /* ancho completo */
    EXPECT_EQ_INT(rects[1].rect.y, 300);
    EXPECT_EQ_INT(rects[1].rect.h, 300);
}

/* -- Anidamiento 2x2: cuatro hojas ----------------------------------------- */

static void test_nested_2x2(void) {
    DockTree t;
    dock_init_single(&t, 0);
    /* dividir la raiz en vertical -> hojas g0 (izq) y g1 (der) */
    int right = dock_split_leaf(&t, t.root, DOCK_VERTICAL, 1);
    EXPECT_TRUE(right != DOCK_NONE);
    /* el nodo izquierdo es child_a del split raiz */
    int rootsplit = t.root;
    int left = t.nodes[rootsplit].child_a;
    /* dividir la izquierda en horizontal -> g0 (arriba) y g2 (abajo) */
    int lbot = dock_split_leaf(&t, left, DOCK_HORIZONTAL, 2);
    EXPECT_TRUE(lbot != DOCK_NONE);
    /* dividir la derecha en horizontal -> g1 (arriba) y g3 (abajo) */
    int rbot = dock_split_leaf(&t, right, DOCK_HORIZONTAL, 3);
    EXPECT_TRUE(rbot != DOCK_NONE);
    EXPECT_EQ_INT(t.leaf_count, 4);

    DockRect area = {0, 0, 800, 600};
    DockLeafRect rects[DOCK_MAX_LEAVES];
    int n = dock_compute_leaf_rects(&t, area, rects, DOCK_MAX_LEAVES);
    EXPECT_EQ_INT(n, 4);

    /* localizar cada cuadrante por group_id y verificar su rect */
    for (int i = 0; i < n; i++) {
        DockRect r = rects[i].rect;
        switch (rects[i].group_id) {
        case 0: /* izquierda-arriba */
            EXPECT_EQ_INT(r.x, 0);   EXPECT_EQ_INT(r.y, 0);
            EXPECT_EQ_INT(r.w, 400); EXPECT_EQ_INT(r.h, 300);
            break;
        case 2: /* izquierda-abajo */
            EXPECT_EQ_INT(r.x, 0);   EXPECT_EQ_INT(r.y, 300);
            EXPECT_EQ_INT(r.w, 400); EXPECT_EQ_INT(r.h, 300);
            break;
        case 1: /* derecha-arriba */
            EXPECT_EQ_INT(r.x, 400); EXPECT_EQ_INT(r.y, 0);
            EXPECT_EQ_INT(r.w, 400); EXPECT_EQ_INT(r.h, 300);
            break;
        case 3: /* derecha-abajo */
            EXPECT_EQ_INT(r.x, 400); EXPECT_EQ_INT(r.y, 300);
            EXPECT_EQ_INT(r.w, 400); EXPECT_EQ_INT(r.h, 300);
            break;
        default: EXPECT_TRUE(0); /* group_id inesperado */
        }
    }
}

/* -- Clamp del ratio: minimo por hoja -------------------------------------- */

static void test_ratio_clamp_min(void) {
    DockTree t;
    dock_init_single(&t, 0);
    int split = t.root;
    dock_split_leaf(&t, split, DOCK_VERTICAL, 1);

    DockRect area = {0, 0, 800, 600};
    /* arrastrar el divisor pegado al borde izquierdo (mx=0) */
    dock_apply_divider(&t, area, split, 0, 300);
    DockLeafRect rects[DOCK_MAX_LEAVES];
    dock_compute_leaf_rects(&t, area, rects, DOCK_MAX_LEAVES);
    /* ninguna hoja baja del minimo */
    for (int i = 0; i < 2; i++)
        EXPECT_TRUE(rects[i].rect.w >= DOCK_LEAF_MIN);

    /* y pegado al borde derecho (mx muy grande) */
    dock_apply_divider(&t, area, split, 99999, 300);
    dock_compute_leaf_rects(&t, area, rects, DOCK_MAX_LEAVES);
    for (int i = 0; i < 2; i++)
        EXPECT_TRUE(rects[i].rect.w >= DOCK_LEAF_MIN);
}

/* -- Hit-test del divisor --------------------------------------------------- */

static void test_hit_divider_vertical(void) {
    DockTree t;
    dock_init_single(&t, 0);
    int split = t.root;
    dock_split_leaf(&t, split, DOCK_VERTICAL, 1);

    DockRect area = {0, 0, 800, 600};
    DockOrient or ;
    /* el borde esta en x=400; un punto justo encima cae sobre el divisor */
    int hit = dock_hit_divider(&t, area, 400, 300, &or);
    EXPECT_EQ_INT(hit, split);
    EXPECT_EQ_INT((int)or, (int)DOCK_VERTICAL);

    /* lejos del borde: ningun divisor */
    int miss = dock_hit_divider(&t, area, 100, 300, &or);
    EXPECT_EQ_INT(miss, DOCK_NONE);
}

static void test_hit_divider_horizontal(void) {
    DockTree t;
    dock_init_single(&t, 0);
    int split = t.root;
    dock_split_leaf(&t, split, DOCK_HORIZONTAL, 1);

    DockRect area = {0, 0, 800, 600};
    DockOrient or ;
    /* el borde esta en y=300 */
    int hit = dock_hit_divider(&t, area, 400, 300, &or);
    EXPECT_EQ_INT(hit, split);
    EXPECT_EQ_INT((int)or, (int)DOCK_HORIZONTAL);
}

/* -- Eliminacion de hoja: colapso del split -------------------------------- */

static void test_remove_leaf_collapse(void) {
    DockTree t;
    dock_init_single(&t, 0);
    int newleaf = dock_split_leaf(&t, t.root, DOCK_VERTICAL, 1);
    EXPECT_EQ_INT(t.leaf_count, 2);

    /* eliminar la hoja nueva: el hermano (g0) vuelve a ocupar todo */
    int ok = dock_remove_leaf(&t, newleaf);
    EXPECT_EQ_INT(ok, 1);
    EXPECT_EQ_INT(t.leaf_count, 1);

    DockRect area = {0, 0, 800, 600};
    DockLeafRect rects[DOCK_MAX_LEAVES];
    int n = dock_compute_leaf_rects(&t, area, rects, DOCK_MAX_LEAVES);
    EXPECT_EQ_INT(n, 1);
    EXPECT_EQ_INT(rects[0].group_id, 0);
    EXPECT_EQ_INT(rects[0].rect.w, 800); /* area completa de nuevo */
    /* el unico nodo restante es la raiz y es hoja */
    EXPECT_EQ_INT((int)t.nodes[t.root].kind, (int)DOCK_LEAF);
}

/* -- Eliminacion en 2x2: el hermano hereda el subarbol --------------------- */

static void test_remove_leaf_nested(void) {
    DockTree t;
    dock_init_single(&t, 0);
    int right = dock_split_leaf(&t, t.root, DOCK_VERTICAL, 1);
    int rootsplit = t.root;
    int left = t.nodes[rootsplit].child_a;
    dock_split_leaf(&t, left, DOCK_HORIZONTAL, 2); /* g0 arriba, g2 abajo */
    EXPECT_EQ_INT(t.leaf_count, 3);

    /* eliminar la derecha (g1): el subarbol izquierdo (g0/g2) ocupa todo */
    int gl1 = dock_leaf_by_group(&t, 1);
    EXPECT_TRUE(gl1 != DOCK_NONE);
    int ok = dock_remove_leaf(&t, gl1);
    EXPECT_EQ_INT(ok, 1);
    EXPECT_EQ_INT(t.leaf_count, 2);

    DockRect area = {0, 0, 800, 600};
    DockLeafRect rects[DOCK_MAX_LEAVES];
    int n = dock_compute_leaf_rects(&t, area, rects, DOCK_MAX_LEAVES);
    EXPECT_EQ_INT(n, 2);
    /* g0 y g2 ahora reparten el alto a todo el ancho */
    for (int i = 0; i < n; i++) {
        EXPECT_TRUE(rects[i].group_id == 0 || rects[i].group_id == 2);
        EXPECT_EQ_INT(rects[i].rect.w, 800);
        EXPECT_EQ_INT(rects[i].rect.h, 300);
    }
}

/* -- alloc_group_id: primer id libre --------------------------------------- */

static void test_alloc_group_id(void) {
    DockTree t;
    dock_init_single(&t, 0);
    EXPECT_EQ_INT(dock_alloc_group_id(&t), 1); /* 0 ocupado -> 1 libre */
    dock_split_leaf(&t, t.root, DOCK_VERTICAL, 1);
    EXPECT_EQ_INT(dock_alloc_group_id(&t), 2); /* 0 y 1 ocupados -> 2 */
}

/* -- Zona de drop: centro y bandas de borde -------------------------------- */

static void test_drop_zone(void) {
    DockRect r = {0, 0, 800, 600};

    /* centro del rect -> CENTER */
    EXPECT_EQ_INT((int)dock_drop_zone(r, 400, 300), (int)DOCK_DZ_CENTER);

    /* banda izquierda (DOCK_DROP_BAND=0.25 -> 200px): un punto pegado al borde
     * izquierdo cae en LEFT */
    EXPECT_EQ_INT((int)dock_drop_zone(r, 5, 300), (int)DOCK_DZ_LEFT);
    /* banda derecha */
    EXPECT_EQ_INT((int)dock_drop_zone(r, 795, 300), (int)DOCK_DZ_RIGHT);
    /* banda superior */
    EXPECT_EQ_INT((int)dock_drop_zone(r, 400, 5), (int)DOCK_DZ_TOP);
    /* banda inferior */
    EXPECT_EQ_INT((int)dock_drop_zone(r, 400, 595), (int)DOCK_DZ_BOTTOM);

    /* fuera del rect -> NONE */
    EXPECT_EQ_INT((int)dock_drop_zone(r, -1, 300), (int)DOCK_DZ_NONE);
    EXPECT_EQ_INT((int)dock_drop_zone(r, 800, 300), (int)DOCK_DZ_NONE);
    EXPECT_EQ_INT((int)dock_drop_zone(r, 400, 600), (int)DOCK_DZ_NONE);

    /* esquina superior-izquierda: gana el borde de menor profundidad RELATIVA.
     * band_w=200, band_h=150.  En (3,3): LEFT=3/200=0.015, TOP=3/150=0.02 ->
     * gana LEFT (mas pegado a su borde en proporcion). */
    EXPECT_EQ_INT((int)dock_drop_zone(r, 3, 3), (int)DOCK_DZ_LEFT);
    /* en (100, 3): TOP=3/150=0.02 < LEFT=100/200=0.5 -> gana TOP */
    EXPECT_EQ_INT((int)dock_drop_zone(r, 100, 3), (int)DOCK_DZ_TOP);
    /* en (3, 100): LEFT=3/200=0.015 < TOP=100/150=0.67 -> gana LEFT */
    EXPECT_EQ_INT((int)dock_drop_zone(r, 3, 100), (int)DOCK_DZ_LEFT);

    /* rect con offset: respeta el origen */
    DockRect off = {100, 50, 400, 400};
    EXPECT_EQ_INT((int)dock_drop_zone(off, 300, 250), (int)DOCK_DZ_CENTER);
    EXPECT_EQ_INT((int)dock_drop_zone(off, 90, 250), (int)DOCK_DZ_NONE);

    /* rect degenerado -> NONE (sin crash) */
    DockRect deg = {0, 0, 0, 0};
    EXPECT_EQ_INT((int)dock_drop_zone(deg, 0, 0), (int)DOCK_DZ_NONE);
}

/* -- Split por lado: la hoja nueva queda en child_a o child_b -------------- */

static void test_split_leaf_side(void) {
    /* new_first=0 -> hoja nueva es child_b (derecha en V) */
    {
        DockTree t;
        dock_init_single(&t, 0);
        int split = t.root;
        int nl = dock_split_leaf_side(&t, split, DOCK_VERTICAL, 1, 0);
        EXPECT_TRUE(nl != DOCK_NONE);
        EXPECT_EQ_INT(t.nodes[split].child_b, nl);          /* nueva detras */
        EXPECT_EQ_INT(t.nodes[t.nodes[split].child_a].group_id, 0); /* orig delante */
        EXPECT_EQ_INT(t.nodes[nl].group_id, 1);
    }
    /* new_first=1 -> hoja nueva es child_a (izquierda/arriba) */
    {
        DockTree t;
        dock_init_single(&t, 0);
        int split = t.root;
        int nl = dock_split_leaf_side(&t, split, DOCK_HORIZONTAL, 1, 1);
        EXPECT_TRUE(nl != DOCK_NONE);
        EXPECT_EQ_INT(t.nodes[split].child_a, nl);          /* nueva delante */
        EXPECT_EQ_INT(t.nodes[t.nodes[split].child_b].group_id, 0); /* orig detras */
        EXPECT_EQ_INT(t.nodes[nl].group_id, 1);

        /* y la hoja nueva (arriba) ocupa la mitad superior */
        DockRect area = {0, 0, 800, 600};
        DockLeafRect rects[DOCK_MAX_LEAVES];
        int n = dock_compute_leaf_rects(&t, area, rects, DOCK_MAX_LEAVES);
        EXPECT_EQ_INT(n, 2);
        for (int i = 0; i < n; i++) {
            if (rects[i].group_id == 1) { /* la nueva, arriba */
                EXPECT_EQ_INT(rects[i].rect.y, 0);
                EXPECT_EQ_INT(rects[i].rect.h, 300);
            } else { /* la original, abajo */
                EXPECT_EQ_INT(rects[i].rect.y, 300);
            }
        }
    }
}

int main(void) {
    tt_suite("dock");
    tt_run("una hoja: area completa", test_single_leaf);
    tt_run("split vertical: mitades por ancho", test_split_vertical);
    tt_run("split horizontal: mitades por alto", test_split_horizontal);
    tt_run("anidamiento 2x2: cuatro hojas", test_nested_2x2);
    tt_run("clamp del ratio: minimo por hoja", test_ratio_clamp_min);
    tt_run("hit divisor vertical", test_hit_divider_vertical);
    tt_run("hit divisor horizontal", test_hit_divider_horizontal);
    tt_run("eliminar hoja: colapso del split", test_remove_leaf_collapse);
    tt_run("eliminar hoja anidada: hermano hereda", test_remove_leaf_nested);
    tt_run("alloc group id: primer libre", test_alloc_group_id);
    tt_run("zona de drop: centro y bandas", test_drop_zone);
    tt_run("split por lado: hoja nueva en child_a/child_b", test_split_leaf_side);
    return tt_summary();
}
