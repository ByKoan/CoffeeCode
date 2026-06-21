/**
 * @file render_ext.c
 * @brief Dibujado del sistema de extensiones.
 *
 * Contiene tres piezas:
 *   1. El CoffeePainter concreto + la vtable CoffeePaint: las primitivas de
 *      dibujo (ABI estable) que el IDE da a las extensiones para que pinten sus
 *      vistas SIN tocar SDL directamente.  Se implementan sobre los envoltorios
 *      de render_internal.h (fill_rect, draw_text_c, ...).
 *   2. ::render_ext_views: recorre las vistas que las extensiones registraron
 *      via CoffeeApi::register_view y llama a su callback de pintado con un
 *      CoffeePainter recortado a su area.
 *   3. ::render_ext_panel: el PANEL DE EXTENSIONES del propio IDE (el
 *      "marketplace de cargadas"): lista las extensiones, con botones para
 *      descargar/recargar y un boton "Instalar" (cargar desde una carpeta).
 *
 * El panel se ancla a la DERECHA de la ventana para no chocar con el explorador
 * de archivos (que vive a la izquierda).  Su geometria la registra el render en
 * e->ui para que el input resuelva los clics con ui_hit / ui_hit_idx.
 */
#include "render_internal.h"
#include "ext/ext_host.h"
#include "layout/layout.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

/* -- Geometria (px) -------------------------------------------------------- */
/* El ancho del panel ya no es fijo: vive en e->ext_panel_w (redimensionable
 * arrastrando el borde).  El valor inicial es LAYOUT_EXT_DEFAULT_W. */
#define EXT_HEADER_H 28     /* alto de la cabecera                   */
#define EXT_ROW_H 46        /* alto de la fila de una extension      */
#define EXT_BTN_W 64        /* ancho de los botones recargar/descargar */
#define EXT_BTN_H 18        /* alto de esos botones                  */
#define EXT_PAD 8           /* margen interior                       */
#define EXT_INSTALL_H 26    /* alto del boton "Instalar"             */
#define EXT_GUTTER_PAD 4    /* sangria del glifo en el gutter (== GUTTER_NUM_PAD) */

/* ===========================================================================
 *  CoffeePainter + vtable CoffeePaint (las primitivas que ven las extensiones)
 * =========================================================================== */

/**
 * @brief Implementacion concreta del CoffeePainter opaco.
 *
 * Lleva el Editor (para acceder al renderer/fuente) y el rectangulo base de la
 * vista; las coordenadas que la extension pasa son RELATIVAS a su area, asi que
 * el painter les suma el origen del area antes de dibujar.  @c clip recorta el
 * dibujo al area de la vista.
 */
struct CoffeePainter {
    Editor *e;        /**< editor (renderer + fuente) */
    CoffeeRect base;  /**< origen + tamano del area asignada a la vista */
};

/** Traduce un CoffeeColor (ABI extension) a un Color del tema. */
static Color cc_to_color(CoffeeColor c) {
    Color out = {c.r, c.g, c.b, c.a};
    return out;
}

static void cp_fill_rect(CoffeePainter *p, CoffeeRect r, CoffeeColor c) {
    set_color(p->e->renderer, c.r, c.g, c.b, c.a);
    fill_rect(p->e->renderer, p->base.x + r.x, p->base.y + r.y, r.w, r.h);
}

static void cp_draw_rect(CoffeePainter *p, CoffeeRect r, CoffeeColor c) {
    set_color(p->e->renderer, c.r, c.g, c.b, c.a);
    stroke_rect(p->e->renderer, p->base.x + r.x, p->base.y + r.y, r.w, r.h);
}

static void cp_draw_line(CoffeePainter *p, int x0, int y0, int x1, int y1,
                         CoffeeColor c) {
    set_color(p->e->renderer, c.r, c.g, c.b, c.a);
    SDL_RenderLine(p->e->renderer, (float)(p->base.x + x0),
                   (float)(p->base.y + y0), (float)(p->base.x + x1),
                   (float)(p->base.y + y1));
}

