/**
 * @file buffer.c
 * @brief Gap buffer de texto con índice de líneas (Vec<size_t>).
 *
 * @note Qué es un gap buffer (para recién llegados). El texto se guarda en un
 * único array contiguo dividido en tres tramos:
 *
 *     [ texto_izquierda | GAP (hueco libre) | texto_derecha ]
 *       0 .. gap_start-1   gap_start..gap_end-1  gap_end .. size-1
 *
 * El "hueco" (gap) es un espacio reservado pero sin usar que vive justo donde
 * está el cursor. Insertar un carácter es escribir en el primer byte del hueco
 * y avanzar @c gap_start: O(1). Borrar es agrandar el hueco moviendo @c
 * gap_start o @c gap_end: O(1). Como el hueco se rellena poco a poco y solo de
 * vez en cuando hay que agrandarlo (realloc + memmove), el coste es O(1)
 * *amortizado*. El truco es que TODO el texto siempre cabe en el array; lo que
 * se mueve al editar es el hueco, no el texto. Mover el cursor a otra posición
 * sí cuesta O(distancia), porque hay que reubicar el hueco trasladando los
 * bytes que quedan en medio (ver ::move_gap_to).
 *
 * Posición "lógica" vs "física": la posición lógica es el índice del carácter
 * como si el hueco no existiera (0 = primer carácter del archivo). La física es
 * el índice real dentro de @c data. ::phys traduce de lógica a física saltando
 * el hueco cuando hace falta.
 *
 * Índice de líneas. Para no recorrer todo el texto cada vez que la UI pregunta
 * "¿en qué línea/columna está esta posición?", se mantiene un vector de offsets
 * @c lines donde @c lines[i] = posición lógica del primer byte de la línea i.
 * Da conteo de líneas O(1), inicio de línea O(1) y posición→(línea,col) en
 * O(log n) por búsqueda binaria. El índice se actualiza de forma incremental
 * tras cada insert/delete (ver ::li_after_insert / ::li_after_delete) en lugar
 * de reconstruirse entero, salvo al cargar un archivo (::li_rebuild).
 */
#include "buffer/buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define LI(b) ((size_t *)(b)->lines.data)
/** Número de líneas como int (la API pública usa int). */
#define LICOUNT(b) ((int)(b)->lines.len)

/**
 * @brief Reconstruye el índice de líneas completo en una sola pasada O(n).
 *
 * Recorre todo el texto carácter a carácter: la línea 0 empieza en 0, y tras
 * cada @c '\n' empieza una línea nueva en la posición siguiente (i+1). Es la
 * forma "desde cero", pensada para cuando no hay índice válido que actualizar
 * incrementalmente; por eso solo se usa en ::buf_load_file (o tras cambios
 * masivos), no en cada tecla.
 *
 * @param b Buffer cuyo índice se reconstruye (su texto ya debe estar cargado).
 * @return 1 si se construyó; 0 si falló alguna ampliación del vector.
 */
static int li_rebuild(Buffer *b) {
    size_t len = buf_length(b);

    /* La línea 0 siempre arranca en el offset 0 (invariante del índice). */
    if (!vec_reserve(&b->lines, 1)) return 0;
    LI(b)[0] = 0;
    b->lines.len = 1;

    for (size_t i = 0; i < len; i++) {
        /* Cada salto de línea abre una línea nueva que empieza en i+1. */
        if (buf_char_at(b, i) == '\n') {
            if (!vec_reserve(&b->lines, b->lines.len + 1)) return 0;
            LI(b)[b->lines.len] = i + 1;
            b->lines.len++;
        }
    }
    return 1;
}

/**
 * @brief Actualiza el índice de líneas tras insertar @p len bytes en @p pos.
 *
 * La inserción tiene dos efectos sobre los offsets guardados:
 *   1. Todo inicio de línea que estaba DESPUÉS del punto de inserción se
 *      desplaza @p len bytes hacia la derecha (su texto se ha corrido).
 *   2. Cada @c '\n' que viene en el texto insertado crea una línea nueva, cuyo
 *      inicio es la posición del byte siguiente al salto.
 * Hacerlo incrementalmente evita reconstruir todo el índice en cada tecla.
 *
 * @param b    Buffer (su texto ya contiene la inserción).
 * @param pos  Posición lógica donde empezó la inserción.
 * @param text Bytes insertados (para buscar los @c '\n').
 * @param len  Número de bytes insertados.
 */
