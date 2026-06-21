/**
 * @file render_ui.c
 * @brief Cromo de la interfaz: navbar, menú "Archivo", barra de pestañas,
 *        banda de atajos y scrollbar vertical.
 *
 * @note Todas las funciones de aquí dibujan con las utilidades compartidas de
 * render.c: ::set_color (fija el color RGBA actual del renderer), ::fill_rect
 * (rectángulo relleno), ::stroke_rect (contorno de 1 px) y ::draw_text
 * (rasteriza texto con SDL_ttf y devuelve su ancho en px). Para medir texto sin
 * dibujarlo se usa @c TTF_GetStringSize, imprescindible para centrar y para
 * dimensionar botones según su etiqueta. El patrón recurrente es: pintar fondo
 * → pintar bordes/acentos → escribir texto encima. Varias de estas funciones,
 * además de dibujar, registran la geometría de los controles en @c e->ui (ver
 * render/ui_hit.h) para que el módulo de input detecte los clics con ui_hit().
 */
#include "render_internal.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

/* ── Menú "Archivo" ─────────────────────────────────────────────────────────
 */
#define MENU_ITEM_H 26       /* alto de cada entrada del menú (px)   */
#define MENU_WIDTH 210       /* ancho del desplegable (px)           */
#define MENU_ITEMS 7         /* número de entradas (incluye separadores) */
#define MENU_SEP_H 8         /* alto de un separador del menú        */
#define MENU_SHADOW 3        /* desplazamiento de la sombra          */
#define MENU_AUTOSAVE_ITEM 5 /* índice del item "Autoguardado"   */

/* Etiquetas del menú; una entrada NULL es un separador (línea horizontal). */
static const char *MENU_LABELS[MENU_ITEMS] = {
    "Nuevo",   "Abrir archivo...", "Abrir carpeta...", NULL,
    "Guardar", "Autoguardado",     "Preferencias"};
/* Atajo mostrado a la derecha de cada entrada (NULL = sin atajo o separador).
 */
static const char *MENU_HINTS[MENU_ITEMS] = {"Ctrl+N", "Ctrl+O", "Ctrl+K", NULL,
                                             "Ctrl+S", NULL,     "Ctrl+,"};

/* ── Navbar (barra superior con el botón "Archivo" y el título) ─────────────
 */
#define NAV_BTN_X 4      /* X del botón "Archivo" (px)            */
#define NAV_BTN_Y 2      /* Y del botón "Archivo" (px)            */
#define NAV_BTN_MIN_W 90 /* ancho mínimo del botón "Archivo" (px) */
#define NAV_TITLE_GAP 8  /* separación mínima entre botón y título */

/* ── Barra de pestañas ──────────────────────────────────────────────────────
 */
#define TAB_CLOSE_W 16       /* ancho reservado para la "×" de cerrar */
#define TAB_PAD 10           /* padding horizontal interno (px)       */
#define TAB_TEXT_EXTRA 4     /* holgura extra del ancho de pestaña    */
#define TAB_MIN_W 80         /* ancho mínimo de una pestaña (px)      */
#define TAB_MAX_W 200        /* ancho máximo de una pestaña (px)      */
#define TAB_ACCENT_H 2       /* línea de acento del tab activo        */
#define TAB_MOD_DOT_SZ 5     /* punto de "modificado"                 */
#define TAB_CLOSE_GLYPH_H 14 /* alto del glifo "×" (para centrarlo)   */
#define TAB_NEW_BTN_W 28     /* ancho del botón "+" de nueva pestaña  */

/* ── Banda de atajos (badges) ───────────────────────────────────────────────
 */
#define BADGE_PAD_X 5
#define BADGE_PAD_Y 2
#define BADGE_GAP 3        /* tras el badge, antes de su etiqueta   */
#define BADGE_LABEL_GAP 14 /* tras la etiqueta, antes del siguiente */
#define SHORTCUT_LEFT_PAD 10
#define SHORTCUT_END_PAD 40 /* margen donde se deja de pintar atajos */

/* ── Scrollbar ──────────────────────────────────────────────────────────────
 */
#define SB_MIN_THUMB_H 20

