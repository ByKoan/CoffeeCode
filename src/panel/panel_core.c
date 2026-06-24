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
    /* estado de color inicial: por defecto (memset deja 0 = negro, no defecto) */
    c->span_count = 0;
    c->span_rgb_count = 0;
    c->sgr.fg = PANEL_COL_DEFAULT;
    c->sgr.bg = PANEL_COL_DEFAULT;
    c->sgr.flags = 0;
    return (int)s->count++;
}

/* -- Color del texto (ANSI SGR) ------------------------------------------- */

/**
 * @brief Descarta los primeros @p drop bytes de los spans del canal.
 *
 * Acompana al recorte de la cabecera de @c text: resta @p drop a los offsets
 * de cada span, descarta los que quedan totalmente antes del nuevo inicio y
 * recorta a 0 el inicio del que quede a caballo.  Mantiene los offsets de los
 * spans validos respecto al @c text ya desplazado.
 *
 * @param c    Canal.
 * @param drop Bytes eliminados de la cabecera de @c text.
 */
static void spans_drop_head(PanelChannel *c, size_t drop) {
    size_t w = 0; /* indice de escritura (compactacion en sitio) */
    for (size_t i = 0; i < c->span_count; ++i) {
        PanelColorSpan sp = c->spans[i];
        if (sp.end <= drop) continue; /* totalmente descartado */
        sp.start = (sp.start > drop) ? sp.start - drop : 0;
        sp.end -= drop;
        c->spans[w++] = sp;
    }
    c->span_count = w;
}

/**
 * @brief Reserva un indice en la tabla span_rgb para un color 256/truecolor.
 *
 * Si la tabla esta llena, reutiliza el ultimo indice (degradacion benigna: el
 * exceso de colores distintos se mapea al ultimo; el caso comun -- pocos
 * colores -- no se ve afectado).
 *
 * @param c   Canal.
 * @param rgb Color empaquetado 0x00RRGGBB.
 * @return Indice en span_rgb para referenciar el color.
 */
static unsigned char rgb_intern(PanelChannel *c, unsigned int rgb) {
    if (c->span_rgb_count >= PANEL_MAX_SPANS) {
        size_t last = PANEL_MAX_SPANS - 1;
        c->span_rgb[last] = rgb;
        return (unsigned char)last;
    }
    size_t idx = c->span_rgb_count++;
    c->span_rgb[idx] = rgb;
    return (unsigned char)idx;
}

/**
 * @brief Indica si el estado SGR @p st es "neutro" (color por defecto, sin
 *        negrita): un tramo con este estado no necesita span.
 */
static int sgr_is_default(const PanelSgrState *st) {
    return st->fg == PANEL_COL_DEFAULT && st->bg == PANEL_COL_DEFAULT &&
           st->flags == 0;
}

/**
 * @brief Anyade un tramo VISIBLE de @p n bytes con el estado SGR vigente.
 *
 * Concentra el recorte del scrollback (texto + spans) y el registro/extension
 * del span de color.  Si el estado es neutro no crea span (el render usa el
 * color por defecto cuando un byte no esta cubierto).  Si el ultimo span es
 * contiguo y tiene exactamente el mismo color, lo extiende en vez de crear uno
 * nuevo (evita fragmentar y agotar el array).
 *
 * @param c   Canal destino.
 * @param p   Bytes visibles a anyadir (sin escapes).
 * @param n   Numero de bytes.
 */
static void chan_append_visible(PanelChannel *c, const char *p, size_t n) {
    if (n == 0) return;
    size_t cap = PANEL_CHAN_CAP - 1; /* reservar el NUL */
    if (n > cap) {
        /* el tramo solo no cabe: quedarse con su cola */
        p += n - cap;
        n = cap;
    }
    if (c->len + n > cap) {
        /* no cabe: descartar la cabecera mas antigua (texto y spans) */
        size_t drop = c->len + n - cap;
        memmove(c->text, c->text + drop, c->len - drop);
        c->len -= drop;
        spans_drop_head(c, drop);
    }
    size_t start = c->len;
    memcpy(c->text + c->len, p, n);
    c->len += n;
    c->text[c->len] = '\0';

    if (sgr_is_default(&c->sgr)) return; /* color por defecto: sin span */

    /* extender el ultimo span si es contiguo y del mismo color */
    if (c->span_count > 0) {
        PanelColorSpan *last = &c->spans[c->span_count - 1];
        if (last->end == start && last->fg == c->sgr.fg &&
            last->bg == c->sgr.bg && last->flags == c->sgr.flags) {
            last->end = c->len;
            return;
        }
    }
    if (c->span_count >= PANEL_MAX_SPANS) {
        /* sin sitio: descartar el span mas antiguo (rota como el texto) */
        memmove(c->spans, c->spans + 1,
                (PANEL_MAX_SPANS - 1) * sizeof(c->spans[0]));
        c->span_count = PANEL_MAX_SPANS - 1;
    }
    PanelColorSpan *sp = &c->spans[c->span_count++];
    sp->start = start;
    sp->end = c->len;
    sp->fg = c->sgr.fg;
    sp->bg = c->sgr.bg;
    sp->flags = c->sgr.flags;
}