static void li_after_insert(Buffer *b, size_t pos, const char *text,
                            size_t len) {
    /* Paso 1: correr +len todos los inicios de línea posteriores a pos. */
    int count = LICOUNT(b);
    for (int i = 0; i < count; i++)
        if (LI(b)[i] > pos) LI(b)[i] += len;

    /* Paso 2: por cada salto de línea insertado, dar de alta su línea. */
    for (size_t k = 0; k < len; k++) {
        if (text[k] == '\n') {
            /* El nuevo inicio de línea es el byte justo tras el '\n'. */
            size_t new_start = pos + k + 1;
            count = LICOUNT(b);
            /* Buscar la posición de inserción en el array de offsets para
             * mantenerlo ordenado: el primer offset mayor que new_start. */
            int ins = count;
            for (int i = 1; i < count; i++) {
                if (LI(b)[i] > new_start) {
                    ins = i;
                    break;
                }
            }
            if (!vec_reserve(&b->lines, (size_t)count + 1)) return;
            /* Abrir hueco en el array desplazando una posición a la derecha. */
            memmove(&LI(b)[ins + 1], &LI(b)[ins],
                    (size_t)(count - ins) * sizeof(size_t));
            LI(b)[ins] = new_start;
            b->lines.len = (size_t)(count + 1);
        }
    }
}

/**
 * @brief Actualiza el índice de líneas tras borrar el rango lógico [from, to).
 *
 * Borrar @c del = to-from bytes afecta al índice de dos maneras:
 *   1. Una línea que empezaba en @c s desaparece si su @c '\n' precedente (que
 *      está en s-1) cae dentro del rango borrado; eso ocurre exactamente cuando
 *      @c from < s <= to. Esas entradas se descartan.
 *   2. Las líneas que empezaban después del rango (@c s > to) se corren @c del
 *      bytes hacia la izquierda. Las anteriores (@c s <= from) no se tocan.
 * Se hace en una sola pasada con dos índices (compactación in-place): @c i lee
 * y @c wr escribe, dejando solo las entradas que sobreviven, ya ajustadas.
 *
 * @param b    Buffer (su texto ya tiene el rango borrado, o se borra a la vez).
 * @param from Offset lógico de inicio del rango borrado (inclusive).
 * @param to   Offset lógico de fin del rango borrado (exclusivo).
 */
static void li_after_delete(Buffer *b, size_t from, size_t to) {
    size_t del = to - from;
    int count = LICOUNT(b);

    /* Compactación in-place: wr es el cursor de escritura sobre el mismo array.
     */
    int wr = 0;
    for (int i = 0; i < count; i++) {
        size_t s = LI(b)[i];
        /* una línea (s>0) desaparece si se borra su '\n' precedente (en s-1),
           lo que ocurre exactamente cuando from < s <= to. */
        if (s > from && s <= to) continue; /* borrar */
        /* sobrevive: si estaba tras el rango, correr -del; si no, dejar igual
         */
        LI(b)[wr++] = (s > to) ? s - del : s;
    }
    b->lines.len = (size_t)wr;
    /* Garantizar el invariante: siempre debe haber al menos la línea 0 en 0. */
    if (b->lines.len == 0) {
        LI(b)[0] = 0;
        b->lines.len = 1;
    }
}

/* -- helpers internos del gap buffer -------------------------------------- */

/** @brief Tamaño actual del hueco (bytes libres). @return gap_end - gap_start.
 */
static size_t gap_size(const Buffer *b) {
    return b->gap_end - b->gap_start;
}

/**
 * @brief Garantiza que el hueco tiene al menos @p need bytes libres, creciendo
 *        el array si hace falta.
 *
 * Cuando el hueco se queda pequeño para la próxima inserción, se realoja el
 * array a un tamaño mayor y se vuelve a colocar la parte derecha del texto
 * pegada al final, de modo que el hueco (entre @c gap_start y @c gap_end) quede
 * más grande. Se reserva @p need + @c BUFFER_GAP_MIN para no realojar en cada
 * carácter: ese margen extra es lo que hace que las inserciones sean O(1)
 * *amortizado* (el coste del realloc se reparte entre muchas inserciones).
 *
 * @param b    Buffer cuyo hueco se amplía.
 * @param need Bytes libres mínimos que debe tener el hueco tras la llamada.
 * @return 1 si hay sitio (ya lo había o se amplió); 0 si el realloc falló.
 */