/**
 * @brief Devuelve el último componente (nombre de archivo/carpeta) de una ruta.
 *
 * Recorre la cadena y se queda con lo que sigue al último separador, sea '/' o
 * '\\' (para que funcione en Linux y Windows). No copia: devuelve un puntero
 * dentro de
 * @p path. P. ej. "src/render/render_ui.c" -> "render_ui.c".
 *
 * @param path Ruta completa. @return Puntero al nombre base dentro de @p path.
 */
static const char *last_path_component(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1; /* tras cada separador, reiniciar base */
    return base;
}

/**
 * @brief Dibuja la barra de navegación superior: botón "Archivo" y título
 * central.
 *
 * Pinta el fondo de la navbar y su separador inferior, el botón "Archivo"
 * (resaltado si el menú está abierto), y el título centrado ("CoffeeCode —
 * archivo" si hay uno abierto). Si el archivo tiene cambios sin guardar, añade
 * un punto rojo a la derecha del título.
 *
 * @param e Editor.
 */
void render_navbar(Editor *e) {
    SDL_Renderer *r = e->renderer;

    set_color_c(r, e->theme.col_navbar_bg);
    fill_rect(r, 0, 0, e->win_w, NAVBAR_HEIGHT); /* fondo de la navbar */
    set_color_c(r, e->theme.col_navbar_sep);
    fill_rect(r, 0, NAVBAR_HEIGHT - 1, e->win_w,
              1); /* separador inferior de 1 px */

    int btn_h =
        NAVBAR_HEIGHT - 4; /* alto del botón con 2 px de margen arriba/abajo */

    /* Ancho del botón "Archivo": se mide una vez (TTF_GetStringSize) y se
     * cachea en una variable static para no remedir el texto en cada frame. */
    static int cached_btn_w = 0;
    if (cached_btn_w == 0) {
        int label_w = 0, label_h = 0;
        TTF_GetStringSize(e->font, "  Archivo  ", 0, &label_w, &label_h);
        cached_btn_w = (label_w > 20)
                           ? label_w
                           : NAV_BTN_MIN_W; /* fallback si midiera raro */
    }
    int btn_w = cached_btn_w;

    /* Botón "Archivo" como componente del tema (UI_STYLE_NAV): fondo invisible
     * en reposo y resaltado cuando el menú está abierto (UI_ACTIVE). El área de
     * clic registrada (UI_BTN_FILE) ocupa toda la altura de la navbar; la caja
     * visible que se dibuja es algo más baja. */
    ui_put(&e->ui, UI_BTN_FILE, (Rect){NAV_BTN_X, 0, btn_w, NAVBAR_HEIGHT});
    Rect file_box = {NAV_BTN_X, NAV_BTN_Y, btn_w, btn_h};
    ui_button(e, UI_ID_NONE, file_box, "  Archivo  ", &e->theme.style_nav,
              e->menu_open ? UI_ACTIVE : UI_NORMAL);

    /* Y para centrar verticalmente el título y el punto de "modificado". */
    int text_y = NAV_BTN_Y + (btn_h - e->font_size) / 2;

    /* Título: "CoffeeCode" y, si hay archivo, " — nombre" (\xe2\x80\x94 es el
     * guión largo "—" codificado en UTF-8). */
    char title[600];
    if (e->filepath[0])
        snprintf(title, sizeof(title), "CoffeeCode \xe2\x80\x94 %s",
                 last_path_component(e->filepath));
    else
        snprintf(title, sizeof(title), "CoffeeCode");

    int title_w = 0, title_h = 0;
    TTF_GetStringSize(e->font, title, 0, &title_w,
                      &title_h); /* medir para centrar */
    int title_x = (e->win_w - title_w) / 2;
    /* no dejar que el título pise el botón "Archivo": empujarlo a su derecha */
    if (title_x < NAV_BTN_X + btn_w + NAV_TITLE_GAP)
        title_x = NAV_BTN_X + btn_w + NAV_TITLE_GAP;
    draw_text_c(e, title, title_x, text_y, e->theme.txt_navbar_title);

    if (e->modified) { /* punto rojo de "hay cambios sin guardar" */
        set_color_c(r, e->theme.col_modified_dot);
        fill_rect(r, title_x + title_w + 6, text_y + e->font_size / 2 - 3, 6,
                  6);
    }

    /* Botones a la derecha de la navbar: "Extensiones" y "Panel" (abren/cierran
     * sus paneles).  Se colocan de derecha a izquierda. */
    {
        int ext_w = 0, ext_h = 0;
        TTF_GetStringSize(e->font, "  Extensiones  ", 0, &ext_w, &ext_h);
        if (ext_w < 40) ext_w = 120; /* fallback si midiera raro */
        Rect ext_box = {e->win_w - ext_w - NAV_BTN_X, NAV_BTN_Y, ext_w, btn_h};
        ui_button(e, UI_EXT_TOGGLE, ext_box, "  Extensiones  ",
                  &e->theme.style_nav,
                  e->ext_panel_open ? UI_ACTIVE : UI_NORMAL);

        /* Boton "Panel" (panel inferior Salida/Logs/Terminal). */
        int pw = 0, ph = 0;
        TTF_GetStringSize(e->font, "  Panel  ", 0, &pw, &ph);
        if (pw < 30) pw = 70;
        Rect panel_box = {ext_box.x - pw - 6, NAV_BTN_Y, pw, btn_h};
        ui_button(e, UI_BOTTOM_TOGGLE, panel_box, "  Panel  ",
                  &e->theme.style_nav,
                  e->bottom_panel_open ? UI_ACTIVE : UI_NORMAL);

        /* Botones de division del editor (split panes), a la izquierda de
         * "Panel".  Dan una via de division independiente del teclado (en
         * algunas distribuciones el atajo con '\' no es comodo de teclear). */
        int sw = 0, sph = 0;
        TTF_GetStringSize(e->font, " [|] ", 0, &sw, &sph);
        if (sw < 24) sw = 44;
        Rect splitv_box = {panel_box.x - sw - 6, NAV_BTN_Y, sw, btn_h};
        ui_button(e, UI_SPLIT_V, splitv_box, " [|] ", &e->theme.style_nav,
                  UI_NORMAL);
        Rect splith_box = {splitv_box.x - sw - 6, NAV_BTN_Y, sw, btn_h};
        ui_button(e, UI_SPLIT_H, splith_box, " [-] ", &e->theme.style_nav,
                  UI_NORMAL);
    }
}