static void cp_draw_text(CoffeePainter *p, int x, int y, const char *utf8,
                         CoffeeColor c) {
    draw_text_c(p->e, utf8, p->base.x + x, p->base.y + y, cc_to_color(c));
}

static int cp_text_width(CoffeePainter *p, const char *utf8) {
    int w = 0, h = 0;
    if (utf8 && utf8[0]) TTF_GetStringSize(p->e->font, utf8, 0, &w, &h);
    return w;
}

static int cp_line_height(CoffeePainter *p) { return p->e->line_height; }

static void cp_set_clip(CoffeePainter *p, CoffeeRect r) {
    SDL_Rect clip = {p->base.x + r.x, p->base.y + r.y, r.w, r.h};
    SDL_SetRenderClipRect(p->e->renderer, &clip);
}

/** Mapea un rol del tema ("bg","fg","accent","gutter") a un CoffeeColor. */
static CoffeeColor cp_theme_color(CoffeePainter *p, const char *role) {
    Color c = p->e->theme.col_bg; /* por defecto: fondo */
    if (role) {
        if (strcmp(role, "fg") == 0)
            c = p->e->theme.txt_status;
        else if (strcmp(role, "accent") == 0)
            c = p->e->theme.col_tab_accent;
        else if (strcmp(role, "gutter") == 0)
            c = p->e->theme.col_gutter;
        else if (strcmp(role, "bg") == 0)
            c = p->e->theme.col_bg;
    }
    CoffeeColor out = {c.r, c.g, c.b, c.a ? c.a : 255};
    return out;
}

/** Vtable estatica con las primitivas (se pasa por puntero a cada paint). */
static const CoffeePaint g_paint_vtable = {
    cp_fill_rect, cp_draw_rect,  cp_draw_line,   cp_draw_text,
    cp_text_width, cp_line_height, cp_set_clip,  cp_theme_color,
};

/* ===========================================================================
 *  render_ext_views: dibujar las vistas registradas por extensiones
 * =========================================================================== */

/**
 * @brief Dibuja una vista de extension dentro del rectangulo @p area.
 *
 * Construye un CoffeePainter recortado a @p area, fija el clip de SDL y llama
 * al callback de pintado de la vista.  Restaura el clip al terminar.
 */
static void paint_one_view(Editor *e, const CoffeeHostView *v, CoffeeRect area) {
    CoffeePainter painter;
    painter.e = e;
    painter.base = area;
    SDL_Rect clip = {area.x, area.y, area.w, area.h};
    SDL_SetRenderClipRect(e->renderer, &clip);
    v->paint((CoffeeHost *)e->ext_host, &painter, &g_paint_vtable, area,
             v->userdata);
    SDL_SetRenderClipRect(e->renderer, NULL); /* quitar el recorte */
}

void render_ext_views(Editor *e) {
    /* Implementado dentro de render_ext_panel: las vistas de tipo PANEL/SIDEBAR
     * se apilan al pie del panel de extensiones.  Esta funcion queda como punto
     * de extension futuro (overlays sobre el editor, segmentos de statusbar). */
    (void)e;
}

/* ===========================================================================
 *  Decoraciones de linea (set_line_background / set_gutter_marker)
 * ---------------------------------------------------------------------------
 *  Las decoraciones son por-BUFFER: se consultan al host con el buffer en vivo
 *  (e->buf), asi cada archivo muestra las suyas.  Sin host/buffer/decoraciones
 *  estas funciones no pintan nada (cero regresion para el editor sin extensiones
 *  o sin diagnosticos).
 * =========================================================================== */

void render_ext_line_backgrounds(Editor *e, int content_right, int text_top,
                                 int visible_lines, int total_lines) {
    if (!e->ext_host || !e->buf) return;
    CoffeeHost *host = (CoffeeHost *)e->ext_host;
    int band_left = render_content_left(e);
    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi; /* linea logica de esta fila */
        if (li >= total_lines) break;
        CoffeeColor bg;
        if (!ext_host_line_background(host, e->buf, (size_t)li, &bg)) continue;
        set_color(e->renderer, bg.r, bg.g, bg.b, bg.a);
        fill_rect(e->renderer, band_left, text_top + vi * e->line_height,
                  content_right - band_left, e->line_height);
    }
}

