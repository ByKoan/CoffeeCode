#pragma once
/**
 * @file tab_reorder.h
 * @brief Logica PURA para reordenar/insertar pestanas arrastrandolas dentro de
 *        una barra (la misma o la de otro grupo), en una posicion concreta.
 *
 * Dos operaciones, ambas sobre enteros crudos (sin SDL ni la struct Editor),
 * por lo que se prueban en headless (ver test/editor/test_tab_reorder.c):
 *
 *   1. ::tab_reorder_insert_index  -- dado un cursor X y los rects de las
 *      pestanas de un grupo (en orden de pantalla), calcula la posicion de
 *      insercion (0..n) bajo el cursor.
 *
 *   2. ::tab_reorder_move  -- mueve la pestana de indice global @p from para que
 *      quede en la posicion @p insert_pos DENTRO de @p target_group, reasigna su
 *      grupo, calcula la permutacion del array de pestanas y remapea todos los
 *      indices guardados (active_tab + group_active_tab[]) para que sigan
 *      apuntando a la MISMA pestana logica.
 */

/** Un rectangulo minimo para el calculo de insercion (solo X y ancho). */
typedef struct {
    int x; /**< X izquierda de la pestana en pantalla. */
    int w; /**< Ancho de la pestana en pixeles.        */
} TabRect;

/**
 * @brief Posicion de insercion (0..n) bajo el cursor en una barra de pestanas.
 *
 * Recorre las @p n pestanas del grupo EN ORDEN DE PANTALLA y devuelve el indice
 * ANTES del cual caeria la pestana arrastrada: la primera cuya mitad (centro X)
 * queda a la derecha del cursor.  Si el cursor esta a la derecha de todas,
 * devuelve @p n (insertar al final).
 *
 * @param rects Array de @p n rects de las pestanas del grupo, en orden visual.
 * @param n     Numero de pestanas del grupo (>=0).
 * @param mx    X del cursor en pixeles.
 * @return Posicion de insercion en [0, n].
 */
int tab_reorder_insert_index(const TabRect *rects, int n, int mx);

/**
 * @brief Mueve la pestana @p from al grupo @p target_group en la posicion
 *        @p insert_pos, reordenando @p tab_groups y remapeando los indices
 *        guardados.
 *
 * Modela el array de pestanas como una lista plana (orden global == orden
 * visual dentro de cada grupo, ya que el render dibuja las pestanas de un grupo
 * en su orden dentro del array).  La pestana @p from se EXTRAE y se RE-INSERTA
 * de modo que sea la @p insert_pos-esima pestana de @p target_group; el resto
 * conserva su orden relativo.  Su grupo pasa a @p target_group.
 *
 * Efectos sobre los arrays (todos in-place):
 *   - @p tab_groups se reordena para reflejar la nueva posicion (y el nuevo
 *     grupo de la pestana movida).
 *   - @p group_active_tab[g] (para todo g) y @p *active_tab se remapean para
 *     seguir apuntando a la MISMA pestana logica que antes.
 *
 * @param tab_groups       Array group_id de cada pestana (longitud @p tab_count).
 * @param tab_count        Numero de pestanas.
 * @param from             Indice global de la pestana a mover.
 * @param target_group     group_id destino donde insertarla.
 * @param insert_pos       Posicion (0..k) dentro de @p target_group; se acota a
 *                         [0, num de pestanas del grupo destino].  La cuenta del
 *                         grupo destino EXCLUYE la propia pestana movida cuando
 *                         ya pertenece a @p target_group (reordenado intra-grupo).
 * @param[in,out] active_tab        Indice global de la pestana activa global.
 * @param group_active_tab Array indexado por group_id (longitud @p group_count).
 * @param group_count      Longitud de @p group_active_tab.
 * @param[out] out_new_order  Si no es NULL, recibe la permutacion aplicada:
 *                            out_new_order[k] = indice VIEJO que pasa a la nueva
 *                            posicion k.  Permite al llamante reordenar arrays
 *                            paralelos (p.ej. la tabla de pestanas del editor)
 *                            de forma identica.  Longitud @p tab_count.
 * @return El nuevo indice global de la pestana movida tras el reordenado.
 */
int tab_reorder_move(int *tab_groups, int tab_count, int from, int target_group,
                     int insert_pos, int *active_tab, int *group_active_tab,
                     int group_count, int *out_new_order);