/**
 * @brief Aplica una secuencia SGR (CSI ... m) al estado de color del canal.
 *
 * @p params son los enteros entre '[' y 'm' (ya parseados); @p nparams su
 * numero (0 = "\x1b[m" == reset).  Soporta: 0 reset, 1 bold, 22 quita bold,
 * 30-37/90-97 fg, 40-47/100-107 bg, 39/49 fg/bg por defecto, y las formas
 * extendidas 38/48 ';5;n' (paleta 256) y '38/48;2;r;g;b' (truecolor).
 *
 * @param c       Canal.
 * @param params  Enteros del SGR.
 * @param nparams Numero de enteros.
 */
static void sgr_apply(PanelChannel *c, const int *params, int nparams) {
    if (nparams == 0) { /* "\x1b[m" equivale a reset */
        c->sgr.fg = PANEL_COL_DEFAULT;
        c->sgr.bg = PANEL_COL_DEFAULT;
        c->sgr.flags = 0;
        return;
    }
    for (int i = 0; i < nparams; ++i) {
        int v = params[i];
        if (v == 0) { /* reset total */
            c->sgr.fg = PANEL_COL_DEFAULT;
            c->sgr.bg = PANEL_COL_DEFAULT;
            c->sgr.flags = 0;
        } else if (v == 1) {
            c->sgr.flags |= PANEL_SGR_BOLD;
        } else if (v == 22) {
            c->sgr.flags &= (unsigned char)~PANEL_SGR_BOLD;
        } else if (v >= 30 && v <= 37) {
            c->sgr.fg = (unsigned char)(v - 30);
            c->sgr.flags &= (unsigned char)~PANEL_SGR_FG_RGB;
        } else if (v >= 90 && v <= 97) {
            c->sgr.fg = (unsigned char)(v - 90 + 8); /* brillante */
            c->sgr.flags &= (unsigned char)~PANEL_SGR_FG_RGB;
        } else if (v == 39) {
            c->sgr.fg = PANEL_COL_DEFAULT;
            c->sgr.flags &= (unsigned char)~PANEL_SGR_FG_RGB;
        } else if (v >= 40 && v <= 47) {
            c->sgr.bg = (unsigned char)(v - 40);
            c->sgr.flags &= (unsigned char)~PANEL_SGR_BG_RGB;
        } else if (v >= 100 && v <= 107) {
            c->sgr.bg = (unsigned char)(v - 100 + 8); /* brillante */
            c->sgr.flags &= (unsigned char)~PANEL_SGR_BG_RGB;
        } else if (v == 49) {
            c->sgr.bg = PANEL_COL_DEFAULT;
            c->sgr.flags &= (unsigned char)~PANEL_SGR_BG_RGB;
        } else if ((v == 38 || v == 48) && i + 1 < nparams) {
            /* color extendido: 38/48 ;5;n (256) o ;2;r;g;b (truecolor) */
            int is_fg = (v == 38);
            int mode = params[++i];
            unsigned int rgb = 0;
            int have = 0;
            if (mode == 5 && i + 1 < nparams) {
                /* paleta 256: mapear el indice n a un RGB aproximado */
                int n = params[++i] & 0xFF;
                unsigned char rr, gg, bb;
                if (n < 16) { /* los 16 primeros son la paleta ANSI */
                    panel_ansi_rgb(n, &rr, &gg, &bb);
                } else if (n < 232) { /* cubo 6x6x6 */
                    int q = n - 16;
                    int ri = (q / 36) % 6, gi = (q / 6) % 6, bi = q % 6;
                    rr = (unsigned char)(ri ? ri * 40 + 55 : 0);
                    gg = (unsigned char)(gi ? gi * 40 + 55 : 0);
                    bb = (unsigned char)(bi ? bi * 40 + 55 : 0);
                } else { /* escala de grises */
                    int g = (n - 232) * 10 + 8;
                    rr = gg = bb = (unsigned char)g;
                }
                rgb = ((unsigned)rr << 16) | ((unsigned)gg << 8) | bb;
                have = 1;
            } else if (mode == 2 && i + 3 < nparams) {
                int rr = params[++i] & 0xFF;
                int gg = params[++i] & 0xFF;
                int bb = params[++i] & 0xFF;
                rgb = ((unsigned)rr << 16) | ((unsigned)gg << 8) |
                      (unsigned)bb;
                have = 1;
            }
            if (have) {
                unsigned char ridx = rgb_intern(c, rgb);
                if (is_fg) {
                    c->sgr.fg = ridx;
                    c->sgr.flags |= PANEL_SGR_FG_RGB;
                } else {
                    c->sgr.bg = ridx;
                    c->sgr.flags |= PANEL_SGR_BG_RGB;
                }
            }
        }
        /* cualquier otro parametro SGR se ignora (subrayado, parpadeo, etc.) */
    }
}

