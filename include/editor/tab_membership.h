#pragma once
/**
 * @file tab_membership.h
 * @brief Logica PURA de la invariante "pestana activa por grupo".
 *
 * Cada grupo de pestanas (una hoja del dock o un panel flotante) registra en
 * @c group_active_tab[group] el indice GLOBAL de su pestana activa.  La
 * invariante que el editor debe mantener SIEMPRE es:
 *
 *   group_active_tab[g] == -1   (grupo vacio: no hay pestana activa)
 *   o bien
 *   tabs[group_active_tab[g]].group == g   (la pestana activa pertenece al grupo)
 *
 * Si esta invariante se rompe (p.ej. al desprender la unica pestana de una hoja
 * a un flotante, dejando el indice viejo apuntando a una pestana que ya cambio
 * de grupo), el render del dock dibujaria el buffer de OTRO grupo -> dos areas
 * mostrando/editando el mismo buffer.
 *
 * Este modulo opera SOLO sobre enteros crudos (el grupo de cada pestana y el
 * array group_active_tab), sin SDL ni la struct Editor, asi que se prueba en
 * headless (ver test/editor/test_tab_membership.c).
 */

/**
 * @brief Indice GLOBAL valido de la pestana activa de @p group, o -1.
 *
 * Devuelve el indice registrado en @p group_active_tab[group] SOLO si sigue
 * siendo una pestana viva que pertenece a @p group; en otro caso, la primera
 * pestana de @p group; y si el grupo esta vacio, -1.  Funcion pura: no muta nada.
 *
 * @param tab_groups       Array con el group_id de cada pestana (longitud @p tab_count).
 * @param tab_count        Numero de pestanas vivas.
 * @param group_active_tab Array indexado por group_id con el indice activo guardado.
 * @param group            group_id a consultar.
 * @return Indice global valido en [0,tab_count) que pertenece a @p group, o -1.
 */
int tab_group_valid_active(const int *tab_groups, int tab_count,
                           const int *group_active_tab, int group);

/**
 * @brief Restaura la invariante de @p group escribiendo en @p group_active_tab.
 *
 * Tras una mutacion de membresia (mover una pestana de grupo, cerrar, etc.),
 * llamar a esta funcion con el grupo afectado deja @c group_active_tab[group]
 * apuntando a una pestana que pertenece a @p group, o a -1 si quedo vacio.
 *
 * @param tab_groups       Array con el group_id de cada pestana (longitud @p tab_count).
 * @param tab_count        Numero de pestanas vivas.
 * @param group_active_tab Array a reparar in-place.
 * @param group            group_id a reparar.
 * @return El valor escrito en @c group_active_tab[group] (indice valido o -1).
 */
int tab_membership_repair(const int *tab_groups, int tab_count,
                          int *group_active_tab, int group);
