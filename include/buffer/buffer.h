#pragma once
#include <stddef.h>
#include "structs/vec.h"

/*
 * Gap Buffer con índice de líneas
 * ----------------------------------------------------------------------------
 * [ texto_izquierda | GAP....... | texto_derecha ]
 *   0 .. gap_start-1              gap_end .. size-1
 *
 * Insertar en cursor: O(1) amortizado
 * Borrar en cursor:   O(1)
 * Mover cursor:       O(distancia)
 *
 * Índice de líneas (line_index)
 * -----------------------------
 * line_index[i] = posición lógica del primer carácter de la línea i.
 * Se mantiene actualizado en cada insert/delete, lo que convierte
 * buf_line_count, buf_line_start y buf_line_col de O(n) a O(1)/O(log n).
 * Esto elimina el cuello de botella en archivos grandes.
 */

#define BUFFER_INIT_SIZE    4096
#define BUFFER_GAP_MIN      64
#define LINE_INDEX_INIT     1024   /* capacidad inicial del índice de líneas */

typedef struct {
    char  *data;        /* array completo                  */
    size_t size;        /* tamaño total del array          */
    size_t gap_start;   /* primer byte del hueco           */
    size_t gap_end;     /* primer byte TRAS el hueco       */

    /* -- índice de líneas ------------------------------------------------ */
    Vec    lines;       /* Vec<size_t>: lines[i] = offset lógico del inicio
                           de la línea i (>= 1 entrada; lines[0] == 0).        */
} Buffer;

/* ciclo de vida */
int    buf_init   (Buffer *b);
void   buf_free   (Buffer *b);

/* edición */
void   buf_insert (Buffer *b, char c);               /* inserta en cursor    */
void   buf_insert_str(Buffer *b, const char *s, size_t len);
void   buf_delete_before(Buffer *b);                 /* backspace            */
void   buf_delete_after (Buffer *b);                 /* supr                 */

/* edición de rangos */
void   buf_delete_range(Buffer *b, size_t from, size_t to);
size_t buf_get_text(const Buffer *b, size_t from, size_t to, char *out);

/* movimiento del cursor (mueve el hueco) */
void   buf_move_left (Buffer *b);
void   buf_move_right(Buffer *b);
void   buf_move_to   (Buffer *b, size_t pos);        /* posición lógica      */

/* consulta */
size_t buf_length    (const Buffer *b);              /* nº de caracteres     */
char   buf_char_at   (const Buffer *b, size_t pos);  /* carácter en pos lóg. */
size_t buf_cursor_pos(const Buffer *b);              /* posición lógica      */

/* utilidades — ahora O(1) gracias al índice de líneas */
int    buf_line_col  (const Buffer *b, size_t pos, int *line, int *col);
size_t buf_line_start(const Buffer *b, size_t pos);  /* inicio de la línea   */
size_t buf_line_end  (const Buffer *b, size_t pos);  /* fin de la línea      */
int    buf_line_count(const Buffer *b);
size_t buf_line_offset(const Buffer *b, int line);   /* offset de inicio de la línea `line` (O(1)) */

/* carga / guarda */
int    buf_load_file (Buffer *b, const char *path);
int    buf_save_file (const Buffer *b, const char *path);
