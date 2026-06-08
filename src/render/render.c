#include "render/render.h"
#include "filetree/filetree.h"
#include <stdio.h>
#include <string.h>

/* -- Colores del tema ------------------------------------------------------ */
#define COL_BG          0x28, 0x2C, 0x34, 0xFF
#define COL_GUTTER      0x21, 0x25, 0x2B, 0xFF
#define COL_CURSOR_LINE 0x2C, 0x31, 0x3C, 0xFF
#define COL_CURSOR      0x52, 0x8B, 0xFF, 0xFF
#define COL_STATUS_BG   0x21, 0x25, 0x2B, 0xFF
#define COL_SEL_BG      0x26, 0x4F, 0x78, 0xFF   /* azul selección */

/* Navbar */
#define COL_NAVBAR_BG   0x1A, 0x1D, 0x23, 0xFF
#define COL_NAVBAR_BTN  0x2C, 0x31, 0x3C, 0xFF  /* hover del botón */
#define COL_MENU_BG     0x1E, 0x22, 0x2A, 0xFF
#define COL_MENU_HOVER  0x3E, 0x44, 0x55, 0xFF
#define COL_MENU_SEP    0x3A, 0x3F, 0x4A, 0xFF
#define COL_MENU_BORDER 0x3A, 0x3F, 0x4A, 0xFF

/* Menú "Archivo" */
#define MENU_ITEM_H     26
#define MENU_WIDTH     210
#define MENU_ITEMS      6

static const char *MENU_LABELS[MENU_ITEMS] = {
    "Nuevo",
    "Abrir archivo...",
    "Abrir carpeta...",
    NULL,
    "Guardar",
    "Autoguardado"
};
static const char *MENU_HINTS[MENU_ITEMS] = {
    "Ctrl+N", "Ctrl+O", "Ctrl+K", NULL, "Ctrl+S", NULL
};

/* Colores panel lateral */
#define COL_FTREE_BG    0x1E, 0x22, 0x2A, 0xFF
#define COL_FTREE_HOVER 0x2C, 0x31, 0x3C, 0xFF
#define COL_FTREE_SEP   0x3A, 0x3F, 0x4A, 0xFF
#define COL_FTREE_DIR   0xE5, 0xC0, 0x7B, 0xFF
#define COL_FTREE_FILE  0xAB, 0xB2, 0xBF, 0xFF
#define COL_FTREE_ROOT  0x61, 0xAF, 0xEF, 0xFF

/* -- Scrollbar ------------------------------------------------------------- */
#define SCROLLBAR_W     8    /* ancho de la barra de scroll (px) */

static void set_color(SDL_Renderer *r, uint8_t R, uint8_t G, uint8_t B, uint8_t A) {
    SDL_SetRenderDrawColor(r, R, G, B, A);
}

static int draw_text(Editor *e, const char *text, int x, int y,
                     uint8_t R, uint8_t G, uint8_t B)
{
    if (!text || !text[0]) return 0;
    SDL_Color col = {R, G, B, 255};
    SDL_Surface *surf = TTF_RenderText_Blended(e->font, text, 0, col);
    if (!surf) return 0;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(e->renderer, surf);
    SDL_FRect dst = {(float)x, (float)y, (float)surf->w, (float)surf->h};
    SDL_RenderTexture(e->renderer, tex, NULL, &dst);
    int w = surf->w;
    SDL_DestroySurface(surf);
    SDL_DestroyTexture(tex);
    return w;
}

/*
 * get_line_text — O(longitud de línea), no O(n).
 * Usa el índice de líneas del Buffer para saltar directamente al inicio
 * de la línea pedida, evitando iterar desde el principio del archivo.
 */
static int get_line_text(Editor *e, int line, char *out, int max) {
    const Buffer *b = e->buf;
    if (line < 0 || line >= buf_line_count(b)) { out[0] = '\0'; return 0; }

    size_t start = buf_line_offset(b, line);
    size_t total = buf_length(b);
    int col = 0;

    for (size_t i = start; i < total && col < max - 1; i++) {
        char c = buf_char_at(b, i);
        if (c == '\n') break;
        if (c == '\t') {
            int spaces = TAB_SIZE - (col % TAB_SIZE);
            for (int s = 0; s < spaces && col < max - 1; s++)
                out[col++] = ' ';
        } else {
            out[col++] = c;
        }
    }
    out[col] = '\0';
    return col;
}

