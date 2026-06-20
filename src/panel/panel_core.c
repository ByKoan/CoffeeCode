/**
 * @file panel_core.c
 * @brief Almacen PURO de los canales del panel inferior (sin SDL ni Editor).
 *
 * Implementa el registro de canales y la manipulacion de su scrollback acotado.
 * No depende de SDL ni de la struct Editor, asi que se compila y prueba en
 * headless (ver test/panel/test_panel.c).  El render y el input del panel viven
 * aparte, sobre el Editor, y consultan este almacen.
 */
#include "panel/panel.h"

#include <string.h>

/** Copia @p src en @p dst (cap @p cap, siempre null-termina). */
static void copy_str(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void panel_store_init(PanelStore *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    /* Canales integrados del IDE.  El primero ("salida") es el destino del
     * output_append por defecto y la pestana activa inicial. */
    panel_register(s, PANEL_DEFAULT_CHANNEL, "Salida");
    panel_register(s, "logs", "Logs");
    panel_register(s, "terminal", "Terminal");
    /* marcarlos como integrados (no los registra ninguna extension) */
    for (size_t i = 0; i < s->count; ++i) s->chans[i].builtin = 1;
}

int panel_find(const PanelStore *s, const char *id) {
    if (!s || !id) return -1;
    for (size_t i = 0; i < s->count; ++i)
        if (strcmp(s->chans[i].id, id) == 0) return (int)i;
    return -1;
}

int panel_register(PanelStore *s, const char *id, const char *title) {
    if (!s || !id || !id[0]) return -1;
    int existing = panel_find(s, id);
    if (existing >= 0) {
        /* ya existe: solo refrescar el titulo si se aporta uno nuevo */
        if (title && title[0])
            copy_str(s->chans[existing].title, PANEL_TITLE_MAX, title);
        return existing;
    }
    if (s->count >= PANEL_MAX_CHANNELS) return -1; /* sin sitio */
    PanelChannel *c = &s->chans[s->count];
    memset(c, 0, sizeof(*c));
    copy_str(c->id, PANEL_ID_MAX, id);
    copy_str(c->title, PANEL_TITLE_MAX, (title && title[0]) ? title : id);
    c->len = 0;
    c->text[0] = '\0';
    c->scroll = 0;
    c->builtin = 0;
    return (int)s->count++;
}

void panel_append(PanelStore *s, const char *id, const char *text) {
    if (!s || !id || !text) return;
    int idx = panel_find(s, id);
    if (idx < 0) idx = panel_register(s, id, NULL); /* crear al vuelo */
    if (idx < 0) return;
    PanelChannel *c = &s->chans[idx];

    size_t add = strlen(text);
    size_t cap = PANEL_CHAN_CAP - 1; /* reservar el NUL */
    if (add > cap) {
        /* texto mas grande que la capacidad: quedarse con la cola */
        text += add - cap;
        add = cap;
    }
    if (c->len + add > cap) {
        /* no cabe: descartar la cabecera mas antigua (scroll del buffer) */
        size_t drop = c->len + add - cap;
        memmove(c->text, c->text + drop, c->len - drop);
        c->len -= drop;
    }
    memcpy(c->text + c->len, text, add);
    c->len += add;
    c->text[c->len] = '\0';
}

void panel_clear(PanelStore *s, const char *id) {
    if (!s || !id) return;
    int idx = panel_find(s, id);
    if (idx < 0) return;
    PanelChannel *c = &s->chans[idx];
    c->text[0] = '\0';
    c->len = 0;
    c->scroll = 0;
}

const PanelChannel *panel_at(const PanelStore *s, size_t idx) {
    if (!s || idx >= s->count) return NULL;
    return &s->chans[idx];
}

/* -- Envoltura del texto al ancho (word-wrap) -----------------------------
 *
 * Layout PURO compartido por el render y el input.  Ancho medido en bytes; el
 * texto del panel es casi siempre ASCII (byte == columna).  Las roturas por
 * ancho nunca parten una secuencia UTF-8 a la mitad: si al alcanzar el limite de
 * columnas el byte siguiente es de continuacion (0x80..0xBF), se retrocede al
 * inicio de su caracter para no cortar a mitad de codepoint.
 */

/** Indica si @p b es un byte de continuacion UTF-8 (10xxxxxx). */
static int is_cont(unsigned char b) { return (b & 0xC0) == 0x80; }

size_t panel_wrap_next(const char *text, size_t start, int cols, PanelRow *row) {
    PanelRow dummy;
    if (!row) row = &dummy;
    if (!text) {
        row->offset = start;
        row->len = 0;
        return (size_t)-1;
    }
    if (cols < 1) cols = 1;

    size_t tlen = strlen(text);
    if (start >= tlen) {
        /* No hay mas filas, salvo el caso "texto vacio" que cuenta como una
         * fila vacia (lo gestiona el llamante usando start==0). */
        row->offset = start;
        row->len = 0;
        return (size_t)-1;
    }

    row->offset = start;

    /* Avanzar hasta `cols` columnas o hasta encontrar un '\n'. */
    size_t i = start;        /* byte actual */
    int col = 0;             /* columnas consumidas en esta fila */
    size_t last_space = 0;   /* byte del ultimo espacio visto (0 = ninguno) */
    int last_space_seen = 0; /* 1 si vimos un espacio dentro del limite */

    while (i < tlen && text[i] != '\n' && col < cols) {
        if (text[i] == ' ') {
            last_space = i;
            last_space_seen = 1;
        }
        ++i;
        ++col;
    }

    if (i < tlen && text[i] == '\n') {
        /* Cabe la linea logica entera: la fila es [start, i), saltamos el '\n'. */
        row->len = i - start;
        return i + 1;
    }
    if (i >= tlen) {
        /* Fin del texto sin '\n': ultima fila de esta linea logica. */
        row->len = i - start;
        return (size_t)-1;
    }

    /* Llegamos al limite de columnas con mas texto en la misma linea logica:
     * hay que envolver.  Caso comun: el caracter justo en el limite es un
     * espacio -> la fila cabe entera y rompemos limpio en ese espacio. */
    if (text[i] == ' ') {
        row->len = i - start;
        /* saltar los espacios consecutivos al inicio de la fila siguiente */
        size_t j = i;
        while (j < tlen && text[j] == ' ') ++j;
        return j;
    }

    /* Preferir romper en el ultimo espacio dentro del limite; si no hubo,
     * romper por caracter (sin partir UTF-8). */
    size_t brk;
    if (last_space_seen && last_space > start) {
        /* Romper en el espacio: la fila no incluye el espacio; la siguiente
         * fila empieza justo despues de el. */
        row->len = last_space - start;
        brk = last_space + 1;
    } else {
        /* Rotura dura por caracter.  Si `i` cae en mitad de un codepoint,
         * retroceder a su inicio para no partirlo. */
        size_t cut = i;
        while (cut > start && is_cont((unsigned char)text[cut])) --cut;
        if (cut == start) cut = i; /* un solo codepoint mas ancho que cols */
        row->len = cut - start;
        brk = cut;
    }
    return brk;
}

int panel_wrap_count(const char *text, int cols) {
    if (!text || !text[0]) return 1; /* texto vacio: una fila vacia */
    if (cols < 1) cols = 1;
    int n = 0;
    size_t pos = 0;
    PanelRow row;
    for (;;) {
        size_t next = panel_wrap_next(text, pos, cols, &row);
        ++n;
        if (next == (size_t)-1) break;
        pos = next;
        /* Una linea logica que termina justo en '\n' (next apunta al byte tras
         * el '\n') y ese byte es el fin del texto: el '\n' final NO crea una
         * fila vacia extra (coherente con el render de una terminal). */
        if (text[pos] == '\0') break;
    }
    return n;
}

size_t panel_rowcol_to_offset(const char *text, int cols, int row, int col) {
    if (!text) return 0;
    if (cols < 1) cols = 1;
    if (row < 0) row = 0;
    if (col < 0) col = 0;

    size_t pos = 0;
    PanelRow r;
    int ri = 0;
    for (;;) {
        size_t next = panel_wrap_next(text, pos, cols, &r);
        if (ri == row) {
            size_t off = r.offset + (size_t)col;
            if ((size_t)col > r.len) off = r.offset + r.len; /* recorte a fin */
            return off;
        }
        if (next == (size_t)-1 || text[next] == '\0') {
            /* row mas alla del final: ultimo offset valido (fin de la ultima
             * fila). */
            return r.offset + r.len;
        }
        pos = next;
        ++ri;
    }
}

void panel_offset_to_rowcol(const char *text, int cols, size_t offset, int *row,
                            int *col) {
    int dr = 0, dc = 0;
    if (!text) {
        if (row) *row = 0;
        if (col) *col = 0;
        return;
    }
    if (cols < 1) cols = 1;
    size_t tlen = strlen(text);
    if (offset > tlen) offset = tlen;

    size_t pos = 0;
    PanelRow r;
    int ri = 0;
    for (;;) {
        size_t next = panel_wrap_next(text, pos, cols, &r);
        size_t row_end = r.offset + r.len; /* fin de los caracteres de la fila */
        /* `offset` pertenece a esta fila si cae en [r.offset, next): asi un
         * offset en el limite de envoltura por ancho cae en la fila siguiente
         * (col 0), coherente con el avance de panel_wrap_next. */
        size_t row_next = (next == (size_t)-1) ? tlen + 1 : next;
        if (offset < row_next || next == (size_t)-1) {
            dr = ri;
            dc = (offset >= r.offset) ? (int)(offset - r.offset) : 0;
            if ((size_t)dc > r.len) dc = (int)r.len;
            (void)row_end;
            break;
        }
        pos = next;
        ++ri;
        if (text[pos] == '\0') {
            /* offset == tlen y el texto acaba en '\n': ultima fila, fin. */
            dr = ri;
            dc = 0;
            break;
        }
    }
    if (row) *row = dr;
    if (col) *col = dc;
}
