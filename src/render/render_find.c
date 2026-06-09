/**
 * @file render_find.c
 * @brief Dibujado de la barra de búsqueda / reemplazo (Ctrl+F).
 *
 * @note SDL/SDL_ttf para recién llegados. El dibujado 2D en SDL gira en torno
 * al "renderer" (@c SDL_Renderer), el contexto de dibujo acelerado por GPU
 * asociado a la ventana. El patrón siempre es el mismo: primero se fija el
 * color de dibujo actual con @c SDL_SetRenderDrawColor(renderer, r, g, b, a)
 * (componentes 0-255), y luego se emite la primitiva. Las primitivas de
 * rectángulo usan @c SDL_FRect (x, y, w, h en coordenadas float): @c
 * SDL_RenderFillRect rellena el rectángulo y
 * @c SDL_RenderRect dibuja solo su contorno. Para el texto, SDL_ttf rasteriza
 * una cadena a un @c SDL_Surface (mapa de píxeles en RAM) con @c
 * TTF_RenderText_Blended; esa superficie se sube a la GPU como @c SDL_Texture y
 * se pinta en pantalla con
 * @c SDL_RenderTexture. Este archivo no llama a esas funciones directamente:
 * usa los envoltorios @c set_color / @c fill_rect / @c stroke_rect / @c
 * draw_text de render_internal.h, que encapsulan ese flujo. @c
 * TTF_GetStringSize mide en píxeles el tamaño que ocuparía una cadena (sin
 * dibujarla), lo que se usa aquí para centrar y posicionar textos dentro de
 * cajas.
 */
#include "render_internal.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

/* -- Geometría (px) -------------------------------------------------------- */
#define FB_PAD 10             /* margen interior de la barra              */
#define FB_GAP 8              /* separación entre etiqueta/campo/botón    */
#define FB_FIELD_H 22         /* alto de cada campo de texto              */
#define FB_ROW_GAP 8          /* separación vertical entre las dos filas  */
#define FB_MIN_FIELD_W 200    /* ancho útil mínimo de campo (para el cálculo) */
#define FB_MIN_W 400          /* ancho mínimo de la barra                 */
#define FB_RIGHT_MARGIN 12    /* separación del borde derecho de la ventana */
#define FB_TOP_MARGIN 8       /* separación bajo la barra de pestañas     */
#define FB_BTN_HPAD 20        /* padding horizontal del botón Reemplazar  */
#define FB_TEXT_PAD 4         /* margen interno del texto dentro del campo */
#define FB_NAV_GAP 4          /* separación entre flechas y contador      */
#define FB_SEL_INSET 2        /* margen vertical del resaltado de selección */
#define FB_LABEL_MARGIN 4     /* margen extra de la columna de etiquetas  */
#define FB_NORESULT_OFFSET 30 /* desplazamiento del texto "Sin resultados" */

/* (los colores de los botones ↑/↓/Reemplazar viven en el tema: ui.h/ui.c) */

/**
 * @brief Calcula la Y para centrar verticalmente el texto dentro de un campo.
 *
 * Un campo mide @c FB_FIELD_H de alto y el texto @c FONT_SIZE; el hueco
 * sobrante se reparte mitad arriba y mitad abajo para centrarlo.
 *
 * @param row_y Coordenada Y (arriba) de la fila/campo.
 * @return Coordenada Y donde empezar a dibujar el texto centrado.
 */
static int field_text_y(int row_y) {
    return row_y + (FB_FIELD_H - FONT_SIZE) / 2;
}

/**
 * @brief Dibuja la caja de un campo de texto (relleno + contorno).
 *
 * Primero rellena el fondo y luego traza el borde por encima. El color cambia
 * según el foco: con foco, fondo más claro y borde de acento (azul); sin foco,
 * fondo apagado y borde gris.
 *
 * @param e       Editor (contiene el @c renderer destino).
 * @param x       X (izquierda) de la caja en píxeles.
 * @param y       Y (arriba) de la caja en píxeles.
 * @param w       Ancho de la caja en píxeles.
 * @param focused 1 si el campo tiene el foco (se resalta); 0 en caso contrario.
 */
static void draw_field_box(Editor *e, int x, int y, int w, int focused) {
    /* Color de relleno según foco: cada componente RGB se interpola entre el
     * tono "con foco" (0x2A,0x2E,0x38) y el "sin foco" (0x25,0x29,0x31). */
    set_color_c(e->renderer,
                focused ? e->theme.fb_col_field_focus : e->theme.fb_col_field);
    fill_rect(e->renderer, x, y, w,
              FB_FIELD_H); /* relleno del fondo del campo */
    /* Borde: color de acento si tiene foco, gris normal si no. */
    if (focused)
        set_color_c(e->renderer, e->theme.fb_col_accent);
    else
        set_color_c(e->renderer, e->theme.fb_col_border);
    stroke_rect(e->renderer, x, y, w, FB_FIELD_H); /* contorno (solo líneas) */
}

