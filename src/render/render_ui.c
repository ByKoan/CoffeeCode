/**
 * @file render_ui.c
 * @brief Cromo de la interfaz: navbar, menú "Archivo", barra de pestañas,
 *        banda de atajos y scrollbar vertical.
 */
#include "render_internal.h"
#include <stdio.h>
#include <string.h>

/* ── Menú "Archivo" ───────────────────────────────────────────────────────── */
#define MENU_ITEM_H   26
#define MENU_WIDTH    210
#define MENU_ITEMS    6
#define MENU_SEP_H    8  /* alto de un separador del menú        */
#define MENU_SHADOW   3  /* desplazamiento de la sombra          */
#define MENU_AUTOSAVE_ITEM 5 /* índice del item "Autoguardado"   */

static const char *MENU_LABELS[MENU_ITEMS] = {"Nuevo", "Abrir archivo...", "Abrir carpeta...",
                                              NULL,    "Guardar",          "Autoguardado"};
static const char *MENU_HINTS[MENU_ITEMS] = {"Ctrl+N", "Ctrl+O", "Ctrl+K", NULL, "Ctrl+S", NULL};

/* ── Navbar ───────────────────────────────────────────────────────────────── */
#define NAV_BTN_X         4
#define NAV_BTN_Y         2
#define NAV_BTN_MIN_W     90
#define NAV_TITLE_GAP     8  /* separación mínima entre botón y título */
#define COL_NAVBAR_SEP    0x3A, 0x3F, 0x4A, 0xFF
#define COL_MODIFIED_DOT  0xE0, 0x6C, 0x75, 0xFF
#define TXT_NAVBAR_BTN    0xCC, 0xCC, 0xCC
#define TXT_NAVBAR_TITLE  0x80, 0x85, 0x95
#define COL_MENU_SHADOW   0x00, 0x00, 0x00, 0x60
#define TXT_MENU_CHECK    0x98, 0xC3, 0x79
#define TXT_MENU_ITEM     0xCC, 0xCC, 0xCC
#define TXT_MENU_HINT     0x60, 0x65, 0x70

/* ── Barra de pestañas ────────────────────────────────────────────────────── */
#define TAB_CLOSE_W       16
#define TAB_PAD           10
#define TAB_TEXT_EXTRA    4   /* holgura extra del ancho de pestaña    */
#define TAB_MIN_W         80
#define TAB_MAX_W         200
#define TAB_ACCENT_H      2   /* línea de acento del tab activo        */
#define TAB_MOD_DOT_SZ    5   /* punto de "modificado"                 */
#define TAB_CLOSE_GLYPH_H 14
#define TAB_NEW_BTN_W     28
#define COL_TABBAR_BG     0x16, 0x19, 0x1F, 255
#define COL_TABBAR_SEP    0x2A, 0x2E, 0x38, 255
#define COL_TAB_ACTIVE    0x1E, 0x22, 0x2B, 255
#define COL_TAB_ACCENT    0x52, 0x8B, 0xD4, 255
#define COL_TAB_MOD_DOT   0xE0, 0x90, 0x40, 255
#define TXT_TAB_NEW       0x55, 0x5A, 0x6A

/* ── Banda de atajos (badges) ─────────────────────────────────────────────── */
#define BADGE_PAD_X       5
#define BADGE_PAD_Y       2
#define BADGE_GAP         3   /* tras el badge, antes de su etiqueta   */
#define BADGE_LABEL_GAP   14  /* tras la etiqueta, antes del siguiente */
#define SHORTCUT_LEFT_PAD 10
#define SHORTCUT_END_PAD  40  /* margen donde se deja de pintar atajos */
#define COL_BADGE_BG      0x3A, 0x3F, 0x4C, 0xFF
#define COL_BADGE_BORDER  0x52, 0x5A, 0x6E, 0xFF
#define COL_BADGE_SHADOW  0x1A, 0x1D, 0x23, 0xFF
#define COL_SHORTCUT_SEP  0x35, 0x3A, 0x45, 0xFF
#define COL_SHORTCUT_BG   0x1E, 0x21, 0x28, 0xFF
#define TXT_BADGE_KEY     0xD0, 0xD8, 0xEA
#define TXT_BADGE_LABEL   0x5C, 0x62, 0x72

/* ── Scrollbar ────────────────────────────────────────────────────────────── */
#define SB_MIN_THUMB_H    20
#define COL_SB_TRACK      0x1E, 0x21, 0x28, 0xFF
#define COL_SB_THUMB      0x42, 0x48, 0x5A, 0xFF
#define COL_SB_BORDER     0x35, 0x3A, 0x45, 0xFF

