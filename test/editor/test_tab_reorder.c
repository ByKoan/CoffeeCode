/**
 * @file test_tab_reorder.c
 * @brief Tests puros del reordenado/insercion de pestanas dentro de una barra.
 *
 * Verifica las dos funciones puras de editor/tab_reorder.h:
 *   - tab_reorder_insert_index: indice de insercion segun la X del cursor.
 *   - tab_reorder_move: reordenado del array + remapeo de los indices guardados
 *     (active_tab + group_active_tab[]) para que sigan apuntando a la MISMA
 *     pestana logica.
 */
#include "ctests.h"
#include "editor/tab_reorder.h"

/* -- tab_reorder_insert_index: calculo de la posicion por X ----------------- */

/* Tres pestanas de 100px (centros en 50,150,250).  El cursor decide el hueco. */
static void test_insert_index_por_x(void) {
    TabRect rects[] = {{0, 100}, {100, 100}, {200, 100}};
    EXPECT_EQ_INT(tab_reorder_insert_index(rects, 3, 10), 0);   /* antes de la 1a */
    EXPECT_EQ_INT(tab_reorder_insert_index(rects, 3, 49), 0);   /* mitad izq de la 1a */
    EXPECT_EQ_INT(tab_reorder_insert_index(rects, 3, 60), 1);   /* mitad der de la 1a */
    EXPECT_EQ_INT(tab_reorder_insert_index(rects, 3, 160), 2);  /* mitad der de la 2a */
    EXPECT_EQ_INT(tab_reorder_insert_index(rects, 3, 300), 3);  /* a la derecha de todas */
}

/* Barra vacia: cualquier X da posicion 0. */
static void test_insert_index_vacia(void) {
    EXPECT_EQ_INT(tab_reorder_insert_index(NULL, 0, 123), 0);
}

/* -- tab_reorder_move: reordenado intra-grupo ------------------------------- */

/* Cuatro pestanas en el mismo grupo (0).  Mover la pestana de la pos 0 a la pos
 * 2 del grupo: el orden de las pestanas cambia y los indices guardados siguen
 * apuntando a las MISMAS pestanas logicas. */
static void test_move_intra_grupo(void) {
    /* tabs A,B,C,D en grupo 0.  Identidad logica = letra. */
    int groups[] = {0, 0, 0, 0};
    int active_tab = 2;            /* C es la activa */
    int group_active[] = {2, 0};  /* grupo 0 activo = C (idx 2) */
    int new_order[4];

    /* mover idx 0 (A) a la posicion 2 del grupo 0 */
    int moved = tab_reorder_move(groups, 4, 0, 0, 2, &active_tab, group_active,
                                 2, new_order);

    /* nuevo orden esperado: B, C, A, D  (A se inserta antes de la 2a pestana del
     * grupo en la lista sin A = {B,C,D} -> entre C y D). */
    EXPECT_EQ_INT(new_order[0], 1); /* B */
    EXPECT_EQ_INT(new_order[1], 2); /* C */
    EXPECT_EQ_INT(new_order[2], 0); /* A (la movida) */
    EXPECT_EQ_INT(new_order[3], 3); /* D */
    EXPECT_EQ_INT(moved, 2);        /* A queda en la pos 2 */

    /* todas siguen en el grupo 0 */
    for (int i = 0; i < 4; i++) EXPECT_EQ_INT(groups[i], 0);

    /* la activa (C, old idx 2) ahora esta en la pos 1 */
    EXPECT_EQ_INT(active_tab, 1);
    EXPECT_EQ_INT(group_active[0], 1);
}

/* Mover de la pos 2 a la pos 0 del mismo grupo (caso del enunciado). */
static void test_move_intra_a_inicio(void) {
    int groups[] = {0, 0, 0};      /* A,B,C en grupo 0 */
    int active_tab = 0;            /* A activa */
    int group_active[] = {0, 0};
    int new_order[3];

    /* mover idx 2 (C) a la pos 0 del grupo -> C,A,B */
    int moved = tab_reorder_move(groups, 3, 2, 0, 0, &active_tab, group_active,
                                 2, new_order);
    EXPECT_EQ_INT(new_order[0], 2); /* C */
    EXPECT_EQ_INT(new_order[1], 0); /* A */
    EXPECT_EQ_INT(new_order[2], 1); /* B */
    EXPECT_EQ_INT(moved, 0);
    /* A (old 0) estaba activa; ahora en pos 1 */
    EXPECT_EQ_INT(active_tab, 1);
    EXPECT_EQ_INT(group_active[0], 1);
}

/* -- tab_reorder_move: mover a OTRO grupo en una posicion concreta ----------- */