/**
 * @brief Resalta (pinta un rectángulo de fondo bajo) el tramo seleccionado del
 * texto.
 *
 * El resaltado abarca el rango de caracteres [sel_start, sel_end). Para saber a
 * qué píxeles corresponde ese tramo, se mide con @c TTF_GetStringSize el ancho
 * del texto que va ANTES de la selección (para el desplazamiento X) y el ancho
 * del propio texto seleccionado (para la anchura del rectángulo).
 *
 * @param e         Editor (renderer + fuente).
 * @param field_x   X (izquierda) del campo en píxeles.
 * @param row_y     Y (arriba) de la fila en píxeles.
 * @param content   Texto completo del campo.
 * @param len       Longitud de @p content en caracteres.
 * @param sel_start Índice de inicio de la selección (inclusive); <0 si no hay.
 * @param sel_end   Índice de fin de la selección (exclusivo).
 */
static void draw_field_selection(Editor *e, int field_x, int row_y,
                                 const char *content, int len, int sel_start,
                                 int sel_end) {
    /* Sin selección válida (vacía, invertida o fuera de rango): nada que
     * dibujar. */
    if (!(sel_start >= 0 && sel_end > sel_start && sel_end <= len)) return;

    /* Partimos el contenido en "lo de antes" y "lo seleccionado" como cadenas
     * con terminador, para poder medir cada parte por separado con la fuente.
     */
    char before[FIND_BAR_MAX], selected[FIND_BAR_MAX];
    int before_len = sel_start; /* nº de caracteres antes de la selección */
    int sel_len = sel_end - sel_start; /* nº de caracteres seleccionados */
    memcpy(before, content, before_len);
    before[before_len] = '\0';
    memcpy(selected, content + before_len, sel_len);
    selected[sel_len] = '\0';

    /* Medir en píxeles el ancho de cada tramo (TTF_GetStringSize no dibuja,
     * solo calcula el tamaño que ocuparía la cadena con esta fuente). */
    int before_w = 0, sel_w = 0, dummy_h = 0;
    if (before_len > 0)
        TTF_GetStringSize(e->font, before, 0, &before_w, &dummy_h);
    if (sel_len > 0) TTF_GetStringSize(e->font, selected, 0, &sel_w, &dummy_h);

    /* Pintar el rectángulo de resaltado: desplazado en X por el texto previo y
     * con la anchura del texto seleccionado; un pequeño inset vertical lo hace
     * más fino que el campo para que se vea el borde. */
    set_color_c(e->renderer, e->theme.fb_col_sel);
    fill_rect(e->renderer, field_x + FB_TEXT_PAD + before_w,
              row_y + FB_SEL_INSET, sel_w, FB_FIELD_H - 2 * FB_SEL_INSET);
}

/**
 * @brief Dibuja el contenido textual de un campo, opcionalmente con cursor.
 *
 * Concatena el texto del campo con un "|" cuando @p show_caret está activo,
 * simulando un cursor parpadeante (el parpadeo lo decide el llamante alternando
 * el flag).
 *
 * @param e          Editor (renderer + fuente).
 * @param field_x    X (izquierda) del campo en píxeles.
 * @param row_y      Y (arriba) de la fila en píxeles.
 * @param content    Texto a mostrar.
 * @param show_caret 1 para añadir el "|" del cursor al final; 0 para no
 * añadirlo.
 */
static void draw_field_text(Editor *e, int field_x, int row_y,
                            const char *content, int show_caret) {
    char buf[512];
    /* Añadir "|" al final si toca mostrar el cursor en este instante de
     * parpadeo. */
    snprintf(buf, sizeof(buf), "%s%s", content, show_caret ? "|" : "");
    draw_text_c(e, buf, field_x + FB_TEXT_PAD, field_text_y(row_y),
                e->theme.fb_txt_field);
}

/**
 * @brief Dibuja la navegación de coincidencias (↑ X/N ↓) y registra los
 * botones.
 *
 * Se coloca pegada al borde derecho del campo de búsqueda: flecha "anterior",
 * el contador "índice/total" en el centro, y flecha "siguiente". Además de
 * pintar, guarda en @c e->find la geometría (x, y, w, h) de ambas flechas, que
 * input_mouse.c consulta luego para saber si un clic cayó sobre ellas.
 *
 * @param e       Editor (renderer + fuente + estado de búsqueda).
 * @param field_x X (izquierda) del campo de búsqueda en píxeles.
 * @param field_w Ancho del campo de búsqueda en píxeles.
 * @param row_y   Y (arriba) de la fila en píxeles.
 */
