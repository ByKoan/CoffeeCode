/**
 * @file test_tabmove.c
 * @brief Tests puros del movimiento de pestanas entre dos arrays tabs[] de
 *        ventanas distintas (multi-ventana), sin SDL ni Editor.
 *
 * Modela el desprender (mover el grupo de un flotante a una ventana nueva) y el
 * fusionar (mover todas las pestanas de una secundaria a la principal) como
 * transferencias entre dos ::TabArray, verificando: el grupo destino asignado,
 * el conteo en ambos lados y la reparacion de los indices guardados del origen.
 */
#include "ctests.h"
#include "app/tabmove.h"

/* Mover UNA pestana de src[idx] a dst con grupo dst_group: append en dst +
 * remove (compactando + reparando indices) en src.  Modela editor_move_tab_between. */
static void move_one(TabArray *src, int idx, TabArray *dst, int dst_group,
                     int *src_active, int *src_group_active, int n_groups) {
    int di = tabmove_append(dst, dst_group);
    EXPECT_NEQ_INT(di, -1);
    tabmove_remove(src, idx, src_active, src_group_active, n_groups);
}

/* Anyadir al final crece el array y asigna el grupo pedido. */
static void test_append(void) {
    int g[4] = {0};
    TabArray t = {g, 0, 4};
    EXPECT_EQ_INT(tabmove_append(&t, 5), 0);
    EXPECT_EQ_INT(tabmove_append(&t, 7), 1);
    EXPECT_EQ_INT(t.count, 2);
    EXPECT_EQ_INT(t.group[0], 5);
    EXPECT_EQ_INT(t.group[1], 7);
    /* lleno: el siguiente append falla */
    tabmove_append(&t, 1);
    tabmove_append(&t, 2);
    EXPECT_EQ_INT(tabmove_append(&t, 9), -1);
    EXPECT_EQ_INT(t.count, 4);
}

/* Quitar compacta y repara los indices guardados mayores que el quitado. */
static void test_remove_repara_indices(void) {
    int g[4] = {0, 0, 1, 1};
    TabArray t = {g, 4, 4};
    int active = 3;                 /* activa global = ultima */
    int group_active[2] = {1, 3};   /* grupo 0 -> tab1, grupo 1 -> tab3 */
    /* quitar la tab 1 (grupo 0): los indices > 1 bajan uno */
    tabmove_remove(&t, 1, &active, group_active, 2);
    EXPECT_EQ_INT(t.count, 3);
    EXPECT_EQ_INT(t.group[0], 0);   /* 0 se queda */
    EXPECT_EQ_INT(t.group[1], 1);   /* lo que era tab2 (grupo 1) */
    EXPECT_EQ_INT(t.group[2], 1);
    EXPECT_EQ_INT(active, 2);            /* 3 -> 2 */
    EXPECT_EQ_INT(group_active[0], 1);  /* 1 == idx quitado: NO se desplaza */
    EXPECT_EQ_INT(group_active[1], 2);  /* 3 -> 2 */
}

/* Desprender: mover el grupo 1 (tabs 1 y 3) de la principal a una ventana nueva.
 * La nueva queda con 2 pestanas en su grupo 0; la principal con las del grupo 0. */
static void test_desprender_grupo(void) {
    int sg[4] = {0, 1, 0, 1};
    TabArray src = {sg, 4, 4};
    int dg[4] = {0};
    TabArray dst = {dg, 0, 4};
    int src_active = 1;
    int src_group_active[2] = {0, 1}; /* grupo 0 -> tab0, grupo 1 -> tab1 */

    /* mover todas las del grupo 1 (re-escaneando desde el principio tras cada
     * remove, igual que editor_transfer_group). */
    for (;;) {
        int si = -1;
        for (int i = 0; i < src.count; i++)
            if (src.group[i] == 1) { si = i; break; }
        if (si < 0) break;
        move_one(&src, si, &dst, 0 /*grupo destino*/, &src_active,
                 src_group_active, 2);
    }

    /* destino: 2 pestanas, ambas en su grupo 0 */
    EXPECT_EQ_INT(dst.count, 2);
    EXPECT_EQ_INT(dst.group[0], 0);
    EXPECT_EQ_INT(dst.group[1], 0);
    /* origen: solo quedan las del grupo 0 */
    EXPECT_EQ_INT(src.count, 2);
    EXPECT_EQ_INT(src.group[0], 0);
    EXPECT_EQ_INT(src.group[1], 0);
}