/**
 * @brief Dibuja el menú desplegable "Archivo" (solo si está abierto).
 *
 * Calcula el alto total sumando la altura de cada entrada (o de separador si la
 * etiqueta es @c NULL), pinta una sombra desplazada (efecto flotante), el fondo
 * y el borde, y luego cada entrada: separador como línea fina, fondo de hover
 * bajo la entrada señalada por el ratón (@c menu_hovered), el texto y, a la
 * derecha, su atajo (solo si no se solapa con la etiqueta). El item
 * "Autoguardado" muestra un tic (✓) cuando está activo.
 *
 * @param e Editor (@c menu_open, @c menu_hovered, @c autosave).
 */
void render_menu(Editor *e) {
    if (!e->menu_open) return; /* menú cerrado: no dibujar nada */
    SDL_Renderer *r = e->renderer;

    int menu_x = NAV_BTN_X,
        menu_y = NAVBAR_HEIGHT; /* esquina del menú, bajo el botón */
    int menu_h = 0;
    /* alto total = suma de las alturas de entradas y separadores */
    for (int i = 0; i < MENU_ITEMS; i++)
        menu_h += MENU_LABELS[i] ? MENU_ITEM_H : MENU_SEP_H;

    set_color_c(
        r, e->theme.col_menu_shadow); /* sombra desplazada (semitransparente) */
    fill_rect(r, menu_x + MENU_SHADOW, menu_y + MENU_SHADOW, MENU_WIDTH,
              menu_h);
    set_color_c(r, e->theme.col_menu_bg);
    fill_rect(r, menu_x, menu_y, MENU_WIDTH, menu_h); /* fondo del menú */
    set_color_c(r, e->theme.col_menu_border);
    stroke_rect(r, menu_x, menu_y, MENU_WIDTH, menu_h); /* borde del menú */

    int item_y = menu_y; /* Y acumulada de la entrada en curso */
    for (int i = 0; i < MENU_ITEMS; i++) {
        if (!MENU_LABELS[i]) { /* separador (etiqueta NULL): línea fina centrada
                                */
            set_color_c(r, e->theme.col_menu_sep);
            fill_rect(r, menu_x + 8, item_y + 4, MENU_WIDTH - 16, 1);
            item_y += MENU_SEP_H;
            continue;
        }

        /* registrar el item (pulsable) para el hit-test por índice */
        ui_put_idx(&e->ui, UI_LIST_MENU_ITEM, i,
                   (Rect){menu_x, item_y, MENU_WIDTH, MENU_ITEM_H});

        if (e->menu_hovered == i) { /* fondo de resaltado bajo el ratón */
            set_color_c(r, e->theme.col_menu_hover);
            fill_rect(r, menu_x + 1, item_y, MENU_WIDTH - 2, MENU_ITEM_H);
        }

        int text_y =
            item_y + (MENU_ITEM_H - e->font_size) / 2; /* centrado vertical */

        /* Item "Autoguardado": tic (✓, \xe2\x9c\x93 en UTF-8) si está activo */
        if (i == MENU_AUTOSAVE_ITEM && e->autosave)
            draw_text_c(e, "\xe2\x9c\x93", menu_x + 4, text_y,
                        e->theme.txt_menu_check);

        draw_text_c(e, MENU_LABELS[i], menu_x + 14, text_y,
                    e->theme.txt_menu_item); /* etiqueta */

        if (MENU_HINTS[i]) { /* atajo alineado a la derecha del menú */
            int hint_w = 0, hint_h = 0, label_w = 0;
            TTF_GetStringSize(e->font, MENU_HINTS[i], 0, &hint_w,
                              &hint_h); /* ancho atajo */
            TTF_GetStringSize(e->font, MENU_LABELS[i], 0, &label_w,
                              &hint_h); /* ancho etiqueta */
            int hint_x =
                menu_x + MENU_WIDTH - hint_w - 10; /* pegado al borde derecho */
            int label_right =
                menu_x + 14 + label_w + 8; /* fin de la etiqueta */
            if (hint_x >
                label_right) /* solo dibujar el atajo si no pisa la etiqueta */
                draw_text_c(e, MENU_HINTS[i], hint_x, text_y,
                            e->theme.txt_menu_hint);
        }
        item_y += MENU_ITEM_H;
    }
}