static void draw_match_nav(Editor *e, int field_x, int field_w, int row_y) {
    int arrow_w = FB_FIELD_H; /* botones cuadrados: ancho = alto del campo */

    /* Texto del contador "actual/total" (match_index es 0-based → se muestra
     * +1). */
    char counter[32];
    snprintf(counter, sizeof(counter), "%d/%d", e->find.match_index + 1,
             e->find.match_count);
    int counter_w = 0, counter_h = 0;
    TTF_GetStringSize(e->font, counter, 0, &counter_w,
                      &counter_h); /* ancho del contador */

    /* Ancho total del bloque [flecha][gap][contador][gap][flecha][gap], para
     * anclarlo al borde derecho del campo. */
    int area_w =
        arrow_w + FB_NAV_GAP + counter_w + FB_NAV_GAP + arrow_w + FB_NAV_GAP;
    int prev_x = field_x + field_w - area_w; /* X de la flecha "anterior" */
    int next_x = prev_x + arrow_w + FB_NAV_GAP + counter_w +
                 FB_NAV_GAP; /* X de "siguiente" */

    /* Flechas como botones reutilizables (estilo del tema); ui_button registra
     * su rect (UI_FIND_PREV / UI_FIND_NEXT) para el hit-test. */
    Rect prev_box = {prev_x, row_y, arrow_w, FB_FIELD_H};
    Rect next_box = {next_x, row_y, arrow_w, FB_FIELD_H};
    ui_button(e, UI_FIND_PREV, prev_box, "↑", &e->theme.style_button,
              UI_NORMAL);
    /* contador "X/N" entre las dos flechas */
    draw_text_c(e, counter, prev_x + arrow_w + FB_NAV_GAP, field_text_y(row_y),
                e->theme.fb_txt_counter);
    ui_button(e, UI_FIND_NEXT, next_box, "↓", &e->theme.style_button,
              UI_NORMAL);
}

/**
 * @brief Dibuja la barra flotante de búsqueda/reemplazo completa (Ctrl+F).
 *
 * Renderiza un panel anclado arriba a la derecha con dos filas: "Buscar:" (con
 * navegación de coincidencias o un aviso "Sin resultados") y "Reemplazar:" (con
 * un botón). El ancho de la barra y de las columnas se calcula a partir del
 * tamaño real (medido) de las etiquetas, para que se adapten a la fuente. Al
 * final guarda toda la geometría en @c e->find para que input_mouse.c pueda
 * mapear clics a campos y botones. No hace nada si la barra está oculta.
 *
 * @param e Editor con el estado de búsqueda (@c e->find) y el renderer/fuente.
 */