/* Fusionar: mover TODAS las pestanas de una secundaria (2 grupos) a la principal,
 * aplanandolas en su grupo de foco (grupo 0). */
static void test_fusionar_todo(void) {
    int sg[3] = {0, 1, 1};       /* secundaria con 2 grupos */
    TabArray src = {sg, 3, 3};
    int dg[5] = {0, 0};           /* principal ya tiene 2 pestanas */
    TabArray dst = {dg, 2, 5};
    int src_active = 2;
    int src_group_active[2] = {0, 1};

    while (src.count > 0)
        move_one(&src, 0, &dst, 0 /*grupo foco de la principal*/, &src_active,
                 src_group_active, 2);

    EXPECT_EQ_INT(src.count, 0);          /* secundaria vacia tras fusionar */
    EXPECT_EQ_INT(dst.count, 5);          /* 2 previas + 3 movidas */
    for (int i = 0; i < dst.count; i++)
        EXPECT_EQ_INT(dst.group[i], 0);   /* todas aplanadas al grupo 0 */
}

/* Mover UNA pestana suelta de una ventana a OTRA por arrastre (editor_transfer_tab):
 * la del medio (idx 1, grupo 0) viaja al grupo de foco (1) de la receptora.  El
 * origen compacta + repara; el destino la anyade al final con el grupo destino. */
static void test_transferir_una_a_otra(void) {
    int sg[3] = {0, 0, 0};       /* origen: 3 pestanas en su grupo 0 */
    TabArray src = {sg, 3, 3};
    int dg[5] = {1, 1};          /* destino: 2 pestanas en su grupo 1 */
    TabArray dst = {dg, 2, 5};
    int src_active = 2;          /* activa global del origen = ultima */
    int src_group_active[2] = {2, 0}; /* grupo 0 -> tab2; (grupo 1 sin uso) */

    /* mover la pestana 1 del origen al grupo 1 del destino */
    move_one(&src, 1, &dst, 1, &src_active, src_group_active, 2);

    /* origen: 2 pestanas, indices reparados (2 -> 1 al compactar) */
    EXPECT_EQ_INT(src.count, 2);
    EXPECT_EQ_INT(src.group[0], 0);
    EXPECT_EQ_INT(src.group[1], 0);
    EXPECT_EQ_INT(src_active, 1);          /* 2 -> 1 */
    EXPECT_EQ_INT(src_group_active[0], 1); /* 2 -> 1 */

    /* destino: 3 pestanas, la movida al final con el grupo de foco (1) */
    EXPECT_EQ_INT(dst.count, 3);
    EXPECT_EQ_INT(dst.group[2], 1);
}

/* Tear-off: sacar la UNICA pestana de una ventana a una ventana NUEVA vacia.
 * El origen queda sin pestanas (count 0); la nueva con esa pestana en su grupo 0. */
static void test_tearoff_una_a_ventana_nueva(void) {
    int sg[1] = {0};
    TabArray src = {sg, 1, 1};
    int dg[2] = {0};
    TabArray dst = {dg, 0, 2}; /* ventana nueva: vacia */
    int src_active = 0;
    int src_group_active[1] = {0};

    move_one(&src, 0, &dst, 0, &src_active, src_group_active, 1);

    EXPECT_EQ_INT(src.count, 0);   /* origen vacio: pasa a la bienvenida */
    EXPECT_EQ_INT(dst.count, 1);   /* la nueva recibe la pestana */
    EXPECT_EQ_INT(dst.group[0], 0);
}

int main(void) {
    tt_suite("tabmove");
    tt_run("append crece y asigna grupo", test_append);
    tt_run("remove compacta y repara indices guardados",
           test_remove_repara_indices);
    tt_run("desprender mueve el grupo a una ventana nueva",
           test_desprender_grupo);
    tt_run("fusionar mueve todas las pestanas a la principal",
           test_fusionar_todo);
    tt_run("transferir UNA pestana arrastrada a otra ventana",
           test_transferir_una_a_otra);
    tt_run("tear-off de la unica pestana a una ventana nueva",
           test_tearoff_una_a_ventana_nueva);
    return tt_summary();
}
