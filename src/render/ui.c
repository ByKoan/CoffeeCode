/**
 * @file ui.c
 * @brief Implementación del registro de hit-test (render/ui_hit.h) y de los
 *        componentes de UI inmediata (ui.h).
 */
#include "ui.h"
#include "render_internal.h" /* set_color, fill_rect, stroke_rect, draw_text */

/* ── Registro de hit-test ───────────────────────────────────────────────────
 */

void ui_reset(UiRegistry *u) {
    for (int i = 0; i < UI_ID_COUNT; i++)
        u->single[i].w = 0; /* w<=0 marca "no registrado este frame" */
    u->indexed_count = 0;
}

void ui_put(UiRegistry *u, UiId id, Rect r) {
    if (id > UI_ID_NONE && id < UI_ID_COUNT) u->single[id] = r;
}

void ui_put_idx(UiRegistry *u, UiList list, int idx, Rect r) {
    if (u->indexed_count < UI_MAX_INDEXED) {
        UiIndexed *e = &u->indexed[u->indexed_count++];
        e->list = list;
        e->idx = idx;
        e->r = r;
    }
}

int ui_hit(const UiRegistry *u, UiId id, int mx, int my) {
    if (id <= UI_ID_NONE || id >= UI_ID_COUNT) return 0;
    Rect r = u->single[id];
    return r.w > 0 && rect_has(r, mx, my);
}

int ui_hit_idx(const UiRegistry *u, UiList list, int mx, int my) {
    /* Recorrido inverso: el último dibujado queda "encima" y gana el click. */
    for (int i = u->indexed_count - 1; i >= 0; i--)
        if (u->indexed[i].list == list && rect_has(u->indexed[i].r, mx, my))
            return u->indexed[i].idx;
    return -1;
}

/* ── Componentes de dibujo ──────────────────────────────────────────────────
 */

void ui_panel(Editor *e, Rect r, Color bg, Color border) {
    set_color(e->renderer, bg.r, bg.g, bg.b, bg.a);
    fill_rect(e->renderer, r.x, r.y, r.w, r.h);
    if (border.a) { /* alfa 0 => sin borde */
        set_color(e->renderer, border.r, border.g, border.b, border.a);
        stroke_rect(e->renderer, r.x, r.y, r.w, r.h);
    }
}

int ui_label(Editor *e, int x, int y, const char *text, Color c) {
    return draw_text(e, text, x, y, c.r, c.g, c.b);
}

void ui_button(Editor *e, UiId id, Rect r, const char *label, const UiStyle *st,
               UiState state) {
    Color bg = st->bg;
    if (state == UI_HOVER)
        bg = st->bg_hover;
    else if (state == UI_ACTIVE)
        bg = st->bg_active;

    ui_panel(e, r, bg, st->border);

    if (label && label[0]) {
        /* centrar el label en la caja (la fuente es monoespaciada, pero usamos
         * la medida real para soportar etiquetas de cualquier longitud). */
        int tw = 0, th = 0;
        TTF_GetStringSize(e->font, label, 0, &tw, &th);
        int tx = r.x + (r.w - tw) / 2;
        int ty = r.y + (r.h - th) / 2;
        ui_label(e, tx, ty, label, st->text);
    }

    if (id != UI_ID_NONE) ui_put(&e->ui, id, r);
}