void render_find_bar(Editor *e) {
    if (!e->find.visible) return; /* barra oculta: nada que dibujar */
    SDL_Renderer *r = e->renderer;
    FindBar *fb = &e->find;

    /* -- Cálculo del layout (medimos las etiquetas con la fuente real) -- */
    int search_label_w = 0, replace_label_w = 0, dummy_h = 0;
    TTF_GetStringSize(e->font, "Buscar:", 0, &search_label_w, &dummy_h);
    TTF_GetStringSize(e->font, "Reemplazar:", 0, &replace_label_w, &dummy_h);
    /* La columna de etiquetas se dimensiona según la más ancha de las dos. */
    int label_col_w =
        (search_label_w > replace_label_w ? search_label_w : replace_label_w) +
        FB_LABEL_MARGIN;

    /* Ancho del botón "Reemplazar" = ancho de su texto + padding horizontal. */
    int replace_btn_label_w = 0;
    TTF_GetStringSize(e->font, "Reemplazar", 0, &replace_btn_label_w, &dummy_h);
    int replace_btn_w = replace_btn_label_w + FB_BTN_HPAD;

    /* Ancho total de la barra sumando paddings, columnas y gaps; con mínimo. */
    int bar_w = FB_PAD + label_col_w + FB_GAP + FB_MIN_FIELD_W + FB_GAP +
                replace_btn_w + FB_PAD;
    if (bar_w < FB_MIN_W) bar_w = FB_MIN_W;
    /* Alto: padding + dos filas de campo separadas por un gap + padding. */
    int bar_h = FB_PAD + FB_FIELD_H + FB_ROW_GAP + FB_FIELD_H + FB_PAD;
    int bar_x = e->win_w - bar_w - FB_RIGHT_MARGIN; /* anclada a la derecha */
    int bar_y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT +
                FB_TOP_MARGIN; /* bajo navbar y pestañas */

    int label_x = bar_x + FB_PAD; /* X de la columna de etiquetas */
    int field_x =
        label_x + label_col_w + FB_GAP; /* X donde empiezan los campos  */
    int search_field_w =
        bar_w - FB_PAD - label_col_w - FB_GAP - FB_PAD; /* fila 1: ocupa todo */
    int replace_field_w =
        search_field_w - FB_GAP - replace_btn_w; /* fila 2: deja el botón */
    int replace_btn_x =
        field_x + replace_field_w + FB_GAP; /* X del botón Reemplazar */

    int row1_y = bar_y + FB_PAD; /* Y de la fila "Buscar"      */
    int row2_y =
        row1_y + FB_FIELD_H + FB_ROW_GAP; /* Y de la fila "Reemplazar"  */

    /* Qué campo tiene el foco: la barra está enfocada y replace_focused indica
     * cuál. */
    int search_focused = (fb->bar_focused && fb->replace_focused == 0);
    int replace_focused = (fb->bar_focused && fb->replace_focused == 1);
    /* Parpadeo del cursor: alterna 0/1 cada 500 ms (SDL_GetTicks = ms desde
     * init). */
    int blink = (SDL_GetTicks() / 500) % 2;

    /* -- Fondo + borde de la barra -- */
    set_color_c(r, e->theme.fb_col_bg);
    fill_rect(r, bar_x, bar_y, bar_w, bar_h);
    set_color_c(r, e->theme.fb_col_border);
    stroke_rect(r, bar_x, bar_y, bar_w, bar_h);

    /* -- Fila 1: Buscar -- */
    draw_text_c(e, "Buscar:", label_x, field_text_y(row1_y),
                e->theme.fb_txt_label); /* etiqueta */
    draw_field_box(e, field_x, row1_y, search_field_w,
                   search_focused); /* caja del campo */
    /* Resaltado de selección (si la hay) debajo del texto. */
    draw_field_selection(e, field_x, row1_y, fb->query, fb->query_len,
                         fb->query_sel_start, fb->query_sel_end);
    /* Texto del campo; el cursor solo parpadea si tiene foco y no hay
     * selección. */
    draw_field_text(e, field_x, row1_y, fb->query,
                    search_focused && fb->query_sel_start < 0 && blink);

    /* A la derecha del campo: navegación "X/N" si hay coincidencias, o el aviso
     * de "Sin resultados" si se escribió algo pero no se encontró nada. */
    if (fb->result_line >= 0 && fb->match_count > 0) {
        draw_match_nav(e, field_x, search_field_w, row1_y);
    } else if (fb->query_len > 0 && fb->match_count == 0) {
        draw_text_c(e, "Sin resultados",
                    field_x + search_field_w / 2 - FB_NORESULT_OFFSET,
                    field_text_y(row1_y), e->theme.fb_txt_noresult);
    }

    /* -- Fila 2: Reemplazar -- */
    draw_text_c(e, "Reemplazar:", label_x, field_text_y(row2_y),
                e->theme.fb_txt_label); /* etiqueta */
    draw_field_box(e, field_x, row2_y, replace_field_w,
                   replace_focused); /* caja del campo */
    draw_field_selection(e, field_x, row2_y, fb->replace, fb->replace_len,
                         fb->replace_sel_start, fb->replace_sel_end);
    draw_field_text(e, field_x, row2_y, fb->replace,
                    replace_focused && fb->replace_sel_start < 0 && blink);

    /* Botón "Reemplazar": estilo de acción del tema (registra UI_FIND_REPLACE).
     */
    Rect replace_box = {replace_btn_x, row2_y, replace_btn_w, FB_FIELD_H};
    ui_button(e, UI_FIND_REPLACE, replace_box, "Reemplazar",
              &e->theme.style_primary, UI_NORMAL);

    /* -- Hit-test: registrar campos y marco en e->ui (lo lee input_mouse) --
     * El área "clicable" de cada campo abarca toda la fila hasta el borde
     * derecho de la barra (igual que el comportamiento anterior). */
    int bar_right = bar_x + bar_w;
    ui_put(&e->ui, UI_FIND_QUERY,
           (Rect){field_x, row1_y, bar_right - field_x, FB_FIELD_H});
    ui_put(&e->ui, UI_FIND_REPL,
           (Rect){field_x, row2_y, bar_right - field_x, FB_FIELD_H});
    ui_put(&e->ui, UI_FIND_BAR, (Rect){bar_x, bar_y, bar_w, bar_h});
}
