/**
 * @file test_tab_membership.c
 * @brief Tests puros de la invariante "pestana activa por grupo".
 *
 * Verifica que tab_group_valid_active nunca devuelve una pestana que no
 * pertenezca al grupo (origen del bug del "buffer compartido": un indice
 * guardado que quedo apuntando a una pestana movida a otro grupo/flotante).
 */
#include "ctests.h"
#include "editor/tab_membership.h"

/* El indice guardado vale si sigue vivo Y pertenece al grupo. */
static void test_guardado_valido(void) {
    int groups[] = {0, 0, 1};       /* tabs 0,1 -> grupo 0; tab 2 -> grupo 1 */
    int active[] = {1, 2, 0, 0};    /* grupo 0 activo=1, grupo 1 activo=2 */
    EXPECT_EQ_INT(tab_group_valid_active(groups, 3, active, 0), 1);
    EXPECT_EQ_INT(tab_group_valid_active(groups, 3, active, 1), 2);
}

/* Indice obsoleto (apunta a una pestana de OTRO grupo): cae a la primera del
 * grupo, nunca a la pestana ajena. */
static void test_indice_obsoleto(void) {
    int groups[] = {0, 1, 0};   /* tab 1 esta en el grupo 1, no en el 0 */
    int active[] = {1, 0};      /* grupo 0 activo=1 -> OBSOLETO (tab 1 es del grupo 1) */
    /* debe devolver la primera del grupo 0 (tab 0), NO la 1 (del grupo 1) */
    EXPECT_EQ_INT(tab_group_valid_active(groups, 3, active, 0), 0);
}

/* El escenario del bug: detachar la unica pestana del dock a un flotante deja
 * el grupo del dock vacio -> debe devolver -1, no la pestana flotante. */
static void test_dock_vacio_tras_detach(void) {
    /* antes: tab 0 en grupo 0 (dock), group_active_tab[0]=0.
     * detach: tab 0 pasa al grupo 1 (flotante) -> groups[0]=1; el guardado del
     * grupo 0 sigue siendo 0 (obsoleto). */
    int groups[] = {1};      /* la unica pestana ya esta en el flotante (grupo 1) */
    int active[] = {0, 0};   /* grupo 0 todavia "apunta" a la tab 0 (obsoleto) */
    EXPECT_EQ_INT(tab_group_valid_active(groups, 1, active, 0), -1); /* dock vacio */
    EXPECT_EQ_INT(tab_group_valid_active(groups, 1, active, 1), 0);  /* flotante */
}

/* Grupo sin ninguna pestana -> -1. */
static void test_grupo_vacio(void) {
    int groups[] = {0, 0};
    int active[] = {0, -1, -1};
    EXPECT_EQ_INT(tab_group_valid_active(groups, 2, active, 5), -1);
    /* sin pestanas vivas: cualquier grupo es vacio */
    EXPECT_EQ_INT(tab_group_valid_active(groups, 0, active, 0), -1);
}

/* tab_membership_repair deja group_active_tab[g] con un indice valido o -1. */
static void test_repair(void) {
    int groups[] = {1};       /* la pestana esta en el grupo 1 */
    int active[] = {0, 0};    /* grupo 0 obsoleto */
    int v = tab_membership_repair(groups, 1, active, 0);
    EXPECT_EQ_INT(v, -1);
    EXPECT_EQ_INT(active[0], -1); /* invariante restaurada */
}

int main(void) {
    tt_suite("tab_membership");
    tt_run("indice guardado valido se conserva", test_guardado_valido);
    tt_run("indice obsoleto cae a la primera del grupo", test_indice_obsoleto);
    tt_run("dock vacio tras detach devuelve -1 (no la flotante)",
           test_dock_vacio_tras_detach);
    tt_run("grupo sin pestanas devuelve -1", test_grupo_vacio);
    tt_run("repair restaura la invariante", test_repair);
    return tt_summary();
}