void panel_append(PanelStore *s, const char *id, const char *text) {
    if (!s || !id || !text) return;
    int idx = panel_find(s, id);
    if (idx < 0) idx = panel_register(s, id, NULL); /* crear al vuelo */
    if (idx < 0) return;
    PanelChannel *c = &s->chans[idx];

    /* Recorrer la entrada separando texto VISIBLE de las secuencias de escape.
     * Los escapes NO se guardan: nunca aparecen bytes crudos en `text`. */
    const char *p = text;
    const char *run = p; /* inicio del tramo visible acumulado */
    while (*p) {
        if ((unsigned char)*p != 0x1B) { /* byte normal: parte del texto */
            ++p;
            continue;
        }
        /* volcar el tramo visible acumulado hasta el ESC */
        if (p > run) chan_append_visible(c, run, (size_t)(p - run));

        /* parsear la secuencia de escape.  Solo CSI ("\x1b[ ... letra") se
         * reconoce; el resto de formas (ESC seguido de un byte) se consumen.*/
        const char *q = p + 1;
        if (*q == '[') { /* CSI: parametros numericos separados por ';' */
            ++q;
            int params[16];
            int nparams = 0;
            int cur = 0;
            int has_digit = 0;
            while (*q && !(*q >= 0x40 && *q <= 0x7E)) {
                /* parametros: digitos y ';' hasta el byte final (0x40..0x7E) */
                if (*q >= '0' && *q <= '9') {
                    cur = cur * 10 + (*q - '0');
                    has_digit = 1;
                } else if (*q == ';') {
                    if (nparams < 16) params[nparams++] = cur;
                    cur = 0;
                    has_digit = 0;
                }
                /* otros bytes intermedios (':' etc.) se ignoran dentro del CSI */
                ++q;
            }
            if (!*q) {
                /* secuencia incompleta al final del buffer: descartar limpio.
                 * El estado SGR no cambia; el resto se aplicara si llega. */
                p = q;
                run = p;
                break;
            }
            char final = *q;
            if (has_digit || nparams > 0) {
                if (nparams < 16) params[nparams++] = cur; /* ultimo parametro */
            }
            if (final == 'm') sgr_apply(c, params, nparams); /* color */
            /* otras letras finales (cursor, borrado, etc.): consumidas, ignoradas */
            p = q + 1; /* tras el byte final */
            run = p;
        } else {
            /* ESC no-CSI (p.ej. "\x1bM"): consumir ESC + el byte siguiente. */
            p = (*q) ? q + 1 : q;
            run = p;
        }
    }
    /* volcar el ultimo tramo visible pendiente */
    if (p > run) chan_append_visible(c, run, (size_t)(p - run));
}

