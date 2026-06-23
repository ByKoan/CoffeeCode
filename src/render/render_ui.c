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
#include <stdlib.h>
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

    /* fondo de la navbar (opaco en BG_MODE_NONE, see-through en los demas) */
    chrome_fill_bg(e, e->theme.col_navbar_bg, 0, 0, e->win_w, NAVBAR_HEIGHT);
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

    /* fondo de la pestaña (activa mas clara): see-through como el resto */
    chrome_fill_bg(e, active ? e->theme.col_tab_active : e->theme.col_tabbar_bg,
                   tx, bar_y, tab_w, bar_h);

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

/** Mide (sin dibujar) el ancho que ocupara la pestana @p index, con la misma
 *  formula que ::draw_tab_active.  Lo usa el scroll de la barra de pestanas. */
static int measure_tab_w(Editor *e, int index) {
    EditorTab *t = &e->tabs[index];
    const char *name =
        last_path_component(t->filepath[0] ? t->filepath : "Sin título");
    int name_w = 0, name_h = 0;
    TTF_GetStringSize(e->font, name, 0, &name_w, &name_h);
    int tab_w = name_w + TAB_CLOSE_W + TAB_PAD * 2 + TAB_TEXT_EXTRA;
    if (tab_w < TAB_MIN_W) tab_w = TAB_MIN_W;
    if (tab_w > TAB_MAX_W) tab_w = TAB_MAX_W;
    return tab_w;
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

    /* fondo de la barra (opaco en BG_MODE_NONE, see-through en los demas) */
    chrome_fill_bg(e, e->theme.col_tabbar_bg, 0, bar_y, e->win_w, bar_h);
    set_color_c(r, e->theme.col_tabbar_sep);
    fill_rect(r, 0, bar_y + bar_h - 1, e->win_w, 1); /* separador inferior */

    /* empezar tras el panel lateral (o su botón si está cerrado) */
    int tabs_start = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    /* el botón "+" queda FIJO a la derecha (siempre accesible aunque haya
     * muchas pestañas y haya que hacer scroll). */
    int new_x = e->win_w - TAB_NEW_BTN_W;
    int avail = new_x - tabs_start; /* ancho visible para las pestañas */

    /* la barra global solo se dibuja sin division; muestra las pestanas de la
     * unica hoja del dock.  Si hay flotantes, sus pestanas viven en otro grupo y
     * NO deben aparecer aqui (las dibuja su propio flotante). */
    int dock_group = (e->dock.node_count > 0 && e->dock.root >= 0)
                         ? e->dock.nodes[e->dock.root].group_id
                         : 0;

    /* Índices de las pestañas que van en ESTA barra (las del dock_group, o
     * todas si no hay flotantes), en orden. */
    int vis[MAX_TABS];
    int nvis = 0;
    for (int i = 0; i < e->tab_count; i++) {
        if (e->float_count > 0 && e->tabs[i].group != dock_group)
            continue;
        vis[nvis++] = i;
    }

    if (nvis == 0) { /* sin pestañas en esta barra: solo el "+" */
        e->tab_scroll = 0;
        chrome_fill_bg(e, e->theme.col_tabbar_bg, tabs_start, bar_y,
                       TAB_NEW_BTN_W, bar_h);
        draw_text_c(e, "+", tabs_start + 7, bar_y + (bar_h - e->font_size) / 2,
                    e->theme.txt_tab_new);
        ui_put(&e->ui, UI_TAB_NEW,
               (Rect){tabs_start, bar_y, TAB_NEW_BTN_W, bar_h});
        return;
    }

    /* Posición (en vis[]) de la primera visible según e->tab_scroll (índice
     * global): el primer vis cuyo índice >= tab_scroll. */
    int scroll_pos = nvis - 1;
    for (int k = 0; k < nvis; k++)
        if (vis[k] >= e->tab_scroll) { scroll_pos = k; break; }
    if (scroll_pos < 0) scroll_pos = 0;

    /* Auto-scroll para que la pestaña activa quede visible, PERO solo cuando la
     * activa cambió desde el último ajuste: así la rueda permite explorar otras
     * pestañas sin que el scroll vuelva a saltar a la activa cada frame. */
    int act_pos = -1;
    for (int k = 0; k < nvis; k++)
        if (vis[k] == e->active_tab) { act_pos = k; break; }
    if (act_pos >= 0 && e->active_tab != e->tab_scroll_seen) {
        if (act_pos < scroll_pos) {
            scroll_pos = act_pos; /* activa a la izquierda: traerla al inicio */
        } else {
            /* avanzar la primera visible hasta que la activa quepa a la dcha */
            while (scroll_pos < act_pos) {
                int sum = 0;
                for (int k = scroll_pos; k <= act_pos; k++)
                    sum += measure_tab_w(e, vis[k]);
                if (sum <= avail) break;
                scroll_pos++;
            }
        }
        e->tab_scroll_seen = e->active_tab;
    }
    if (scroll_pos < 0) scroll_pos = 0;
    if (scroll_pos > nvis - 1) scroll_pos = nvis - 1;
    e->tab_scroll = vis[scroll_pos]; /* persistir como índice global */

    /* Recortar el dibujo de las pestañas a su franja (no invadir el "+"). */
    SDL_Rect clip = {tabs_start, bar_y, avail > 0 ? avail : 0, bar_h};
    SDL_SetRenderClipRect(r, &clip);
    int tx = tabs_start;
    for (int k = scroll_pos; k < nvis; k++) {
        int w = measure_tab_w(e, vis[k]);
        if (tx + w > new_x && k > scroll_pos)
            break; /* no cabe entera (al menos la 1ª se dibuja, recortada) */
        tx += draw_tab(e, vis[k], tx, bar_y, bar_h);
        if (tx >= new_x) break;
    }
    SDL_SetRenderClipRect(r, NULL);

    /* Botón "+" (nueva pestaña), fijo a la derecha. */
    chrome_fill_bg(e, e->theme.col_tabbar_bg, new_x, bar_y, TAB_NEW_BTN_W,
                   bar_h);
    draw_text_c(e, "+", new_x + 7, bar_y + (bar_h - e->font_size) / 2,
                e->theme.txt_tab_new);
    ui_put(&e->ui, UI_TAB_NEW, (Rect){new_x, bar_y, TAB_NEW_BTN_W, bar_h});
}

void render_tabbar_group(Editor *e, int group, int bar_y, int pane_left,
                         int pane_right) {
    SDL_Renderer *r = e->renderer;
    int bar_h = TAB_BAR_HEIGHT;

    /* fondo + separador inferior de la franja de este panel */
    chrome_fill_bg(e, e->theme.col_tabbar_bg, pane_left, bar_y,
                   pane_right - pane_left, bar_h);
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
        chrome_fill_bg(e, e->theme.col_tabbar_bg, tx, bar_y, TAB_NEW_BTN_W,
                       bar_h);
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
    /* fondo de la banda (solo sobre el área del editor): see-through como el
     * resto del cromo */
    chrome_fill_bg(e, e->theme.col_shortcut_bg, left_offset, sep_y + 1,
                   e->win_w - left_offset, SHORTCUT_HEIGHT - 1);

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

/* Numero de filas de contenido visibles del popup de hover. */
#define HOVER_BODY_ROWS 16

/* --- Resaltado ligero del contenido del popup (IR / bytecode / asm) --- */
static int hv_id_ch(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '.';
}
static int hv_id_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '.';
}
static int hv_is_type(const char *s) {
    static const char *T[] = {
        "i8",   "i16",     "i32",     "i64",  "u8",      "u16",
        "u32",  "u64",     "f32",     "f64",  "ptr",     "void",
        "bool", "byte",    "word",    "dword","qword",   "xmmword",
        "ymmword", "zmmword", 0};
    for (int i = 0; T[i]; ++i)
        if (strcmp(s, T[i]) == 0) return 1;
    return 0;
}
static int hv_is_reg(const char *s) {
    static const char *R[] = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "rip",
        "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp", "ax",
        "bx",  "cx",  "dx",  "al",  "bl",  "cl",  "dl",  "cs",  "ds",
        "es",  "fs",  "gs",  "ss",  0};
    for (int i = 0; R[i]; ++i)
        if (strcmp(s, R[i]) == 0) return 1;
    size_t n = strlen(s);
    if (n >= 2 && s[0] == 'r' && s[1] >= '0' && s[1] <= '9') return 1;
    if (n >= 4 && (strncmp(s, "xmm", 3) == 0 || strncmp(s, "ymm", 3) == 0 ||
                   strncmp(s, "zmm", 3) == 0))
        return 1;
    return 0;
}
/* Dibuja una linea de codigo (IR/bytecode/asm) con tokens coloreados. */
static void draw_code_line(Editor *e, const char *line, int n, int x, int y) {
    int cw = e->char_w > 0 ? e->char_w : 8;
    int i = 0, first_id = 0, after_eq = 0;
    /* Linea de diff (pestana IR): "- " eliminado por el optimizador (rojo),
     * "+ " generado (verde).  Se pinta la linea entera de su color. */
    if (n >= 2 && line[1] == ' ' && (line[0] == '-' || line[0] == '+')) {
        char buf[2048];
        int L = n;
        if (L > 2047) L = 2047;
        memcpy(buf, line, L);
        buf[L] = 0;
        if (line[0] == '-')
            draw_text(e, buf, x, y, 0xD0, 0x6A, 0x6A); /* rojo: eliminado */
        else
            draw_text(e, buf, x, y, 0x7A, 0xC0, 0x7A); /* verde: generado */
        return;
    }
    while (i < n) {
        char c = line[i];
        /* comentario hasta fin de linea (// de IR/C o ; de asm) */
        if ((c == '/' && i + 1 < n && line[i + 1] == '/') || c == ';') {
            char buf[1024];
            int L = n - i;
            if (L > 1023) L = 1023;
            memcpy(buf, line + i, L);
            buf[L] = 0;
            draw_text(e, buf, x + i * cw, y, 0x6A, 0x99, 0x55);
            return;
        }
        if (c == ' ' || c == '\t') { i++; continue; }
        /* numero (decimal o 0xHEX, con signo) */
        if ((c >= '0' && c <= '9') ||
            (c == '-' && i + 1 < n && line[i + 1] >= '0' && line[i + 1] <= '9')) {
            int j = i + 1;
            while (j < n && (hv_id_ch(line[j]) || line[j] == 'x' ||
                             line[j] == 'X'))
                j++;
            char buf[64];
            int L = j - i;
            if (L > 63) L = 63;
            memcpy(buf, line + i, L);
            buf[L] = 0;
            draw_text(e, buf, x + i * cw, y, 0xB5, 0xCE, 0xA8);
            i = j;
            continue;
        }
        /* valor SSA del IR: %name */
        if (c == '%') {
            int j = i + 1;
            while (j < n && hv_id_ch(line[j])) j++;
            char buf[64];
            int L = j - i;
            if (L > 63) L = 63;
            memcpy(buf, line + i, L);
            buf[L] = 0;
            draw_text(e, buf, x + i * cw, y, 0xDC, 0xDC, 0xAA);
            i = j;
            continue;
        }
        /* identificador: tipo / registro / op-mnemonico / normal */
        if (hv_id_start(c)) {
            int j = i + 1;
            while (j < n && hv_id_ch(line[j])) j++;
            char buf[64];
            int L = j - i;
            if (L > 63) L = 63;
            memcpy(buf, line + i, L);
            buf[L] = 0;
            int rr = 0xCC, gg = 0xCC, bb = 0xD4;
            if (hv_is_type(buf)) {
                rr = 0x4E; gg = 0xC9; bb = 0xB0; /* turquesa */
            } else if (hv_is_reg(buf)) {
                rr = 0xE0; gg = 0x6C; bb = 0x75; /* coral */
            } else if (!first_id || after_eq) {
                rr = 0xC5; gg = 0x86; bb = 0xC0; /* malva: op/mnemonico */
            }
            draw_text(e, buf, x + i * cw, y, rr, gg, bb);
            first_id = 1;
            after_eq = 0;
            i = j;
            continue;
        }
        /* puntuacion / otro: un caracter en color por defecto */
        {
            char buf[2] = {c, 0};
            draw_text(e, buf, x + i * cw, y, 0x9A, 0x9A, 0xA6);
            if (c == '=') after_eq = 1;
            i++;
        }
    }
}