/* -- Navbar --------------------------------------------------------------- */
static void render_navbar(Editor *e) {
    SDL_Renderer *r = e->renderer;

    set_color(r, COL_NAVBAR_BG);
    SDL_FRect nb = {0, 0, (float)e->win_w, (float)NAVBAR_HEIGHT};
    SDL_RenderFillRect(r, &nb);

    set_color(r, 0x3A, 0x3F, 0x4A, 0xFF);
    SDL_FRect sep = {0, (float)(NAVBAR_HEIGHT - 1), (float)e->win_w, 1};
    SDL_RenderFillRect(r, &sep);

    int btn_x = 4, btn_y = 2;
    int btn_h = NAVBAR_HEIGHT - 4;

    /* Ancho del boton: calcular una vez y cachear en un static */
    static int cached_btn_w = 0;
    if (cached_btn_w == 0) {
        int lw = 0, lh = 0;
        TTF_GetStringSize(e->font, "  Archivo  ", 0, &lw, &lh);
        cached_btn_w = (lw > 20) ? lw : 90;
    }
    int btn_w = cached_btn_w;

    if (e->menu_open) {
        set_color(r, COL_NAVBAR_BTN);
        SDL_FRect btn = {(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h};
        SDL_RenderFillRect(r, &btn);
    }

    int ty = btn_y + (btn_h - FONT_SIZE) / 2;
    draw_text(e, "  Archivo  ", btn_x, ty, 0xCC, 0xCC, 0xCC);

    /* Titulo: siempre "CoffeeCode" + nombre del archivo si hay uno */
    char nav_title[600];
    if (e->filepath[0]) {
        const char *fname = e->filepath + strlen(e->filepath);
        while (fname > e->filepath && *(fname-1) != '/' && *(fname-1) != '\\') fname--;
        snprintf(nav_title, sizeof(nav_title), "CoffeeCode \xe2\x80\x94 %s", fname);
    } else {
        snprintf(nav_title, sizeof(nav_title), "CoffeeCode");
    }

    int tw = 0, th = 0;
    TTF_GetStringSize(e->font, nav_title, 0, &tw, &th);
    int cx = (e->win_w - tw) / 2;
    if (cx < btn_x + btn_w + 8) cx = btn_x + btn_w + 8;
    draw_text(e, nav_title, cx, ty, 0x80, 0x85, 0x95);

    if (e->modified) {
        set_color(r, 0xE0, 0x6C, 0x75, 0xFF);
        SDL_FRect dot = {(float)(cx + tw + 6), (float)(ty + FONT_SIZE/2 - 3), 6, 6};
        SDL_RenderFillRect(r, &dot);
    }
}

/* -- Menú desplegable ------------------------------------------------------ */
static void render_menu(Editor *e) {
    if (!e->menu_open) return;
    SDL_Renderer *r = e->renderer;

    int mx = 4, my = NAVBAR_HEIGHT;
    int total_h = 0;
    for (int i = 0; i < MENU_ITEMS; i++)
        total_h += MENU_LABELS[i] ? MENU_ITEM_H : 8;

    set_color(r, 0x00, 0x00, 0x00, 0x60);
    SDL_FRect shadow = {(float)(mx+3), (float)(my+3),
                        (float)MENU_WIDTH, (float)total_h};
    SDL_RenderFillRect(r, &shadow);

    set_color(r, COL_MENU_BG);
    SDL_FRect bg = {(float)mx, (float)my, (float)MENU_WIDTH, (float)total_h};
    SDL_RenderFillRect(r, &bg);

    set_color(r, COL_MENU_BORDER);
    SDL_FRect borders[4] = {
        {(float)mx, (float)my, (float)MENU_WIDTH, 1},
        {(float)mx, (float)(my+total_h-1), (float)MENU_WIDTH, 1},
        {(float)mx, (float)my, 1, (float)total_h},
        {(float)(mx+MENU_WIDTH-1), (float)my, 1, (float)total_h}
    };
    for (int i = 0; i < 4; i++) SDL_RenderFillRect(r, &borders[i]);

    int iy = my;
    for (int i = 0; i < MENU_ITEMS; i++) {
        if (!MENU_LABELS[i]) {
            set_color(r, COL_MENU_SEP);
            SDL_FRect s = {(float)(mx+8), (float)(iy+4),
                           (float)(MENU_WIDTH-16), 1};
            SDL_RenderFillRect(r, &s);
            iy += 8;
            continue;
        }

        if (e->menu_hovered == i) {
            set_color(r, COL_MENU_HOVER);
            SDL_FRect hi = {(float)(mx+1), (float)iy,
                            (float)(MENU_WIDTH-2), (float)MENU_ITEM_H};
            SDL_RenderFillRect(r, &hi);
        }

        int ty2 = iy + (MENU_ITEM_H - FONT_SIZE) / 2;

        /* Item de autoguardado: mostrar checkmark si esta activo */
        if (i == 5) {
            if (e->autosave) {
                draw_text(e, "\xe2\x9c\x93", mx + 4, ty2, 0x98, 0xC3, 0x79);
            }
        }

        draw_text(e, MENU_LABELS[i], mx + 14, ty2, 0xCC, 0xCC, 0xCC);

        if (MENU_HINTS[i]) {
            int hw = 0, hh = 0;
            TTF_GetStringSize(e->font, MENU_HINTS[i], 0, &hw, &hh);
            /* Verificar que el hint no se solape con el label */
            int label_end_x = 0;
            TTF_GetStringSize(e->font, MENU_LABELS[i], 0, &label_end_x, &hh);
            int hint_x = mx + MENU_WIDTH - hw - 10;
            int label_right = mx + 14 + label_end_x + 8;
            if (hint_x > label_right) {
                draw_text(e, MENU_HINTS[i], hint_x, ty2, 0x60, 0x65, 0x70);
            }
        }
        iy += MENU_ITEM_H;
    }
}

/* -- Panel lateral -------------------------------------------------------- */
static void render_filetree(Editor *e) {
    FileTree *ft = &e->ftree;
    if (!ft->open) return;
    SDL_Renderer *r = e->renderer;

    int panel_x = 0;
    int panel_y = NAVBAR_HEIGHT;
    int panel_w = ft->width;
    int panel_h = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT;

    set_color(r, COL_FTREE_BG);
    SDL_FRect bg = {(float)panel_x, (float)panel_y,
                    (float)panel_w, (float)panel_h};
    SDL_RenderFillRect(r, &bg);

    set_color(r, COL_FTREE_SEP);
    SDL_FRect sep = {(float)(panel_x + panel_w - 1), (float)panel_y,
                     1.0f, (float)panel_h};
    SDL_RenderFillRect(r, &sep);

    int btn_w = FTREE_TOGGLE_BTN_W;
    int btn_h = 40;
    /* El boton toggle se centra en el area visible debajo de la tabbar */
    int content_top = panel_y + TAB_BAR_HEIGHT;
    int content_h   = panel_h - TAB_BAR_HEIGHT;
    int btn_y = content_top + (content_h - btn_h) / 2;
    int btn_x = panel_x + panel_w - btn_w;
    set_color(r, 0x2C, 0x31, 0x3C, 0xFF);
    SDL_FRect tbtn = {(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h};
    SDL_RenderFillRect(r, &tbtn);
    draw_text(e, "<", btn_x + 2, btn_y + (btn_h - FONT_SIZE) / 2,
              0x61, 0xAF, 0xEF);

    /* Header del panel: debajo de la tabbar para que no quede tapado */
    int header_h = 26;
    int header_y = content_top;
    set_color(r, 0x17, 0x1A, 0x21, 0xFF);
    SDL_FRect hdr = {(float)panel_x, (float)header_y,
                     (float)(panel_w - btn_w), (float)header_h};
    SDL_RenderFillRect(r, &hdr);

    /* Titulo "CoffeeCode" si no hay carpeta, o nombre de carpeta si hay */
    const char *rname = ft->root_path;
    char root_label[64];
    if (rname && rname[0]) {
        const char *s = rname + strlen(rname);
        while (s > rname && *(s-1) != '/' && *(s-1) != '\\') s--;
        snprintf(root_label, sizeof(root_label), " %s", *s ? s : rname);
    } else {
        snprintf(root_label, sizeof(root_label), " CoffeeCode");
    }
    draw_text(e, root_label, panel_x + 4,
              header_y + (header_h - FONT_SIZE) / 2,
              0x61, 0xAF, 0xEF);

    int visible_rows = (content_h - header_h) / FTREE_ITEM_H;
    int vis_count    = ftree_visible_count(ft);
    int max_scroll   = vis_count - visible_rows;
    if (ft->scroll > max_scroll) ft->scroll = max_scroll;
    if (ft->scroll < 0)         ft->scroll = 0;

    int drawn = 0;
    for (int i = 0; i < ftree_count(ft) && drawn < visible_rows + ft->scroll; i++) {
        FEntry *en = ftree_entry(ft, i);
        if (!en->visible) continue;
        int vis_idx = drawn++;
        if (vis_idx < ft->scroll) continue;
        int row = vis_idx - ft->scroll;

        int ey = header_y + header_h + row * FTREE_ITEM_H;
        int ex = panel_x + 4 + en->depth * FTREE_INDENT;

        if (ft->hovered == i) {
            set_color(r, COL_FTREE_HOVER);
            SDL_FRect hi = {(float)panel_x, (float)ey,
                            (float)(panel_w - btn_w), (float)FTREE_ITEM_H};
            SDL_RenderFillRect(r, &hi);
        }

        if (en->type == FTYPE_DIR) {
            const char *icon = en->expanded ? "v " : "> ";
            draw_text(e, icon, ex, ey + (FTREE_ITEM_H - FONT_SIZE) / 2,
                      0xE5, 0xC0, 0x7B);
            ex += FTREE_ICON_W;
        } else {
            ex += FTREE_ICON_W;
        }

        char label[128];
        int max_chars = (panel_w - btn_w - ex - 4) / (e->char_w > 0 ? e->char_w : 8);
        if (max_chars < 3) max_chars = 3;
        if ((int)strlen(en->name) > max_chars) {
            strncpy(label, en->name, (size_t)(max_chars - 2));
            label[max_chars - 2] = '.';
            label[max_chars - 1] = '.';
            label[max_chars]     = '\0';
        } else {
            strncpy(label, en->name, sizeof(label) - 1);
            label[sizeof(label) - 1] = '\0';
        }

        if (en->type == FTYPE_DIR) {
            draw_text(e, label, ex, ey + (FTREE_ITEM_H - FONT_SIZE) / 2,
                      0xE5, 0xC0, 0x7B);
        } else {
            draw_text(e, label, ex, ey + (FTREE_ITEM_H - FONT_SIZE) / 2,
                      0xAB, 0xB2, 0xBF);
        }
    }
}

static void render_filetree_toggle_closed(Editor *e) {
    if (e->ftree.open) return;
    SDL_Renderer *r = e->renderer;
    int btn_w = FTREE_TOGGLE_BTN_W;
    int btn_h = 40;
    int panel_y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    int panel_h = e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT;
    int btn_y   = panel_y + (panel_h - btn_h) / 2;

    set_color(r, 0x2C, 0x31, 0x3C, 0xFF);
    SDL_FRect tbtn = {0.0f, (float)btn_y, (float)btn_w, (float)btn_h};
    SDL_RenderFillRect(r, &tbtn);

    set_color(r, COL_FTREE_SEP);
    SDL_FRect sep = {(float)(btn_w - 1), (float)panel_y, 1.0f, (float)panel_h};
    SDL_RenderFillRect(r, &sep);

    draw_text(e, ">", 2, btn_y + (btn_h - FONT_SIZE) / 2,
              0x61, 0xAF, 0xEF);
}

/* -- Barra de búsqueda ----------------------------------------------------- */
/* -- Tab bar --------------------------------------------------------------- */
static void render_tabbar(Editor *e)
{
    SDL_Renderer *r = e->renderer;
    int bar_y = NAVBAR_HEIGHT;
    int bar_h = TAB_BAR_HEIGHT;

    /* fondo de toda la barra */
    set_color(r, 0x16, 0x19, 0x1F, 255);
    SDL_FRect bg = {0, (float)bar_y, (float)e->win_w, (float)bar_h};
    SDL_RenderFillRect(r, &bg);

    /* línea separadora inferior */
    set_color(r, 0x2A, 0x2E, 0x38, 255);
    SDL_FRect sep = {0, (float)(bar_y + bar_h - 1), (float)e->win_w, 1};
    SDL_RenderFillRect(r, &sep);

    int left = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    int tx = left;

    for (int i = 0; i < e->tab_count; i++) {
        EditorTab *t = &e->tabs[i];
        int active = (i == e->active_tab);

        /* nombre corto del archivo */
        const char *name = t->filepath[0] ? t->filepath : "Sin título";
        /* buscar la última / o \ */
        const char *slash = name;
        for (const char *p = name; *p; p++)
            if (*p == '/' || *p == '\\') slash = p + 1;
        name = slash;

        /* medir texto para calcular ancho del tab */
        int tw = 0, th = 0;
        TTF_GetStringSize(e->font, name, 0, &tw, &th);
        int close_w = 16;
        int pad = 10;
        int tab_w = tw + close_w + pad * 2 + 4;
        if (tab_w < 80)  tab_w = 80;
        if (tab_w > 200) tab_w = 200;

        /* guardar posición para click detection */
        e->tabs[i].tab_x = tx;
        e->tabs[i].tab_w = tab_w;

        /* fondo del tab */
        if (active) {
            set_color(r, 0x1E, 0x22, 0x2B, 255);
        } else {
            set_color(r, 0x16, 0x19, 0x1F, 255);
        }
        SDL_FRect tbg = {(float)tx, (float)bar_y, (float)tab_w, (float)bar_h};
        SDL_RenderFillRect(r, &tbg);

        /* borde derecho separador */
        set_color(r, 0x2A, 0x2E, 0x38, 255);
        SDL_FRect tbord = {(float)(tx + tab_w - 1), (float)bar_y, 1, (float)bar_h};
        SDL_RenderFillRect(r, &tbord);

        /* línea de acento en el tab activo */
        if (active) {
            set_color(r, 0x52, 0x8B, 0xD4, 255);
            SDL_FRect accent = {(float)tx, (float)bar_y, (float)tab_w, 2};
            SDL_RenderFillRect(r, &accent);
        }

        /* texto del tab — truncado si no cabe */
        int text_max_w = tab_w - close_w - pad * 2 - 4;
        int ty2 = bar_y + (bar_h - FONT_SIZE) / 2;
        uint8_t tr = active ? 0xCC : 0x66, tg = active ? 0xCC : 0x6A, tb2 = active ? 0xDD : 0x75;
        /* renderizar con clip temporal */
        SDL_Rect clip = {tx + pad, bar_y, text_max_w, bar_h};
        SDL_SetRenderClipRect(r, &clip);
        draw_text(e, name, tx + pad, ty2, tr, tg, tb2);
        SDL_SetRenderClipRect(r, NULL);

        /* punto de modificado */
        if (t->modified) {
            set_color(r, 0xE0, 0x90, 0x40, 255);
            SDL_FRect dot = {(float)(tx + pad + text_max_w + 2), (float)(ty2 + FONT_SIZE/2 - 3), 5, 5};
            SDL_RenderFillRect(r, &dot);
        }

        /* botón × de cerrar */
        int cx = tx + tab_w - close_w - 2;
        int cy = bar_y + (bar_h - 14) / 2;
        e->tabs[i].close_x = cx;
        e->tabs[i].close_y = cy;
        uint8_t xr = active ? 0x88 : 0x44, xg = active ? 0x88 : 0x44, xb = active ? 0x88 : 0x44;
        draw_text(e, "×", cx, cy, xr, xg, xb);

        tx += tab_w;
    }

    /* botón + nuevo tab */
    e->tab_new_btn_x = tx;
    int btn_plus_w = 28;
    set_color(r, 0x16, 0x19, 0x1F, 255);
    SDL_FRect pbg = {(float)tx, (float)bar_y, (float)btn_plus_w, (float)bar_h};
    SDL_RenderFillRect(r, &pbg);
    int pty = bar_y + (bar_h - FONT_SIZE) / 2;
    draw_text(e, "+", tx + 7, pty, 0x55, 0x5A, 0x6A);
}

static void render_find_bar(Editor *e)
{
    if (!e->find.visible) return;

    /* Medir etiquetas con la fuente real para evitar recortes */
    int lbl1_w = 0, lbl2_w = 0, lh = 0;
    TTF_GetStringSize(e->font, "Buscar:",     0, &lbl1_w, &lh);
    TTF_GetStringSize(e->font, "Reemplazar:", 0, &lbl2_w, &lh);
    int lbl_w   = (lbl1_w > lbl2_w ? lbl1_w : lbl2_w) + 4; /* el más ancho + margen */
    int btn_lbl_w = 0;
    TTF_GetStringSize(e->font, "Reemplazar", 0, &btn_lbl_w, &lh);
    int pad      = 10;
    int gap      = 8;
    int btn_w    = btn_lbl_w + 20; /* texto + padding horizontal */
    int field_h  = 22;
    int row_gap  = 8;

    int w = pad + lbl_w + gap + 200 + gap + btn_w + pad; /* mínimo útil */
    if (w < 400) w = 400;
    int h = pad + field_h + row_gap + field_h + pad;
    int x = e->win_w - w - 12;
    int y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT + 8;

    /* coordenadas derivadas */
    int lbl_x   = x + pad;
    int field_x = lbl_x + lbl_w + gap;
    int field_w = w - pad - lbl_w - gap - pad;        /* campo fila 1: ocupa todo */
    int fld2_w  = field_w - gap - btn_w;              /* campo fila 2: deja espacio al botón */
    int btn_x   = field_x + fld2_w + gap;

    int row1_y  = y + pad;
    int row2_y  = row1_y + field_h + row_gap;

    /* -- Fondo + borde -- */
    set_color(e->renderer, 0x1E,0x22,0x2A,255);
    SDL_FRect bg = {(float)x,(float)y,(float)w,(float)h};
    SDL_RenderFillRect(e->renderer, &bg);
    set_color(e->renderer, 0x3A,0x3F,0x4A,255);
    SDL_FRect borders[4] = {
        {(float)x,(float)y,(float)w,1},
        {(float)x,(float)(y+h-1),(float)w,1},
        {(float)x,(float)y,1,(float)h},
        {(float)(x+w-1),(float)y,1,(float)h}
    };
    for (int i = 0; i < 4; i++) SDL_RenderFillRect(e->renderer, &borders[i]);

    /* helper: dibuja un campo de texto */
#define DRAW_FIELD(fx, fy, fw, focused)     do {         set_color(e->renderer, (focused)?0x2A:0x25, (focused)?0x2E:0x29, (focused)?0x38:0x31, 255);         SDL_FRect _bg = {(float)(fx),(float)(fy),(float)(fw),(float)field_h};         SDL_RenderFillRect(e->renderer, &_bg);         set_color(e->renderer, (focused)?0x52:0x3A, (focused)?0x8B:0x3F, (focused)?0xD4:0x4A, 255);         SDL_FRect _b[4] = {             {(float)(fx),(float)(fy),(float)(fw),1},             {(float)(fx),(float)((fy)+field_h-1),(float)(fw),1},             {(float)(fx),(float)(fy),1,(float)field_h},             {(float)((fx)+(fw)-1),(float)(fy),1,(float)field_h}         };         for (int _i=0;_i<4;_i++) SDL_RenderFillRect(e->renderer, &_b[_i]);     } while(0)

    int focused1 = (e->find.bar_focused && e->find.replace_focused == 0);
    int focused2 = (e->find.bar_focused && e->find.replace_focused == 1);

    /* -- Fila 1: Buscar -- */
    /* etiqueta centrada verticalmente */
    draw_text(e, "Buscar:", lbl_x, row1_y + (field_h - FONT_SIZE)/2, 0x88,0x8C,0x99);
    DRAW_FIELD(field_x, row1_y, field_w, focused1);

    /* resaltado de selección en campo buscar */
    if (e->find.query_sel_start >= 0 && e->find.query_sel_end > e->find.query_sel_start
        && e->find.query_sel_end <= e->find.query_len) {
        char tmp_before[FIND_BAR_MAX];
        char tmp_sel[FIND_BAR_MAX];
        int before_len = e->find.query_sel_start;
        int sel_len    = e->find.query_sel_end - e->find.query_sel_start;
        memcpy(tmp_before, e->find.query, before_len);
        tmp_before[before_len] = '\0';
        memcpy(tmp_sel, e->find.query + before_len, sel_len);
        tmp_sel[sel_len] = '\0';
        int bw = 0, sw = 0, sh = 0;
        if (before_len > 0) TTF_GetStringSize(e->font, tmp_before, 0, &bw, &sh);
        if (sel_len   > 0) TTF_GetStringSize(e->font, tmp_sel,    0, &sw, &sh);
        int sel_x = field_x + 4 + bw;
        int sel_y = row1_y + 2;
        int sel_h = field_h - 4;
        set_color(e->renderer, 0x26, 0x4F, 0x78, 200);
        SDL_FRect sel_rect = {(float)sel_x, (float)sel_y, (float)sw, (float)sel_h};
        SDL_RenderFillRect(e->renderer, &sel_rect);
    }

    char txt1[512];
    snprintf(txt1,sizeof(txt1),"%s%s",
             e->find.query,
             (focused1 && e->find.query_sel_start < 0 && (SDL_GetTicks()/500)%2) ? "|" : "");
    draw_text(e, txt1, field_x+4, row1_y+(field_h-FONT_SIZE)/2, 220,220,220);

    if (e->find.result_line >= 0 && e->find.match_count > 0) {
        /* -- Botones ↑ ↓ + contador "X/N" a la derecha del campo buscar -- */
        int arrow_w = field_h;  /* botones cuadrados */
        int ctr_w   = 0, ctr_h = 0;
        char ctr_lbl[32];
        snprintf(ctr_lbl, sizeof(ctr_lbl), "%d/%d", e->find.match_index + 1, e->find.match_count);
        TTF_GetStringSize(e->font, ctr_lbl, 0, &ctr_w, &ctr_h);

        int btn_area_w = arrow_w + 4 + ctr_w + 4 + arrow_w + 4;
        int prev_x = field_x + field_w - btn_area_w;
        int next_x = prev_x + arrow_w + 4 + ctr_w + 4;

        /* Guardar geometría para click detection */
        e->find.prev_btn_x = prev_x; e->find.prev_btn_y = row1_y;
        e->find.prev_btn_w = arrow_w; e->find.prev_btn_h = field_h;
        e->find.next_btn_x = next_x; e->find.next_btn_y = row1_y;
        e->find.next_btn_w = arrow_w; e->find.next_btn_h = field_h;

        /* Botón ↑ (prev) */
        set_color(e->renderer, 0x2A,0x2E,0x38,255);
        SDL_FRect pbg = {(float)prev_x,(float)row1_y,(float)arrow_w,(float)field_h};
        SDL_RenderFillRect(e->renderer, &pbg);
        set_color(e->renderer, 0x3A,0x3F,0x4A,255);
        SDL_FRect pb[4] = {
            {(float)prev_x,(float)row1_y,(float)arrow_w,1},
            {(float)prev_x,(float)(row1_y+field_h-1),(float)arrow_w,1},
            {(float)prev_x,(float)row1_y,1,(float)field_h},
            {(float)(prev_x+arrow_w-1),(float)row1_y,1,(float)field_h}
        };
        for (int i=0;i<4;i++) SDL_RenderFillRect(e->renderer, &pb[i]);
        { int tw=0,th=0; TTF_GetStringSize(e->font,"↑",0,&tw,&th);
          draw_text(e,"↑", prev_x+(arrow_w-tw)/2, row1_y+(field_h-FONT_SIZE)/2, 180,200,230); }

        /* Contador X/N */
        draw_text(e, ctr_lbl,
                  prev_x + arrow_w + 4,
                  row1_y + (field_h - FONT_SIZE)/2, 97,175,239);

        /* Botón ↓ (next) */
        set_color(e->renderer, 0x2A,0x2E,0x38,255);
        SDL_FRect nbg = {(float)next_x,(float)row1_y,(float)arrow_w,(float)field_h};
        SDL_RenderFillRect(e->renderer, &nbg);
        set_color(e->renderer, 0x3A,0x3F,0x4A,255);
        SDL_FRect nb[4] = {
            {(float)next_x,(float)row1_y,(float)arrow_w,1},
            {(float)next_x,(float)(row1_y+field_h-1),(float)arrow_w,1},
            {(float)next_x,(float)row1_y,1,(float)field_h},
            {(float)(next_x+arrow_w-1),(float)row1_y,1,(float)field_h}
        };
        for (int i=0;i<4;i++) SDL_RenderFillRect(e->renderer, &nb[i]);
        { int tw=0,th=0; TTF_GetStringSize(e->font,"↓",0,&tw,&th);
          draw_text(e,"↓", next_x+(arrow_w-tw)/2, row1_y+(field_h-FONT_SIZE)/2, 180,200,230); }
    } else if (e->find.query_len > 0 && e->find.match_count == 0) {
        draw_text(e, "Sin resultados", field_x+field_w/2-30, row1_y+(field_h-FONT_SIZE)/2, 200,80,80);
    }

    /* -- Fila 2: Reemplazar -- */
    draw_text(e, "Reemplazar:", lbl_x, row2_y + (field_h - FONT_SIZE)/2, 0x88,0x8C,0x99);
    DRAW_FIELD(field_x, row2_y, fld2_w, focused2);

    /* resaltado de selección en campo reemplazar */
    if (e->find.replace_sel_start >= 0 && e->find.replace_sel_end > e->find.replace_sel_start
        && e->find.replace_sel_end <= e->find.replace_len) {
        char tmp_before[FIND_BAR_MAX];
        char tmp_sel[FIND_BAR_MAX];
        int before_len = e->find.replace_sel_start;
        int sel_len    = e->find.replace_sel_end - e->find.replace_sel_start;
        memcpy(tmp_before, e->find.replace, before_len);
        tmp_before[before_len] = '\0';
        memcpy(tmp_sel, e->find.replace + before_len, sel_len);
        tmp_sel[sel_len] = '\0';
        int bw = 0, sw = 0, sh = 0;
        if (before_len > 0) TTF_GetStringSize(e->font, tmp_before, 0, &bw, &sh);
        if (sel_len   > 0) TTF_GetStringSize(e->font, tmp_sel,    0, &sw, &sh);
        int sel_x = field_x + 4 + bw;
        int sel_y = row2_y + 2;
        int sel_h = field_h - 4;
        set_color(e->renderer, 0x26, 0x4F, 0x78, 200);
        SDL_FRect sel_rect = {(float)sel_x, (float)sel_y, (float)sw, (float)sel_h};
        SDL_RenderFillRect(e->renderer, &sel_rect);
    }

    char txt2[512];
    snprintf(txt2,sizeof(txt2),"%s%s",
             e->find.replace,
             (focused2 && e->find.replace_sel_start < 0 && (SDL_GetTicks()/500)%2) ? "|" : "");
    draw_text(e, txt2, field_x+4, row2_y+(field_h-FONT_SIZE)/2, 220,220,220);

    /* -- Botón "Reemplazar" -- */
    set_color(e->renderer, 0x2C,0x5F,0x8C,255);
    SDL_FRect btn = {(float)btn_x,(float)row2_y,(float)btn_w,(float)field_h};
    SDL_RenderFillRect(e->renderer, &btn);
    set_color(e->renderer, 0x52,0x8B,0xD4,255);
    SDL_FRect btnb[4] = {
        {(float)btn_x,(float)row2_y,(float)btn_w,1},
        {(float)btn_x,(float)(row2_y+field_h-1),(float)btn_w,1},
        {(float)btn_x,(float)row2_y,1,(float)field_h},
        {(float)(btn_x+btn_w-1),(float)row2_y,1,(float)field_h}
    };
    for (int i=0;i<4;i++) SDL_RenderFillRect(e->renderer, &btnb[i]);
    /* texto del botón centrado */
    {
        int tw=0, th=0;
        TTF_GetStringSize(e->font, "Reemplazar", 0, &tw, &th);
        int tx = btn_x + (btn_w - tw) / 2;
        int ty = row2_y + (field_h - FONT_SIZE) / 2;
        draw_text(e, "Reemplazar", tx, ty, 210,230,255);
    }

#undef DRAW_FIELD

    /* guardar geometría para click detection */
    e->find.replace_btn_x = btn_x;
    e->find.replace_btn_y = row2_y;
    e->find.replace_btn_w = btn_w;
    e->find.replace_btn_h = field_h;
    e->find.bar_x   = x;
    e->find.bar_y   = y;
    e->find.bar_w   = w;
    e->find.bar_h   = h;
    e->find.field_x = field_x;
    e->find.row1_y  = row1_y;
    e->find.row2_y  = row2_y;
    e->find.field_h = field_h;
}

/* -- Atajos visuales como badges de teclado ------------------------ */
static void render_shortcut_badge(Editor *e, const char *key, const char *label,
                                  int *x, int y)
{
    SDL_Renderer *r = e->renderer;

    /* medir texto de la tecla */
    int kw = 0, kh = 0;
    TTF_GetStringSize(e->font, key, 0, &kw, &kh);

    int pad_x = 5, pad_y = 2;
    int badge_w = kw + pad_x * 2;
    int badge_h = FONT_SIZE + pad_y * 2;

    /* fondo del badge */
    set_color(r, 0x3A, 0x3F, 0x4C, 0xFF);
    SDL_FRect bg = {(float)*x, (float)y, (float)badge_w, (float)badge_h};
    SDL_RenderFillRect(r, &bg);

    /* borde del badge */
    set_color(r, 0x52, 0x5A, 0x6E, 0xFF);
    SDL_FRect borders[4] = {
        {(float)*x, (float)y, (float)badge_w, 1},
        {(float)*x, (float)(y + badge_h - 1), (float)badge_w, 1},
        {(float)*x, (float)y, 1, (float)badge_h},
        {(float)(*x + badge_w - 1), (float)y, 1, (float)badge_h}
    };
    for (int i = 0; i < 4; i++) SDL_RenderFillRect(r, &borders[i]);

    /* sombra inferior del badge (efecto 3D) */
    set_color(r, 0x1A, 0x1D, 0x23, 0xFF);
    SDL_FRect shadow = {(float)*x, (float)(y + badge_h), (float)badge_w, 1};
    SDL_RenderFillRect(r, &shadow);

    /* texto de la tecla */
    draw_text(e, key, *x + pad_x, y + pad_y, 0xD0, 0xD8, 0xEA);

    *x += badge_w + 3;

    /* etiqueta descriptiva */
    if (label && label[0]) {
        int lw = draw_text(e, label, *x, y + pad_y, 0x5C, 0x62, 0x72);
        *x += lw + 14;
    }
}

static void render_shortcuts(Editor *e)
{
    /* offset dinámico según panel lateral */
    int left_offset = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;

    /* Línea separadora encima de los atajos */
    set_color(e->renderer, 0x35, 0x3A, 0x45, 0xFF);
    int sep_y = e->win_h - STATUS_HEIGHT - SHORTCUT_HEIGHT;
    SDL_FRect sep_line = {0, (float)sep_y, (float)e->win_w, 1};
    SDL_RenderFillRect(e->renderer, &sep_line);

    /* Fondo de la banda de atajos — solo en el área del editor (no sobre el panel) */
    set_color(e->renderer, 0x1E, 0x21, 0x28, 0xFF);
    SDL_FRect band = {(float)left_offset, (float)(sep_y + 1),
                      (float)(e->win_w - left_offset), (float)(SHORTCUT_HEIGHT - 1)};
    SDL_RenderFillRect(e->renderer, &band);

    /* Renderizar cada atajo como badge + etiqueta, partiendo desde left_offset */
    int badge_y = sep_y + (SHORTCUT_HEIGHT - FONT_SIZE) / 2 - 2;
    int x = left_offset + 10;

    struct { const char *key; const char *label; } shortcuts[] = {
        {"Ctrl+F",  "Buscar"},
        {"Ctrl+B",  "Panel"},
        {"Ctrl+D",  "Duplicar"},
        {"Ctrl+L",  "Sel. línea"},
        {"Ctrl+/",  "Comentar"},
        {"Ctrl+Z",  "Undo"},
        {"Ctrl+Y",  "Redo"},
        {"Ctrl+S",  "Guardar"},
        {"Ctrl+N",  "Nuevo"},
    };
    int n = (int)(sizeof(shortcuts) / sizeof(shortcuts[0]));

    for (int i = 0; i < n; i++) {
        render_shortcut_badge(e, shortcuts[i].key, shortcuts[i].label, &x, badge_y);
        if (x > e->win_w - 40) break;
    }
}

/* -- Scrollbar vertical --------------------------------------------- */
static void render_scrollbar(Editor *e, int left_offset)
{
    SDL_Renderer *r = e->renderer;
    int total_lines   = buf_line_count(e->buf);
    int text_height   = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT - SHORTCUT_HEIGHT;
    int visible_lines = text_height / LINE_HEIGHT;

    /* solo mostrar si hay contenido que se sale */
    if (total_lines <= visible_lines) return;

    int sb_x = e->win_w - SCROLLBAR_W - 1;
    int sb_y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    int sb_h = text_height;

    /* fondo de la pista */
    set_color(r, 0x1E, 0x21, 0x28, 0xFF);
    SDL_FRect track = {(float)sb_x, (float)sb_y, (float)SCROLLBAR_W, (float)sb_h};
    SDL_RenderFillRect(r, &track);

    /* tamaño y posición del thumb */
    float ratio       = (float)visible_lines / (float)total_lines;
    int thumb_h       = (int)(sb_h * ratio);
    if (thumb_h < 20) thumb_h = 20;

    int max_scroll    = total_lines - visible_lines;
    float scroll_frac = (max_scroll > 0) ? (float)e->scroll_line / (float)max_scroll : 0.0f;
    int thumb_y       = sb_y + (int)(scroll_frac * (sb_h - thumb_h));

    /* thumb */
    set_color(r, 0x42, 0x48, 0x5A, 0xFF);
    SDL_FRect thumb = {(float)(sb_x + 1), (float)thumb_y,
                       (float)(SCROLLBAR_W - 2), (float)thumb_h};
    SDL_RenderFillRect(r, &thumb);

    /* borde izquierdo de la pista */
    set_color(r, 0x35, 0x3A, 0x45, 0xFF);
    SDL_FRect border = {(float)sb_x, (float)sb_y, 1, (float)sb_h};
    SDL_RenderFillRect(r, &border);

    (void)left_offset;
}

/* -- Resaltado de selección ----------------------------------------- */
static void render_selection(Editor *e, int left_offset, int text_top, int visible_lines)
{
    if (!e->sel_active) return;

    size_t from, to;
    if (!editor_sel_range(e, &from, &to)) return;

    SDL_Renderer *r = e->renderer;
    int text_x = left_offset + GUTTER_WIDTH + PADDING_LEFT;
    int total_lines = buf_line_count(e->buf);

    /* O(log n): usar buf_line_col con búsqueda binaria */
    int from_line, from_col, to_line, to_col;
    buf_line_col(e->buf, from, &from_line, &from_col);
    buf_line_col(e->buf, to,   &to_line,   &to_col);

    set_color(r, COL_SEL_BG);

    /* Si to_col == 0 y to_line > from_line, el cursor está al inicio de
     * to_line: visualmente la selección cubre hasta el \n de (to_line-1),
     * así que pintamos hasta to_line-1 completa y no tocamos to_line. */
    int paint_to_line = to_line;
    int paint_to_col  = to_col;
    if (to_col == 0 && to_line > from_line) {
        paint_to_line = to_line - 1;
        /* col_end para esa línea = longitud + 1 (incluye \n visual) */
        size_t ls = buf_line_offset(e->buf, paint_to_line);
        size_t le = buf_line_end(e->buf, ls);
        paint_to_col = (int)(le - ls) + 1;
    }

    for (int li = from_line; li <= paint_to_line && li < total_lines; li++) {
        int vi = li - e->scroll_line;
        if (vi < 0 || vi >= visible_lines) continue;

        int y = text_top + vi * LINE_HEIGHT;

        int col_start = (li == from_line) ? from_col : 0;
        int col_end;
        if (li == paint_to_line) {
            col_end = paint_to_col;
        } else {
            /* toda la línea hasta el final + 1 para incluir el \n visualmente */
            size_t ls = buf_line_offset(e->buf, li);
            size_t le = buf_line_end(e->buf, ls);
            col_end = (int)(le - ls) + 1;
        }

        int x_start = text_x + (col_start - e->scroll_col) * e->char_w;
        int x_end   = text_x + (col_end   - e->scroll_col) * e->char_w;
        if (x_start < text_x) x_start = text_x;
        if (x_end   < x_start) x_end = x_start + e->char_w;

        SDL_FRect sel_rect = {(float)x_start, (float)y,
                              (float)(x_end - x_start), (float)LINE_HEIGHT};
        SDL_RenderFillRect(r, &sel_rect);
    }
}

/* -- render_frame ---------------------------------------------------------- */
void render_frame(Editor *e) {
    SDL_Renderer *r = e->renderer;

    int left_offset;
    if (e->ftree.open)
        left_offset = e->ftree.width;
    else
        left_offset = FTREE_TOGGLE_BTN_W;

    int text_top      = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    int text_height   = e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT - STATUS_HEIGHT - SHORTCUT_HEIGHT;
    int visible_lines = text_height / LINE_HEIGHT;
    int total_lines   = (e->tab_count > 0) ? buf_line_count(e->buf) : 0;

    /* fondo */
    set_color(r, COL_BG);
    SDL_RenderClear(r);

    /* -- Pantalla vacía cuando no hay ningún archivo abierto -- */
    if (e->tab_count == 0) {
        render_navbar(e);
        render_tabbar(e);
        if (e->ftree.open) render_filetree(e);
        else               render_filetree_toggle_closed(e);
        render_menu(e);

        /* mensaje centrado — adaptado al ancho disponible */
        const char *line1 = "No hay ningún archivo abierto";
        const char *hint1 = "Ctrl+O  Abrir archivo";
        const char *hint2 = "Ctrl+N  Nuevo archivo";
        const char *hint3 = "Ctrl+K  Abrir carpeta";
        int w1=0, wh=0, h=0;
        TTF_GetStringSize(e->font, line1, 0, &w1, &h);
        TTF_GetStringSize(e->font, hint1, 0, &wh, &h);
        int left_off  = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
        int area_left = left_off;
        int area_w    = e->win_w - area_left;
        int area_top  = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
        int area_h    = e->win_h - area_top - STATUS_HEIGHT;
        int cx        = area_left + area_w / 2;
        int mid_y     = area_top + area_h / 2;
        /* título */
        draw_text(e, line1, cx - w1/2, mid_y - LINE_HEIGHT * 2, 0x6B, 0x72, 0x88);
        /* atajos en tres líneas separadas */
        draw_text(e, hint1, cx - wh/2, mid_y,                  0x45, 0x4C, 0x5E);
        draw_text(e, hint2, cx - wh/2, mid_y + LINE_HEIGHT,     0x45, 0x4C, 0x5E);
        draw_text(e, hint3, cx - wh/2, mid_y + LINE_HEIGHT * 2, 0x45, 0x4C, 0x5E);

        /* barra de estado mínima */
        {
            int sy = e->win_h - STATUS_HEIGHT;
            set_color(r, COL_STATUS_BG);
            SDL_FRect sb = {0, (float)sy, (float)e->win_w, (float)STATUS_HEIGHT};
            SDL_RenderFillRect(r, &sb);
            set_color(r, 0x35, 0x3A, 0x45, 0xFF);
            SDL_FRect sep_s = {0, (float)sy, (float)e->win_w, 1};
            SDL_RenderFillRect(r, &sep_s);
            draw_text(e, "  CoffeeCode", 0, sy + (STATUS_HEIGHT - FONT_SIZE) / 2,
                      0x98, 0xC3, 0x79);
        }

        SDL_RenderPresent(r);
        return;
    }

    /* actualizar lexer */
    {
        int in_block = 0;
        for (int li = 0; li < lexer_cache_count(e->lex); li++) {
            if (*lexer_cache_dirty_at(e->lex, li)) {
                char line_buf[4096];
                get_line_text(e, li, line_buf, sizeof(line_buf));
                in_block = lexer_tokenize_line(line_buf,
                               (int)strlen(line_buf),
                               lexer_cache_line(e->lex, li), in_block);
                *lexer_cache_dirty_at(e->lex, li) = 0;
            }
        }
    }

    /* -- Resaltado de línea activa (solo cuando no hay selección) -- */
    if (!e->sel_active) {
        int vi_cursor = e->cursor_line - e->scroll_line;
        if (vi_cursor >= 0 && vi_cursor < visible_lines) {
            int y = text_top + vi_cursor * LINE_HEIGHT;
            set_color(r, COL_CURSOR_LINE);
            SDL_FRect hl = {0, (float)y, (float)e->win_w, (float)LINE_HEIGHT};
            SDL_RenderFillRect(r, &hl);
        }
    }

    /* -- Resaltado de selección (encima del fondo, debajo del texto) -- */
    render_selection(e, left_offset, text_top, visible_lines);

    /* -- líneas de texto -- */
    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi;
        if (li >= total_lines) break;

        int y      = text_top + vi * LINE_HEIGHT;
        int text_x = left_offset + GUTTER_WIDTH + PADDING_LEFT;



        char line_buf[4096];
        int  line_len = get_line_text(e, li, line_buf, sizeof(line_buf));

        if (li < lexer_cache_count(e->lex) && lexer_cache_line(e->lex, li)->count > 0) {
            LineTokens *lt = lexer_cache_line(e->lex, li);
            int drawn_to = 0;

            for (int ti = 0; ti < lt->count; ti++) {
                Token *tok = &lt->tokens[ti];
                int col_end = tok->col + tok->len - e->scroll_col;
                if (col_end <= 0) { drawn_to = tok->col + tok->len; continue; }

                if (drawn_to < tok->col) {
                    int gap_start = drawn_to - e->scroll_col;
                    if (gap_start < 0) gap_start = 0;
                    int gap_len = tok->col - e->scroll_col - gap_start;
                    if (gap_len > 0 && gap_start + e->scroll_col < line_len) {
                        char tmp[256];
                        int cp = gap_len < 255 ? gap_len : 255;
                        memcpy(tmp, line_buf + gap_start + e->scroll_col, (size_t)cp);
                        tmp[cp] = '\0';
                        Color dc = TOKEN_COLORS[TOK_DEFAULT];
                        draw_text(e, tmp,
                                  text_x + gap_start * e->char_w,
                                  y + (LINE_HEIGHT - FONT_SIZE) / 2,
                                  dc.r, dc.g, dc.b);
                    }
                }

                int draw_col    = (tok->col > e->scroll_col) ? tok->col - e->scroll_col : 0;
                int actual_start = tok->col < e->scroll_col ? e->scroll_col : tok->col;
                int actual_len   = tok->col + tok->len - actual_start;
                if (actual_len <= 0) { drawn_to = tok->col + tok->len; continue; }
                if (actual_start + actual_len > line_len)
                    actual_len = line_len - actual_start;
                if (actual_len <= 0) { drawn_to = tok->col + tok->len; continue; }

                char tmp[256];
                int cp = actual_len < 255 ? actual_len : 255;
                memcpy(tmp, line_buf + actual_start, (size_t)cp);
                tmp[cp] = '\0';

                Color tc = TOKEN_COLORS[tok->type];
                (void)col_end;
                draw_text(e, tmp,
                          text_x + draw_col * e->char_w,
                          y + (LINE_HEIGHT - FONT_SIZE) / 2,
                          tc.r, tc.g, tc.b);
                drawn_to = tok->col + tok->len;
            }

            if (drawn_to < line_len) {
                int start = drawn_to - e->scroll_col;
                if (start < 0) start = 0;
                if (start < line_len) {
                    char tmp[256];
                    int cp = (line_len - start) < 255 ? (line_len - start) : 255;
                    memcpy(tmp, line_buf + start, (size_t)cp);
                    tmp[cp] = '\0';
                    Color dc = TOKEN_COLORS[TOK_DEFAULT];
                    draw_text(e, tmp,
                              text_x + start * e->char_w,
                              y + (LINE_HEIGHT - FONT_SIZE) / 2,
                              dc.r, dc.g, dc.b);
                }
            }
        } else {
            Color dc = TOKEN_COLORS[TOK_DEFAULT];
            int start = e->scroll_col < line_len ? e->scroll_col : line_len;
            if (start < line_len) {
                draw_text(e, line_buf + start, text_x,
                          y + (LINE_HEIGHT - FONT_SIZE) / 2,
                          dc.r, dc.g, dc.b);
            }
        }
    }

    /* -- gutter -- */
    set_color(r, COL_GUTTER);
    SDL_FRect gutter = {(float)left_offset, (float)text_top,
                        (float)GUTTER_WIDTH, (float)text_height};
    SDL_RenderFillRect(r, &gutter);

    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi;
        if (li >= total_lines) break;
        char num[16];
        snprintf(num, sizeof(num), "%4d", li + 1);
        int y = text_top + vi * LINE_HEIGHT;
        draw_text(e, num, left_offset + 4, y + (LINE_HEIGHT - FONT_SIZE) / 2,
                  0x49, 0x50, 0x5E);
    }

    /* -- cursor -- */
    {
        int vis_line = e->cursor_line - e->scroll_line;
        int vis_col  = e->cursor_col  - e->scroll_col;
        if (vis_line >= 0 && vis_line < visible_lines && vis_col >= 0) {
            int cx = left_offset + GUTTER_WIDTH + PADDING_LEFT + vis_col * e->char_w;
            int cy = text_top + vis_line * LINE_HEIGHT;
            set_color(r, COL_CURSOR);
            SDL_FRect cur = {(float)cx, (float)cy, 2.0f, (float)LINE_HEIGHT};
            SDL_RenderFillRect(r, &cur);
        }
    }

    /* atajos visuales desactivados */

    /* -- barra de estado -- */
    {
        int sy = e->win_h - STATUS_HEIGHT;
        set_color(r, COL_STATUS_BG);
        SDL_FRect sb = {0, (float)sy, (float)e->win_w, (float)STATUS_HEIGHT};
        SDL_RenderFillRect(r, &sb);

        /* línea separadora superior de status */
        set_color(r, 0x35, 0x3A, 0x45, 0xFF);
        SDL_FRect sep_status = {0, (float)sy, (float)e->win_w, 1};
        SDL_RenderFillRect(r, &sep_status);

        char status[128];
        snprintf(status, sizeof(status), " CoffeeCode | Ln %d, Col %d%s |",
                 e->cursor_line + 1, e->cursor_col + 1,
                 e->modified ? "  *" : "");
        draw_text(e, status, 0, sy + (STATUS_HEIGHT - FONT_SIZE) / 2,
                  0x98, 0xC3, 0x79);
    }

    /* -- scrollbar vertical -- */
    render_scrollbar(e, left_offset);

    /* -- panel lateral -- */
    if (e->ftree.open)
        render_filetree(e);
    else
        render_filetree_toggle_closed(e);

    /* -- navbar -- */
    render_navbar(e);

    /* -- tab bar -- */
    render_tabbar(e);

    /* -- find bar -- */
    render_find_bar(e);

    /* -- menú -- */
    render_menu(e);

    SDL_RenderPresent(r);
}