int render_ext_gutter_marker(Editor *e, int li, int gutter_x, int y) {
    if (!e->ext_host || !e->buf) return 0;
    CoffeeHost *host = (CoffeeHost *)e->ext_host;
    const char *glyph = NULL;
    CoffeeColor color;
    if (!ext_host_gutter_marker(host, e->buf, (size_t)li, &glyph, &color))
        return 0;
    if (!glyph || !glyph[0]) return 0;
    /* dibujar el glifo a la izquierda del gutter, alineado verticalmente como
     * el numero de linea; el color es el que fijo la extension. */
    draw_text(e, glyph, gutter_x + EXT_GUTTER_PAD,
              y + (e->line_height - e->font_size) / 2, color.r, color.g,
              color.b);
    return 1;
}

/* ===========================================================================
 *  render_ext_panel: el panel de extensiones del IDE (marketplace de cargadas)
 * =========================================================================== */

int render_ext_panel_width(Editor *e) {
    return e->ext_panel_open ? e->ext_panel_w : 0;
}

/** Trunca @p name con ".." si supera @p max_chars caracteres (in situ). */
static void ext_truncate(char *out, size_t out_sz, const char *name,
                         int max_chars) {
    if (max_chars < 3) max_chars = 3;
    if ((int)strlen(name) > max_chars) {
        size_t keep = (size_t)(max_chars - 2);
        if (keep > out_sz - 1) keep = out_sz - 1;
        memcpy(out, name, keep);
        out[keep] = '.';
        out[keep + 1] = '.';
        out[keep + 2] = '\0';
    } else {
        snprintf(out, out_sz, "%s", name);
    }
}