static int ensure_gap(Buffer *b, size_t need) {
    if (gap_size(b) >= need) return 1; /* ya cabe: nada que hacer */

    /* Nuevo hueco = lo pedido + margen; nuevo tamaño = texto + nuevo hueco. */
    size_t new_gap = need + BUFFER_GAP_MIN;
    size_t old_size = b->size;
    size_t new_size = old_size + new_gap - gap_size(b);

    char *nd = realloc(b->data, new_size);
    if (!nd) return 0;
    b->data = nd;

    /* La parte derecha del texto (la que va tras el hueco) debe quedar pegada
     * al nuevo final del array; al hacerlo, el hueco crece por la izquierda. */
    size_t right_len = old_size - b->gap_end;
    memmove(b->data + new_size - right_len, b->data + b->gap_end, right_len);

    b->gap_end = new_size - right_len;
    b->size = new_size;
    return 1;
}

/**
 * @brief Reubica el hueco para que empiece en la posición lógica @p pos.
 *
 * Mover el cursor en un gap buffer es mover el hueco. Para ello se trasladan
 * los bytes que quedan entre la posición actual del hueco y @p pos "al otro
 * lado" del hueco mediante un único @c memmove. De ahí que mover el cursor sea
 * O(distancia): cuanto más lejos esté @p pos, más bytes hay que copiar. La
 * posición lógica no cambia para el texto; solo se desplaza la ventana libre.
 *
 * @param b   Buffer.
 * @param pos Posición lógica destino del inicio del hueco (el cursor).
 */
static void move_gap_to(Buffer *b, size_t pos) {
    size_t cur = b->gap_start;
    if (pos == cur) return; /* el hueco ya está donde se pide */

    if (pos < cur) {
        /* Mover el hueco a la IZQUIERDA: el bloque [pos, cur) salta al final
         * del hueco (su borde derecho), abriendo sitio antes de él. */
        size_t len = cur - pos;
        memmove(b->data + b->gap_end - len, b->data + pos, len);
        b->gap_start = pos;
        b->gap_end -= len;
    } else {
        /* Mover el hueco a la DERECHA: el bloque que sigue al hueco salta a
         * donde estaba @c gap_start, dejando el hueco más allá. */
        size_t len = pos - cur;
        memmove(b->data + cur, b->data + b->gap_end, len);
        b->gap_start = pos;
        b->gap_end += len;
    }
}

/**
 * @brief Traduce una posición lógica a su índice físico dentro de @c data.
 *
 * Si @p pos está antes del hueco, el índice físico coincide. Si está a partir
 * del hueco, hay que saltarse los @c gap_size bytes libres sumándolos.
 *
 * @param b   Buffer.
 * @param pos Posición lógica.
 * @return Índice físico equivalente dentro de @c b->data.
 */
static size_t phys(const Buffer *b, size_t pos) {
    return pos < b->gap_start ? pos : pos + gap_size(b);
}

/* -- ciclo de vida -------------------------------------------------------- */

/**
 * @brief Deja el buffer como uno vacío (texto vacío e índice con la línea 0).
 *
 * Aloja el array inicial completo como un único hueco (gap_start=0,
 * gap_end=size) y siembra el índice con la línea 0 en offset 0. Asume que
 * @c b->lines ya está inicializado (vec_init).
 *
 * @param b Buffer a vaciar.
 * @return 1 si se alojó la memoria; 0 si falló malloc o la reserva del índice.
 */
static int buf_set_empty(Buffer *b) {
    b->data = malloc(BUFFER_INIT_SIZE);
    if (!b->data) return 0;
    b->size = BUFFER_INIT_SIZE;
    /* Todo el array es hueco: no hay texto ni a la izquierda ni a la derecha.
     */
    b->gap_start = 0;
    b->gap_end = BUFFER_INIT_SIZE;

    if (!vec_reserve(&b->lines, LINE_INDEX_INIT)) {
        free(b->data);
        b->data = NULL;
        return 0;
    }
    /* Invariante del índice: siempre existe la línea 0 empezando en 0. */
    LI(b)[0] = 0;
    b->lines.len = 1;
    return 1;
}

/**
 * @brief Inicializa un buffer recién declarado: índice vacío + texto vacío.
 *
 * @param b Buffer a inicializar.
 * @return 1 si todo se alojó correctamente; 0 si falló alguna reserva.
 */
int buf_init(Buffer *b) {
    vec_init(&b->lines, sizeof(size_t)); /* el índice es un Vec<size_t> */
    return buf_set_empty(b);
}

