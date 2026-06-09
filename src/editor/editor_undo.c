/**
 * @file editor_undo.c
 * @brief Pila de undo/redo (sobre Ring<UndoEntry>) y aplicación de las
 *        operaciones de deshacer/rehacer.
 *
 * @note Modelo de undo/redo. Las ediciones se apilan en un @c Ring (búfer
 * circular de capacidad @c UNDO_MAX): cuando se llena, la entrada más antigua
 * se sobrescribe (historial acotado). La novedad de este diseño es que redo NO
 * usa una segunda pila: las entradas deshechas se quedan en el ring y un
 * contador,
 * @c redo_top, dice cuántas de las más recientes están "deshechas" (pendientes
 * de rehacer). Así, las entradas vivas (las que se pueden deshacer) son las
 * @c (len - redo_top) primeras, y las deshechas son las @c redo_top últimas. Al
 * apilar una edición NUEVA se descarta esa rama de redo: rehacer ya no tendría
 * sentido porque la historia divergió.
 */
#include "editor_internal.h"

/**
 * @brief Libera el texto malloc'd de una entrada de undo y la deja vacía.
 * @param ue Entrada cuyo @c text se libera (queda a NULL, @c len a 0).
 */
static void undo_entry_free(UndoEntry *ue) {
    free(ue->text);
    ue->text = NULL;
    ue->len = 0;
}

/**
 * @brief Descarta la rama de redo: las entradas deshechas pendientes de
 * rehacer.
 *
 * Son las @c redo_top entradas más recientes del ring (las del final). Se sacan
 * por la cola con @c ring_pop_back y se liberan sus textos. Se llama al apilar
 * una edición nueva, porque a partir de ese punto la historia de redo ya no es
 * válida.
 *
 * @param us Pila de undo cuya rama de redo se elimina.
 */
static void undo_discard_redo(UndoStack *us) {
    while (us->redo_top > 0) {
        UndoEntry ue;
        /* ring_pop_back saca la entrada más reciente (la del final del ring) */
        if (ring_pop_back(&us->entries, &ue)) undo_entry_free(&ue);
        us->redo_top--;
    }
}

/**
 * @brief Apila una operación de edición en la pila de undo.
 *
 * Paso a paso: (1) descarta la rama de redo pendiente (una edición nueva
 * invalida los redos); (2) construye la @c UndoEntry copiando los bytes
 * afectados a un
 * @c text propio (malloc'd, terminado en NUL); (3) si el ring está lleno,
 * libera antes el @c text de la entrada más antigua, que @c ring_push va a
 * sobrescribir (el ring no gestiona esa memoria); (4) empuja la entrada y deja
 * @c redo_top a 0.
 *
 * @param us          Pila de undo destino.
 * @param type        Tipo de operación: @c UNDO_INSERT o @c UNDO_DELETE.
 * @param pos         Posición lógica en el buffer donde ocurrió la edición.
 * @param text        Bytes afectados (insertados o borrados); se copian.
 * @param len         Número de bytes de @p text.
 * @param cursor_line Línea del cursor tras la edición (para restaurar al
 * rehacer).
 * @param cursor_col  Columna del cursor tras la edición.
 */
static void undo_push(UndoStack *us, UndoType type, size_t pos,
                      const char *text, size_t len, int cursor_line,
                      int cursor_col) {
    undo_discard_redo(
        us); /* la nueva edición invalida cualquier redo pendiente */

    UndoEntry ue = {.type = type,
                    .pos = pos,
                    .len = len,
                    .cursor_line_after = cursor_line,
                    .cursor_col_after = cursor_col};
    /* copia propia de los bytes afectados (+1 para el NUL terminador) */
    ue.text = malloc(len + 1);
    if (ue.text) {
        memcpy(ue.text, text, len);
        ue.text[len] = '\0';
    }

    /* el ring no libera lo que sobrescribe: liberar el text de la más antigua
     */
    if (ring_full(&us->entries)) {
        UndoEntry *oldest = (UndoEntry *)ring_front(&us->entries);
        if (oldest) free(oldest->text);
    }
    ring_push(&us->entries, &ue);
    us->redo_top = 0; /* tras apilar no hay nada que rehacer */
}

/**
 * @brief Registra en la pila de undo una inserción de texto.
 *
 * Lo llama input.c justo después de insertar @p text en el buffer, para poder
 * deshacer esa inserción más tarde (el undo la borrará).
 *
 * @param e    Editor (su @c undo apunta a la pila de la pestaña activa).
 * @param pos  Posición lógica donde se insertó.
 * @param text Bytes insertados.
 * @param len  Número de bytes insertados.
 */