/** Último componente (nombre de archivo/carpeta) de una ruta. */
static const char *last_path_component(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    return base;
}

void render_navbar(Editor *e) {
    SDL_Renderer *r = e->renderer;

    set_color(r, COL_NAVBAR_BG);
    fill_rect(r, 0, 0, e->win_w, NAVBAR_HEIGHT);
    set_color(r, COL_NAVBAR_SEP);
    fill_rect(r, 0, NAVBAR_HEIGHT - 1, e->win_w, 1);

    int btn_h = NAVBAR_HEIGHT - 4;

    /* Ancho del botón "Archivo": se calcula una vez y se cachea */
    static int cached_btn_w = 0;
    if (cached_btn_w == 0) {
        int label_w = 0, label_h = 0;
        TTF_GetStringSize(e->font, "  Archivo  ", 0, &label_w, &label_h);
        cached_btn_w = (label_w > 20) ? label_w : NAV_BTN_MIN_W;
    }
    int btn_w = cached_btn_w;

    if (e->menu_open) {
        set_color(r, COL_NAVBAR_BTN);
        fill_rect(r, NAV_BTN_X, NAV_BTN_Y, btn_w, btn_h);
    }

    int text_y = NAV_BTN_Y + (btn_h - FONT_SIZE) / 2;
    draw_text(e, "  Archivo  ", NAV_BTN_X, text_y, TXT_NAVBAR_BTN);

    /* Título: "CoffeeCode" + nombre del archivo abierto si lo hay */
    char title[600];
    if (e->filepath[0])
        snprintf(title, sizeof(title), "CoffeeCode \xe2\x80\x94 %s",
                 last_path_component(e->filepath));
    else
        snprintf(title, sizeof(title), "CoffeeCode");

    int title_w = 0, title_h = 0;
    TTF_GetStringSize(e->font, title, 0, &title_w, &title_h);
    int title_x = (e->win_w - title_w) / 2;
    if (title_x < NAV_BTN_X + btn_w + NAV_TITLE_GAP) title_x = NAV_BTN_X + btn_w + NAV_TITLE_GAP;
    draw_text(e, title, title_x, text_y, TXT_NAVBAR_TITLE);

    if (e->modified) {
        set_color(r, COL_MODIFIED_DOT);
        fill_rect(r, title_x + title_w + 6, text_y + FONT_SIZE / 2 - 3, 6, 6);
    }
}

void render_menu(Editor *e) {
    if (!e->menu_open) return;
    SDL_Renderer *r = e->renderer;

    int menu_x = NAV_BTN_X, menu_y = NAVBAR_HEIGHT;
    int menu_h = 0;
    for (int i = 0; i < MENU_ITEMS; i++)
        menu_h += MENU_LABELS[i] ? MENU_ITEM_H : MENU_SEP_H;

    set_color(r, COL_MENU_SHADOW);
    fill_rect(r, menu_x + MENU_SHADOW, menu_y + MENU_SHADOW, MENU_WIDTH, menu_h);
    set_color(r, COL_MENU_BG);
    fill_rect(r, menu_x, menu_y, MENU_WIDTH, menu_h);
    set_color(r, COL_MENU_BORDER);
    stroke_rect(r, menu_x, menu_y, MENU_WIDTH, menu_h);

    int item_y = menu_y;
    for (int i = 0; i < MENU_ITEMS; i++) {
        if (!MENU_LABELS[i]) { /* separador */
            set_color(r, COL_MENU_SEP);
            fill_rect(r, menu_x + 8, item_y + 4, MENU_WIDTH - 16, 1);
            item_y += MENU_SEP_H;
            continue;
        }

        if (e->menu_hovered == i) {
            set_color(r, COL_MENU_HOVER);
            fill_rect(r, menu_x + 1, item_y, MENU_WIDTH - 2, MENU_ITEM_H);
        }

        int text_y = item_y + (MENU_ITEM_H - FONT_SIZE) / 2;

        /* Item "Autoguardado": checkmark si está activo */
        if (i == MENU_AUTOSAVE_ITEM && e->autosave)
            draw_text(e, "\xe2\x9c\x93", menu_x + 4, text_y, TXT_MENU_CHECK);

        draw_text(e, MENU_LABELS[i], menu_x + 14, text_y, TXT_MENU_ITEM);

        if (MENU_HINTS[i]) {
            int hint_w = 0, hint_h = 0, label_w = 0;
            TTF_GetStringSize(e->font, MENU_HINTS[i], 0, &hint_w, &hint_h);
            TTF_GetStringSize(e->font, MENU_LABELS[i], 0, &label_w, &hint_h);
            int hint_x = menu_x + MENU_WIDTH - hint_w - 10;
            int label_right = menu_x + 14 + label_w + 8;
            if (hint_x > label_right) /* solo si no se solapa con el label */
                draw_text(e, MENU_HINTS[i], hint_x, text_y, TXT_MENU_HINT);
        }
        item_y += MENU_ITEM_H;
    }
}