/**
 * @brief Libera la memoria del buffer (array de texto e índice de líneas).
 *
 * @param b Buffer a destruir; queda en un estado cero reutilizable.
 */
void buf_free(Buffer *b) {
    free(b->data);
    vec_free(&b->lines);
    b->data = NULL;
    b->size = b->gap_start = b->gap_end = 0;
}

/* -- edición -------------------------------------------------------------- */

/**
 * @brief Inserta un carácter en la posición del cursor (inicio del hueco).
 *
 * Escribe en el primer byte libre del hueco y avanza @c gap_start: O(1)
 * amortizado (solo ::ensure_gap puede realojar de vez en cuando).
 *
 * @param b Buffer.
 * @param c Carácter a insertar.
 */
void buf_insert(Buffer *b, char c) {
    if (!ensure_gap(b, 1)) return; /* asegurar 1 byte libre en el hueco */
    size_t pos = b->gap_start;
    b->data[b->gap_start++] = c; /* ocupar el primer byte del hueco */
    li_after_insert(b, pos, &c, 1);
}

/**
 * @brief Inserta @p len bytes en la posición del cursor.
 *
 * Versión por bloque de ::buf_insert: copia de golpe @p len bytes en el hueco.
 *
 * @param b   Buffer.
 * @param s   Bytes a insertar.
 * @param len Número de bytes.
 */
void buf_insert_str(Buffer *b, const char *s, size_t len) {
    if (!ensure_gap(b, len)) return; /* asegurar len bytes libres en el hueco */
    size_t pos = b->gap_start;
    memcpy(b->data + b->gap_start, s, len);
    b->gap_start += len;
    li_after_insert(b, pos, s, len);
}

/**
 * @brief Borra el carácter a la izquierda del cursor (tecla Backspace).
 *
 * Agranda el hueco por la izquierda retrocediendo @c gap_start: el carácter
 * "borrado" simplemente pasa a formar parte del hueco. O(1).
 *
 * @param b Buffer.
 */
void buf_delete_before(Buffer *b) {
    if (b->gap_start == 0) return; /* nada a la izquierda */
    size_t pos = b->gap_start - 1;
    li_after_delete(b, pos, pos + 1);
    b->gap_start--;
}

/**
 * @brief Borra el carácter a la derecha del cursor (tecla Supr/Delete).
 *
 * Agranda el hueco por la derecha avanzando @c gap_end. O(1).
 *
 * @param b Buffer.
 */
void buf_delete_after(Buffer *b) {
    if (b->gap_end == b->size) return; /* nada a la derecha */
    size_t pos = b->gap_start;
    li_after_delete(b, pos, pos + 1);
    b->gap_end++;
}

/* -- edición de rangos ----------------------------------------------------- */

/**
 * @brief Borra el rango lógico [from, to) (p. ej. una selección).
 *
 * Recorta los límites a [0, longitud], actualiza el índice y luego absorbe el
 * rango dentro del hueco: lleva el inicio del hueco a @p from y extiende
 * @c gap_end por los @c to-from bytes borrados, que pasan a ser hueco.
 *
 * @param b    Buffer.
 * @param from Offset lógico de inicio (inclusive).
 * @param to   Offset lógico de fin (exclusivo).
 */
void buf_delete_range(Buffer *b, size_t from, size_t to) {
    size_t len = buf_length(b);
    if (from > len) from = len; /* recortar a rango válido */
    if (to > len) to = len;
    if (from >= to) return; /* rango vacío o invertido: nada que borrar */

    li_after_delete(b, from, to);
    move_gap_to(b, from);      /* situar el hueco al inicio del rango */
    b->gap_end += (to - from); /* tragarse el rango ampliando el hueco */
}

/**
 * @brief Copia el texto del rango lógico [from, to) en @p out (sin terminador).
 *
 * @param b    Buffer.
 * @param from Offset lógico de inicio (inclusive), recortado a rango válido.
 * @param to   Offset lógico de fin (exclusivo), recortado a rango válido.
 * @param out  Destino; debe tener sitio para al menos @c to-from bytes.
 * @return Número de bytes copiados (0 si el rango es vacío).
 */
size_t buf_get_text(const Buffer *b, size_t from, size_t to, char *out) {
    size_t len = buf_length(b);
    if (from > len) from = len;
    if (to > len) to = len;
    if (from >= to) return 0;

    /* Copia carácter a carácter usando buf_char_at, que salta el hueco. */
    size_t n = to - from;
    for (size_t i = 0; i < n; i++)
        out[i] = buf_char_at(b, from + i);
    return n;
}