/* Familia GPR de 64 bits de un token de registro x86 ("eax"->"rax",
 * "r8d"->"r8", ...).  Devuelve el indice en kFam o -1 si no es un GPR. */
static int asm_reg_family(const char *t) {
    static const struct {
        const char *n;
        int fam;
    } kReg[] = {
        {"rax", 0},  {"eax", 0},  {"ax", 0},   {"al", 0},   {"ah", 0},
        {"rbx", 1},  {"ebx", 1},  {"bx", 1},   {"bl", 1},   {"bh", 1},
        {"rcx", 2},  {"ecx", 2},  {"cx", 2},   {"cl", 2},   {"ch", 2},
        {"rdx", 3},  {"edx", 3},  {"dx", 3},   {"dl", 3},   {"dh", 3},
        {"rsi", 4},  {"esi", 4},  {"si", 4},   {"sil", 4},
        {"rdi", 5},  {"edi", 5},  {"di", 5},   {"dil", 5},
        {"rbp", 6},  {"ebp", 6},  {"bp", 6},   {"bpl", 6},
        {"rsp", 7},  {"esp", 7},  {"sp", 7},   {"spl", 7},
        {"r8", 8},   {"r8d", 8},  {"r8w", 8},  {"r8b", 8},
        {"r9", 9},   {"r9d", 9},  {"r9w", 9},  {"r9b", 9},
        {"r10", 10}, {"r10d", 10},{"r10w", 10},{"r10b", 10},
        {"r11", 11}, {"r11d", 11},{"r11w", 11},{"r11b", 11},
        {"r12", 12}, {"r12d", 12},{"r12w", 12},{"r12b", 12},
        {"r13", 13}, {"r13d", 13},{"r13w", 13},{"r13b", 13},
        {"r14", 14}, {"r14d", 14},{"r14w", 14},{"r14b", 14},
        {"r15", 15}, {"r15d", 15},{"r15w", 15},{"r15b", 15},
    };
    for (size_t k = 0; k < sizeof(kReg) / sizeof(kReg[0]); ++k)
        if (strcmp(t, kReg[k].n) == 0) return kReg[k].fam;
    return -1;
}

/* Color estable por registro (mismo registro -> mismo color siempre).  rbp/rsp
 * en gris (de-enfasis del marco); el resto en una paleta distinguible. */
static void asm_reg_rgb(int fam, int *R, int *G, int *B) {
    static const int kPal[16][3] = {
        {0xE6, 0x9A, 0x9A}, {0xE6, 0xC8, 0x8A}, {0x8A, 0xC4, 0xE6},
        {0x8A, 0xE0, 0xA6}, {0xC6, 0xA4, 0xEE}, {0xEE, 0xA4, 0xCE},
        {0x86, 0x8E, 0x9A}, {0x86, 0x8E, 0x9A}, {0x6F, 0xD0, 0xC8},
        {0x6F, 0xD0, 0xC8}, {0x7C, 0xC0, 0xD8}, {0x7C, 0xC0, 0xD8},
        {0x9C, 0xC8, 0x88}, {0x9C, 0xC8, 0x88}, {0xC8, 0xB0, 0x78},
        {0xC8, 0xB0, 0x78},
    };
    *R = kPal[fam][0];
    *G = kPal[fam][1];
    *B = kPal[fam][2];
}

/* Pinta una linea de DESENSAMBLADO x86 (columna nativa del godbolt): mnemonico
 * en malva, registros con color estable por registro, inmediatos en ambar (con
 * su decimal cuando son 0xHEX de datos, no de saltos), puntuacion atenuada y el
 * comentario "; ..." en gris.  Distinto de draw_code_line (que es sintaxis Vex).
 */
static void draw_asm_line(Editor *e, const char *line, int x, int y,
                          int show_notes) {
    int cw = e->char_w > 0 ? e->char_w : 8;
    int n = (int)strlen(line);
    int i = 0, first_id = 1, is_branch = 0;
    while (i < n) {
        char c = line[i];
        if (c == ';') { /* comentario hasta fin de linea (atenuado) */
            if (!show_notes) return; /* notas ocultas por opcion de vista */
            char buf[512];
            int L = n - i;
            if (L > 511) L = 511;
            memcpy(buf, line + i, L);
            buf[L] = 0;
            draw_text(e, buf, x + i * cw, y, 0x6A, 0x86, 0x66);
            return;
        }
        if (c == ' ' || c == '\t') { i++; continue; }
        /* numero / inmediato (decimal o 0xHEX, con signo) */
        if ((c >= '0' && c <= '9') ||
            (c == '-' && i + 1 < n && line[i + 1] >= '0' && line[i + 1] <= '9')) {
            int j = i + 1;
            while (j < n && (hv_id_ch(line[j]) || line[j] == 'x' ||
                             line[j] == 'X'))
                j++;
            char buf[64];
            int L = j - i;
            if (L > 63) L = 63;
            memcpy(buf, line + i, L);
            buf[L] = 0;
            draw_text(e, buf, x + i * cw, y, 0xD6, 0xB0, 0x76);
            /* Decimal de un inmediato 0xHEX de DATOS (no de salto) y solo si
             * cierra la linea -> evita solaparse con tokens posteriores. */
            int k = j;
            while (k < n && (line[k] == ' ' || line[k] == '\t')) k++;
            if (!is_branch && k >= n && L > 2 && buf[0] == '0' &&
                (buf[1] == 'x' || buf[1] == 'X')) {
                long long v = strtoll(buf + 2, NULL, 16);
                if (v >= 10) {
                    char db[32];
                    snprintf(db, sizeof db, " (%lld)", v);
                    draw_text(e, db, x + j * cw, y, 0x60, 0x70, 0x80);
                }
            }
            i = j;
            continue;
        }
        /* identificador: mnemonico (primero) / registro / otro */
        if (hv_id_start(c)) {
            int j = i + 1;
            while (j < n && hv_id_ch(line[j])) j++;
            char buf[64];
            int L = j - i;
            if (L > 63) L = 63;
            memcpy(buf, line + i, L);
            buf[L] = 0;
            int rr = 0xCC, gg = 0xCC, bb = 0xD4, fam;
            if (first_id) {
                rr = 0xC5; gg = 0x86; bb = 0xC0; /* mnemonico malva */
                if (buf[0] == 'j' || strcmp(buf, "call") == 0 ||
                    strcmp(buf, "loop") == 0 || strcmp(buf, "ret") == 0)
                    is_branch = 1;
                first_id = 0;
            } else if ((fam = asm_reg_family(buf)) >= 0) {
                asm_reg_rgb(fam, &rr, &gg, &bb);
            }
            draw_text(e, buf, x + i * cw, y, rr, gg, bb);
            i = j;
            continue;
        }
        { /* puntuacion */
            char buf[2] = {c, 0};
            draw_text(e, buf, x + i * cw, y, 0x80, 0x84, 0x90);
            i++;
        }
    }
}