/** Dibuja la pestaña @p index en x=@p tx y guarda su geometría de clic. */
static int draw_tab(Editor *e, int index, int tx, int bar_y, int bar_h) {
    SDL_Renderer *r = e->renderer;
    EditorTab *t = &e->tabs[index];
    int active = (index == e->active_tab);

    const char *name = last_path_component(t->filepath[0] ? t->filepath : "Sin título");

    int name_w = 0, name_h = 0;
    TTF_GetStringSize(e->font, name, 0, &name_w, &name_h);
    int tab_w = name_w + TAB_CLOSE_W + TAB_PAD * 2 + TAB_TEXT_EXTRA;
    if (tab_w < TAB_MIN_W) tab_w = TAB_MIN_W;
    if (tab_w > TAB_MAX_W) tab_w = TAB_MAX_W;

    t->tab_x = tx;
    t->tab_w = tab_w;

    if (active)
        set_color(r, COL_TAB_ACTIVE);
    else
        set_color(r, COL_TABBAR_BG);
    fill_rect(r, tx, bar_y, tab_w, bar_h);

    set_color(r, COL_TABBAR_SEP); /* borde derecho */
    fill_rect(r, tx + tab_w - 1, bar_y, 1, bar_h);

    if (active) { /* línea de acento superior */
        set_color(r, COL_TAB_ACCENT);
        fill_rect(r, tx, bar_y, tab_w, TAB_ACCENT_H);
    }

    /* Nombre, recortado con clip para que no rebose */
    int text_max_w = tab_w - TAB_CLOSE_W - TAB_PAD * 2 - TAB_TEXT_EXTRA;
    int text_y = bar_y + (bar_h - FONT_SIZE) / 2;
    SDL_Rect clip = {tx + TAB_PAD, bar_y, text_max_w, bar_h};
    SDL_SetRenderClipRect(r, &clip);
    if (active)
        draw_text(e, name, tx + TAB_PAD, text_y, 0xCC, 0xCC, 0xDD);
    else
        draw_text(e, name, tx + TAB_PAD, text_y, 0x66, 0x6A, 0x75);
    SDL_SetRenderClipRect(r, NULL);

    if (t->modified) {
        set_color(r, COL_TAB_MOD_DOT);
        fill_rect(r, tx + TAB_PAD + text_max_w + 2, text_y + FONT_SIZE / 2 - 3, TAB_MOD_DOT_SZ,
                  TAB_MOD_DOT_SZ);
    }

    /* Botón × de cerrar */
    int close_x = tx + tab_w - TAB_CLOSE_W - 2;
    int close_y = bar_y + (bar_h - TAB_CLOSE_GLYPH_H) / 2;
    t->close_x = close_x;
    t->close_y = close_y;
    uint8_t close_shade = active ? 0x88 : 0x44;
    draw_text(e, "×", close_x, close_y, close_shade, close_shade, close_shade);

    return tab_w;
}

void render_tabbar(Editor *e) {
    SDL_Renderer *r = e->renderer;
    int bar_y = NAVBAR_HEIGHT;
    int bar_h = TAB_BAR_HEIGHT;

    set_color(r, COL_TABBAR_BG);
    fill_rect(r, 0, bar_y, e->win_w, bar_h);
    set_color(r, COL_TABBAR_SEP);
    fill_rect(r, 0, bar_y + bar_h - 1, e->win_w, 1);

    int tx = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    for (int i = 0; i < e->tab_count; i++)
        tx += draw_tab(e, i, tx, bar_y, bar_h);

    /* Botón + (nueva pestaña) */
    e->tab_new_btn_x = tx;
    set_color(r, COL_TABBAR_BG);
    fill_rect(r, tx, bar_y, TAB_NEW_BTN_W, bar_h);
    draw_text(e, "+", tx + 7, bar_y + (bar_h - FONT_SIZE) / 2, TXT_TAB_NEW);
}

