#pragma once
/**
 * @file tabmove.h
 * @brief Logica PURA (sin SDL ni Editor) del movimiento de pestanas entre dos
 *        arrays tabs[] de distintas ventanas (multi-ventana).
 *
 * Mover una pestana de una ventana a otra es: (a) ANYADIR su grupo al final del
 * array destino, (b) QUITARLA del origen compactando, y (c) REPARAR los indices
 * guardados del origen (la pestana activa y la activa-por-grupo) que se
 * desplazaron al compactar.  Esta cabecera modela esa aritmetica sobre arrays
 * planos de @c group_id (int) para poder probarla en headless, exactamente la
 * misma que aplican @c editor_transfer_group y @c editor_merge_all sobre los
 * structs EditorTab reales (que son copiables superficialmente).
 */
#include <stddef.h>

/** Modelo minimo de un array de pestanas: solo el group_id de cada una. */
typedef struct {
    int *group;   /**< group_id de cada pestana (longitud = count)      */
    int count;    /**< numero de pestanas                                */
    int cap;      /**< capacidad del array group[]                       */
} TabArray;

/**
 * @brief Anyade una pestana del grupo @p new_group al final de @p t.
 * @return Indice de la pestana anyadida, o -1 si no cabe (count == cap).
 */
int tabmove_append(TabArray *t, int new_group);

/**
 * @brief Quita la pestana de indice @p idx de @p t, compactando, y desplaza los
 *        indices guardados @p p_active y @p group_active[] (longitud
 *        @p n_groups) que fueran mayores que @p idx (igual que editor_tab_close).
 *
 * @param t            Array de pestanas (se compacta in situ).
 * @param idx          Indice a quitar (0 <= idx < count).
 * @param p_active     [in/out] indice activo global a reparar (puede ser NULL).
 * @param group_active [in/out] indices activos por grupo a reparar (o NULL).
 * @param n_groups     Longitud de @p group_active.
 */
void tabmove_remove(TabArray *t, int idx, int *p_active, int *group_active,
                    int n_groups);