/**
 * @brief Dibuja una pestaña y registra su geometría para la detección de clics.
 *
 * Calcula el ancho según el nombre (acotado a [@c TAB_MIN_W, @c TAB_MAX_W]),
 * pinta el fondo (más claro si es la activa), el borde derecho y, en la activa,
 * una línea de acento arriba. Escribe el nombre recortado con un rectángulo de
 * clip
 * (@c SDL_SetRenderClipRect limita el dibujo a esa zona: lo que sobresalga se
 * corta en vez de invadir el botón de cerrar), un punto si hay cambios y la "×"
 * de cerrar. Guarda en la pestaña @c tab_x/@c tab_w y @c close_x/@c close_y
 * para que el input sepa dónde están sus zonas pulsables.
 *
 * @param e Editor. @param index Índice de la pestaña. @param tx X de inicio
 * (px).
 * @param bar_y Y de la barra. @param bar_h Alto de la barra.
 * @return Ancho dibujado de la pestaña (para avanzar a la siguiente).
 */
static int draw_tab_active(Editor *e, int index, int tx, int bar_y, int bar_h,
                           int active) {
    SDL_Renderer *r = e->renderer;
    EditorTab *t = &e->tabs[index];

    /* nombre = último componente de la ruta, o "Sin título" si aún no se guardó
     */
    const char *name =
        last_path_component(t->filepath[0] ? t->filepath : "Sin título");

    int name_w = 0, name_h = 0;
    TTF_GetStringSize(e->font, name, 0, &name_w, &name_h); /* medir el nombre */
    /* ancho = nombre + botón cerrar + paddings, acotado a un rango */
    int tab_w = name_w + TAB_CLOSE_W + TAB_PAD * 2 + TAB_TEXT_EXTRA;
    if (tab_w < TAB_MIN_W) tab_w = TAB_MIN_W;
    if (tab_w > TAB_MAX_W) tab_w = TAB_MAX_W;

    /* registrar el rectángulo de la pestaña para el hit-test (por índice) */
    ui_put_idx(&e->ui, UI_LIST_TAB, index, (Rect){tx, bar_y, tab_w, bar_h});

    if (active)
        set_color_c(
            r, e->theme.col_tab_active); /* pestaña activa: fondo más claro */
    else
        set_color_c(r, e->theme.col_tabbar_bg);
    fill_rect(r, tx, bar_y, tab_w, bar_h);

    set_color_c(r, e->theme.col_tabbar_sep); /* borde derecho separador */
    fill_rect(r, tx + tab_w - 1, bar_y, 1, bar_h);

    if (active) { /* línea de acento superior en la activa */
        set_color_c(r, e->theme.col_tab_accent);
        fill_rect(r, tx, bar_y, tab_w, TAB_ACCENT_H);
    }

    /* Nombre: se acota el dibujo a un rectángulo de clip para que no rebose el
     * botón de cerrar; SDL recorta cualquier píxel fuera de "clip". */
    int text_max_w = tab_w - TAB_CLOSE_W - TAB_PAD * 2 - TAB_TEXT_EXTRA;
    int text_y = bar_y + (bar_h - e->font_size) / 2;
    SDL_Rect clip = {tx + TAB_PAD, bar_y, text_max_w, bar_h};
    SDL_SetRenderClipRect(r, &clip); /* activar recorte */
    if (active)
        draw_text(e, name, tx + TAB_PAD, text_y, 0xCC, 0xCC,
                  0xDD); /* texto claro */
    else
        draw_text(e, name, tx + TAB_PAD, text_y, 0x66, 0x6A,
                  0x75); /* texto tenue */
    SDL_SetRenderClipRect(
        r, NULL); /* desactivar recorte (volver a dibujar libre) */

    if (t->modified) { /* punto de "cambios sin guardar" */
        set_color_c(r, e->theme.col_tab_mod_dot);
        fill_rect(r, tx + TAB_PAD + text_max_w + 2,
                  text_y + e->font_size / 2 - 3, TAB_MOD_DOT_SZ,
                  TAB_MOD_DOT_SZ);
    }

    /* Botón × de cerrar (más visible en la activa); se guarda su posición */
    int close_x = tx + tab_w - TAB_CLOSE_W - 2;
    int close_y = bar_y + (bar_h - TAB_CLOSE_GLYPH_H) / 2;
    /* botón cerrar: zona pulsable cuadrada (TAB_CLOSE_W) registrada por índice
     */
    ui_put_idx(&e->ui, UI_LIST_TAB_CLOSE, index,
               (Rect){close_x, close_y, TAB_CLOSE_W, TAB_CLOSE_W});
    uint8_t close_shade =
        active ? 0x88 : 0x44; /* gris (un mismo valor en R=G=B) */
    draw_text(e, "×", close_x, close_y, close_shade, close_shade, close_shade);

    return tab_w;
}

