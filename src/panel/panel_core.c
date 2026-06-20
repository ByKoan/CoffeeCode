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
