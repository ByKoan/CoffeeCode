#include "editor_internal.h"

static void undo_entry_free(UndoEntry *ue) {
    free(ue->text);
    ue->text = NULL;
    ue->len = 0;
}

static void undo_discard_redo(UndoStack *us) {
    /* las entradas rehacibles son las redo_top más recientes (final del ring) */
    while (us->redo_top > 0) {
        UndoEntry ue;
        if (ring_pop_back(&us->entries, &ue)) undo_entry_free(&ue);
        us->redo_top--;
    }
}

static void undo_push(UndoStack *us, UndoType type, size_t pos, const char *text, size_t len,
                      int cl, int cc) {
    undo_discard_redo(us);

    UndoEntry ue;
    ue.type = type;
    ue.pos = pos;
    ue.text = malloc(len + 1);
    if (ue.text) {
        memcpy(ue.text, text, len);
        ue.text[len] = '\0';
    }
    ue.len = len;
    ue.cursor_line_after = cl;
    ue.cursor_col_after = cc;

    /* si el ring está lleno, push sobrescribe la más antigua: liberar su text */
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

void editor_undo(Editor *e) {
    UndoStack *us = e->undo;
    int count = (int)ring_len(&us->entries);
    if (count == 0 || count == us->redo_top) return;

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
    editor_sync_cursor(e);
    editor_update_lexer(e, 0);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
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
    editor_sync_cursor(e);
    editor_update_lexer(e, 0);
    editor_ensure_visible(e);
    e->modified = 1;
    e->needs_redraw = 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * INIT / FREE / RUN
 * ═══════════════════════════════════════════════════════════════════════════ */