/* Pestanas: A,B en grupo 0; C,D en grupo 1.  Mover B (idx 1, grupo 0) a la pos
 * 1 del grupo 1: B cambia de grupo y queda entre C y D.  El grupo 0 pierde una.
 * Los indices guardados se remapean. */
static void test_move_entre_grupos(void) {
    int groups[] = {0, 0, 1, 1};   /* A,B | C,D */
    int active_tab = 3;            /* D activa */
    int group_active[] = {0, 2};   /* grupo 0 = A(0), grupo 1 = C(2) */
    int new_order[4];

    /* mover idx 1 (B) al grupo 1 en la pos 1 (entre C y D) */
    int moved = tab_reorder_move(groups, 4, 1, 1, 1, &active_tab, group_active,
                                 2, new_order);

    /* lista sin B = {A,C,D}; insertar B antes de la 1a pestana del grupo 1
     * (insert_pos=1 -> tras la 1a del grupo) => A, C, B, D */
    EXPECT_EQ_INT(new_order[0], 0); /* A */
    EXPECT_EQ_INT(new_order[1], 2); /* C */
    EXPECT_EQ_INT(new_order[2], 1); /* B (movida) */
    EXPECT_EQ_INT(new_order[3], 3); /* D */
    EXPECT_EQ_INT(moved, 2);

    /* grupos resultantes: A=0, C=1, B=1, D=1 */
    EXPECT_EQ_INT(groups[0], 0);
    EXPECT_EQ_INT(groups[1], 1);
    EXPECT_EQ_INT(groups[2], 1);
    EXPECT_EQ_INT(groups[3], 1);

    /* D (old 3) era activa; ahora en pos 3 */
    EXPECT_EQ_INT(active_tab, 3);
    /* grupo 0 seguia apuntando a A (old 0) -> ahora pos 0 */
    EXPECT_EQ_INT(group_active[0], 0);
    /* grupo 1 seguia apuntando a C (old 2) -> ahora pos 1 */
    EXPECT_EQ_INT(group_active[1], 1);
}

/* Mover a OTRO grupo al INICIO (pos 0). */
static void test_move_entre_grupos_inicio(void) {
    int groups[] = {0, 1, 1};      /* A | C,D */
    int active_tab = 0;
    int group_active[] = {0, 1};   /* grupo1 = C(1) */
    int new_order[3];

    /* mover A (idx 0) al grupo 1 en la pos 0 -> A, C, D pero A ahora grupo 1.
     * lista sin A = {C,D}; insertar antes de la 1a del grupo 1 (C) => A,C,D */
    int moved = tab_reorder_move(groups, 3, 0, 1, 0, &active_tab, group_active,
                                 2, new_order);
    EXPECT_EQ_INT(new_order[0], 0); /* A */
    EXPECT_EQ_INT(new_order[1], 1); /* C */
    EXPECT_EQ_INT(new_order[2], 2); /* D */
    EXPECT_EQ_INT(moved, 0);
    EXPECT_EQ_INT(groups[0], 1); /* A ahora en grupo 1 */
    EXPECT_EQ_INT(groups[1], 1);
    EXPECT_EQ_INT(groups[2], 1);
    /* grupo 1 seguia apuntando a C (old 1) -> ahora pos 1 */
    EXPECT_EQ_INT(group_active[1], 1);
}

/* Mover a OTRO grupo al FINAL: insert_pos == num de pestanas del grupo destino. */
static void test_move_entre_grupos_final(void) {
    int groups[] = {0, 1, 1};      /* A | C,D */
    int active_tab = 0;
    int group_active[] = {0, 1};
    int new_order[3];

    /* mover A (idx 0) al grupo 1 al final (pos 2): lista sin A = {C,D}; tras la
     * ultima del grupo 1 (D) => C, D, A */
    int moved = tab_reorder_move(groups, 3, 0, 1, 2, &active_tab, group_active,
                                 2, new_order);
    EXPECT_EQ_INT(new_order[0], 1); /* C */
    EXPECT_EQ_INT(new_order[1], 2); /* D */
    EXPECT_EQ_INT(new_order[2], 0); /* A (movida, al final) */
    EXPECT_EQ_INT(moved, 2);
    EXPECT_EQ_INT(groups[2], 1); /* A ahora grupo 1 */
}

int main(void) {
    tt_suite("tab_reorder");
    tt_run("insert_index segun X", test_insert_index_por_x);
    tt_run("insert_index barra vacia", test_insert_index_vacia);
    tt_run("move intra-grupo de pos 0 a pos 2", test_move_intra_grupo);
    tt_run("move intra-grupo al inicio", test_move_intra_a_inicio);
    tt_run("move entre grupos en pos concreta", test_move_entre_grupos);
    tt_run("move entre grupos al inicio", test_move_entre_grupos_inicio);
    tt_run("move entre grupos al final", test_move_entre_grupos_final);
    return tt_summary();
}