/** Envoltura: pestaña con el resaltado de activa basado en e->active_tab (caso
 *  sin división del editor). */
static int draw_tab(Editor *e, int index, int tx, int bar_y, int bar_h) {
    return draw_tab_active(e, index, tx, bar_y, bar_h, index == e->active_tab);
}

/**
 * @brief Dibuja la barra de pestañas completa y el botón "+" de nueva pestaña.
 *
 * Pinta el fondo y separador inferior, recorre las pestañas con ::draw_tab
 * (avanzando la X por el ancho de cada una) y, al final, el botón "+". Guarda
 * en
 * @c tab_new_btn_x la X del "+" para que el input detecte el clic.
 *
 * @param e Editor.
 */
void render_tabbar(Editor *e) {
    SDL_Renderer *r = e->renderer;
    int bar_y = NAVBAR_HEIGHT;
    int bar_h = TAB_BAR_HEIGHT;

    set_color_c(r, e->theme.col_tabbar_bg);
    fill_rect(r, 0, bar_y, e->win_w, bar_h); /* fondo de la barra */
    set_color_c(r, e->theme.col_tabbar_sep);
    fill_rect(r, 0, bar_y + bar_h - 1, e->win_w, 1); /* separador inferior */

    /* empezar tras el panel lateral (o su botón si está cerrado) */
    int tx = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    for (int i = 0; i < e->tab_count; i++)
        tx += draw_tab(e, i, tx, bar_y, bar_h); /* cada pestaña avanza tx */

    /* Botón + (nueva pestaña), justo después de la última */
    set_color_c(r, e->theme.col_tabbar_bg);
    fill_rect(r, tx, bar_y, TAB_NEW_BTN_W, bar_h);
    draw_text_c(e, "+", tx + 7, bar_y + (bar_h - e->font_size) / 2,
                e->theme.txt_tab_new);
    /* registrar el botón "+" para el hit-test */
    ui_put(&e->ui, UI_TAB_NEW, (Rect){tx, bar_y, TAB_NEW_BTN_W, bar_h});
}

