/**
 * @file tabmove.c
 * @brief Implementacion pura del movimiento de pestanas entre arrays (ver
 *        app/tabmove.h).  Sin SDL ni Editor: probada en headless.
 */
#include "app/tabmove.h"

int tabmove_append(TabArray *t, int new_group) {
    if (!t || t->count >= t->cap) return -1; /* sin sitio */
    int idx = t->count++;
    t->group[idx] = new_group;
    return idx;
}

void tabmove_remove(TabArray *t, int idx, int *p_active, int *group_active,
                    int n_groups) {
    if (!t || idx < 0 || idx >= t->count) return;
    /* compactar: desplazar a la izquierda las pestanas posteriores */
    for (int i = idx; i < t->count - 1; i++) t->group[i] = t->group[i + 1];
    t->count--;
    /* reparar los indices guardados que se desplazaron una posicion */
    if (p_active && *p_active > idx) (*p_active)--;
    if (group_active)
        for (int g = 0; g < n_groups; g++)
            if (group_active[g] > idx) group_active[g]--;
}