void render_ext_panel(Editor *e) {
    if (!e->ext_panel_open) return;
    CoffeeHost *host = (CoffeeHost *)e->ext_host;
    SDL_Renderer *r = e->renderer;

    int panel_w = e->ext_panel_w;
    int panel_x = e->win_w - panel_w;
    int panel_y = NAVBAR_HEIGHT;
    int panel_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;

    /* fondo + separador izquierdo + registro del marco (consume clics dentro).
     * El fondo va por chrome_fill_bg para componer con el fondo en see-through;
     * el separador se conserva. */
    chrome_fill_bg(e, e->theme.col_ftree_bg, panel_x, panel_y, panel_w, panel_h);
    if (e->theme.col_ftree_sep.a) {
        set_color_c(r, e->theme.col_ftree_sep);
        stroke_rect(r, panel_x, panel_y, panel_w, panel_h);
    }
    ui_put(&e->ui, UI_EXT_PANEL, (Rect){panel_x, panel_y, panel_w, panel_h});

    int cy = panel_y; /* cursor vertical de dibujo */

    /* -- Cabecera -- */
    chrome_fill_bg(e, e->theme.col_ftree_header, panel_x, cy, panel_w,
                   EXT_HEADER_H);
    draw_text_c(e, " Extensiones", panel_x + EXT_PAD,
                cy + (EXT_HEADER_H - e->font_size) / 2, e->theme.ftree_txt_root);
    cy += EXT_HEADER_H;

    /* -- Boton "Instalar extension" -- */
    Rect install = {panel_x + EXT_PAD, cy + 4, panel_w - 2 * EXT_PAD,
                    EXT_INSTALL_H};
    ui_button(e, UI_EXT_INSTALL, install, "+ Instalar extension",
              &e->theme.style_primary, UI_NORMAL);
    cy += EXT_INSTALL_H + 8;

    /* -- Lista de extensiones cargadas (introspeccion del host) -- */
    size_t n = host ? ext_host_count(host) : 0;
    int any = 0;
    for (size_t i = 0; i < n; ++i) {
        const char *id = NULL, *dir = NULL;
        int active = 0;
        if (!ext_host_info(host, i, &id, NULL, &dir, &active)) continue;
        if (!active || !id) continue; /* saltar slots libres */
        any = 1;

        int row_y = cy;
        /* fondo de la fila */
        set_color_c(r, e->theme.col_ftree_header);
        fill_rect(r, panel_x + EXT_PAD, row_y, panel_w - 2 * EXT_PAD,
                  EXT_ROW_H - 4);

        /* nombre (id) de la extension */
        char label[64];
        int char_w = (e->char_w > 0 ? e->char_w : 8);
        int max_chars = (panel_w - 2 * EXT_PAD - 8) / char_w;
        ext_truncate(label, sizeof(label), id, max_chars);
        draw_text_c(e, label, panel_x + 2 * EXT_PAD, row_y + 4,
                    e->theme.ftree_txt_dir);

        /* estado "activa" + botones recargar / descargar */
        draw_text_c(e, "activa", panel_x + 2 * EXT_PAD, row_y + 4 + e->font_size,
                    e->theme.ftree_txt_file);

        int btn_y = row_y + 4 + e->font_size;
        Rect rel = {panel_x + panel_w - EXT_PAD - 2 * EXT_BTN_W - 6, btn_y,
                    EXT_BTN_W, EXT_BTN_H};
        Rect unl = {panel_x + panel_w - EXT_PAD - EXT_BTN_W, btn_y, EXT_BTN_W,
                    EXT_BTN_H};
        ui_button(e, UI_ID_NONE, rel, "recargar", &e->theme.style_button,
                  UI_NORMAL);
        ui_put_idx(&e->ui, UI_LIST_EXT_RELOAD, (int)i, rel);
        ui_button(e, UI_ID_NONE, unl, "descargar", &e->theme.style_button,
                  UI_NORMAL);
        ui_put_idx(&e->ui, UI_LIST_EXT_UNLOAD, (int)i, unl);

        cy += EXT_ROW_H;
        if (cy > panel_y + panel_h - EXT_ROW_H) break; /* lleno */
    }
    if (!any) {
        draw_text_c(e, " (ninguna cargada)", panel_x + EXT_PAD, cy,
                    e->theme.ftree_txt_file);
        cy += e->line_height;
    }

    /* -- Vistas registradas por extensiones (register_view) --
     * La salida de las extensiones ya NO se dibuja aqui: vive en la pestana
     * "Salida" del panel inferior (ver render_bottom.c). */
    size_t nv = host ? ext_host_view_count(host) : 0;
    for (size_t i = 0; i < nv; ++i) {
        CoffeeHostView v;
        if (!ext_host_view_at(host, i, &v)) continue;
        if (v.kind != COFFEE_VIEW_PANEL && v.kind != COFFEE_VIEW_SIDEBAR)
            continue;
        int avail = panel_y + panel_h - cy - 4;
        if (avail < 24) break; /* no queda hueco */
        int vh = avail > 80 ? 80 : avail;
        if (v.title && v.title[0]) {
            draw_text_c(e, v.title, panel_x + EXT_PAD, cy, e->theme.ftree_txt_root);
            cy += e->line_height;
            vh -= e->line_height;
        }
        CoffeeRect area = {panel_x + EXT_PAD, cy, panel_w - 2 * EXT_PAD, vh};
        paint_one_view(e, &v, area);
        cy += vh + 4;
    }

    /* -- Divisor agarrable: borde izquierdo del panel --
     * Cuando el cursor lo sobrevuela o se esta arrastrando, se resalta (mas
     * brillante y un poco mas ancho) para que el usuario VEA que es agarrable. */
    int active = (e->dragging_divider == DIVIDER_EXT_PANEL_LEFT) ||
                 (e->hovered_divider == DIVIDER_EXT_PANEL_LEFT);
    if (active) {
        Color hl = {120, 170, 230, 255}; /* azul de realce, agarre visible */
        set_color_c(r, hl);
        fill_rect(r, panel_x - 1, panel_y, 2, panel_h);
    }
}