/* ----------------------------------------------------------------------------
 * Renderer minimo de Markdown + HTML 1.0 para la pestana Doc del hover.
 * Soporta INLINE: **negrita** / *cursiva* / `codigo` / [texto](url) y los tags
 * <b> <strong> <i> <em> <code> y entidades &lt; &gt; &amp; &quot; &#39;.  Los
 * marcadores se consumen (no ocupan columna); el resto se dibuja con el color
 * del estilo activo.  El nivel de BLOQUE (cabeceras #, vinetas, fences ```,
 * <h1>, <li>, <pre>, <br>) lo maneja render_doc_body por linea.
 * -------------------------------------------------------------------------- */
static void draw_md_inline(Editor *e, const char *s, int n, int x, int y,
                           int base_r, int base_g, int base_b) {
    int cw = e->char_w > 0 ? e->char_w : 8;
    int col = 0, i = 0;
    int bold = 0, ital = 0, code = 0, link = 0;
    while (i < n) {
        char c = s[i];
        /* Entidad HTML: &nombre; -> caracter. */
        if (c == '&') {
            const char *rep = NULL;
            if (n - i >= 4 && strncmp(s + i, "&lt;", 4) == 0) { rep = "<"; i += 4; }
            else if (n - i >= 4 && strncmp(s + i, "&gt;", 4) == 0) { rep = ">"; i += 4; }
            else if (n - i >= 5 && strncmp(s + i, "&amp;", 5) == 0) { rep = "&"; i += 5; }
            else if (n - i >= 6 && strncmp(s + i, "&quot;", 6) == 0) { rep = "\""; i += 6; }
            else if (n - i >= 5 && strncmp(s + i, "&#39;", 5) == 0) { rep = "'"; i += 5; }
            if (rep) {
                int rr = bold ? 0xFF : (code ? 0x9E : (link ? 0x6C : base_r));
                int gg = bold ? 0xFF : (code ? 0xD0 : (link ? 0xB0 : base_g));
                int bb = bold ? 0xFF : (code ? 0x8A : (link ? 0xE0 : base_b));
                char buf[2] = {rep[0], 0};
                draw_text(e, buf, x + col * cw, y, rr, gg, bb);
                col++;
                continue;
            }
        }
        /* Tag HTML: <b> <i> <code> <strong> <em> y sus cierres; otros se
         * ignoran (strip).  No avanzan columna. */
        if (c == '<') {
            int j = i + 1;
            while (j < n && s[j] != '>') j++;
            int taglen = j - (i + 1);
            char tag[16];
            int tl = taglen < 15 ? taglen : 15;
            memcpy(tag, s + i + 1, tl);
            tag[tl] = 0;
            /* normalizar a minusculas el nombre */
            for (int k = 0; tag[k]; ++k)
                if (tag[k] >= 'A' && tag[k] <= 'Z') tag[k] += 32;
            if (!strcmp(tag, "b") || !strcmp(tag, "strong")) bold = 1;
            else if (!strcmp(tag, "/b") || !strcmp(tag, "/strong")) bold = 0;
            else if (!strcmp(tag, "i") || !strcmp(tag, "em")) ital = 1;
            else if (!strcmp(tag, "/i") || !strcmp(tag, "/em")) ital = 0;
            else if (!strcmp(tag, "code")) code = 1;
            else if (!strcmp(tag, "/code")) code = 0;
            i = (j < n) ? j + 1 : n; /* saltar hasta despues de '>' */
            continue;
        }
        /* Marcadores Markdown. */
        if (c == '*' && i + 1 < n && s[i + 1] == '*') { bold = !bold; i += 2; continue; }
        if ((c == '*' || c == '_') &&
            !(i + 1 < n && s[i + 1] == c)) { ital = !ital; i += 1; continue; }
        if (c == '`') { code = !code; i += 1; continue; }
        if (c == '[') { link = 1; i += 1; continue; }
        if (c == ']' && link) {
            link = 0;
            /* saltar el (url) si lo hay */
            int j = i + 1;
            if (j < n && s[j] == '(') {
                while (j < n && s[j] != ')') j++;
                i = (j < n) ? j + 1 : n;
            } else {
                i = j;
            }
            continue;
        }
        /* Caracter normal con el color del estilo activo. */
        int rr = base_r, gg = base_g, bb = base_b;
        if (code) { rr = 0x9E; gg = 0xD0; bb = 0x8A; }
        else if (link) { rr = 0x6C; gg = 0xB0; bb = 0xE0; }
        else if (bold) { rr = 0xFF; gg = 0xFF; bb = 0xFF; }
        else if (ital) { rr = 0xC6; gg = 0xBE; bb = 0xDC; }
        char buf[2] = {c, 0};
        draw_text(e, buf, x + col * cw, y, rr, gg, bb);
        col++;
        i++;
    }
}