/* -- movimiento ----------------------------------------------------------- */

/**
 * @brief Mueve el cursor un carácter a la izquierda.
 *
 * Caso O(1) de ::move_gap_to: traslada un solo byte (el que está justo antes
 * del hueco) al otro lado del hueco, desplazando este una posición a la izq.
 *
 * @param b Buffer.
 */
void buf_move_left(Buffer *b) {
    if (b->gap_start == 0) return; /* ya al inicio del texto */
    b->gap_end--;
    b->data[b->gap_end] =
        b->data[b->gap_start - 1]; /* carácter cruza el hueco */
    b->gap_start--;
}

/**
 * @brief Mueve el cursor un carácter a la derecha.
 *
 * Simétrico de ::buf_move_left: el byte que sigue al hueco cruza al lado
 * izquierdo y el hueco avanza una posición. O(1).
 *
 * @param b Buffer.
 */
void buf_move_right(Buffer *b) {
    if (b->gap_end == b->size) return;           /* ya al final del texto */
    b->data[b->gap_start] = b->data[b->gap_end]; /* carácter cruza el hueco */
    b->gap_start++;
    b->gap_end++;
}

/**
 * @brief Mueve el cursor a una posición lógica arbitraria. O(distancia).
 *
 * @param b   Buffer.
 * @param pos Posición lógica destino (se recorta a [0, longitud]).
 */
void buf_move_to(Buffer *b, size_t pos) {
    size_t len = buf_length(b);
    if (pos > len) pos = len;
    move_gap_to(b, pos);
}

/* -- consulta ------------------------------------------------------------- */

/**
 * @brief Longitud lógica del texto (nº de caracteres, sin contar el hueco).
 *
 * @param b Buffer.
 * @return Bytes de texto = tamaño del array menos el tamaño del hueco.
 */
size_t buf_length(const Buffer *b) {
    return b->size - gap_size(b);
}

/**
 * @brief Devuelve el carácter en la posición lógica @p pos.
 *
 * @param b   Buffer.
 * @param pos Posición lógica (se asume válida, < buf_length).
 * @return El carácter, leído del índice físico que ::phys calcula.
 */
char buf_char_at(const Buffer *b, size_t pos) {
    return b->data[phys(b, pos)];
}

/**
 * @brief Posición lógica del cursor (que coincide con el inicio del hueco).
 *
 * @param b Buffer.
 * @return Offset lógico del cursor.
 */
size_t buf_cursor_pos(const Buffer *b) {
    return b->gap_start;
}

/**
 * @brief Número de líneas del texto. O(1): consulta el tamaño del índice.
 *
 * @param b Buffer.
 * @return Número de líneas (siempre >= 1).
 */
int buf_line_count(const Buffer *b) {
    return LICOUNT(b);
}

/**
 * @brief Offset lógico donde empieza la línea @p line. O(1) (acceso directo).
 *
 * @param b    Buffer.
 * @param line Índice de línea (se recorta a [0, nº_líneas-1]).
 * @return Offset lógico del primer carácter de esa línea.
 */
size_t buf_line_offset(const Buffer *b, int line) {
    int n = LICOUNT(b);
    if (line < 0) line = 0;
    if (line >= n) line = n - 1;
    return LI(b)[line];
}

/**
 * @brief Índice de la línea que contiene @p pos (el mayor start <= pos). O(log
 * n).
 *
 * Búsqueda binaria sobre el índice de líneas, que está ordenado de forma
 * creciente. Busca el último offset de inicio que no supera @p pos; @c best
 * recuerda el mejor candidato hallado mientras se estrecha el intervalo.
 *
 * @param b   Buffer.
 * @param pos Posición lógica.
 * @return Índice de la línea que contiene @p pos.
 */
static int li_line_of(const Buffer *b, size_t pos) {
    int lo = 0, hi = LICOUNT(b) - 1, best = 0;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2; /* punto medio sin desbordar */
        if (LI(b)[mid] <= pos) {
            best = mid; /* candidato válido: puede haber otro mayor a la dcha */
            lo = mid + 1;
        } else
            hi = mid - 1; /* este inicio ya pasa pos: buscar a la izquierda */
    }
    return best;
}

/**
 * @brief Offset de inicio de la línea que contiene @p pos. O(log n).
 *
 * @param b   Buffer.
 * @param pos Posición lógica.
 * @return Offset lógico del inicio de su línea.
 */
