#include "buffer/buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ══════════════════════════════════════════════════════════════════════════
 * ÍNDICE DE LÍNEAS  (Vec<size_t>)
 * --------------------------------------------------------------------------
 * b->lines es un vector dinámico de offsets: lines[i] = posición lógica del
 * primer byte de la línea i. Invariante: lines[0] == 0 y siempre hay >= 1.
 *
 * Antes era un array crecido a mano (line_index/line_count/line_cap); ahora
 * reutiliza el vector genérico (structs/vec). El acceso en caliente usa la
 * macro LI(b) (puntero al array) re-leído tras cada realloc, sin coste extra.
 * ══════════════════════════════════════════════════════════════════════════ */

/** Puntero al array de offsets (se re-deriva tras cada posible realloc). */
#define LI(b)      ((size_t *)(b)->lines.data)
/** Número de líneas como int (la API pública usa int). */
#define LICOUNT(b) ((int)(b)->lines.len)

/*
 * Reconstruye el índice completo en una sola pasada O(n).
 * Se llama solo desde buf_load_file (o tras cambios masivos).
 */
static int li_rebuild(Buffer *b) {
    size_t len = buf_length(b);

    if (!vec_reserve(&b->lines, 1)) return 0;
    LI(b)[0] = 0;
    b->lines.len = 1;

    for (size_t i = 0; i < len; i++) {
        if (buf_char_at(b, i) == '\n') {
            if (!vec_reserve(&b->lines, b->lines.len + 1)) return 0;
            LI(b)[b->lines.len] = i + 1;
            b->lines.len++;
        }
    }
    return 1;
}

/*
 * Actualiza el índice tras insertar `len` bytes en la posición lógica `pos`.
 * Desplaza +len las entradas posteriores y añade una por cada '\n' insertado.
 */
static void li_after_insert(Buffer *b, size_t pos, const char *text, size_t len) {
    int count = LICOUNT(b);
    for (int i = 0; i < count; i++)
        if (LI(b)[i] > pos)
            LI(b)[i] += len;

    for (size_t k = 0; k < len; k++) {
        if (text[k] == '\n') {
            size_t new_start = pos + k + 1;
            count = LICOUNT(b);
            int ins = count;
            for (int i = 1; i < count; i++) {
                if (LI(b)[i] > new_start) { ins = i; break; }
            }
            if (!vec_reserve(&b->lines, (size_t)count + 1)) return;
            memmove(&LI(b)[ins + 1], &LI(b)[ins],
                    (size_t)(count - ins) * sizeof(size_t));
            LI(b)[ins] = new_start;
            b->lines.len = (size_t)(count + 1);
        }
    }
}

/*
 * Actualiza el índice tras borrar el rango lógico [from, to).
 * Elimina las entradas que caen dentro y ajusta -del las posteriores.
 */
static void li_after_delete(Buffer *b, size_t from, size_t to) {
    size_t del = to - from;
    int count = LICOUNT(b);

    int wr = 0;
    for (int i = 0; i < count; i++) {
        size_t s = LI(b)[i];
        /* una línea (s>0) desaparece si se borra su '\n' precedente (en s-1),
           lo que ocurre exactamente cuando from < s <= to. */
        if (s > from && s <= to) continue;          /* borrar */
        LI(b)[wr++] = (s > to) ? s - del : s;
    }
    b->lines.len = (size_t)wr;
    if (b->lines.len == 0) {
        LI(b)[0] = 0;
        b->lines.len = 1;
    }
}

/* -- helpers internos del gap buffer -------------------------------------- */

static size_t gap_size(const Buffer *b) {
    return b->gap_end - b->gap_start;
}

static int ensure_gap(Buffer *b, size_t need) {
    if (gap_size(b) >= need) return 1;

    size_t new_gap  = need + BUFFER_GAP_MIN;
    size_t old_size = b->size;
    size_t new_size = old_size + new_gap - gap_size(b);

    char *nd = realloc(b->data, new_size);
    if (!nd) return 0;
    b->data = nd;

    size_t right_len = old_size - b->gap_end;
    memmove(b->data + new_size - right_len,
            b->data + b->gap_end,
            right_len);

    b->gap_end = new_size - right_len;
    b->size    = new_size;
    return 1;
}

static void move_gap_to(Buffer *b, size_t pos) {
    size_t cur = b->gap_start;
    if (pos == cur) return;

    if (pos < cur) {
        size_t len = cur - pos;
        memmove(b->data + b->gap_end - len,
                b->data + pos,
                len);
        b->gap_start = pos;
        b->gap_end  -= len;
    } else {
        size_t len = pos - cur;
        memmove(b->data + cur,
                b->data + b->gap_end,
                len);
        b->gap_start  = pos;
        b->gap_end   += len;
    }
}

static size_t phys(const Buffer *b, size_t pos) {
    return pos < b->gap_start ? pos : pos + gap_size(b);
}

/* -- ciclo de vida -------------------------------------------------------- */

int buf_init(Buffer *b) {
    b->data = malloc(BUFFER_INIT_SIZE);
    if (!b->data) return 0;
    b->size      = BUFFER_INIT_SIZE;
    b->gap_start = 0;
    b->gap_end   = BUFFER_INIT_SIZE;

    vec_init(&b->lines, sizeof(size_t));
    if (!vec_reserve(&b->lines, LINE_INDEX_INIT)) {
        free(b->data); b->data = NULL; return 0;
    }
    LI(b)[0]     = 0;
    b->lines.len = 1;
    return 1;
}