void render_tabbar_group(Editor *e, int group, int bar_y, int pane_left,
                         int pane_right) {
    SDL_Renderer *r = e->renderer;
    int bar_h = TAB_BAR_HEIGHT;

    /* fondo + separador inferior de la franja de este panel */
    set_color_c(r, e->theme.col_tabbar_bg);
    fill_rect(r, pane_left, bar_y, pane_right - pane_left, bar_h);
    set_color_c(r, e->theme.col_tabbar_sep);
    fill_rect(r, pane_left, bar_y + bar_h - 1, pane_right - pane_left, 1);

    /* recortar el dibujo de las pestañas a la franja del panel para que no
     * invadan el panel contiguo */
    SDL_Rect clip = {pane_left, bar_y, pane_right - pane_left, bar_h};
    SDL_SetRenderClipRect(r, &clip);

    int active_idx = e->group_active_tab[group];
    int tx = pane_left;
    for (int i = 0; i < e->tab_count; i++) {
        if (e->tabs[i].group != group) continue; /* solo las de este grupo */
        if (tx >= pane_right) break;              /* sin sitio para más */
        tx += draw_tab_active(e, i, tx, bar_y, bar_h, i == active_idx);
    }

    /* Botón "+" de nueva pestaña del grupo, si cabe. */
    if (tx + TAB_NEW_BTN_W <= pane_right) {
        set_color_c(r, e->theme.col_tabbar_bg);
        fill_rect(r, tx, bar_y, TAB_NEW_BTN_W, bar_h);
        draw_text_c(e, "+", tx + 7, bar_y + (bar_h - e->font_size) / 2,
                    e->theme.txt_tab_new);
        ui_put_idx(&e->ui, UI_LIST_SPLIT_NEW, group,
                   (Rect){tx, bar_y, TAB_NEW_BTN_W, bar_h});
    }

    SDL_SetRenderClipRect(r, NULL); /* desactivar recorte */
}

/**
 * @brief Dibuja un "badge" (tecla en recuadro) seguido de su etiqueta.
 *
 * Pinta el recuadro de la tecla (fondo, borde y una línea de sombra inferior
 * para dar relieve), su texto, y a continuación la etiqueta descriptiva. Avanza
 * @p *x (paso por referencia) hasta el inicio del siguiente badge, de modo que
 * el llamante pueda encadenarlos en fila.
 *
 * @param e   Editor. @param key Texto de la tecla (p. ej. "Ctrl+F").
 * @param label Etiqueta (p. ej. "Buscar"); puede ser vacía.
 * @param[in,out] x X actual; se actualiza al hueco siguiente. @param y Y del
 * badge.
 */