size_t buf_line_start(const Buffer *b, size_t pos) {
    return LI(b)[li_line_of(b, pos)];
}

/**
 * @brief Calcula (línea, columna) de la posición lógica @p pos. O(log n).
 *
 * La línea sale de la búsqueda binaria; la columna es la distancia desde el
 * inicio de esa línea hasta @p pos.
 *
 * @param b         Buffer.
 * @param pos       Posición lógica.
 * @param[out] line Línea (base 0).
 * @param[out] col  Columna (base 0).
 * @return 1 siempre (firma uniforme para el llamante).
 */
int buf_line_col(const Buffer *b, size_t pos, int *line, int *col) {
    int ln = li_line_of(b, pos);
    *line = ln;
    *col = (int)(pos - LI(b)[ln]); /* columna = offset relativo al inicio */
    return 1;
}

/**
 * @brief Offset del fin de la línea que empieza/contiene @p pos (antes del
 * '\n').
 *
 * O(longitud de la línea): el índice da inicios de línea, no finales, así que
 * hay que avanzar hasta el primer @c '\n' o el final del texto.
 *
 * @param b   Buffer.
 * @param pos Posición lógica desde la que avanzar (típicamente un inicio de
 * línea).
 * @return Offset del @c '\n' que cierra la línea, o el final del texto si no
 * hay.
 */
size_t buf_line_end(const Buffer *b, size_t pos) {
    size_t len = buf_length(b);
    while (pos < len && buf_char_at(b, pos) != '\n')
        pos++;
    return pos;
}

/* -- carga / guarda ------------------------------------------------------- */

/**
 * @brief Carga un archivo entero en el buffer y reconstruye el índice.
 *
 * Lee el archivo en binario midiendo su tamaño con fseek/ftell, lo aloja con un
 * hueco mínimo al final (todo el texto queda a la izquierda, cursor al final) y
 * construye el índice de líneas de una pasada con ::li_rebuild.
 *
 * @param b    Buffer destino (su contenido anterior se descarta).
 * @param path Ruta del archivo.
 * @return 1 si se cargó (o el archivo estaba vacío); 0 si no se pudo
 * abrir/alojar.
 */
int buf_load_file(Buffer *b, const char *path) {
    FILE *f = fopen(path, "rb"); /* binario: no traducir saltos de línea */
    if (!f) return 0;

    /* Medir el tamaño saltando al final y consultando la posición. */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Liberar texto anterior; el índice de líneas (Vec) se conserva y reutiliza
     */
    free(b->data);
    b->data = NULL;
    if (b->lines.elem == 0) /* defensivo: por si nunca se inicializó */
        vec_init(&b->lines, sizeof(size_t));

    if (fsize <= 0) {
        fclose(f);
        return buf_set_empty(b); /* archivo vacío */
    }

    /* Alojar exactamente lo necesario + hueco mínimo */
    b->data = malloc((size_t)fsize + BUFFER_GAP_MIN);
    if (!b->data) {
        fclose(f);
        return 0;
    }

    size_t nread = fread(b->data, 1, (size_t)fsize, f);
    fclose(f);

    /* El texto ocupa [0, nread); el hueco va detrás. Cursor al final del texto.
     */
    b->gap_start = nread;
    b->gap_end = nread + BUFFER_GAP_MIN;
    b->size = nread + BUFFER_GAP_MIN;
    memset(b->data + nread, 0, BUFFER_GAP_MIN); /* limpiar el hueco (higiene) */

    b->lines.len = 0;     /* li_rebuild lo rellena */
    return li_rebuild(b); /* construir índice en una pasada O(n) */
}

/**
 * @brief Guarda el texto del buffer en un archivo.
 *
 * El texto está partido por el hueco, así que se escriben dos tramos: la parte
 * izquierda [0, gap_start) y la derecha [gap_end, size). El hueco intermedio no
 * se escribe nunca (no es texto real).
 *
 * @param b    Buffer a guardar.
 * @param path Ruta de destino.
 * @return 1 si se escribió; 0 si no se pudo abrir el archivo.
 */
int buf_save_file(const Buffer *b, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;

    /* Tramo izquierdo del texto (anterior al hueco). */
    if (b->gap_start > 0) fwrite(b->data, 1, b->gap_start, f);

    /* Tramo derecho del texto (posterior al hueco). */
    size_t right_start = b->gap_end;
    size_t right_len = b->size - b->gap_end;
    if (right_len > 0) fwrite(b->data + right_start, 1, right_len, f);

    fclose(f);
    return 1;
}
