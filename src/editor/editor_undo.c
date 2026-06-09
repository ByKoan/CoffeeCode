/**
 * @file editor_undo.c
 * @brief Pila de undo/redo (sobre Ring<UndoEntry>) y aplicación de las
 *        operaciones de deshacer/rehacer.
 */
#include "editor_internal.h"

/** Libera el texto de una entrada de undo. */
static void undo_entry_free(UndoEntry *ue) {
    free(ue->text);
    ue->text = NULL;
    ue->len = 0;
}

/** Descarta la rama de redo (entradas deshechas pendientes de rehacer). */
static void undo_discard_redo(UndoStack *us) {
    while (us->redo_top > 0) {
        UndoEntry ue;
        if (ring_pop_back(&us->entries, &ue)) undo_entry_free(&ue);
        us->redo_top--;
    }
}

/** Apila una operación de edición (descartando la rama de redo pendiente). */
static void undo_push(UndoStack *us, UndoType type, size_t pos, const char *text, size_t len,
                      int cursor_line, int cursor_col) {
    undo_discard_redo(us);

    UndoEntry ue = {.type = type,
                    .pos = pos,
                    .len = len,
                    .cursor_line_after = cursor_line,
                    .cursor_col_after = cursor_col};
    ue.text = malloc(len + 1);
    if (ue.text) {
        memcpy(ue.text, text, len);
        ue.text[len] = '\0';
    }

    /* el ring no libera lo que sobrescribe: liberar el text de la más antigua */
    if (ring_full(&us->entries)) {
        UndoEntry *oldest = (UndoEntry *)ring_front(&us->entries);
        if (oldest) free(oldest->text);
    }
    ring_push(&us->entries, &ue);
    us->redo_top = 0;
}

void editor_undo_push_insert(Editor *e, size_t pos, const char *text, size_t len) {
    undo_push(e->undo, UNDO_INSERT, pos, text, len, e->cursor_line, e->cursor_col);
}

void editor_undo_push_delete(Editor *e, size_t pos, const char *text, size_t len) {
    undo_push(e->undo, UNDO_DELETE, pos, text, len, e->cursor_line, e->cursor_col);
}

/** Refresca el estado tras aplicar un undo o redo. */
static void after_undo_redo(Editor *e) {
    editor_sync_cursor(e);
    editor_update_lexer(e, 0);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
}

void editor_undo(Editor *e) {
    UndoStack *us = e->undo;
    int count = (int)ring_len(&us->entries);
    if (count == 0 || count == us->redo_top) return;

    /* la entrada a deshacer es la más reciente aún no deshecha */
    UndoEntry *ue = (UndoEntry *)ring_at(&us->entries, (size_t)(count - 1 - us->redo_top));
    if (ue->type == UNDO_INSERT) {
        buf_delete_range(e->buf, ue->pos, ue->pos + ue->len);
        buf_move_to(e->buf, ue->pos);
    } else {
        buf_move_to(e->buf, ue->pos);
        buf_insert_str(e->buf, ue->text, ue->len);
        buf_move_to(e->buf, ue->pos);
    }
    us->redo_top++;
    after_undo_redo(e);
}

void editor_redo(Editor *e) {
    UndoStack *us = e->undo;
    if (us->redo_top == 0) return;

    us->redo_top--;
    int count = (int)ring_len(&us->entries);
    UndoEntry *ue = (UndoEntry *)ring_at(&us->entries, (size_t)(count - 1 - us->redo_top));
    if (ue->type == UNDO_INSERT) {
        buf_move_to(e->buf, ue->pos);
        buf_insert_str(e->buf, ue->text, ue->len);
        buf_move_to(e->buf, ue->pos + ue->len);
    } else {
        buf_delete_range(e->buf, ue->pos, ue->pos + ue->len);
        buf_move_to(e->buf, ue->pos);
    }
    after_undo_redo(e);
}
