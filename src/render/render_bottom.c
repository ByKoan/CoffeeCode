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

/** Cuenta las lineas (separadas por '\n') de @p text. */
static int count_lines(const char *text) {
    int n = 1;
    for (const char *p = text; *p; ++p)
        if (*p == '\n') ++n;
    return n;
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
    int visible_lines = body_h / line_h;
    int total = count_lines(ch->text);

    /* recortar el scroll del canal a un rango valido */
    int max_scroll = total - visible_lines;
    if (max_scroll < 0) max_scroll = 0;
    int scroll = ch->scroll;
    if (scroll < 0) scroll = 0;
    if (scroll > max_scroll) scroll = max_scroll;

    /* rango de seleccion (en lineas, ordenado) */
    int sel_lo = -1, sel_hi = -1;
    if (e->bottom_sel_active && e->bottom_sel_anchor >= 0) {
        /* el extremo movil es la ultima linea visible bajo el cursor; lo
         * guardamos en el propio scroll-relativo via bottom_sel_active.  Para
         * el resaltado usamos [anchor, caret]; el caret lo deja el input en
         * bottom_sel_anchor cuando no arrastra.  Simplificamos: resaltar solo
         * la linea ancla cuando no hay arrastre activo se hace en input. */
        sel_lo = e->bottom_sel_anchor;
        sel_hi = e->bottom_sel_anchor;
        if (e->bottom_sel_caret >= 0) {
            sel_lo = e->bottom_sel_anchor < e->bottom_sel_caret
                         ? e->bottom_sel_anchor
                         : e->bottom_sel_caret;
            sel_hi = e->bottom_sel_anchor < e->bottom_sel_caret
                         ? e->bottom_sel_caret
                         : e->bottom_sel_anchor;
        }
    }

    /* dibujar linea a linea, las que caen dentro de la ventana visible.  Las
     * lineas de fallo (contienen " fallo:") se pintan en rojo. */
    Color err_col = {220, 80, 80, 255};
    const char *p = ch->text;
    int li = 0;          /* indice de linea actual del canal */
    char line[512];
    while (*p || li == 0) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        int vi = li - scroll; /* fila visible */
        if (vi >= 0 && vi < visible_lines) {
            int ly = body_y + vi * line_h;
            /* resaltado de la seleccion */
            if (sel_lo >= 0 && li >= sel_lo && li <= sel_hi) {
                set_color_c(r, e->theme.col_sel_bg);
                fill_rect(r, left, ly, width, line_h);
            }
            if (len > 0) {
                size_t cp = len < sizeof(line) ? len : sizeof(line) - 1;
                memcpy(line, p, cp);
                line[cp] = '\0';
                int is_err = (strstr(line, " fallo:") != NULL);
                draw_text_c(e, line, left + BOTTOM_PAD, ly,
                            is_err ? err_col : e->theme.ftree_txt_file);
            }
        }
        if (!nl) break;
        p = nl + 1;
        ++li;
        if (li - scroll >= visible_lines && li > sel_hi) break; /* nada mas que pintar */
    }

    SDL_SetRenderClipRect(r, NULL); /* quitar el recorte */
}
