#include "render_internal.h"
#include <stdio.h>
#include <string.h>

/* Menú "Archivo" */
#define MENU_ITEM_H 26
#define MENU_WIDTH 210
#define MENU_ITEMS 6

static const char *MENU_LABELS[MENU_ITEMS] = {"Nuevo", "Abrir archivo...", "Abrir carpeta...",
                                              NULL,    "Guardar",          "Autoguardado"};
static const char *MENU_HINTS[MENU_ITEMS] = {"Ctrl+N", "Ctrl+O", "Ctrl+K", NULL, "Ctrl+S", NULL};

/* -- Navbar --------------------------------------------------------------- */
void render_navbar(Editor *e) {
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
        while (fname > e->filepath && *(fname - 1) != '/' && *(fname - 1) != '\\')
            fname--;
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
        SDL_FRect dot = {(float)(cx + tw + 6), (float)(ty + FONT_SIZE / 2 - 3), 6, 6};
        SDL_RenderFillRect(r, &dot);
    }
}

/* -- Menú desplegable ------------------------------------------------------ */
void render_menu(Editor *e) {
    if (!e->menu_open) return;
    SDL_Renderer *r = e->renderer;

    int mx = 4, my = NAVBAR_HEIGHT;
    int total_h = 0;
    for (int i = 0; i < MENU_ITEMS; i++)
        total_h += MENU_LABELS[i] ? MENU_ITEM_H : 8;

    set_color(r, 0x00, 0x00, 0x00, 0x60);
    SDL_FRect shadow = {(float)(mx + 3), (float)(my + 3), (float)MENU_WIDTH, (float)total_h};
    SDL_RenderFillRect(r, &shadow);

    set_color(r, COL_MENU_BG);
    SDL_FRect bg = {(float)mx, (float)my, (float)MENU_WIDTH, (float)total_h};
    SDL_RenderFillRect(r, &bg);

    set_color(r, COL_MENU_BORDER);
    SDL_FRect borders[4] = {{(float)mx, (float)my, (float)MENU_WIDTH, 1},
                            {(float)mx, (float)(my + total_h - 1), (float)MENU_WIDTH, 1},
                            {(float)mx, (float)my, 1, (float)total_h},
                            {(float)(mx + MENU_WIDTH - 1), (float)my, 1, (float)total_h}};
    for (int i = 0; i < 4; i++)
        SDL_RenderFillRect(r, &borders[i]);

    int iy = my;
    for (int i = 0; i < MENU_ITEMS; i++) {
        if (!MENU_LABELS[i]) {
            set_color(r, COL_MENU_SEP);
            SDL_FRect s = {(float)(mx + 8), (float)(iy + 4), (float)(MENU_WIDTH - 16), 1};
            SDL_RenderFillRect(r, &s);
            iy += 8;
            continue;
        }

        if (e->menu_hovered == i) {
            set_color(r, COL_MENU_HOVER);
            SDL_FRect hi = {(float)(mx + 1), (float)iy, (float)(MENU_WIDTH - 2),
                            (float)MENU_ITEM_H};
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

/* -- Barra de búsqueda ----------------------------------------------------- */
/* -- Tab bar --------------------------------------------------------------- */
void render_tabbar(Editor *e) {
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
        if (tab_w < 80) tab_w = 80;
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
            SDL_FRect dot = {(float)(tx + pad + text_max_w + 2), (float)(ty2 + FONT_SIZE / 2 - 3),
                             5, 5};
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

/* -- Atajos visuales como badges de teclado ------------------------ */
static void render_shortcut_badge(Editor *e, const char *key, const char *label, int *x, int y) {
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
    SDL_FRect borders[4] = {{(float)*x, (float)y, (float)badge_w, 1},
                            {(float)*x, (float)(y + badge_h - 1), (float)badge_w, 1},
                            {(float)*x, (float)y, 1, (float)badge_h},
                            {(float)(*x + badge_w - 1), (float)y, 1, (float)badge_h}};
    for (int i = 0; i < 4; i++)
        SDL_RenderFillRect(r, &borders[i]);

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

void render_shortcuts(Editor *e) {
    /* offset dinámico según panel lateral */
    int left_offset = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;

    /* Línea separadora encima de los atajos */
    set_color(e->renderer, 0x35, 0x3A, 0x45, 0xFF);
    int sep_y = e->win_h - STATUS_HEIGHT - SHORTCUT_HEIGHT;
    SDL_FRect sep_line = {0, (float)sep_y, (float)e->win_w, 1};
    SDL_RenderFillRect(e->renderer, &sep_line);

    /* Fondo de la banda de atajos — solo en el área del editor (no sobre el panel) */
    set_color(e->renderer, 0x1E, 0x21, 0x28, 0xFF);
    SDL_FRect band = {(float)left_offset, (float)(sep_y + 1), (float)(e->win_w - left_offset),
                      (float)(SHORTCUT_HEIGHT - 1)};
    SDL_RenderFillRect(e->renderer, &band);

    /* Renderizar cada atajo como badge + etiqueta, partiendo desde left_offset */
    int badge_y = sep_y + (SHORTCUT_HEIGHT - FONT_SIZE) / 2 - 2;
    int x = left_offset + 10;

    struct {
        const char *key;
        const char *label;
    } shortcuts[] = {
        {"Ctrl+F", "Buscar"},     {"Ctrl+B", "Panel"},    {"Ctrl+D", "Duplicar"},
        {"Ctrl+L", "Sel. línea"}, {"Ctrl+/", "Comentar"}, {"Ctrl+Z", "Undo"},
        {"Ctrl+Y", "Redo"},       {"Ctrl+S", "Guardar"},  {"Ctrl+N", "Nuevo"},
    };
    int n = (int)(sizeof(shortcuts) / sizeof(shortcuts[0]));

    for (int i = 0; i < n; i++) {
        render_shortcut_badge(e, shortcuts[i].key, shortcuts[i].label, &x, badge_y);
        if (x > e->win_w - 40) break;
    }
}

/* -- Scrollbar vertical --------------------------------------------- */
void render_scrollbar(Editor *e, int left_offset) {
    SDL_Renderer *r = e->renderer;
    int total_lines = buf_line_count(e->buf);
    int text_height = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT - SHORTCUT_HEIGHT;
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
    float ratio = (float)visible_lines / (float)total_lines;
    int thumb_h = (int)(sb_h * ratio);
    if (thumb_h < 20) thumb_h = 20;

    int max_scroll = total_lines - visible_lines;
    float scroll_frac = (max_scroll > 0) ? (float)e->scroll_line / (float)max_scroll : 0.0f;
    int thumb_y = sb_y + (int)(scroll_frac * (sb_h - thumb_h));

    /* thumb */
    set_color(r, 0x42, 0x48, 0x5A, 0xFF);
    SDL_FRect thumb = {(float)(sb_x + 1), (float)thumb_y, (float)(SCROLLBAR_W - 2), (float)thumb_h};
    SDL_RenderFillRect(r, &thumb);

    /* borde izquierdo de la pista */
    set_color(r, 0x35, 0x3A, 0x45, 0xFF);
    SDL_FRect border = {(float)sb_x, (float)sb_y, 1, (float)sb_h};
    SDL_RenderFillRect(r, &border);

    (void)left_offset;
}