/** Dibuja un badge de tecla + su etiqueta; avanza *x al siguiente hueco. */
static void draw_shortcut_badge(Editor *e, const char *key, const char *label, int *x, int y) {
    SDL_Renderer *r = e->renderer;

    int key_w = 0, key_h = 0;
    TTF_GetStringSize(e->font, key, 0, &key_w, &key_h);
    int badge_w = key_w + BADGE_PAD_X * 2;
    int badge_h = FONT_SIZE + BADGE_PAD_Y * 2;

    set_color(r, COL_BADGE_BG);
    fill_rect(r, *x, y, badge_w, badge_h);
    set_color(r, COL_BADGE_BORDER);
    stroke_rect(r, *x, y, badge_w, badge_h);
    set_color(r, COL_BADGE_SHADOW); /* sombra inferior (efecto 3D) */
    fill_rect(r, *x, y + badge_h, badge_w, 1);

    draw_text(e, key, *x + BADGE_PAD_X, y + BADGE_PAD_Y, TXT_BADGE_KEY);
    *x += badge_w + BADGE_GAP;

    if (label && label[0]) {
        int label_w = draw_text(e, label, *x, y + BADGE_PAD_Y, TXT_BADGE_LABEL);
        *x += label_w + BADGE_LABEL_GAP;
    }
}

void render_shortcuts(Editor *e) {
    SDL_Renderer *r = e->renderer;
    int left_offset = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    int sep_y = e->win_h - STATUS_HEIGHT - SHORTCUT_HEIGHT;

    set_color(r, COL_SHORTCUT_SEP);
    fill_rect(r, 0, sep_y, e->win_w, 1);
    set_color(r, COL_SHORTCUT_BG); /* solo sobre el área del editor */
    fill_rect(r, left_offset, sep_y + 1, e->win_w - left_offset, SHORTCUT_HEIGHT - 1);

    static const struct {
        const char *key;
        const char *label;
    } SHORTCUTS[] = {
        {"Ctrl+F", "Buscar"},     {"Ctrl+B", "Panel"},    {"Ctrl+D", "Duplicar"},
        {"Ctrl+L", "Sel. línea"}, {"Ctrl+/", "Comentar"}, {"Ctrl+Z", "Undo"},
        {"Ctrl+Y", "Redo"},       {"Ctrl+S", "Guardar"},  {"Ctrl+N", "Nuevo"},
    };
    int badge_y = sep_y + (SHORTCUT_HEIGHT - FONT_SIZE) / 2 - 2;
    int x = left_offset + SHORTCUT_LEFT_PAD;
    for (int i = 0; i < (int)(sizeof(SHORTCUTS) / sizeof(SHORTCUTS[0])); i++) {
        draw_shortcut_badge(e, SHORTCUTS[i].key, SHORTCUTS[i].label, &x, badge_y);
        if (x > e->win_w - SHORTCUT_END_PAD) break;
    }
}

void render_scrollbar(Editor *e, int left_offset) {
    (void)left_offset;
    SDL_Renderer *r = e->renderer;
    int total_lines = buf_line_count(e->buf);
    int text_height = e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT - SHORTCUT_HEIGHT;
    int visible_lines = text_height / LINE_HEIGHT;

    if (total_lines <= visible_lines) return; /* todo cabe: sin scrollbar */

    int sb_x = e->win_w - SCROLLBAR_W - 1;
    int sb_y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    int sb_h = text_height;

    set_color(r, COL_SB_TRACK);
    fill_rect(r, sb_x, sb_y, SCROLLBAR_W, sb_h);

    int thumb_h = (int)(sb_h * ((float)visible_lines / (float)total_lines));
    if (thumb_h < SB_MIN_THUMB_H) thumb_h = SB_MIN_THUMB_H;
    int max_scroll = total_lines - visible_lines;
    float scroll_frac = (max_scroll > 0) ? (float)e->scroll_line / (float)max_scroll : 0.0f;
    int thumb_y = sb_y + (int)(scroll_frac * (sb_h - thumb_h));

    set_color(r, COL_SB_THUMB);
    fill_rect(r, sb_x + 1, thumb_y, SCROLLBAR_W - 2, thumb_h);
    set_color(r, COL_SB_BORDER);
    fill_rect(r, sb_x, sb_y, 1, sb_h);
}
