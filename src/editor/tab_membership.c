/**
 * @file tab_membership.c
 * @brief Implementacion pura de la invariante "pestana activa por grupo"
 *        (ver editor/tab_membership.h).
 */
#include "editor/tab_membership.h"

int tab_group_valid_active(const int *tab_groups, int tab_count,
                           const int *group_active_tab, int group) {
    /* sin pestanas vivas: el grupo esta vacio por definicion */
    if (tab_count <= 0) return -1;

    /* el indice guardado vale solo si esta vivo Y pertenece al grupo */
    int saved = group_active_tab[group];
    if (saved >= 0 && saved < tab_count && tab_groups[saved] == group)
        return saved;

    /* si no, la primera pestana del grupo (la que sobreviva tras la mutacion) */
    for (int i = 0; i < tab_count; i++)
        if (tab_groups[i] == group) return i;

    /* ninguna pestana pertenece al grupo: vacio */
    return -1;
}

int tab_membership_repair(const int *tab_groups, int tab_count,
                          int *group_active_tab, int group) {
    int v = tab_group_valid_active(tab_groups, tab_count, group_active_tab, group);
    group_active_tab[group] = v; /* deja la invariante restaurada (indice valido o -1) */
    return v;
}