void render_hover_popup(Editor *e) {
    HoverPopup *h = &e->hover;
    if (!h->visible || h->n_tabs <= 0) return;
    SDL_Renderer *r = e->renderer;

    int cw = e->char_w > 0 ? e->char_w : 8;
    int lh = e->line_height;
    int pad = 8;
    int hdr_h = e->font_size + 12; /* franja de pestanas + arrastre + cerrar */
    int rsz = 14;                  /* tirador de redimension */

    /* Colocacion inicial: tamano por defecto + posicion bajo el ancla.  Tras
     * que el usuario lo mueva/redimensione, se respeta SU geometria. */
    if (h->needs_place) {
        int w0 = 74 * cw + pad * 2;
        if (w0 > e->win_w - 16) w0 = e->win_w - 16;
        int h0 = hdr_h + HOVER_BODY_ROWS * lh + pad;
        int x0 = h->anchor_x;
        int y0 = h->anchor_y + lh + 2;
        if (x0 + w0 > e->win_w - 8) x0 = e->win_w - 8 - w0;
        if (x0 < 8) x0 = 8;
        if (y0 + h0 > e->win_h - STATUS_HEIGHT) {
            y0 = h->anchor_y - h0 - 2;
            if (y0 < NAVBAR_HEIGHT + TAB_BAR_HEIGHT)
                y0 = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
        }
        h->rect_x = x0;
        h->rect_y = y0;
        h->rect_w = w0;
        h->rect_h = h0;
        h->needs_place = 0;
    }
    /* Clamp defensivo (tamano minimo + dentro de la ventana). */
    if (h->rect_w < 24 * cw) h->rect_w = 24 * cw;
    if (h->rect_h < hdr_h + lh * 3) h->rect_h = hdr_h + lh * 3;
    if (h->rect_w > e->win_w) h->rect_w = e->win_w;
    if (h->rect_x < 0) h->rect_x = 0;
    if (h->rect_y < NAVBAR_HEIGHT) h->rect_y = NAVBAR_HEIGHT;
    if (h->rect_x + h->rect_w > e->win_w) h->rect_x = e->win_w - h->rect_w;
    if (h->rect_y + h->rect_h > e->win_h) h->rect_y = e->win_h - h->rect_h;

    int x = h->rect_x, y = h->rect_y, w = h->rect_w, hh = h->rect_h;

    /* Fondo CON la transparencia del IDE (see-through como el resto del chrome)
     * + borde manual de 1px. */
    chrome_fill_bg(e, e->theme.col_menu_bg, x, y, w, hh);
    set_color_c(r, e->theme.col_menu_border);
    fill_rect(r, x, y, w, 1);
    fill_rect(r, x, y + hh - 1, w, 1);
    fill_rect(r, x, y, 1, hh);
    fill_rect(r, x + w - 1, y, 1, hh);

    /* Franja de pestanas (clicables).  El boton de cerrar va a la derecha; el
     * hueco entre la ultima pestana y el boton es la zona de arrastre. */
    int close_w = hdr_h;
    int tabs_right = x + w - close_w;
    int tx = x;
    for (int i = 0; i < h->n_tabs; ++i) {
        const char *name = h->tab_names[i] ? h->tab_names[i] : "";
        int tw = (int)strlen(name) * cw + pad * 2;
        if (tx + tw > tabs_right) tw = tabs_right - tx;
        if (tw <= 0) break;
        Rect tr = {tx, y, tw, hdr_h};
        ui_put_idx(&e->ui, UI_LIST_HOVER_TAB, i, tr);
        if (i == h->active_tab) {
            set_color_c(r, e->theme.col_tab_active);
            fill_rect(r, tx, y, tw, hdr_h);
            set_color_c(r, e->theme.col_tab_accent);
            fill_rect(r, tx, y, tw, 2);
            draw_text(e, name, tx + pad, y + (hdr_h - e->font_size) / 2, 0xDD,
                      0xDD, 0xEE);
        } else {
            draw_text(e, name, tx + pad, y + (hdr_h - e->font_size) / 2, 0x88,
                      0x8C, 0x99);
        }
        set_color_c(r, e->theme.col_tabbar_sep);
        fill_rect(r, tx + tw - 1, y, 1, hdr_h);
        tx += tw;
    }
    /* Boton de cerrar (x) a la derecha de la franja. */
    set_color_c(r, e->theme.col_tabbar_sep);
    fill_rect(r, tabs_right, y, 1, hdr_h);
    draw_text(e, "x", tabs_right + (close_w - cw) / 2,
              y + (hdr_h - e->font_size) / 2, 0xC8, 0x80, 0x80);
    set_color_c(r, e->theme.col_menu_border);
    fill_rect(r, x, y + hdr_h - 1, w, 1); /* linea bajo la franja */

    /* Contenido del tab activo (recortado, scrollable).  El numero de filas se
     * deriva del alto ACTUAL (el usuario puede haberlo redimensionado). */
    int body_y0 = y + hdr_h;
    int body_h = hh - hdr_h;
    int rows = (body_h - pad) / lh;
    if (rows < 1) rows = 1;
    const char *content = h->tab_content[h->active_tab];
    h->gb_active = 0; /* lo re-activa el branch 0x1D si aplica */
    h->ir_active = 0; /* lo re-activa el bloque 0x1C si aplica */
    int body_x = x + pad;
    int by = body_y0 + pad / 2;

    /* Pestana IR unica con selector de sub-vistas (contenido 0x1C): dibuja una
     * fila de botones [lado a lado | unificado] y deja como contenido efectivo
     * la sub-vista activa (el resto del render la trata como 0x1E o texto). */
    char *ir_copy = NULL;
    if (content && content[0] == 0x1C) {
        h->ir_active = 1;
        size_t L = strlen(content);
        ir_copy = (char *)malloc(L + 1);
        const char *sub0 = "", *sub1 = "";
        if (ir_copy) {
            memcpy(ir_copy, content, L + 1);
            char *q = ir_copy + 1; /* saltar el 0x1C inicial */
            sub0 = q;
            char *sep = strchr(q, 0x1C);
            if (sep) {
                *sep = 0;
                sub1 = sep + 1;
            }
        }
        /* Fila selectora (dos botones).  Geometria publicada para el input. */
        int sel_y = by;
        int b0x = body_x, b0w = 14 * cw;       /* "lado a lado" */
        int b1x = b0x + b0w + cw, b1w = 12 * cw; /* "unificado" */
        h->ir_sel_y = sel_y;
        h->ir_sel_x0 = b0x;
        h->ir_sel_mid = b1x - cw / 2;
        h->ir_sel_x1 = b1x + b1w;
        for (int bi = 0; bi < 2; ++bi) {
            int bxx = bi == 0 ? b0x : b1x;
            int bww = bi == 0 ? b0w : b1w;
            const char *lbl = bi == 0 ? "lado a lado" : "unificado";
            if (h->ir_submode == bi) {
                set_color_c(r, e->theme.col_tab_active);
                fill_rect(r, bxx, sel_y - 1, bww, lh);
                set_color_c(r, e->theme.col_tab_accent);
                fill_rect(r, bxx, sel_y - 1, bww, 2);
                draw_text(e, lbl, bxx + 4, sel_y, 0xDD, 0xDD, 0xEE);
            } else {
                draw_text(e, lbl, bxx + 4, sel_y, 0x88, 0x8C, 0x99);
            }
        }
        /* El cuerpo real baja una fila; el resto del render usa la sub-vista. */
        body_y0 += lh;
        body_h -= lh;
        rows -= 1;
        if (rows < 1) rows = 1;
        by += lh;
        content = (h->ir_submode == 0) ? sub0 : sub1;
    }

    SDL_Rect clip = {x + 1, body_y0, w - 2, body_h - 2};
    SDL_SetRenderClipRect(r, &clip);
    if (!content) {
        draw_text(e, "Cargando...", body_x, by, 0x88, 0x8C, 0x99);
    } else if (content[0] == 0x1E) {
        /* Vista DIFF LADO A LADO: cada linea es "lm \x1f L \x1f rm \x1f R".
         * Maquetamos dos columnas segun el ancho ACTUAL del popup. */
        int avail = (w - 2 * pad) / cw;
        int col_w = (avail - 3) / 2;
        if (col_w < 4) col_w = 4;
        int midx = body_x + col_w * cw + cw;
        int rx = midx + 2 * cw;
        /* separador vertical continuo */
        set_color_c(r, e->theme.col_tabbar_sep);
        fill_rect(r, midx + cw / 2, body_y0, 1, body_h - 2);
        const char *p = content;
        const char *nl0 = strchr(p, '\n');
        p = nl0 ? nl0 + 1 : p + 1; /* saltar el sentinela */
        int skip = h->scroll < 0 ? 0 : h->scroll;
        int row = 0;
        char fbuf[2048];
        while (*p && row < rows) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (skip > 0) {
                skip--;
            } else {
                size_t cpy = len < sizeof(fbuf) - 1 ? len : sizeof(fbuf) - 1;
                memcpy(fbuf, p, cpy);
                fbuf[cpy] = 0;
                /* parsear lm \x1f L \x1f rm \x1f R */
                char lm = ' ', rm = ' ';
                char *L = fbuf, *R = (char *)"";
                char *s1 = strchr(fbuf, 0x1F);
                if (s1) {
                    lm = fbuf[0];
                    *s1 = 0;
                    L = s1 + 1;
                    char *s2 = strchr(L, 0x1F);
                    if (s2) {
                        *s2 = 0;
                        char *mr = s2 + 1;
                        rm = mr[0];
                        char *s3 = strchr(mr, 0x1F);
                        if (s3) R = s3 + 1;
                    }
                }
                int yy = by + row * lh;
                /* columna izquierda (pre): roja si eliminada/cambiada. */
                {
                    char lb[1024];
                    int lc = (int)strlen(L);
                    if (lc > col_w) lc = col_w;
                    if (lc > 1023) lc = 1023;
                    memcpy(lb, L, lc);
                    lb[lc] = 0;
                    if (lm == '-')
                        draw_text(e, lb, body_x, yy, 0xD0, 0x6A, 0x6A);
                    else
                        draw_code_line(e, lb, lc, body_x, yy);
                }
                /* indicador de correspondencia para los cambios (->). */
                if (lm == '-' && rm == '+')
                    draw_text(e, ">", midx, yy, 0xC8, 0xB0, 0x60);
                /* columna derecha (post): verde si anyadida/cambiada. */
                {
                    char rb[1024];
                    int rc = (int)strlen(R);
                    if (rc > col_w) rc = col_w;
                    if (rc > 1023) rc = 1023;
                    memcpy(rb, R, rc);
                    rb[rc] = 0;
                    if (rm == '+')
                        draw_text(e, rb, rx, yy, 0x7A, 0xC0, 0x7A);
                    else
                        draw_code_line(e, rb, rc, rx, yy);
                }
                row++;
            }
            if (!nl) break;
            p = nl + 1;
        }
    } else if (content[0] == 0x1D) {
        /* =====================================================================
         * VISTA DE CODIGO CORRELADO ("Godbolt") -- CAPACIDAD GENERICA DEL HOST.
         *
         * Este renderer NO es de ninguna extension: dibuja lo que describa el
         * payload (codificado por sentinelas) que CUALQUIER extension manda via
         * set_hover_tab.  Una extension que produzca este formato obtiene gratis
         * dos columnas correladas (fuente | asm), cross-highlight por linea,
         * flechas de control de flujo, banda de stack frame y correlacion con
         * una representacion intermedia -- sin tocar el render.
         *
         * CONTRATO DEL PAYLOAD (1 fila por linea; campos separados por 0x1F):
         *   byte 0 = 0x1D, luego '\n'.
         *   H \x1f <texto cabecera>                         estadisticas
         *   S \x1f <linea> \x1f <texto>                     linea de fuente
         *   A \x1f <linea> \x1f <addr> \x1f <ir_id> \x1f <texto>   instr. asm
         *       (<addr> vacio = no-nativo p.ej. bytecode; <ir_id> = id de la op
         *        IR que la genero, o 4294967295 = sintetica)
         *   F \x1f <label> \x1f <kind> \x1f <size> \x1f <nombre>   slot de frame
         *       (kind: retaddr|saved|local|reserved)
         *   I \x1f <linea> \x1f <op IR>                     IR por linea
         *   J \x1f <ir_id> \x1f <op IR>                     IR por id (exacta)
         *
         * Las CAPACIDADES (flechas, frame, modos de correlacion IR) son
         * genericas y se gobiernan por Settings host-globales (hover_arrows /
         * hover_frame / hover_notes / hover_ir_mode) compartidos por todas las
         * extensiones; el toggle es el componente generico ui_toggle.  La col.
         * fuente NO scrollea (es corta); la nativa usa h->scroll. */
        size_t clen = strlen(content);
        char *cp = (char *)malloc(clen + 1);
        if (cp) {
            memcpy(cp, content, clen + 1);
            int cap = 1;
            for (size_t i = 0; i < clen; ++i)
                if (cp[i] == '\n') ++cap;
            typedef struct {
                int line;
                const char *a;
                const char *b;
                int ir_id; /* op IR exacta (modo IR=exacto); -1 = sintetica */
            } GbRow;
            typedef struct {
                const char *label;
                const char *kind;
                int size;
                const char *name;
            } FrRow;
            typedef struct {
                int line;
                const char *text;
            } IrRow;
            GbRow *src = (GbRow *)malloc(sizeof(GbRow) * cap);
            GbRow *asml = (GbRow *)malloc(sizeof(GbRow) * cap);
            FrRow *frm = (FrRow *)malloc(sizeof(FrRow) * cap);
            IrRow *irr = (IrRow *)malloc(sizeof(IrRow) * cap);
            IrRow *jrr = (IrRow *)malloc(sizeof(IrRow) * cap); /* mapa exacto: line=id */
            int ns = 0, na = 0, nf = 0, nir = 0, njr = 0;
            const char *hdr = NULL;
            if (src && asml && frm && irr && jrr) {
                char *p = strchr(cp, '\n');
                p = p ? p + 1 : cp; /* saltar el sentinela */
                while (*p) {
                    char *nl = strchr(p, '\n');
                    if (nl) *nl = 0;
                    char kind = p[0];
                    char *f1 = strchr(p, 0x1F);
                    if (f1) {
                        *f1 = 0;
                        char *c1 = f1 + 1, *c2 = NULL, *c3 = NULL, *c4 = NULL;
                        char *f2 = strchr(c1, 0x1F);
                        if (f2) {
                            *f2 = 0;
                            c2 = f2 + 1;
                            char *f3 = strchr(c2, 0x1F);
                            if (f3) {
                                *f3 = 0;
                                c3 = f3 + 1;
                                char *f4 = strchr(c3, 0x1F);
                                if (f4) { *f4 = 0; c4 = f4 + 1; }
                            }
                        }
                        if (kind == 'H') hdr = c1;
                        else if (kind == 'S') {
                            src[ns].line = atoi(c1);
                            src[ns].a = c2 ? c2 : "";
                            src[ns].b = NULL;
                            ++ns;
                        } else if (kind == 'A') {
                            /* A\x1f linea \x1f addr \x1f ir_id \x1f texto */
                            asml[na].line = atoi(c1);
                            asml[na].a = c2 ? c2 : "";
                            asml[na].ir_id =
                                c3 ? (int)strtoul(c3, NULL, 10) : -1;
                            if ((unsigned)asml[na].ir_id == 0xFFFFFFFFu)
                                asml[na].ir_id = -1;
                            asml[na].b = c4 ? c4 : "";
                            ++na;
                        } else if (kind == 'F') {
                            /* F\x1f label \x1f kind \x1f size \x1f name */
                            frm[nf].label = c1 ? c1 : "";
                            frm[nf].kind = c2 ? c2 : "";
                            frm[nf].size = c3 ? atoi(c3) : 0;
                            frm[nf].name = c4 ? c4 : "";
                            ++nf;
                        } else if (kind == 'I') {
                            /* I\x1f linea \x1f op IR */
                            irr[nir].line = atoi(c1);
                            irr[nir].text = c2 ? c2 : "";
                            ++nir;
                        } else if (kind == 'J') {
                            /* J\x1f ir_id \x1f op IR (mapa exacto) */
                            jrr[njr].line =
                                c1 ? (int)strtoul(c1, NULL, 10) : -1;
                            jrr[njr].text = c2 ? c2 : "";
                            ++njr;
                        }
                    }
                    if (!nl) break;
                    p = nl + 1;
                }

                /* Layout de las dos columnas.  El ancho de la col. fuente es
                 * configurable por el usuario (arrastrar el separador). */
                int avail = (w - 2 * pad) / cw;
                if (avail < 10) avail = 10;
                int sp = h->gb_split_pct;
                if (sp <= 0) sp = 45; /* default */
                if (sp < 20) sp = 20;
                if (sp > 75) sp = 75;
                int src_cols = avail * sp / 100;
                if (src_cols < 6) src_cols = 6;
                if (src_cols > avail - 6) src_cols = avail - 6;
                int sepx = body_x + src_cols * cw + cw / 2;
                int asm_x = body_x + (src_cols + 2) * cw;
                int hrows = hdr ? 1 : 0;
                int body_rows = rows - hrows;
                if (body_rows < 1) body_rows = 1;
                int body_top = by + hrows * lh;
                /* Reservar una banda inferior para el stack frame (titulo +
                 * filas), sin comerse mas de 1/3 del cuerpo. */
                int frame_h = 0;
                if (nf > 0 && e->settings.hover_frame) {
                    frame_h = nf + 1; /* +1 titulo */
                    int maxf = body_rows / 3;
                    if (maxf < 2) maxf = 2;
                    if (frame_h > maxf) frame_h = maxf;
                    body_rows -= frame_h;
                    if (body_rows < 1) { body_rows = 1; frame_h = rows - hrows - 1; }
                }
                /* Modo IR "panel" (2): banda inferior con las ops IR de la
                 * linea activa (apuntada/fijada). */
                int ir_h = 0;
                if (e->settings.hover_ir_mode == 2 && nir > 0) {
                    ir_h = 6; /* titulo + hasta 5 ops */
                    int maxi = body_rows / 3;
                    if (maxi < 2) maxi = 2;
                    if (ir_h > maxi) ir_h = maxi;
                    body_rows -= ir_h;
                    if (body_rows < 1) body_rows = 1;
                }

                /* --- Construir flechas de salto -----------------------------
                 * Nativo (JIT/AOT): destino por 0xADDR contra la columna addr.
                 * Bytecode (.vel): destino por ETIQUETA (jmp/cmpjmp/decjnz
                 * <label> -> fila "<label>:").  Se construyen ANTES del render
                 * para reservar el canalon y desplazar el codigo. */
                int is_native = (na > 0 && asml[0].a && asml[0].a[0]);
                typedef struct { int from, to, lane; } GbArrow;
                GbArrow *arw = (GbArrow *)malloc(sizeof(GbArrow) * (na + 1));
                int narw = 0;
                if (arw) {
                    for (int i = 0; i < na; ++i) {
                        const char *t = asml[i].b;
                        if (!t || !t[0]) continue;
                        int to = -1;
                        if (is_native) {
                            if (t[0] != 'j') continue; /* solo j* (no call) */
                            const char *hx = strstr(t, "0x");
                            if (!hx) continue;
                            long tgt = strtol(hx + 2, NULL, 16);
                            for (int k = 0; k < na; ++k)
                                if (asml[k].a && asml[k].a[0] &&
                                    strtol(asml[k].a, NULL, 16) == tgt) {
                                    to = k;
                                    break;
                                }
                        } else {
                            /* jmp / jmp.jXX / cmpjmp.cc / decjnz <label> */
                            if (strncmp(t, "jmp", 3) != 0 &&
                                strncmp(t, "cmpjmp", 6) != 0 &&
                                strncmp(t, "decjnz", 6) != 0)
                                continue;
                            int L = (int)strlen(t), e2 = L;
                            while (e2 > 0 && (t[e2 - 1] == ' ' || t[e2 - 1] == '\t'))
                                --e2;
                            int b2 = e2;
                            while (b2 > 0 && t[b2 - 1] != ' ' && t[b2 - 1] != '\t' &&
                                   t[b2 - 1] != ',')
                                --b2;
                            int ll = e2 - b2;
                            if (ll <= 0 || ll > 126) continue;
                            char lbl[128];
                            memcpy(lbl, t + b2, ll);
                            lbl[ll] = 0;
                            /* El destino suele venir como @Absolute("code.X")
                             * o @StringRef("code.X"); extraer la etiqueta X. */
                            char *dot = strstr(lbl, "code.");
                            if (dot) {
                                char *sX = dot + 5;
                                char *eX = sX;
                                while (*eX && *eX != '"' && *eX != ')') ++eX;
                                ll = (int)(eX - sX);
                                memmove(lbl, sX, ll);
                                lbl[ll] = 0;
                            }
                            for (int k = 0; k < na; ++k) {
                                const char *bt = asml[k].b;
                                if (!bt) continue;
                                while (*bt == ' ' || *bt == '\t') ++bt;
                                size_t bl2 = strlen(bt);
                                if (bl2 == (size_t)ll + 1 && bt[bl2 - 1] == ':' &&
                                    strncmp(bt, lbl, ll) == 0) {
                                    to = k;
                                    break;
                                }
                            }
                        }
                        if (to < 0 || to == i) continue;
                        arw[narw].from = i;
                        arw[narw].to = to;
                        arw[narw].lane = 0;
                        ++narw;
                    }
                    for (int a = 0; a < narw; ++a) { /* carriles greedy */
                        int lane = 0, clash = 1;
                        int a0 = arw[a].from < arw[a].to ? arw[a].from : arw[a].to;
                        int a1 = arw[a].from < arw[a].to ? arw[a].to : arw[a].from;
                        while (clash && lane < 4) {
                            clash = 0;
                            for (int b = 0; b < a; ++b) {
                                if (arw[b].lane != lane) continue;
                                int b0 = arw[b].from < arw[b].to ? arw[b].from
                                                                 : arw[b].to;
                                int b1 = arw[b].from < arw[b].to ? arw[b].to
                                                                 : arw[b].from;
                                if (!(a1 < b0 || b1 < a0)) { clash = 1; break; }
                            }
                            if (clash) ++lane;
                        }
                        arw[a].lane = lane;
                    }
                }
                /* Layout: canalon de flechas (gw columnas) entre el prefijo
                 * (Lnnn[+offset]) y el codigo, solo si hay flechas. */
                int gw = (narw > 0 && e->settings.hover_arrows) ? 4 : 0;
                int code_col = (is_native ? 11 : 5) + gw; /* col. del codigo */
                int gx = asm_x + (is_native ? 11 : 5) * cw; /* base del canalon */

                /* Publicar geometria + estado godbolt para el input (click-
                 * select de linea + arrastre del separador). */
                h->gb_active = 1;
                h->gb_sepx = sepx;
                h->gb_body_top = body_top;
                h->gb_lh = lh;
                h->gb_left_n = 0;
                h->gb_right_n = 0;

                /* Barra de toggles (derecha) + cabecera de stats (izquierda,
                 * recortada para no solaparse con los chips).  ui_toggle es el
                 * componente generico; valores host-globales en Settings. */
                {
                    static const char *kIrName[5] = {"IR:off", "IR:grupo",
                                                     "IR:panel", "IR:3col",
                                                     "IR:exacto"};
                    struct { UiId id; const char *l; int on; } tg[] = {
                        {UI_HOVER_OPT_IR,
                         kIrName[e->settings.hover_ir_mode % 5],
                         e->settings.hover_ir_mode != 0},
                        {UI_HOVER_OPT_NOTES, "notas", e->settings.hover_notes},
                        {UI_HOVER_OPT_FRAME, "frame", e->settings.hover_frame},
                        {UI_HOVER_OPT_ARROWS, "flechas",
                         e->settings.hover_arrows},
                    };
                    /* 1) posiciones (derecha -> izquierda). */
                    int chip_x[4], tx = x + w - pad;
                    for (int ti = 0; ti < 4; ++ti) {
                        int tw = 0, th = 0;
                        TTF_GetStringSize(e->font, tg[ti].l, 0, &tw, &th);
                        int chipw = tw + cw + 4;
                        tx -= chipw + 4;
                        chip_x[ti] = tx;
                    }
                    int chips_left = chip_x[3]; /* el mas a la izquierda */
                    /* 2) cabecera recortada hasta antes de los chips. */
                    if (hdr) {
                        int maxc = (chips_left - body_x) / cw - 1;
                        if (maxc < 0) maxc = 0;
                        char hb[256];
                        int hl = (int)strlen(hdr);
                        if (hl > maxc) hl = maxc;
                        if (hl > 255) hl = 255;
                        memcpy(hb, hdr, hl);
                        hb[hl] = 0;
                        draw_text(e, hb, body_x, by, 0x88, 0x8C, 0x99);
                    }
                    /* 3) chips. */
                    for (int ti = 0; ti < 4; ++ti)
                        ui_toggle(e, tg[ti].id, chip_x[ti], by - 1, tg[ti].l,
                                  tg[ti].on);
                }
                /* Separador vertical (resaltado si se esta arrastrando). */
                set_color_c(r, h->gb_split_drag ? e->theme.col_tab_accent
                                                : e->theme.col_tabbar_sep);
                fill_rect(r, sepx, body_y0, h->gb_split_drag ? 2 : 1,
                          body_h - 2);

                /* Linea .vex bajo el raton (hover, en cualquier columna). */
                int hover_line = -1;
                if (h->last_mx >= x && h->last_mx < x + w &&
                    h->last_my >= body_top && h->last_my < y + hh) {
                    int vr = (h->last_my - body_top) / lh;
                    if (h->last_mx < sepx) {
                        if (vr >= 0 && vr < ns) hover_line = src[vr].line;
                    } else {
                        int ai = (h->scroll < 0 ? 0 : h->scroll) + vr;
                        if (ai >= 0 && ai < na) hover_line = asml[ai].line;
                    }
                }
                int sel_line = h->gb_sel_line; /* linea fijada al click */

                /* Helper local de highlight de fila: bg si correla (hover o
                 * fijada); barra de acento mas marcada si esta FIJADA. */
                /* Columna fuente (sin scroll) -- con resaltado de sintaxis. */
                for (int i = 0; i < ns && i < body_rows; ++i) {
                    int yy = by + (hrows + i) * lh;
                    int ln = src[i].line;
                    int is_sel = (ln != 0 && ln == sel_line);
                    int is_hov = (ln != 0 && ln == hover_line);
                    /* Caja AGRUPADA: una sola caja alrededor del run contiguo
                     * de filas con la misma linea (no una cajita por fila). */
                    if (is_sel || is_hov) {
                        int bl = is_sel ? sel_line : hover_line;
                        int prev = (i > 0) ? src[i - 1].line : -999;
                        int next = (i + 1 < ns && i + 1 < body_rows)
                                       ? src[i + 1].line
                                       : -999;
                        int rw = sepx - (x + 1);
                        if (is_sel) { /* fondo solido (texto nitido) */
                            set_color_c(r, e->theme.col_tab_active);
                            fill_rect(r, x + 1, yy - 1, rw, lh);
                        }
                        set_color_c(r, e->theme.col_tab_accent);
                        fill_rect(r, x + 1, yy - 1, is_sel ? 3 : 2, lh); /* barra */
                        if (is_sel) { /* bordes solo en los extremos del run */
                            if (prev != bl) fill_rect(r, x + 1, yy - 1, rw, 1);
                            if (next != bl)
                                fill_rect(r, x + 1, yy + lh - 2, rw, 1);
                        }
                    }
                    char pre[16];
                    snprintf(pre, sizeof pre, "L%-4d", ln);
                    draw_text(e, pre, body_x, yy, 0x6C, 0xB0, 0xE0);
                    char tb[1024];
                    int tc = (int)strlen(src[i].a);
                    int lim = src_cols - 5;
                    if (lim < 0) lim = 0;
                    if (tc > lim) tc = lim;
                    if (tc > 1023) tc = 1023;
                    memcpy(tb, src[i].a, tc);
                    tb[tc] = 0;
                    draw_code_line(e, tb, tc, body_x + 5 * cw, yy);
                    if (i < HOVER_GB_ROWS) h->gb_left_lines[i] = ln;
                    if (i + 1 > h->gb_left_n) h->gb_left_n = i + 1;
                }

                /* Columna nativo (scrollable) -- con resaltado de sintaxis.
                 * `row` es la fila VISUAL (en modo IR=grupo se intercalan
                 * cabeceras IR, que tambien consumen filas). */
                int skip = h->scroll < 0 ? 0 : h->scroll;
                int row = 0;
                int grp = (e->settings.hover_ir_mode == 1);
                int exa = (e->settings.hover_ir_mode == 4);
                int prev_grp_line = -1;
                int prev_exa_id = -2;
                /* Banda zebra por grupo IR: la cabecera y SU asm comparten el
                 * mismo tinte, alternando por grupo -> deja claro que asm
                 * pertenece a que op IR (arriba/abajo). */
                int ir_par = 0;
                /* Fila VISUAL de cada asm (para que las flechas sigan alineadas
                 * aunque el modo grupo intercale cabeceras IR).  -1 = no
                 * visible este frame. */
                int *asml_vrow = (int *)malloc(sizeof(int) * (na + 1));
                if (asml_vrow)
                    for (int q = 0; q <= na; ++q) asml_vrow[q] = -1;
                int last_vis_idx = skip - 1;
                for (int i = skip; i < na && row < body_rows; ++i) {
                    int ln = asml[i].line;
                    /* Modo IR=grupo: cabecera(s) IR antes del 1er asm de cada
                     * grupo de linea. */
                    if (grp && ln != 0 && ln != prev_grp_line) {
                        prev_grp_line = ln;
                        ir_par ^= 1;
                        for (int q = 0; q < nir && row < body_rows; ++q) {
                            if (irr[q].line != ln) continue;
                            int hyy = by + (hrows + row) * lh;
                            if (ir_par) {
                                set_color(r, 0x26, 0x2B, 0x35, 0xFF);
                                fill_rect(r, sepx + 1, hyy - 1,
                                          (x + w - 1) - (sepx + 1), lh);
                            }
                            draw_text(e, "IR", asm_x, hyy, 0x70, 0x82, 0x70);
                            /* El op IR empieza tras el canalon de flechas (en
                             * code_col), dejando el canalon libre para que las
                             * lineas verticales de las flechas no lo pisen. */
                            draw_code_line(e, irr[q].text,
                                           (int)strlen(irr[q].text),
                                           asm_x + code_col * cw, hyy);
                            if (row < HOVER_GB_ROWS) h->gb_right_lines[row] = ln;
                            h->gb_right_n = row + 1;
                            ++row;
                        }
                        if (row >= body_rows) break;
                    }
                    /* Modo IR=exacto: cabecera con la op IR EXACTA antes del
                     * primer asm de cada cambio de ir_id (correlacion 1:1). */
                    if (exa && asml[i].ir_id >= 0 &&
                        asml[i].ir_id != prev_exa_id) {
                        prev_exa_id = asml[i].ir_id;
                        ir_par ^= 1;
                        const char *opt = NULL;
                        for (int q = 0; q < njr; ++q)
                            if (jrr[q].line == asml[i].ir_id) {
                                opt = jrr[q].text;
                                break;
                            }
                        if (opt && row < body_rows) {
                            int hyy = by + (hrows + row) * lh;
                            if (ir_par) {
                                set_color(r, 0x26, 0x2B, 0x35, 0xFF);
                                fill_rect(r, sepx + 1, hyy - 1,
                                          (x + w - 1) - (sepx + 1), lh);
                            }
                            draw_text(e, "IR", asm_x, hyy, 0x70, 0x82, 0x70);
                            draw_code_line(e, opt, (int)strlen(opt),
                                           asm_x + code_col * cw, hyy);
                            if (row < HOVER_GB_ROWS) h->gb_right_lines[row] = ln;
                            h->gb_right_n = row + 1;
                            ++row;
                            if (row >= body_rows) break;
                        }
                    }
                    int yy = by + (hrows + row) * lh;
                    int is_sel = (ln != 0 && ln == sel_line);
                    int is_hov = (ln != 0 && ln == hover_line);
                    int rw = (x + w - 1) - (sepx + 1);
                    /* Banda zebra del grupo IR (mismo tinte que su cabecera). */
                    if ((grp || exa) && ir_par && !is_sel) {
                        set_color(r, 0x26, 0x2B, 0x35, 0xFF);
                        fill_rect(r, sepx + 1, yy - 1, rw, lh);
                    }
                    /* Caja AGRUPADA del run contiguo (igual que la col. fuente). */
                    if (is_sel || is_hov) {
                        int bl = is_sel ? sel_line : hover_line;
                        int prev = (i > skip) ? asml[i - 1].line : -999;
                        int next = (i + 1 < na && row + 1 < body_rows)
                                       ? asml[i + 1].line
                                       : -999;
                        if (is_sel) {
                            set_color_c(r, e->theme.col_tab_active);
                            fill_rect(r, sepx + 1, yy - 1, rw, lh);
                        }
                        set_color_c(r, e->theme.col_tab_accent);
                        fill_rect(r, sepx + 1, yy - 1, is_sel ? 3 : 2, lh);
                        if (is_sel) {
                            if (prev != bl) fill_rect(r, sepx + 1, yy - 1, rw, 1);
                            if (next != bl)
                                fill_rect(r, sepx + 1, yy + lh - 2, rw, 1);
                        }
                    }
                    char pre[16];
                    snprintf(pre, sizeof pre, "L%-4d", ln);
                    draw_text(e, pre, asm_x, yy, 0x6C, 0xB0, 0xE0);
                    /* Columna +offset solo si hay addr (JIT/AOT); el bytecode
                     * no tiene offset de byte -> el codigo va justo tras Lnnn. */
                    if (asml[i].a && asml[i].a[0]) {
                        char ab[24];
                        snprintf(ab, sizeof ab, "+%s", asml[i].a);
                        draw_text(e, ab, asm_x + 5 * cw, yy, 0x70, 0x74, 0x80);
                    }
                    /* code_col incluye el canalon de flechas (gw cols) cuando
                     * lo hay; igual para nativo (tras Lnnn+offset) y bytecode
                     * (tras Lnnn). */
                    int code_x = asm_x + code_col * cw;
                    /* Nativo (JIT/AOT, con offset) -> coloreado x86; bytecode
                     * (.vel, sin offset) -> sintaxis Vex.  En bytecode, las
                     * etiquetas (`X:`) van a la izquierda y su codigo se tabula
                     * debajo, para distinguir que instrucciones caen en cada
                     * etiqueta. */
                    if (asml[i].a && asml[i].a[0]) {
                        draw_asm_line(e, asml[i].b, code_x, yy,
                                      e->settings.hover_notes);
                    } else {
                        const char *bt = asml[i].b;
                        int blen = (int)strlen(bt);
                        int is_label = (blen > 0 && bt[blen - 1] == ':');
                        int ind = is_label ? 0 : 2; /* tabular instrucciones */
                        draw_code_line(e, bt, blen, code_x + ind * cw, yy);
                    }
                    if (row < HOVER_GB_ROWS) h->gb_right_lines[row] = ln;
                    h->gb_right_n = row + 1;
                    if (asml_vrow) asml_vrow[i] = row; /* fila visual de este asm */
                    last_vis_idx = i;
                    ++row;
                }

                /* --- Dibujar flechas de salto (construidas arriba) ---------
                 * Conector vertical en el canalon desde el salto hasta su
                 * destino, con cabeza de flecha; acento si el salto o el
                 * destino estan en la linea .vex apuntada/fijada. */
                if (arw && e->settings.hover_arrows) {
                    for (int a = 0; a < narw; ++a) {
                        int fr = arw[a].from, to = arw[a].to;
                        /* Fila visual de cada extremo (NULL/fuera = -1).  Asi
                         * las flechas siguen alineadas aunque el modo grupo
                         * intercale cabeceras IR. */
                        int fr_vis = (asml_vrow && fr >= skip &&
                                      fr <= last_vis_idx)
                                         ? asml_vrow[fr]
                                         : -1;
                        int to_vis = (asml_vrow && to >= skip &&
                                      to <= last_vis_idx)
                                         ? asml_vrow[to]
                                         : -1;
                        /* clamp: fuera por arriba (-1) o por abajo (body_rows) */
                        int fr_row =
                            fr_vis >= 0 ? fr_vis : (fr < skip ? -1 : body_rows);
                        int to_row =
                            to_vis >= 0 ? to_vis : (to < skip ? -1 : body_rows);
                        int lo = fr_row < to_row ? fr_row : to_row;
                        int hi = fr_row > to_row ? fr_row : to_row;
                        if (hi < 0 || lo >= body_rows) continue; /* ambos fuera */
                        int cy0 = lo < 0 ? 0 : lo;
                        int cy1 = hi >= body_rows ? body_rows - 1 : hi;
                        int y0 = by + (hrows + cy0) * lh + lh / 2;
                        int y1 = by + (hrows + cy1) * lh + lh / 2;
                        int ax = gx + arw[a].lane * 3;
                        int hot = 0;
                        int fl = asml[fr].line, tl = asml[to].line;
                        if (sel_line != 0 && (fl == sel_line || tl == sel_line))
                            hot = 1;
                        if (hover_line != 0 &&
                            (fl == hover_line || tl == hover_line))
                            hot = 1;
                        if (hot)
                            set_color_c(r, e->theme.col_tab_accent);
                        else
                            set_color(r, 0x6A, 0x8C, 0xB8, 0xFF);
                        fill_rect(r, ax, y0, 1, y1 - y0 + 1); /* vertical */
                        if (to_vis >= 0) { /* cabeza en destino (visible) */
                            int ty = by + (hrows + to_vis) * lh + lh / 2;
                            fill_rect(r, ax, ty, cw, 1);
                            fill_rect(r, ax + cw - 3, ty - 2, 1, 5);
                            fill_rect(r, ax + cw - 4, ty - 1, 1, 3);
                        }
                        if (fr_vis >= 0) { /* tick en origen (visible) */
                            int fy = by + (hrows + fr_vis) * lh + lh / 2;
                            fill_rect(r, ax, fy, cw / 2, 1);
                        }
                    }
                    free(arw);
                }
                free(asml_vrow);

                /* Banda IR (modo panel=2): ops IR de la linea activa
                 * (fijada por click, o apuntada por el raton). */
                if (ir_h > 0) {
                    int al = (sel_line > 0) ? sel_line : hover_line;
                    int iy0 = by + (hrows + body_rows) * lh;
                    set_color_c(r, e->theme.col_tabbar_sep);
                    fill_rect(r, x + 1, iy0 + 1, w - 2, 1);
                    char title[64];
                    if (al > 0)
                        snprintf(title, sizeof title, "IR (linea %d)", al);
                    else
                        snprintf(title, sizeof title, "IR (apunta una linea)");
                    draw_text(e, title, body_x, iy0 + 3, 0x88, 0x8C, 0x99);
                    int shown = ir_h - 1, drawn = 0;
                    for (int i = 0; i < nir && drawn < shown; ++i) {
                        if (al <= 0 || irr[i].line != al) continue;
                        int yy = iy0 + (1 + drawn) * lh + 3;
                        /* Resaltado de sintaxis IR (%valores, mnemonicos,
                         * numeros) via draw_code_line. */
                        draw_code_line(e, irr[i].text, (int)strlen(irr[i].text),
                                       body_x + 2 * cw, yy);
                        ++drawn;
                    }
                }

                /* Banda inferior: stack frame (debug-info).  Una fila por
                 * slot: label  [kind]  sz=N  nombre.  Color segun kind. */
                if (frame_h > 0) {
                    int fy0 = by + (hrows + body_rows + ir_h) * lh;
                    /* Separador horizontal + titulo. */
                    set_color_c(r, e->theme.col_tabbar_sep);
                    fill_rect(r, x + 1, fy0 + 1, w - 2, 1);
                    draw_text(e, "stack frame", body_x, fy0 + 3, 0x88, 0x8C,
                              0x99);
                    int shown = frame_h - 1; /* filas de datos visibles */
                    for (int i = 0; i < nf && i < shown; ++i) {
                        int yy = fy0 + (1 + i) * lh + 3;
                        const char *k = frm[i].kind;
                        /* color por kind: saved/retaddr=azul, local=verde,
                         * reserved=gris. */
                        int cr = 0x80, cg = 0x86, cb = 0x90; /* reserved */
                        if (strcmp(k, "local") == 0) { cr = 0x9E; cg = 0xD0; cb = 0x7A; }
                        else if (strcmp(k, "saved") == 0) { cr = 0x6C; cg = 0xB0; cb = 0xE0; }
                        else if (strcmp(k, "retaddr") == 0) { cr = 0xD0; cg = 0xA0; cb = 0x6C; }
                        char fb[256];
                        snprintf(fb, sizeof fb, "%-10s %-8s sz=%-3d %s",
                                 frm[i].label, k, frm[i].size, frm[i].name);
                        draw_text(e, fb, body_x, yy, cr, cg, cb);
                    }
                }
            }
            free(src);
            free(asml);
            free(frm);
            free(irr);
            free(jrr);
            free(cp);
        }
    } else if (h->active_tab == 0) {
        /* Pestana Doc: Markdown + HTML 1.0 minimo.  Bloque: cabeceras (# /
         * <h1-3>), vinetas (- * + / <li>), fences de codigo (``` / <pre>);
         * inline via draw_md_inline.  Se cuentan TODAS las lineas para el
         * scroll (manteniendo el estado de fence aunque la linea este oculta). */
        const char *p = content;
        int skip = h->scroll < 0 ? 0 : h->scroll;
        int row = 0, idx = 0, in_fence = 0;
        char linebuf[2048];
        while (*p && row < rows) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            size_t cpy = len < sizeof(linebuf) - 1 ? len : sizeof(linebuf) - 1;
            memcpy(linebuf, p, cpy);
            linebuf[cpy] = 0;
            char *t = linebuf;
            while (*t == ' ' || *t == '\t') t++;
            int is_fence = (strncmp(t, "```", 3) == 0) ||
                           (strncmp(t, "<pre", 4) == 0) ||
                           (strncmp(t, "</pre", 5) == 0);
            if (idx++ < skip) { /* oculta por scroll: solo actualizar fence */
                if (is_fence) in_fence = !in_fence;
                if (!nl) break;
                p = nl + 1;
                continue;
            }
            int yy = by + row * lh;
            if (is_fence) {
                in_fence = !in_fence;
                set_color_c(r, e->theme.col_tabbar_sep);
                fill_rect(r, body_x, yy + lh / 2, 30 * cw, 1);
            } else if (in_fence) {
                draw_code_line(e, linebuf, (int)cpy, body_x, yy);
            } else {
                int hdr = 0, bullet = 0;
                char *body = t;
                if (t[0] == '#') {
                    while (*body == '#') body++;
                    while (*body == ' ') body++;
                    hdr = 1;
                } else if (t[0] == '<' && t[1] == 'h' && t[2] >= '1' &&
                           t[2] <= '3') {
                    char *gt = strchr(t, '>');
                    body = gt ? gt + 1 : t;
                    hdr = 1;
                } else if ((t[0] == '-' || t[0] == '*' || t[0] == '+') &&
                           t[1] == ' ') {
                    body = t + 2;
                    bullet = 1;
                } else if (strncmp(t, "<li>", 4) == 0) {
                    body = t + 4;
                    bullet = 1;
                }
                int bx = body_x;
                if (bullet) {
                    draw_text(e, "\xe2\x80\xa2", body_x, yy, 0x88, 0x8C, 0x99);
                    bx = body_x + 2 * cw;
                }
                int blen = (int)strlen(body);
                if (hdr)
                    draw_md_inline(e, body, blen, bx, yy, 0x4E, 0xC9, 0xB0);
                else
                    draw_md_inline(e, body, blen, bx, yy, 0xCC, 0xCC, 0xD4);
            }
            row++;
            if (!nl) break;
            p = nl + 1;
        }
    } else {
        const char *p = content;
        int skip = h->scroll < 0 ? 0 : h->scroll;
        int row = 0;
        char linebuf[2048];
        while (*p && row < rows) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (skip > 0) {
                skip--;
            } else {
                size_t cpy =
                    len < sizeof(linebuf) - 1 ? len : sizeof(linebuf) - 1;
                memcpy(linebuf, p, cpy);
                linebuf[cpy] = 0;
                draw_code_line(e, linebuf, (int)cpy, body_x, by + row * lh);
                row++;
            }
            if (!nl) break;
            p = nl + 1;
        }
    }
    SDL_SetRenderClipRect(r, NULL);
    free(ir_copy); /* copia mutable de la vista IR multi (si la hubo) */

    /* Tirador de redimension (esquina inferior derecha): tres pips diagonales. */
    set_color_c(r, e->theme.col_tabbar_sep);
    for (int k = 0; k < 3; ++k) {
        int off = 4 + k * 4;
        fill_rect(r, x + w - off, y + hh - 4, 2, 2);
    }
    (void)rsz;
}

