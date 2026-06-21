/**
 * @file tab_reorder.c
 * @brief Implementacion pura del reordenado/insercion de pestanas dentro de una
 *        barra (ver editor/tab_reorder.h).
 */
#include "editor/tab_reorder.h"

#define TAB_REORDER_MAX 64 /* tope defensivo de pestanas para los buffers locales */

int tab_reorder_insert_index(const TabRect *rects, int n, int mx) {
    /* primera pestana cuyo centro X queda a la derecha del cursor: ahi se
     * inserta ANTES.  Si ninguna, al final (n). */
    for (int i = 0; i < n; i++) {
        int center = rects[i].x + rects[i].w / 2;
        if (mx < center) return i;
    }
    return n;
}

int tab_reorder_move(int *tab_groups, int tab_count, int from, int target_group,
                     int insert_pos, int *active_tab, int *group_active_tab,
                     int group_count, int *out_new_order) {
    if (tab_count <= 0 || from < 0 || from >= tab_count) return from;
    if (tab_count > TAB_REORDER_MAX) return from; /* defensivo: no deberia pasar */

    /* (1) Lista de indices viejos SIN la pestana movida, conservando el orden. */
    int rest[TAB_REORDER_MAX];
    int rest_n = 0;
    for (int i = 0; i < tab_count; i++)
        if (i != from) rest[rest_n++] = i;

    /* (2) Acotar insert_pos a [0, num pestanas del grupo destino en `rest`]. */
    int dst_count = 0;
    for (int i = 0; i < rest_n; i++)
        if (tab_groups[rest[i]] == target_group) dst_count++;
    if (insert_pos < 0) insert_pos = 0;
    if (insert_pos > dst_count) insert_pos = dst_count;

    /* (3) Posicion ABSOLUTA dentro de `rest` donde re-insertar: justo antes de
     *     la pestana `insert_pos`-esima del grupo destino.  Si insert_pos ==
     *     dst_count (al final del grupo), va tras la ultima pestana del grupo;
     *     para que quede CONTIGUA a las del grupo (y no al final global), se
     *     inserta tras la ultima pestana del grupo vista en `rest`. */
    int abs_pos;
    if (dst_count == 0) {
        /* el grupo destino no tiene mas pestanas: insertar al final global */
        abs_pos = rest_n;
    } else {
        int seen = 0;        /* pestanas del grupo destino vistas */
        int last_dst = -1;   /* ultima posicion en `rest` de una del grupo */
        abs_pos = -1;
        for (int i = 0; i < rest_n; i++) {
            if (tab_groups[rest[i]] == target_group) {
                if (seen == insert_pos) { abs_pos = i; break; }
                seen++;
                last_dst = i;
            }
        }
        if (abs_pos < 0) abs_pos = last_dst + 1; /* insert_pos == dst_count: tras la ultima */
    }

    /* (4) Construir el nuevo orden global de indices viejos: `rest` con `from`
     *     re-insertado en abs_pos. */
    int new_order[TAB_REORDER_MAX];
    int k = 0;
    for (int i = 0; i < rest_n; i++) {
        if (i == abs_pos) new_order[k++] = from;
        new_order[k++] = rest[i];
    }
    if (abs_pos == rest_n) new_order[k++] = from; /* al final */

    /* (5) old->new: inverso de new_order.  old_to_new[old] = nuevo indice. */
    int old_to_new[TAB_REORDER_MAX];
    for (int i = 0; i < tab_count; i++) old_to_new[new_order[i]] = i;

    /* exponer la permutacion para reordenar arrays paralelos del llamante */
    if (out_new_order)
        for (int i = 0; i < tab_count; i++) out_new_order[i] = new_order[i];

    /* (6) Reordenar tab_groups segun new_order y fijar el grupo de la movida. */
    int new_groups[TAB_REORDER_MAX];
    for (int i = 0; i < tab_count; i++) new_groups[i] = tab_groups[new_order[i]];
    for (int i = 0; i < tab_count; i++) tab_groups[i] = new_groups[i];
    int moved_new = old_to_new[from];
    tab_groups[moved_new] = target_group; /* su nuevo grupo */

    /* (7) Remapear los indices guardados a su nueva posicion. */
    if (active_tab && *active_tab >= 0 && *active_tab < tab_count)
        *active_tab = old_to_new[*active_tab];
    if (group_active_tab) {
        for (int g = 0; g < group_count; g++) {
            int v = group_active_tab[g];
            if (v >= 0 && v < tab_count) group_active_tab[g] = old_to_new[v];
        }
    }

    return moved_new;
}