void buf_free(Buffer *b) {
    free(b->data);
    vec_free(&b->lines);
    b->data = NULL;
    b->size = b->gap_start = b->gap_end = 0;
}

/* -- edición -------------------------------------------------------------- */

void buf_insert(Buffer *b, char c) {
    if (!ensure_gap(b, 1)) return;
    size_t pos = b->gap_start;
    b->data[b->gap_start++] = c;
    li_after_insert(b, pos, &c, 1);
}

void buf_insert_str(Buffer *b, const char *s, size_t len) {
    if (!ensure_gap(b, len)) return;
    size_t pos = b->gap_start;
    memcpy(b->data + b->gap_start, s, len);
    b->gap_start += len;
    li_after_insert(b, pos, s, len);
}

void buf_delete_before(Buffer *b) {
    if (b->gap_start == 0) return;
    size_t pos = b->gap_start - 1;
    li_after_delete(b, pos, pos + 1);
    b->gap_start--;
}

void buf_delete_after(Buffer *b) {
    if (b->gap_end == b->size) return;
    size_t pos = b->gap_start;
    li_after_delete(b, pos, pos + 1);
    b->gap_end++;
}

/* -- edición de rangos ----------------------------------------------------- */

void buf_delete_range(Buffer *b, size_t from, size_t to) {
    size_t len = buf_length(b);
    if (from > len) from = len;
    if (to   > len) to   = len;
    if (from >= to) return;

    li_after_delete(b, from, to);
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

/* -- movimiento ----------------------------------------------------------- */

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

/* -- consulta ------------------------------------------------------------- */

size_t buf_length(const Buffer *b) {
    return b->size - gap_size(b);
}

char buf_char_at(const Buffer *b, size_t pos) {
    return b->data[phys(b, pos)];
}

size_t buf_cursor_pos(const Buffer *b) {
    return b->gap_start;
}

/* O(1) — sólo consulta el índice */
int buf_line_count(const Buffer *b) {
    return LICOUNT(b);
}

/* O(1) — offset de inicio de la línea `line` (con clamp a rango válido) */
size_t buf_line_offset(const Buffer *b, int line) {
    int n = LICOUNT(b);
    if (line < 0)  line = 0;
    if (line >= n) line = n - 1;
    return LI(b)[line];
}

/*
 * buf_line_start: O(log n)
 * Búsqueda binaria del mayor índice de línea cuyo start <= pos.
 */
size_t buf_line_start(const Buffer *b, size_t pos) {
    int lo = 0, hi = LICOUNT(b) - 1, best = 0;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (LI(b)[mid] <= pos) { best = mid; lo = mid + 1; }
        else                    hi = mid - 1;
    }
    return LI(b)[best];
}

/*
 * buf_line_col: O(log n) para la línea + O(1) para la columna
 */
int buf_line_col(const Buffer *b, size_t pos, int *line, int *col) {
    int lo = 0, hi = LICOUNT(b) - 1, ln = 0;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (LI(b)[mid] <= pos) { ln = mid; lo = mid + 1; }
        else                    hi = mid - 1;
    }
    *line = ln;
    *col  = (int)(pos - LI(b)[ln]);
    return 1;
}

/*
 * buf_line_end: O(longitud de línea) — itera solo dentro de la línea
 */
size_t buf_line_end(const Buffer *b, size_t pos) {
    size_t len = buf_length(b);
    while (pos < len && buf_char_at(b, pos) != '\n') pos++;
    return pos;
}

/* -- carga / guarda ------------------------------------------------------- */

int buf_load_file(Buffer *b, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Liberar texto anterior; el índice de líneas (Vec) se conserva y reutiliza */
    free(b->data);
    b->data = NULL;
    if (b->lines.elem == 0)          /* defensivo: por si nunca se inicializó */
        vec_init(&b->lines, sizeof(size_t));

    if (fsize <= 0) {
        fclose(f);
        /* Buffer vacío */
        b->data = malloc(BUFFER_INIT_SIZE);
        if (!b->data) return 0;
        b->size      = BUFFER_INIT_SIZE;
        b->gap_start = 0;
        b->gap_end   = BUFFER_INIT_SIZE;
        if (!vec_reserve(&b->lines, LINE_INDEX_INIT)) {
            free(b->data); b->data = NULL; return 0;
        }
        LI(b)[0]     = 0;
        b->lines.len = 1;
        return 1;
    }

    /* Alojar exactamente lo necesario + hueco mínimo */
    b->data = malloc((size_t)fsize + BUFFER_GAP_MIN);
    if (!b->data) { fclose(f); return 0; }

    size_t nread = fread(b->data, 1, (size_t)fsize, f);
    fclose(f);

    b->gap_start = nread;
    b->gap_end   = nread + BUFFER_GAP_MIN;
    b->size      = nread + BUFFER_GAP_MIN;
    memset(b->data + nread, 0, BUFFER_GAP_MIN);

    b->lines.len = 0;                 /* li_rebuild lo rellena */
    return li_rebuild(b);             /* construir índice en una pasada O(n) */
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
