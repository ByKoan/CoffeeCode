#include "buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── helpers internos ───────────────────────────────────────────────────── */

static size_t gap_size(const Buffer *b) {
    return b->gap_end - b->gap_start;
}

/* Asegura que el hueco tenga al menos `need` bytes */
static int ensure_gap(Buffer *b, size_t need) {
    if (gap_size(b) >= need) return 1;

    size_t new_gap  = need + BUFFER_GAP_MIN;
    size_t old_size = b->size;
    size_t new_size = old_size + new_gap - gap_size(b);

    char *nd = realloc(b->data, new_size);
    if (!nd) return 0;
    b->data = nd;

    /* mueve el bloque derecho para abrir espacio */
    size_t right_len = old_size - b->gap_end;
    memmove(b->data + new_size - right_len,
            b->data + b->gap_end,
            right_len);

    b->gap_end = new_size - right_len;
    b->size    = new_size;
    return 1;
}

/* Mueve el hueco a la posición lógica `pos` */
static void move_gap_to(Buffer *b, size_t pos) {
    size_t cur = b->gap_start;
    if (pos == cur) return;

    if (pos < cur) {
        /* mover texto de [pos, cur) hacia la derecha (dentro del hueco) */
        size_t len = cur - pos;
        memmove(b->data + b->gap_end - len,
                b->data + pos,
                len);
        b->gap_start = pos;
        b->gap_end  -= len;
    } else {
        /* mover texto de [gap_end, gap_end+(pos-cur)) hacia la izquierda */
        size_t len = pos - cur;
        memmove(b->data + cur,
                b->data + b->gap_end,
                len);
        b->gap_start  = pos;
        b->gap_end   += len;
    }
}

/* Convierte posición lógica → índice físico en el array */
static size_t phys(const Buffer *b, size_t pos) {
    return pos < b->gap_start ? pos : pos + gap_size(b);
}

/* ── ciclo de vida ──────────────────────────────────────────────────────── */

int buf_init(Buffer *b) {
    b->data = malloc(BUFFER_INIT_SIZE);
    if (!b->data) return 0;
    b->size      = BUFFER_INIT_SIZE;
    b->gap_start = 0;
    b->gap_end   = BUFFER_INIT_SIZE;
    return 1;
}

void buf_free(Buffer *b) {
    free(b->data);
    b->data = NULL;
    b->size = b->gap_start = b->gap_end = 0;
}

/* ── edición ────────────────────────────────────────────────────────────── */

void buf_insert(Buffer *b, char c) {
    if (!ensure_gap(b, 1)) return;
    b->data[b->gap_start++] = c;
}

void buf_insert_str(Buffer *b, const char *s, size_t len) {
    if (!ensure_gap(b, len)) return;
    memcpy(b->data + b->gap_start, s, len);
    b->gap_start += len;
}

void buf_delete_before(Buffer *b) {
    if (b->gap_start == 0) return;
    b->gap_start--;
}

void buf_delete_after(Buffer *b) {
    if (b->gap_end == b->size) return;
    b->gap_end++;
}

/* ── NUEVO: edición de rangos ───────────────────────────────────────────── */

void buf_delete_range(Buffer *b, size_t from, size_t to) {
    size_t len = buf_length(b);
    if (from > len) from = len;
    if (to   > len) to   = len;
    if (from >= to) return;

    /* Mover el hueco a `from`, luego extenderlo hasta `to` */
    move_gap_to(b, from);
    b->gap_end += (to - from);
}

size_t buf_get_text(const Buffer *b, size_t from, size_t to, char *out) {
    size_t len = buf_length(b);
    if (from > len) from = len;
    if (to   > len) to   = len;
    if (from >= to) return 0;

    size_t n = to - from;
    for (size_t i = 0; i < n; i++)
        out[i] = buf_char_at(b, from + i);
    return n;
}

/* ── movimiento ─────────────────────────────────────────────────────────── */

void buf_move_left(Buffer *b) {
    if (b->gap_start == 0) return;
    b->gap_end--;
    b->data[b->gap_end] = b->data[b->gap_start - 1];
    b->gap_start--;
}

void buf_move_right(Buffer *b) {
    if (b->gap_end == b->size) return;
    b->data[b->gap_start] = b->data[b->gap_end];
    b->gap_start++;
    b->gap_end++;
}

void buf_move_to(Buffer *b, size_t pos) {
    size_t len = buf_length(b);
    if (pos > len) pos = len;
    move_gap_to(b, pos);
}

/* ── consulta ───────────────────────────────────────────────────────────── */

size_t buf_length(const Buffer *b) {
    return b->size - gap_size(b);
}

char buf_char_at(const Buffer *b, size_t pos) {
    return b->data[phys(b, pos)];
}

size_t buf_cursor_pos(const Buffer *b) {
    return b->gap_start;
}

int buf_line_count(const Buffer *b) {
    size_t len = buf_length(b);
    int lines = 1;
    for (size_t i = 0; i < len; i++)
        if (buf_char_at(b, i) == '\n') lines++;
    return lines;
}

int buf_line_col(const Buffer *b, size_t pos, int *line, int *col) {
    *line = 0; *col = 0;
    for (size_t i = 0; i < pos; i++) {
        if (buf_char_at(b, i) == '\n') { (*line)++; *col = 0; }
        else (*col)++;
    }
    return 1;
}

size_t buf_line_start(const Buffer *b, size_t pos) {
    if (pos == 0) return 0;
    size_t i = pos - 1;
    while (i > 0 && buf_char_at(b, i) != '\n') i--;
    return (buf_char_at(b, i) == '\n') ? i + 1 : 0;
}

size_t buf_line_end(const Buffer *b, size_t pos) {
    size_t len = buf_length(b);
    while (pos < len && buf_char_at(b, pos) != '\n') pos++;
    return pos;
}

/* ── carga / guarda ─────────────────────────────────────────────────────── */

int buf_load_file(Buffer *b, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0) { fclose(f); return 1; }

    /* reinicia el buffer */
    buf_free(b);
    b->data = malloc((size_t)fsize + BUFFER_GAP_MIN);
    if (!b->data) { fclose(f); return 0; }

    size_t read = fread(b->data, 1, (size_t)fsize, f);
    fclose(f);

    b->gap_start = read;
    b->gap_end   = read + BUFFER_GAP_MIN;
    b->size      = read + BUFFER_GAP_MIN;
    memset(b->data + read, 0, BUFFER_GAP_MIN);
    return 1;
}

int buf_save_file(const Buffer *b, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;

    if (b->gap_start > 0)
        fwrite(b->data, 1, b->gap_start, f);

    size_t right_start = b->gap_end;
    size_t right_len   = b->size - b->gap_end;
    if (right_len > 0)
        fwrite(b->data + right_start, 1, right_len, f);

    fclose(f);
    return 1;
}