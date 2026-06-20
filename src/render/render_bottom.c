/**
 * @file render_bottom.c
 * @brief Dibujado del panel inferior con pestanas (Salida / Logs / Terminal +
 *        canales registrados por extensiones).
 *
 * El panel inferior se ancla al pie de la ventana (encima de la barra de
 * estado) y ocupa el area del editor: entre el explorador (izquierda) y el
 * panel de extensiones (derecha).  Su cabecera es una TIRA DE PESTANAS (una por
 * canal); debajo se dibuja el scrollback del canal activo con scroll por rueda y
 * resaltado de la seleccion de lineas.
 *
 * El texto de cada canal vive en el almacen puro PanelStore (panel/panel.h); el
 * estado de la UI (alto, pestana activa, scroll, foco, seleccion) vive en el
 * Editor.  La geometria de las pestanas y del cuerpo se registra en e->ui para
 * que el input resuelva clics, foco y rueda.
 */
#include "render_internal.h"
#include "panel/panel.h"
#include "layout/layout.h"
#include "ui.h"
#include <string.h>

/* -- Geometria (px) -------------------------------------------------------- */
#define BOTTOM_PAD 6      /* margen interior del cuerpo de texto */
#define BOTTOM_TAB_PAD 10 /* margen horizontal del texto de cada pestana */

int render_bottom_panel_height(Editor *e) {
    return e->bottom_panel_open ? e->bottom_panel_h : 0;
}

/** X izquierda del panel: tras el explorador (si abierto) o su boton. */
static int bottom_left(Editor *e) {
    return e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
}

/** X derecha del panel (exclusiva): antes del panel de extensiones (si abierto). */
static int bottom_right(Editor *e) {
    return e->win_w - (e->ext_panel_open ? e->ext_panel_w : 0);
}

/** Columnas (en bytes) que caben en el cuerpo de @p width px (>=1). */
static int body_cols(Editor *e, int width) {
    int char_w = (e->char_w > 0 ? e->char_w : 8);
    int cols = (width - 2 * BOTTOM_PAD) / char_w;
    if (cols < 1) cols = 1;
    return cols;
}