static void draw_shortcut_badge(Editor *e, const char *key, const char *label,
                                int *x, int y) {
    SDL_Renderer *r = e->renderer;

    int key_w = 0, key_h = 0;
    TTF_GetStringSize(e->font, key, 0, &key_w, &key_h); /* medir la tecla */
    int badge_w = key_w + BADGE_PAD_X * 2; /* recuadro = texto + padding */
    int badge_h = e->font_size + BADGE_PAD_Y * 2;

    set_color_c(r, e->theme.col_badge_bg);
    fill_rect(r, *x, y, badge_w, badge_h); /* fondo del recuadro */
    set_color_c(r, e->theme.col_badge_border);
    stroke_rect(r, *x, y, badge_w, badge_h); /* borde del recuadro */
    set_color_c(
        r,
        e->theme.col_badge_shadow); /* línea de sombra inferior (efecto 3D) */
    fill_rect(r, *x, y + badge_h, badge_w, 1);

    draw_text_c(e, key, *x + BADGE_PAD_X, y + BADGE_PAD_Y,
                e->theme.txt_badge_key); /* texto de la tecla */
    *x += badge_w + BADGE_GAP;           /* avanzar tras el recuadro */

    if (label && label[0]) { /* etiqueta a la derecha del badge */
        int label_w = draw_text_c(e, label, *x, y + BADGE_PAD_Y,
                                  e->theme.txt_badge_label);
        *x += label_w + BADGE_LABEL_GAP; /* avanzar tras la etiqueta */
    }
}

/**
 * @brief Dibuja la banda de atajos de teclado bajo el área de edición.
 *
 * Pinta el separador superior y el fondo (solo sobre el área del editor, no
 * bajo el panel lateral) y va colocando badges de izquierda a derecha con
 * ::draw_shortcut_badge, parando cuando se acerca al borde derecho de la
 * ventana.
 *
 * @note Solo se dibuja si está activada en preferencias (@c show_shortcuts); si
 * no, no se reserva espacio (::editor_shortcut_h devuelve 0) ni se pinta.
 * @param e Editor.
 */
void render_shortcuts(Editor *e) {
    if (!e->settings.show_shortcuts) return; /* barra de atajos oculta */
    SDL_Renderer *r = e->renderer;
    int left_offset = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    int sep_y = e->win_h - STATUS_HEIGHT -
                SHORTCUT_HEIGHT; /* Y del separador superior */

    set_color_c(r, e->theme.col_shortcut_sep);
    fill_rect(r, 0, sep_y, e->win_w, 1); /* separador de 1 px */
    set_color_c(
        r, e->theme.col_shortcut_bg); /* fondo solo sobre el área del editor */
    fill_rect(r, left_offset, sep_y + 1, e->win_w - left_offset,
              SHORTCUT_HEIGHT - 1);

    /* lista de atajos (tecla, etiqueta) a mostrar en la banda */
    static const struct {
        const char *key;
        const char *label;
    } SHORTCUTS[] = {
        {"Ctrl+F", "Buscar"},   {"Ctrl+B", "Panel"},
        {"Ctrl+D", "Duplicar"}, {"Ctrl+L", "Sel. línea"},
        {"Ctrl+/", "Comentar"}, {"Ctrl+Z", "Undo"},
        {"Ctrl+Y", "Redo"},     {"Ctrl+S", "Guardar"},
        {"Ctrl+N", "Nuevo"},
    };
    int badge_y = sep_y + (SHORTCUT_HEIGHT - e->font_size) / 2 -
                  2;                         /* Y centrada de los badges */
    int x = left_offset + SHORTCUT_LEFT_PAD; /* X de partida */
    for (int i = 0; i < (int)(sizeof(SHORTCUTS) / sizeof(SHORTCUTS[0])); i++) {
        draw_shortcut_badge(e, SHORTCUTS[i].key, SHORTCUTS[i].label, &x,
                            badge_y); /* avanza x */
        if (x > e->win_w - SHORTCUT_END_PAD)
            break; /* sin sitio para más: parar */
    }
}

/**
 * @brief Dibuja la barra de scroll vertical (track + "thumb") si hace falta.
 *
 * Si todo el texto cabe en pantalla no dibuja nada. Si no, pinta el carril
 * (track) y encima el "thumb" (el recuadro arrastrable), cuyo alto es
 * proporcional a la fracción de texto visible y cuya posición refleja @c
 * scroll_line dentro del rango desplazable. @c SB_MIN_THUMB_H evita que el
 * thumb sea diminuto en archivos grandes.
 *
 * @param e           Editor. @param left_offset Sin uso (se descarta con
 * (void)).
 */
