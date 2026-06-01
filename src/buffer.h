#pragma once
#include <stddef.h>

/*
 * Gap Buffer
 * ──────────
 * [ texto_izquierda | GAP....... | texto_derecha ]
 *   0 .. gap_start-1              gap_end .. size-1
 *
 * Insertar en cursor: O(1) amortizado
 * Borrar en cursor:   O(1)
 * Mover cursor:       O(distancia)
 */

#define BUFFER_INIT_SIZE 4096
#define BUFFER_GAP_MIN   64

typedef struct {
    char  *data;        /* array completo                  */
    size_t size;        /* tamaño total del array          */
    size_t gap_start;   /* primer byte del hueco           */
    size_t gap_end;     /* primer byte TRAS el hueco       */
} Buffer;

/* ciclo de vida */
int    buf_init   (Buffer *b);
void   buf_free   (Buffer *b);

/* edición */
void   buf_insert (Buffer *b, char c);               /* inserta en cursor    */
void   buf_insert_str(Buffer *b, const char *s, size_t len);
void   buf_delete_before(Buffer *b);                 /* backspace            */
void   buf_delete_after (Buffer *b);                 /* supr                 */

/* ── NUEVO: edición de rangos ───────────────────────────────────────────── */
/* Elimina el rango lógico [from, to) y deja el cursor en `from`.            */
void   buf_delete_range(Buffer *b, size_t from, size_t to);
/* Copia el rango lógico [from, to) en `out` (sin NUL final).
   Devuelve el número de bytes copiados. `out` debe tener >= (to-from) bytes. */
size_t buf_get_text(const Buffer *b, size_t from, size_t to, char *out);

/* movimiento del cursor (mueve el hueco) */
void   buf_move_left (Buffer *b);
void   buf_move_right(Buffer *b);
void   buf_move_to   (Buffer *b, size_t pos);        /* posición lógica      */

/* consulta */
size_t buf_length    (const Buffer *b);              /* nº de caracteres     */
char   buf_char_at   (const Buffer *b, size_t pos);  /* carácter en pos lóg. */
size_t buf_cursor_pos(const Buffer *b);              /* posición lógica      */

/* utilidades */
int    buf_line_col  (const Buffer *b, size_t pos, int *line, int *col);
size_t buf_line_start(const Buffer *b, size_t pos);  /* inicio de la línea   */
size_t buf_line_end  (const Buffer *b, size_t pos);  /* fin de la línea      */
int    buf_line_count(const Buffer *b);

/* carga / guarda */
int    buf_load_file (Buffer *b, const char *path);
int    buf_save_file (const Buffer *b, const char *path);