/**
 * @brief Reduce el rect @p r de una hoja a la sub-zona que ocupara la pestana
 *        segun la zona de drop @p zone (mitad/banda de borde, o el rect entero
 *        para CENTER).  Sirve para dibujar la guia visual del destino.
 */
static DockRect drag_zone_rect(DockRect r, int zone) {
    switch (zone) {
    case DOCK_DZ_LEFT:   r.w = r.w / 2; break;
    case DOCK_DZ_RIGHT:  r.x += r.w - r.w / 2; r.w = r.w / 2; break;
    case DOCK_DZ_TOP:    r.h = r.h / 2; break;
    case DOCK_DZ_BOTTOM: r.y += r.h - r.h / 2; r.h = r.h / 2; break;
    default: break; /* CENTER: el rect completo de la hoja */
    }
    return r;
}

void render_float_dock_guide(Editor *e) {
    /* solo durante el arrastre de un flotante (por el titulo) con destino */
    if (e->float_drag < 0 || e->float_resizing) return;
    if (e->float_dock_target_group < 0 || e->float_dock_zone == DOCK_DZ_NONE)
        return;
    SDL_Renderer *r = e->renderer;

    /* rect de la hoja destino (la del group_id anotado durante el arrastre) */
    DockRect area = editor_dock_area(e);
    DockLeafRect leaves[DOCK_MAX_LEAVES];
    int n = dock_compute_leaf_rects(&e->dock, area, leaves, DOCK_MAX_LEAVES);
    DockRect leaf_rect;
    int found = 0;
    for (int i = 0; i < n; i++)
        if (leaves[i].group_id == e->float_dock_target_group) {
            leaf_rect = leaves[i].rect;
            found = 1;
            break;
        }
    if (!found) return;

    /* mismo overlay translucido de zona que el arrastre de pestanas */
    DockRect z = drag_zone_rect(leaf_rect, e->float_dock_zone);
    Color acc = e->theme.col_tab_accent;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    set_color(r, acc.r, acc.g, acc.b, 0x40); /* relleno translucido */
    fill_rect(r, z.x, z.y, z.w, z.h);
    set_color(r, acc.r, acc.g, acc.b, 0xC0); /* borde mas opaco */
    stroke_rect(r, z.x, z.y, z.w, z.h);
    stroke_rect(r, z.x + 1, z.y + 1, z.w - 2, z.h - 2);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void render_tab_drag(Editor *e) {
    if (!e->dragging_tab || e->drag_tab < 0) return; /* sin arrastre real */
    SDL_Renderer *r = e->renderer;

    /* Modo flotante: con Ctrl pulsado, soltar DESPRENDE la pestana a un panel
     * flotante en vez de acoplarla; la guia lo indica con un marco fantasma del
     * tamano por defecto del flotante centrado en el cursor, sin overlay de zona. */
    int float_mode = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
    if (float_mode) {
        Rect b = editor_float_bounds(e);
        FloatPanel ghost;
        ghost.group_id = -1;
        ghost.rect.w = FLOAT_DEFAULT_W;
        ghost.rect.h = FLOAT_DEFAULT_H;
        ghost.rect = float_clamp_move(ghost.rect, e->drag_mx - FLOAT_DEFAULT_W / 2,
                                      e->drag_my - FLOAT_TITLEBAR_H / 2, b);
        Color acc = e->theme.col_tab_accent;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        set_color(r, acc.r, acc.g, acc.b, 0x30);
        fill_rect(r, ghost.rect.x, ghost.rect.y, ghost.rect.w, ghost.rect.h);
        set_color(r, acc.r, acc.g, acc.b, 0xC0);
        stroke_rect(r, ghost.rect.x, ghost.rect.y, ghost.rect.w, ghost.rect.h);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    /* Objetivo de BARRA (reordenar/insertar): la barra manda sobre el dock, asi
     * que se dibuja una linea vertical de insercion y NO el overlay de zona. */
    int bar_target = !float_mode && e->tab_reorder_group >= 0;
    if (bar_target) {
        Color acc = e->theme.col_tab_accent;
        int lx = e->tab_reorder_x;
        int ly = e->tab_reorder_bar_y;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        set_color(r, acc.r, acc.g, acc.b, 0xFF);
        fill_rect(r, lx - 1, ly, 2, TAB_BAR_HEIGHT); /* linea fina de insercion */
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    /* zona destino bajo el cursor */
    int group = -1, zone = DOCK_DZ_NONE;
    DockRect leaf_rect;
    int over = !float_mode && !bar_target &&
               editor_drag_target(e, e->drag_mx, e->drag_my, &group, &zone,
                                  &leaf_rect);

    /* overlay translucido de la zona destino (si el cursor esta sobre una hoja
     * y la zona no es nula). */
    if (over && zone != DOCK_DZ_NONE) {
        DockRect z = drag_zone_rect(leaf_rect, zone);
        Color acc = e->theme.col_tab_accent;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        set_color(r, acc.r, acc.g, acc.b, 0x40); /* relleno translucido */
        fill_rect(r, z.x, z.y, z.w, z.h);
        set_color(r, acc.r, acc.g, acc.b, 0xC0); /* borde mas opaco */
        stroke_rect(r, z.x, z.y, z.w, z.h);
        stroke_rect(r, z.x + 1, z.y + 1, z.w - 2, z.h - 2);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    /* "fantasma" del titulo de la pestana junto al cursor */
    if (e->drag_tab < e->tab_count) {
        const char *name = last_path_component(
            e->tabs[e->drag_tab].filepath[0] ? e->tabs[e->drag_tab].filepath
                                             : "Sin título");
        int tw = 0, th = 0;
        TTF_GetStringSize(e->font, name, 0, &tw, &th);
        int gw = tw + TAB_PAD * 2;
        int gh = TAB_BAR_HEIGHT;
        int gx = e->drag_mx + 12; /* ligeramente a la derecha del cursor */
        int gy = e->drag_my - gh / 2;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        set_color_c(r, e->theme.col_tab_active);
        SDL_SetRenderDrawColor(r, e->theme.col_tab_active.r,
                               e->theme.col_tab_active.g,
                               e->theme.col_tab_active.b, 0xE0);
        fill_rect(r, gx, gy, gw, gh);
        Color acc = e->theme.col_tab_accent;
        set_color(r, acc.r, acc.g, acc.b, 0xFF);
        fill_rect(r, gx, gy, gw, TAB_ACCENT_H); /* acento superior */
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        int ty = gy + (gh - e->font_size) / 2;
        draw_text(e, name, gx + TAB_PAD, ty, 0xCC, 0xCC, 0xDD);

        /* en modo flotante, una etiqueta bajo el fantasma lo deja claro */
        if (float_mode)
            draw_text_c(e, "flotante", gx + TAB_PAD, gy + gh + 2,
                        e->theme.col_tab_accent);
    }
}

void render_drag_window_highlight(Editor *e) {
    if (!e->drag_hover_highlight) return; /* no es la ventana destino del arrastre */
    SDL_Renderer *r = e->renderer;
    Color acc = e->theme.col_tab_accent;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    /* velo translucido sobre toda la ventana para sugerir "soltar aqui" */
    set_color(r, acc.r, acc.g, acc.b, 0x18);
    fill_rect(r, 0, 0, e->win_w, e->win_h);
    /* marco grueso de acento en el borde de la ventana */
    set_color(r, acc.r, acc.g, acc.b, 0xFF);
    for (int i = 0; i < 3; i++)
        stroke_rect(r, i, i, e->win_w - 2 * i, e->win_h - 2 * i);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}