void render_scrollbar(Editor *e, int left_offset) {
    (void)left_offset; /* no se usa; se marca para evitar warning de parámetro
                          sin usar */
    SDL_Renderer *r = e->renderer;
    int total_lines = buf_line_count(e->buf);
    int text_height =
        e->win_h - NAVBAR_HEIGHT - STATUS_HEIGHT - SHORTCUT_HEIGHT;
    int visible_lines = text_height / e->line_height;

    if (total_lines <= visible_lines) return; /* todo cabe: sin scrollbar */

    int sb_x =
        e->win_w - SCROLLBAR_W - 1; /* X de la barra (pegada a la derecha) */
    int sb_y = NAVBAR_HEIGHT +
               TAB_BAR_HEIGHT; /* Y de inicio (bajo navbar y pestañas) */
    int sb_h = text_height;    /* alto del carril */

    /* registrar el carril para el hit-test: input arranca el arrastre con
     * ui_hit(UI_SCROLLBAR), y solo cuando hay scrollbar (se llegó hasta aquí).
     */
    ui_put(&e->ui, UI_SCROLLBAR, (Rect){sb_x, sb_y, e->win_w - sb_x, sb_h});

    set_color_c(r, e->theme.col_sb_track);
    fill_rect(r, sb_x, sb_y, SCROLLBAR_W, sb_h); /* carril de fondo */

    /* alto del thumb proporcional a (líneas visibles / total), con un mínimo */
    int thumb_h = (int)(sb_h * ((float)visible_lines / (float)total_lines));
    if (thumb_h < SB_MIN_THUMB_H) thumb_h = SB_MIN_THUMB_H;
    int max_scroll =
        total_lines - visible_lines; /* desplazamiento máximo en líneas */
    /* fracción [0..1] de cuánto se ha desplazado el scroll */
    float scroll_frac =
        (max_scroll > 0) ? (float)e->scroll_line / (float)max_scroll : 0.0f;
    int thumb_y =
        sb_y +
        (int)(scroll_frac * (sb_h - thumb_h)); /* posición vertical del thumb */

    set_color_c(r, e->theme.col_sb_thumb);
    fill_rect(r, sb_x + 1, thumb_y, SCROLLBAR_W - 2, thumb_h); /* thumb */
    set_color_c(r, e->theme.col_sb_border);
    fill_rect(r, sb_x, sb_y, 1, sb_h); /* borde izquierdo del carril */
}

/** Callback de ::ui_list: una fila del selector = el nombre de la codificación.
 */
static void enc_row(Editor *e, int i, Rect row, int selected, void *ud) {
    (void)ud;
    (void)selected;
    draw_text_c(e, encoding_name((TextEncoding)i), row.x + 8,
                row.y + (row.h - e->font_size) / 2,
                e->theme.tokens[TOK_DEFAULT]);
}

void render_enc_popup(Editor *e) {
    if (!e->enc_popup) return;

    int row_h = e->font_size + 10;
    int hdr_h = e->font_size + 14;
    int w = 210;
    int list_h = row_h * ENC_COUNT;
    int h = hdr_h + list_h;
    int x = e->win_w - w - 8;
    int y = e->win_h - STATUS_HEIGHT - h;
    if (x < 0) x = 0;
    if (y < NAVBAR_HEIGHT) y = NAVBAR_HEIGHT;

    /* fondo del popup */
    ui_panel(e, (Rect){x, y, w, h}, e->theme.col_menu_bg,
             e->theme.col_menu_border);

    /* cabecera: dos botones de modo (el activo resaltado) */
    int bw = w / 2;
    Rect b1 = {x, y, bw, hdr_h};
    Rect b2 = {x + bw, y, w - bw, hdr_h};
    ui_button(e, UI_ENC_MODE_REOPEN, b1, "Reabrir", &e->theme.style_button,
              e->enc_popup_mode == 0 ? UI_ACTIVE : UI_NORMAL);
    ui_button(e, UI_ENC_MODE_SAVE, b2, "Guardar", &e->theme.style_button,
              e->enc_popup_mode == 1 ? UI_ACTIVE : UI_NORMAL);

    /* lista de codificaciones (selección = la actual) */
    Rect lb = {x, y + hdr_h, w, list_h};
    ui_list(e, lb, UI_ENC_LIST, UI_LIST_ENC, ENC_COUNT, row_h,
            &e->enc_popup_scroll, (int)e->encoding, enc_row, NULL);
}
