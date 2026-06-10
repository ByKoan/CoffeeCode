/**
 * @file ui.c
 * @brief Implementación del registro de hit-test (render/ui_hit.h) y de los
 *        componentes de UI inmediata (ui.h).
 */
#include "ui.h"
#include "render_internal.h" /* set_color, fill_rect, stroke_rect, draw_text */

/* Los estilos de botón (UI_STYLE_*) ahora viven en el tema:
 * e->theme.style_button / style_primary / style_nav (ver render/theme.c). */

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

void ui_list(Editor *e, Rect bounds, UiId area_id, UiList row_list, int count,
             int row_h, int *scroll, int selected, UiRowDraw draw_row,
             void *ud) {
    SDL_Renderer *r = e->renderer;

    /* Panel de fondo + registro del área completa (para la rueda del ratón). */
    ui_panel(e, bounds, e->theme.col_menu_bg, e->theme.col_menu_border);
    ui_put(&e->ui, area_id, bounds);

    int visible = (row_h > 0) ? bounds.h / row_h : 0;
    int max_scroll = count - visible;
    if (max_scroll < 0) max_scroll = 0;
    /* recortar el scroll a un rango válido (in/out) */
    if (*scroll < 0) *scroll = 0;
    if (*scroll > max_scroll) *scroll = max_scroll;

    int sb_w =
        (count > visible) ? 6 : 0; /* hueco del scrollbar si hace falta */
    int row_w = bounds.w - sb_w;

    /* Filas visibles: resaltado de selección + contenido (callback) + hit-test
     */
    for (int v = 0; v < visible; v++) {
        int i = *scroll + v;
        if (i >= count) break;
        Rect rr = {bounds.x, bounds.y + v * row_h, row_w, row_h};
        if (i == selected) {
            set_color_c(r, e->theme.col_menu_hover);
            fill_rect(r, rr.x, rr.y, rr.w, rr.h);
        }
        draw_row(e, i, rr, i == selected, ud);
        ui_put_idx(&e->ui, row_list, i, rr);
    }

    /* Pulgar de scroll proporcional. */
    if (sb_w && max_scroll > 0) {
        int thumb_h = bounds.h * visible / count;
        if (thumb_h < 16) thumb_h = 16;
        int thumb_y = bounds.y + (bounds.h - thumb_h) * (*scroll) / max_scroll;
        set_color_c(r, e->theme.col_sb_thumb);
        fill_rect(r, bounds.x + bounds.w - sb_w, thumb_y, sb_w, thumb_h);
    }
}