void panel_clear(PanelStore *s, const char *id) {
    if (!s || !id) return;
    int idx = panel_find(s, id);
    if (idx < 0) return;
    PanelChannel *c = &s->chans[idx];
    c->text[0] = '\0';
    c->len = 0;
    c->scroll = 0;
    /* vaciar el color: spans, tabla RGB y estado SGR vuelven a por defecto */
    c->span_count = 0;
    c->span_rgb_count = 0;
    c->sgr.fg = PANEL_COL_DEFAULT;
    c->sgr.bg = PANEL_COL_DEFAULT;
    c->sgr.flags = 0;
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

/* -- Paleta y consulta de color ANSI -------------------------------------- */

/* Paleta ANSI de 16 colores (tema oscuro estandar).  Vive aqui en un unico
 * sitio: 0-7 estandar, 8-15 brillantes.  El render la consulta via los
 * helpers de abajo para colorear cada sub-tramo. */
static const unsigned char k_ansi16[16][3] = {
    {0, 0, 0},       /* 0 negro */
    {205, 49, 49},   /* 1 rojo */
    {13, 188, 121},  /* 2 verde */
    {229, 229, 16},  /* 3 amarillo */
    {36, 114, 200},  /* 4 azul */
    {188, 63, 188},  /* 5 magenta */
    {17, 168, 205},  /* 6 cian */
    {229, 229, 229}, /* 7 blanco */
    {102, 102, 102}, /* 8 negro brillante (gris) */
    {241, 76, 76},   /* 9 rojo brillante */
    {35, 209, 139},  /* 10 verde brillante */
    {245, 245, 67},  /* 11 amarillo brillante */
    {59, 142, 234},  /* 12 azul brillante */
    {214, 112, 214}, /* 13 magenta brillante */
    {41, 184, 219},  /* 14 cian brillante */
    {255, 255, 255}, /* 15 blanco brillante */
};

void panel_ansi_rgb(int idx, unsigned char *r, unsigned char *g,
                    unsigned char *b) {
    if (idx < 0) idx = 0;
    if (idx > 15) idx = 15;
    if (r) *r = k_ansi16[idx][0];
    if (g) *g = k_ansi16[idx][1];
    if (b) *b = k_ansi16[idx][2];
}

/** Resuelve un (indice/RGB, flags) a RGB.  @p is_rgb: el indice es RGB. */
static void resolve_color(const PanelChannel *c, unsigned char color,
                          int is_rgb, int bold, unsigned char *r,
                          unsigned char *g, unsigned char *b) {
    if (is_rgb) {
        unsigned int rgb = (color < c->span_rgb_count) ? c->span_rgb[color] : 0;
        if (r) *r = (unsigned char)((rgb >> 16) & 0xFF);
        if (g) *g = (unsigned char)((rgb >> 8) & 0xFF);
        if (b) *b = (unsigned char)(rgb & 0xFF);
        return;
    }
    int idx = color;
    /* bold sobre un color estandar (0-7) -> su variante brillante (8-15) */
    if (bold && idx >= 0 && idx <= 7) idx += 8;
    panel_ansi_rgb(idx, r, g, b);
}

void panel_span_fg(const PanelChannel *c, const PanelColorSpan *sp,
                   unsigned char *r, unsigned char *g, unsigned char *b,
                   int *is_default) {
    if (!c || !sp || sp->fg == PANEL_COL_DEFAULT) {
        if (is_default) *is_default = 1;
        return;
    }
    if (is_default) *is_default = 0;
    resolve_color(c, sp->fg, (sp->flags & PANEL_SGR_FG_RGB) != 0,
                  (sp->flags & PANEL_SGR_BOLD) != 0, r, g, b);
}

void panel_span_bg(const PanelChannel *c, const PanelColorSpan *sp,
                   unsigned char *r, unsigned char *g, unsigned char *b,
                   int *is_default) {
    if (!c || !sp || sp->bg == PANEL_COL_DEFAULT) {
        if (is_default) *is_default = 1;
        return;
    }
    if (is_default) *is_default = 0;
    /* el fondo no aplica el "bold->brillante" (solo afecta al texto) */
    resolve_color(c, sp->bg, (sp->flags & PANEL_SGR_BG_RGB) != 0, 0, r, g, b);
}

const PanelColorSpan *panel_span_at(const PanelChannel *c, size_t off,
                                    size_t *hint) {
    if (!c || c->span_count == 0) return NULL;
    size_t i = (hint && *hint < c->span_count) ? *hint : 0;
    /* si la pista quedo por delante de off, reiniciar la busqueda */
    if (c->spans[i].start > off) i = 0;
    for (; i < c->span_count; ++i) {
        const PanelColorSpan *sp = &c->spans[i];
        if (off < sp->start) break; /* spans ordenados: ya pasamos off */
        if (off < sp->end) {        /* off en [start, end) */
            if (hint) *hint = i;
            return sp;
        }
    }
    return NULL; /* sin span: color por defecto */
}