void render_bottom_panel(Editor *e) {
    if (!e->bottom_panel_open) return;
    SDL_Renderer *r = e->renderer;

    int left = bottom_left(e);
    int right = bottom_right(e);
    int width = right - left;
    if (width < 40) return; /* sin sitio util (paneles laterales muy anchos) */

    int top = e->win_h - STATUS_HEIGHT - e->bottom_panel_h;
    int height = e->bottom_panel_h;

    /* fondo + borde superior + registro del marco (foco + consumir clics) */
    ui_panel(e, (Rect){left, top, width, height}, e->theme.col_ftree_bg,
             e->theme.col_ftree_sep);
    ui_put(&e->ui, UI_BOTTOM_PANEL, (Rect){left, top, width, height});

    /* -- Tira de pestanas (una por canal) -- */
    set_color_c(r, e->theme.col_ftree_header);
    fill_rect(r, left, top, width, BOTTOM_TAB_H);

    int tab_x = left;
    int char_w = (e->char_w > 0 ? e->char_w : 8);
    size_t n = e->panels.count;
    for (size_t i = 0; i < n; ++i) {
        const PanelChannel *c = panel_at(&e->panels, i);
        if (!c) continue;
        int tw = (int)strlen(c->title) * char_w + 2 * BOTTOM_TAB_PAD;
        if (tab_x + tw > right) break; /* no caben mas pestanas */
        int activei = ((int)i == e->bottom_active_chan);
        /* fondo de la pestana activa resaltado */
        if (activei) {
            set_color_c(r, e->theme.col_tab_active);
            fill_rect(r, tab_x, top, tw, BOTTOM_TAB_H);
            /* franja de acento bajo la pestana activa */
            set_color_c(r, e->theme.col_tab_accent);
            fill_rect(r, tab_x, top + BOTTOM_TAB_H - 2, tw, 2);
        }
        draw_text_c(e, c->title, tab_x + BOTTOM_TAB_PAD,
                    top + (BOTTOM_TAB_H - e->font_size) / 2,
                    activei ? e->theme.ftree_txt_root : e->theme.ftree_txt_file);
        ui_put_idx(&e->ui, UI_LIST_BOTTOM_TAB, (int)i,
                   (Rect){tab_x, top, tw, BOTTOM_TAB_H});
        tab_x += tw;
    }

    /* -- Cuerpo: scrollback del canal activo -- */
    int body_y = top + BOTTOM_TAB_H;
    int body_h = height - BOTTOM_TAB_H;
    ui_put(&e->ui, UI_BOTTOM_BODY, (Rect){left, body_y, width, body_h});

    const PanelChannel *ch = panel_at(&e->panels, (size_t)e->bottom_active_chan);
    if (!ch) return;

    /* La pestana Terminal es hoy un placeholder (sin entrada interactiva). */
    if (strcmp(ch->id, "terminal") == 0 && ch->len == 0) {
        draw_text_c(e, "Terminal interactiva: proximamente.",
                    left + BOTTOM_PAD, body_y + BOTTOM_PAD,
                    e->theme.ftree_txt_file);
        return;
    }

    /* recortar el dibujo al cuerpo */
    SDL_Rect clip = {left, body_y, width, body_h};
    SDL_SetRenderClipRect(r, &clip);

    int line_h = e->line_height;
    int visible_rows = body_h / line_h;
    int cols = body_cols(e, width);
    int total_rows = panel_wrap_count(ch->text, cols);

    /* recortar el scroll del canal a un rango valido (en FILAS VISUALES) */
    int max_scroll = total_rows - visible_rows;
    if (max_scroll < 0) max_scroll = 0;
    int scroll = ch->scroll;
    if (scroll < 0) scroll = 0;
    if (scroll > max_scroll) scroll = max_scroll;

    /* rango de seleccion en byte-offsets, ordenado.  anchor==caret => vacia (no
     * se resalta nada): esto evita el "resaltado fantasma" tras un clic simple. */
    long sel_lo = -1, sel_hi = -1;
    if (e->bottom_sel_active && e->bottom_sel_anchor >= 0 &&
        e->bottom_sel_caret >= 0 && e->bottom_sel_anchor != e->bottom_sel_caret) {
        sel_lo = e->bottom_sel_anchor < e->bottom_sel_caret
                     ? e->bottom_sel_anchor
                     : e->bottom_sel_caret;
        sel_hi = e->bottom_sel_anchor < e->bottom_sel_caret
                     ? e->bottom_sel_caret
                     : e->bottom_sel_anchor;
    }

    /* dibujar fila visual a fila visual, las que caen dentro de la ventana.  El
     * coloreado en rojo de las lineas de fallo (contienen " fallo:") se aplica a
     * la LINEA LOGICA completa aunque se envuelva en varias filas. */
    Color err_col = {220, 80, 80, 255};
    const char *text = ch->text;
    size_t pos = 0;
    PanelRow row;
    int ri = 0;        /* indice de fila visual */
    int line_is_err = 0;
    char line[1024];
    /* `pos` arranca en una linea logica; se marca si esa linea es de fallo. */
    line_is_err = (strstr(text, " fallo:") != NULL) &&
                  (strchr(text, '\n') == NULL ||
                   strstr(text, " fallo:") < strchr(text, '\n'));
    for (;;) {
        size_t next = panel_wrap_next(text, pos, cols, &row);
        int vi = ri - scroll; /* fila visible en pantalla */
        if (vi >= 0 && vi < visible_rows) {
            int ly = body_y + vi * line_h;
            Color def_fg = line_is_err ? err_col : e->theme.ftree_txt_file;
            size_t rend = row.offset + row.len;

            /* 1) Fondos de color de los spans (ANSI bg).  Se pintan PRIMERO
             * para que la seleccion (paso 2) quede por encima y siga visible. */
            if (row.len > 0) {
                size_t hint = 0;
                for (size_t seg = row.offset; seg < rend;) {
                    const PanelColorSpan *sp = panel_span_at(ch, seg, &hint);
                    size_t sub_end = rend;
                    if (sp && sp->end < sub_end) sub_end = sp->end;
                    if (!sp) { /* hueco: hasta el siguiente span o fin de fila */
                        for (size_t k = 0; k < ch->span_count; ++k) {
                            size_t st = ch->spans[k].start;
                            if (st > seg && st < sub_end) sub_end = st;
                        }
                    }
                    if (sp) {
                        int bg_def = 1;
                        unsigned char rr, gg, bb;
                        panel_span_bg(ch, sp, &rr, &gg, &bb, &bg_def);
                        if (!bg_def) {
                            int sx = left + BOTTOM_PAD +
                                     (int)(seg - row.offset) * char_w;
                            Color bg = {rr, gg, bb, 255};
                            set_color_c(r, bg);
                            fill_rect(r, sx, ly,
                                      (int)(sub_end - seg) * char_w, line_h);
                        }
                    }
                    seg = sub_end;
                }
            }

            /* 2) Resaltado de la seleccion: el tramo de columnas de esta fila
             * dentro de [sel_lo, sel_hi).  Sobre los fondos de color. */
            if (sel_lo >= 0) {
                long rs = (long)row.offset;
                long re = (long)(row.offset + row.len);
                long a = sel_lo > rs ? sel_lo : rs; /* inicio del tramo */
                long b = sel_hi < re ? sel_hi : re; /* fin del tramo */
                if (b > a) {
                    int hx = left + BOTTOM_PAD + (int)(a - rs) * char_w;
                    int hw = (int)(b - a) * char_w;
                    set_color_c(r, e->theme.col_sel_bg);
                    fill_rect(r, hx, ly, hw, line_h);
                }
            }

            /* 3) Texto, partido por los spans: cada sub-tramo con su color de
             * primer plano (o el color por defecto del panel / rojo de error).*/
            if (row.len > 0) {
                size_t hint = 0;
                for (size_t seg = row.offset; seg < rend;) {
                    const PanelColorSpan *sp = panel_span_at(ch, seg, &hint);
                    size_t sub_end = rend;
                    Color fg = def_fg;
                    if (sp) {
                        if (sp->end < sub_end) sub_end = sp->end;
                        int is_def = 1;
                        unsigned char rr, gg, bb;
                        panel_span_fg(ch, sp, &rr, &gg, &bb, &is_def);
                        if (!is_def) {
                            fg.r = rr; fg.g = gg; fg.b = bb; fg.a = 255;
                        }
                    } else { /* hueco: hasta el siguiente span o fin de fila */
                        for (size_t k = 0; k < ch->span_count; ++k) {
                            size_t st = ch->spans[k].start;
                            if (st > seg && st < sub_end) sub_end = st;
                        }
                    }
                    size_t sub_len = sub_end - seg;
                    int sx = left + BOTTOM_PAD +
                             (int)(seg - row.offset) * char_w;
                    size_t cp =
                        sub_len < sizeof(line) ? sub_len : sizeof(line) - 1;
                    memcpy(line, text + seg, cp);
                    line[cp] = '\0';
                    draw_text_c(e, line, sx, ly, fg);
                    seg = sub_end;
                }
            }
        }
        if (next == (size_t)-1 || text[next] == '\0') break;
        /* la nueva fila inicia una nueva linea logica si next salto un '\n' */
        if (next > 0 && text[next - 1] == '\n') {
            const char *ln_end = strchr(text + next, '\n');
            const char *f = strstr(text + next, " fallo:");
            line_is_err = (f != NULL) && (ln_end == NULL || f < ln_end);
        }
        pos = next;
        ++ri;
        if (ri - scroll >= visible_rows) break; /* nada mas visible que pintar */
    }

    SDL_SetRenderClipRect(r, NULL); /* quitar el recorte */
}