void editor_undo_push_insert(Editor *e, size_t pos, const char *text,
                             size_t len) {
    undo_push(e->undo, UNDO_INSERT, pos, text, len, e->cursor_line,
              e->cursor_col);
}

/**
 * @brief Registra en la pila de undo un borrado de texto.
 *
 * Lo llama input.c justo antes/después de borrar @p text del buffer, para poder
 * deshacer ese borrado más tarde (el undo lo reinsertará).
 *
 * @param e    Editor (su @c undo apunta a la pila de la pestaña activa).
 * @param pos  Posición lógica donde se borró.
 * @param text Bytes borrados (se copian para poder restaurarlos).
 * @param len  Número de bytes borrados.
 */
void editor_undo_push_delete(Editor *e, size_t pos, const char *text,
                             size_t len) {
    undo_push(e->undo, UNDO_DELETE, pos, text, len, e->cursor_line,
              e->cursor_col);
}

/**
 * @brief Refresca el estado del editor tras aplicar un undo o un redo.
 *
 * Sincroniza el cursor desde el buffer, marca el lexer para re-tokenizar,
 * asegura que el cursor quede visible, y señala el documento como modificado y
 * pendiente de redibujar.
 *
 * @param e Editor a refrescar.
 */
static void after_undo_redo(Editor *e) {
    editor_sync_cursor(e);
    editor_update_lexer(e, 0);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
}

/**
 * @brief Deshace la última edición no deshecha (Ctrl+Z).
 *
 * La entrada a deshacer es la más reciente de las "vivas": índice
 * @c (len - 1 - redo_top) en el ring. Se invierte su operación:
 *   - @c UNDO_INSERT (se había insertado): para deshacer, se BORRA el rango.
 *   - @c UNDO_DELETE (se había borrado): para deshacer, se REINSERTA el texto.
 * Luego incrementa @c redo_top (esa entrada pasa a la rama de redo). No hace
 * nada si no hay entradas vivas (ring vacío o todo ya deshecho).
 *
 * @param e Editor sobre el que deshacer.
 */
void editor_undo(Editor *e) {
    UndoStack *us = e->undo;
    int count = (int)ring_len(&us->entries);
    /* nada vivo que deshacer: ring vacío o todas las entradas ya deshechas */
    if (count == 0 || count == us->redo_top) return;

    /* la entrada a deshacer es la más reciente aún no deshecha */
    UndoEntry *ue =
        (UndoEntry *)ring_at(&us->entries, (size_t)(count - 1 - us->redo_top));
    if (ue->type == UNDO_INSERT) {
        /* deshacer una inserción = borrar lo insertado */
        buf_delete_range(e->buf, ue->pos, ue->pos + ue->len);
        buf_move_to(e->buf, ue->pos);
    } else {
        /* deshacer un borrado = reinsertar lo borrado */
        buf_move_to(e->buf, ue->pos);
        buf_insert_str(e->buf, ue->text, ue->len);
        buf_move_to(e->buf, ue->pos);
    }
    us->redo_top++; /* la entrada pasa a estar "deshecha" (rehacerla es posible)
                     */
    after_undo_redo(e);
}

/**
 * @brief Rehace la última edición deshecha (Ctrl+Y / Ctrl+Shift+Z).
 *
 * Solo tiene efecto si hay rama de redo (@c redo_top > 0). Decrementa @c
 * redo_top (la entrada vuelve a estar "viva") y reaplica su operación ORIGINAL
 * — al revés que undo:
 *   - @c UNDO_INSERT: vuelve a INSERTAR el texto.
 *   - @c UNDO_DELETE: vuelve a BORRAR el rango.
 *
 * @param e Editor sobre el que rehacer.
 */
void editor_redo(Editor *e) {
    UndoStack *us = e->undo;
    if (us->redo_top == 0) return; /* no hay nada deshecho que rehacer */

    us->redo_top--; /* la entrada más reciente deshecha vuelve a estar viva */
    int count = (int)ring_len(&us->entries);
    UndoEntry *ue =
        (UndoEntry *)ring_at(&us->entries, (size_t)(count - 1 - us->redo_top));
    if (ue->type == UNDO_INSERT) {
        /* rehacer una inserción = volver a insertar */
        buf_move_to(e->buf, ue->pos);
        buf_insert_str(e->buf, ue->text, ue->len);
        buf_move_to(e->buf, ue->pos + ue->len);
    } else {
        /* rehacer un borrado = volver a borrar */
        buf_delete_range(e->buf, ue->pos, ue->pos + ue->len);
        buf_move_to(e->buf, ue->pos);
    }
    after_undo_redo(e);
}
