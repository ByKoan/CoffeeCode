/**
 * @file render.c
 * @brief Orquestación del frame y utilidades de dibujo compartidas del módulo
 *        render. Las secciones concretas (navbar, pestañas, panel, etc.) están
 *        en render_ui.c / render_filetree.c / render_find.c.
 *
 * @note Render con SDL3 para recién llegados. SDL dibuja en modo "inmediato":
 * cada frame se construye desde cero sobre un renderer (el contexto de dibujo
 * 2D acelerado por GPU). El patrón es siempre el mismo:
 *   1. @c SDL_SetRenderDrawColor(r, R,G,B,A): fija el "color actual" del
 * renderer (RGBA, 0-255). Todo lo que se dibuje después usa ese color hasta
 * cambiarlo.
 *   2. @c SDL_RenderClear(r): borra todo el frame con el color actual (fondo).
 *   3. Se dibujan formas: @c SDL_RenderFillRect (rectángulo relleno) y
 *      @c SDL_RenderRect (solo el contorno), ambos sobre @c SDL_FRect, que es
 * un rectángulo con coordenadas en float {x, y, w, h} en píxeles de ventana.
 *   4. @c SDL_RenderPresent(r): muestra en pantalla el frame ya dibujado. SDL
 * usa doble búfer: se dibuja en un búfer oculto y "present" lo intercambia con
 * el visible de golpe, evitando parpadeos. El texto NO se dibuja con esas
 * primitivas: hay que rasterizarlo con SDL_ttf (ver ::draw_text). El origen de
 * coordenadas es la esquina superior izquierda, con +X a la derecha y +Y hacia
 * abajo.
 */
#include "render_internal.h"
#include <stdio.h>
#include <string.h>

/* ── Constantes ─────────────────────────────────────────────────────────────
 */
#define CURSOR_W 2       /* ancho del cursor (px)                       */
#define GUTTER_NUM_PAD 4 /* sangría del número de línea en el gutter     */
#define LINE_BUF_SZ 4096 /* buffer temporal por línea visible           */
#define TOKEN_CHUNK 255  /* máx. caracteres por fragmento de texto       */

/* ── Utilidades de dibujo compartidas ───────────────────────────────────────
 */

/**
 * @brief Fija el color de dibujo actual del renderer (estado global del
 * render).
 *
 * Envoltura fina sobre @c SDL_SetRenderDrawColor. SDL es una máquina de estado:
 * este color se aplica a todas las primitivas posteriores (fill_rect,
 * stroke_rect, clear) hasta la siguiente llamada. No afecta al texto (que lleva
 * su propio color).
 *
 * @param r Renderer destino.
 * @param R,G,B Componentes de color (0-255).
 * @param A Alfa/opacidad (0 transparente, 255 opaco).
 */
void set_color(SDL_Renderer *r, uint8_t R, uint8_t G, uint8_t B, uint8_t A) {
    SDL_SetRenderDrawColor(r, R, G, B, A);
}

/** Igual que ::set_color pero tomando un ::Color del tema (e->theme.*). */
void set_color_c(SDL_Renderer *r, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

/** Igual que ::draw_text pero tomando el color como ::Color (ignora el alfa).
 */
int draw_text_c(Editor *e, const char *text, int x, int y, Color c) {
    return draw_text(e, text, x, y, c.r, c.g, c.b);
}

/**
 * @brief Rellena un rectángulo con el color actual del renderer.
 *
 * SDL trabaja con @c SDL_FRect (coordenadas float), así que se convierten los
 * int.
 * @param r Renderer. @param x,y Esquina superior izquierda. @param w,h Tamaño
 * (px).
 */
void fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_FRect rect = {(float)x, (float)y, (float)w, (float)h};
    SDL_RenderFillRect(r, &rect);
}

/**
 * @brief Dibuja el contorno (1 px) de un rectángulo con el color actual.
 *
 * En vez de usar @c SDL_RenderRect, se construyen los 4 lados como rectángulos
 * rellenos de 1 px (arriba, abajo, izquierda, derecha) y se pintan de una sola
 * llamada con @c SDL_RenderFillRects (versión en lote de fill_rect). Esto da un
 * borde nítido de exactamente 1 px independiente del backend.
 *
 * @param r Renderer. @param x,y Esquina superior izquierda. @param w,h Tamaño
 * (px).
 */
void stroke_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_FRect sides[4] = {
        {(float)x, (float)y, (float)w, 1},            /* borde superior */
        {(float)x, (float)(y + h - 1), (float)w, 1},  /* borde inferior */
        {(float)x, (float)y, 1, (float)h},            /* borde izquierdo */
        {(float)(x + w - 1), (float)y, 1, (float)h}}; /* borde derecho   */
    SDL_RenderFillRects(r, sides, 4);
}

/**
 * @brief Rasteriza y dibuja una cadena de texto con la fuente del editor.
 *
 * SDL no sabe dibujar texto; hace falta SDL_ttf. El proceso de tres pasos es:
 *   1. @c TTF_RenderText_Blended(fuente, texto, 0, color): rasteriza el texto a
 * una
 *      @c SDL_Surface (mapa de píxeles en RAM, lado CPU). "Blended" hace
 * antialias de calidad con canal alfa. El @c 0 es la longitud (0 = cadena
 * terminada en
 *      '\0'). Devuelve @c NULL si falla.
 *   2. @c SDL_CreateTextureFromSurface: sube esa surface a la GPU como
 *      @c SDL_Texture, que es lo que el renderer puede dibujar rápido.
 *   3. @c SDL_RenderTexture(r, tex, NULL, &dst): dibuja la textura completa
 * (src
 *      @c NULL = toda) en el rectángulo destino @p dst (posición + tamaño del
 * glifo). Surface y textura se liberan SIEMPRE al final (@c SDL_DestroySurface
 * /
 * @c SDL_DestroyTexture); si no, cada frame fugaría memoria de CPU y GPU.
 *
 * @param e       Editor (aporta @c font y @c renderer).
 * @param text    Cadena UTF-8 a dibujar; si es @c NULL o vacía no hace nada.
 * @param x,y     Esquina superior izquierda donde colocar el texto (px).
 * @param R,G,B   Color del texto (alfa fijo a 255, opaco).
 * @return Ancho en píxeles del texto dibujado (útil para colocar lo siguiente),
 * 0 si no se dibujó nada.
 */
int draw_text_font(Editor *e, TTF_Font *font, const char *text, int x, int y,
                   Color c) {
    if (!font || !text || !text[0]) return 0; /* nada que dibujar */
    SDL_Color col = {c.r, c.g, c.b, 255};     /* color opaco para el glifo */
    /* texto -> píxeles (RAM) */
    SDL_Surface *surf = TTF_RenderText_Blended(font, text, 0, col);
    if (!surf) return 0; /* fallo de rasterizado */
    /* subir a la GPU */
    SDL_Texture *tex = SDL_CreateTextureFromSurface(e->renderer, surf);
    /* destino = tam. glifo */
    SDL_FRect dst = {(float)x, (float)y, (float)surf->w, (float)surf->h};
    SDL_RenderTexture(e->renderer, tex, NULL, &dst); /* dibujar la textura */
    int w = surf->w;          /* guardar ancho antes de liberar */
    SDL_DestroySurface(surf); /* liberar surface (RAM) */
    SDL_DestroyTexture(tex);  /* liberar textura (GPU) */
    return w;
}

int draw_text(Editor *e, const char *text, int x, int y, uint8_t R, uint8_t G,
              uint8_t B) {
    Color c = {R, G, B, 255};
    return draw_text_font(e, e->font, text, x, y, c); /* fuente del editor */
}

/**
 * @brief Extrae el texto de una línea expandiendo tabs a espacios.
 *
 * Copia los caracteres de la línea @p line en @p out, parando en el '\n' o al
 * llenar @p max. Cada tabulador se sustituye por los espacios que faltan para
 * llegar a la siguiente parada de tabulación (@c TAB_SIZE), de modo que el
 * render alinee las columnas en su rejilla fija de ancho de carácter.
 *
 * Coste O(longitud de línea), no O(n): usa el índice de líneas del Buffer
 * (@c buf_line_offset) para saltar directamente al inicio de la línea pedida en
 * vez de recorrer el archivo desde el principio.
 *
 * @param e    Editor (aporta el buffer activo).
 * @param line Índice de línea (0-based).
 * @param out  Buffer de salida (se termina siempre con '\0').
 * @param max  Tamaño de @p out; nunca se escriben más de @p max-1 caracteres.
 * @return Número de caracteres (ya expandidos) escritos en @p out.
 */
int get_line_text(Editor *e, int line, char *out, int max) {
    const Buffer *b = e->buf;
    if (line < 0 || line >= buf_line_count(b)) { /* línea fuera de rango */
        out[0] = '\0';
        return 0;
    }

    /* offset lógico del inicio de la línea */
    size_t start = buf_line_offset(b, line);
    size_t total = buf_length(b); /* total de caracteres del buffer */
    int col = 0;                  /* columna visual de salida */

    for (size_t i = start; i < total && col < max - 1; i++) {
        char c = buf_char_at(b, i);
        if (c == '\n') break; /* fin de la línea */
        if (c == '\t') {
            /* tab: rellenar hasta la siguiente parada (ancho de tab
             * configurable) */
            int spaces = e->settings.tab_width - (col % e->settings.tab_width);
            for (int s = 0; s < spaces && col < max - 1; s++)
                out[col++] = ' ';
        } else {
            out[col++] = c;
        }
    }
    out[col] = '\0';
    return col;
}

/**
 * @brief Ancho visual de una línea (en caracteres) incluyendo el '\n' final.
 *
 * Sirve para pintar la selección hasta el borde derecho de líneas completas (el
 * +1 representa la celda del salto de línea, que se resalta para indicar que la
 * selección abarca el fin de la línea).
 *
 * @param e Editor. @param line Índice de línea. @return Ancho en celdas, +1.
 */
static int line_visual_width(Editor *e, int line) {
    size_t ls = buf_line_offset(e->buf, line); /* inicio de la línea */
    size_t le = buf_line_end(e->buf, ls);      /* fin (sin el '\n') */
    return (int)(le - ls) + 1; /* +1: incluir la celda del '\n' */
}

/* ── Resaltado de selección ─────────────────────────────────────────────────
 */

/**
 * @brief Pinta el fondo azulado de la selección de texto, línea a línea.
 *
 * Obtiene el rango lógico [from, to) de ::editor_sel_range, lo convierte a
 * (línea, columna) en ambos extremos y dibuja un rectángulo por cada línea
 * visible abarcada: la primera empieza en su columna inicial, la última termina
 * en la suya, y las intermedias se pintan completas. Coordenadas de píxel =
 * origen del texto + (columna - scroll) * ancho_de_carácter, con clamping al
 * borde izquierdo del área.
 *
 * @param e            Editor (selección, scroll, buffer).
 * @param left_offset  Desplazamiento izquierdo (ancho del panel lateral o su
 * botón).
 * @param text_top     Y del inicio del área de texto (px).
 * @param visible_lines Número de líneas que caben en la vista.
 */
void render_selection(Editor *e, int left_offset, int text_top,
                      int visible_lines) {
    if (!e->sel_active) return; /* sin selección no hay nada que pintar */

    size_t from, to;
    if (!editor_sel_range(e, &from, &to)) return; /* selección vacía */

    SDL_Renderer *r = e->renderer;
    /* X del primer carácter */
    int text_x = left_offset + GUTTER_WIDTH + PADDING_LEFT;
    int total_lines = buf_line_count(e->buf);

    int from_line, from_col, to_line, to_col;
    /* extremo inicial -> (lín,col) */
    buf_line_col(e->buf, from, &from_line, &from_col);
    /* extremo final   -> (lín,col) */
    buf_line_col(e->buf, to, &to_line, &to_col);

    set_color_c(
        r,
        e->theme.col_sel_bg); /* azul de selección para todos los rectángulos */

    /* Si to_col == 0 y to_line > from_line, el cursor está al inicio de
     * to_line: la selección cubre hasta el '\n' de (to_line-1), así que
     * pintamos esa línea completa y no tocamos to_line. */
    int paint_to_line = to_line;
    int paint_to_col = to_col;
    if (to_col == 0 && to_line > from_line) {
        paint_to_line = to_line - 1;
        paint_to_col = line_visual_width(e, paint_to_line);
    }

    for (int li = from_line; li <= paint_to_line && li < total_lines; li++) {
        int vi = li - e->scroll_line; /* índice visible (0 = primera fila) */
        if (vi < 0 || vi >= visible_lines)
            continue; /* línea fuera de la vista */

        int y = text_top + vi * e->line_height;
        /* primera línea: empieza en from_col; resto: desde la columna 0 */
        int col_start = (li == from_line) ? from_col : 0;
        /* última línea: termina en paint_to_col; resto: hasta el ancho visual
         */
        int col_end =
            (li == paint_to_line) ? paint_to_col : line_visual_width(e, li);

        /* columnas -> píxeles, descontando el scroll horizontal */
        int x_start = text_x + (col_start - e->scroll_col) * e->char_w;
        int x_end = text_x + (col_end - e->scroll_col) * e->char_w;
        if (x_start < text_x) x_start = text_x; /* no invadir el gutter */
        if (x_end < x_start)
            x_end = x_start + e->char_w; /* asegurar ancho mínimo */

        fill_rect(r, x_start, y, x_end - x_start, e->line_height);
    }
}

/* ── render_frame y sus ayudantes ───────────────────────────────────────────
 */

/**
 * @brief Dibuja la barra de estado inferior con el texto @p text.
 *
 * Pinta el fondo a lo ancho de la ventana, una línea separadora de 1 px arriba
 * y el texto centrado verticalmente en la barra.
 *
 * @param e    Editor. @param text Texto a mostrar (alineado a la izquierda).
 */
static void draw_status_bar(Editor *e, const char *text) {
    SDL_Renderer *r = e->renderer;
    int y = e->win_h - STATUS_HEIGHT; /* la barra va pegada al borde inferior */
    set_color_c(r, e->theme.col_status_bg);
    fill_rect(r, 0, y, e->win_w, STATUS_HEIGHT); /* fondo de la barra */
    set_color_c(r, e->theme.col_status_sep);
    fill_rect(r, 0, y, e->win_w, 1); /* separador superior de 1 px */
    /* centrado vertical: (alto barra - alto fuente) / 2 */
    draw_text_c(e, text, 0, y + (STATUS_HEIGHT - e->font_size) / 2,
                e->theme.txt_status);
}

/**
 * @brief Pantalla de bienvenida cuando no hay ningún archivo abierto.
 *
 * Dibuja el cromo habitual (navbar, pestañas, panel lateral, menú) y, centrado
 * en el área del editor, un título y tres pistas de atajos. Calcula los anchos
 * del texto con @c TTF_GetStringSize (mide sin dibujar) para poder centrarlos.
 * Termina con su propio @c SDL_RenderPresent porque ::render_frame retorna
 * antes de su "present" cuando no hay pestañas.
 *
 * @param e Editor.
 */
static void render_empty_screen(Editor *e) {
    /* cromo de UI igual que con archivo abierto */
    render_navbar(e);
    render_tabbar(e);
    if (e->ftree.open)
        render_filetree(e);
    else
        render_filetree_toggle_closed(e);
    render_menu(e);

    const char *title = "No hay ningún archivo abierto";
    const char *hints[] = {"Ctrl+O  Abrir archivo", "Ctrl+N  Nuevo archivo",
                           "Ctrl+K  Abrir carpeta"};
    /* medir anchos (sin dibujar) para poder centrar el texto */
    int title_w = 0, hint_w = 0, h = 0;
    TTF_GetStringSize(e->font, title, 0, &title_w, &h);
    /* hints monoespaciados: igual ancho */
    TTF_GetStringSize(e->font, hints[0], 0, &hint_w, &h);

    /* centro del área disponible (descontando panel lateral arriba y status
     * abajo) */
    int area_left = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    int area_top = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    int center_x = area_left + (e->win_w - area_left) / 2;
    int mid_y = area_top + (e->win_h - area_top - STATUS_HEIGHT) / 2;

    /* título dos líneas por encima del centro; cada pista una línea más abajo
     */
    draw_text_c(e, title, center_x - title_w / 2, mid_y - e->line_height * 2,
                e->theme.txt_welcome_title);
    for (int i = 0; i < 3; i++)
        draw_text_c(e, hints[i], center_x - hint_w / 2,
                    mid_y + e->line_height * i, e->theme.txt_welcome_hint);

    draw_status_bar(e, "  CoffeeCode");
    /* mostrar el frame (render_frame ya retornó) */
    SDL_RenderPresent(e->renderer);
}

/**
 * @brief Re-tokeniza las líneas marcadas como "sucias" en la cache del lexer.
 *
 * El resaltado se cachea por línea (cada @c LineTokens guarda los tokens de una
 * línea). Aquí se recorren todas y, las marcadas como sucias por una edición,
 * se vuelven a tokenizar con @c tokenize_line del resaltador (@c e->hl). El
 * estado
 * @c in_block (¿estamos dentro de un comentario de bloque?) se arrastra de una
 * línea a la siguiente, porque un /​* abierto afecta a las líneas
 * posteriores. Tras tokenizar, se limpia el flag "sucio" de esa línea.
 *
 * @param e Editor (lexer, buffer y resaltador activos).
 */
static void update_lexer_cache(Editor *e) {
    /* Si la pestaña activa tiene un cliente LSP con tokens válidos, usarlos
     * directamente en lugar del tokenizador estático. El LSP proporciona
     * resaltado semántico preciso para cualquier lenguaje soportado. */
    EditorTab *active_tab = (e->tab_count > 0) ? &e->tabs[e->active_tab] : NULL;
    int use_lsp =
        (active_tab && active_tab->lsp_active && active_tab->lsp.cache.ready);

    if (use_lsp) {
        /* Rellenar la cache del lexer con los tokens semánticos LSP */
        for (int li = 0; li < lexer_cache_count(e->lex); li++) {
            if (*lexer_cache_dirty_at(e->lex, li)) {
                lsp_fill_line_tokens(&active_tab->lsp, li,
                                     lexer_cache_line(e->lex, li));
                *lexer_cache_dirty_at(e->lex, li) = 0;
            }
        }
        return;
    }

    /* Fallback: tokenizador estático propio (C/texto plano) */
    int in_block = 0; /* ¿venimos dentro de un comentario de bloque? */
    for (int li = 0; li < lexer_cache_count(e->lex); li++) {
        /* línea pendiente de re-resaltar */
        if (*lexer_cache_dirty_at(e->lex, li)) {
            char line_buf[LINE_BUF_SZ];
            /* texto expandido de la línea */
            get_line_text(e, li, line_buf, sizeof(line_buf));
            /* tokenizar; devuelve el nuevo estado in_block para la línea
             * siguiente */
            in_block =
                e->hl->tokenize_line(e->hl, line_buf, (int)strlen(line_buf),
                                     lexer_cache_line(e->lex, li), in_block);
            *lexer_cache_dirty_at(e->lex, li) = 0; /* ya está limpia */
        }
    }
}

/**
 * @brief Dibuja el fragmento @c line[src, src+len) en @p px con el color @p c.
 *
 * Copia el fragmento a un buffer temporal terminado en '\0' (draw_text necesita
 * una cadena C) y lo rasteriza. Recorta la longitud a @c TOKEN_CHUNK para no
 * desbordar el buffer de pila @c tmp.
 *
 * @param e   Editor. @param line Línea origen. @param src Offset dentro de @p
 * line.
 * @param len Longitud del fragmento. @param px,y Posición destino (px). @param
 * c Color.
 */
static void draw_substr(Editor *e, const char *line, int src, int len, int px,
                        int y, Color c) {
    if (len <= 0) return;
    if (len > TOKEN_CHUNK)
        len = TOKEN_CHUNK; /* no rebasar el buffer temporal */
    char tmp[TOKEN_CHUNK + 1];
    memcpy(tmp, line + src, (size_t)len);
    tmp[len] = '\0'; /* draw_text espera cadena C */
    draw_text(e, tmp, px, y, c.r, c.g, c.b);
}

/**
 * @brief Dibuja una línea de texto con resaltado de sintaxis por tokens.
 *
 * Obtiene el texto de la línea y, si tiene tokens cacheados, recorre cada token
 * dibujando: los huecos sin token en color por defecto y cada token en el color
 * de su tipo (@c e->theme.tokens). Todo se ajusta al scroll horizontal (@c
 * scroll_col): las columnas a la izquierda del scroll se recortan. Si la línea
 * no tiene tokens, dibuja el texto plano en color por defecto.
 *
 * @param e      Editor. @param li Índice de línea. @param y Y de la fila (px).
 * @param text_x X del primer carácter de texto (px).
 */
static void render_text_line(Editor *e, int li, int y, int text_x) {
    char line_buf[LINE_BUF_SZ];
    /* texto expandido */
    int line_len = get_line_text(e, li, line_buf, sizeof(line_buf));
    /* centrado vertical en la fila */
    int text_y = y + (e->line_height - e->font_size) / 2;

    if (!(li < lexer_cache_count(e->lex) &&
          lexer_cache_line(e->lex, li)->count > 0)) {
        /* sin tokens: dibujar el resto de la línea en color por defecto */
        /* saltar lo desplazado */
        int start = e->scroll_col < line_len ? e->scroll_col : line_len;
        if (start < line_len) {
            Color dc = e->theme.tokens[TOK_DEFAULT];
            draw_text(e, line_buf + start, text_x, text_y, dc.r, dc.g, dc.b);
        }
        return;
    }

    /* tokens cacheados de esta línea */
    LineTokens *lt = lexer_cache_line(e->lex, li);
    int drawn_to = 0; /* columna lógica ya cubierta */
    for (int ti = 0; ti < lt->count; ti++) {
        Token *tok = &lt->tokens[ti];
        /* fin del token en coords de vista */
        int col_end = tok->col + tok->len - e->scroll_col;
        if (col_end <= 0) { /* token totalmente a la izquierda */
            drawn_to = tok->col + tok->len;
            continue;
        }

        /* hueco (texto sin token) antes de este token: pintarlo en color por
         * defecto */
        if (drawn_to < tok->col) {
            /* inicio del hueco en vista */
            int gap_start = drawn_to - e->scroll_col;
            if (gap_start < 0) gap_start = 0; /* recortar lo desplazado */
            int gap_len = tok->col - e->scroll_col - gap_start;
            if (gap_len > 0 && gap_start + e->scroll_col < line_len)
                draw_substr(e, line_buf, gap_start + e->scroll_col, gap_len,
                            text_x + gap_start * e->char_w, text_y,
                            e->theme.tokens[TOK_DEFAULT]);
        }

        /* columna de dibujo (vista) y rango real del token tras recortar el
         * scroll */
        int draw_col =
            (tok->col > e->scroll_col) ? tok->col - e->scroll_col : 0;
        int actual_start = tok->col < e->scroll_col ? e->scroll_col : tok->col;
        int actual_len = tok->col + tok->len - actual_start;
        if (actual_len <= 0) { /* nada visible de este token */
            drawn_to = tok->col + tok->len;
            continue;
        }
        if (actual_start + actual_len > line_len)
            actual_len = line_len - actual_start; /* clamp */
        if (actual_len <= 0) {
            drawn_to = tok->col + tok->len;
            continue;
        }

        /* dibujar el token con el color de su tipo */
        draw_substr(e, line_buf, actual_start, actual_len,
                    text_x + draw_col * e->char_w, text_y,
                    e->theme.tokens[tok->type]);
        drawn_to = tok->col + tok->len;
    }

    /* texto restante tras el último token (en color por defecto) */
    if (drawn_to < line_len) {
        int start = drawn_to - e->scroll_col;
        if (start < 0) start = 0;
        if (start < line_len)
            draw_substr(e, line_buf, start, line_len - start,
                        text_x + start * e->char_w, text_y,
                        e->theme.tokens[TOK_DEFAULT]);
    }
}

/**
 * @brief Dibuja todas las líneas de texto visibles del área de edición.
 *
 * Recorre las filas visibles (de @c scroll_line hacia abajo) y delega cada una
 * en
 * ::render_text_line, parando al pasar la última línea del buffer.
 *
 * @param e Editor. @param left_offset Desplazamiento izquierdo (panel/botón).
 * @param text_top Y de inicio del texto. @param visible_lines Filas que caben.
 * @param total_lines Total de líneas del buffer.
 */
static void render_text_area(Editor *e, int left_offset, int text_top,
                             int visible_lines, int total_lines) {
    /* X del primer carácter */
    int text_x = left_offset + GUTTER_WIDTH + PADDING_LEFT;
    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi; /* línea lógica de esta fila */
        if (li >= total_lines) break; /* no hay más texto */
        render_text_line(e, li, text_top + vi * e->line_height, text_x);
    }
}

/**
 * @brief Dibuja el gutter: el margen izquierdo con los números de línea.
 *
 * Pinta el fondo del gutter y, para cada línea visible, su número (1-based)
 * alineado a la derecha con @c "%4d". Los números van en un gris tenue.
 *
 * @param e Editor. @param left_offset Desplazamiento izquierdo. @param text_top
 * Y.
 * @param text_height Alto del área de texto. @param visible_lines Filas
 * visibles.
 * @param total_lines Total de líneas.
 */
static void render_gutter(Editor *e, int left_offset, int text_top,
                          int text_height, int visible_lines, int total_lines) {
    set_color_c(e->renderer, e->theme.col_gutter);
    /* fondo del gutter */
    fill_rect(e->renderer, left_offset, text_top, GUTTER_WIDTH, text_height);

    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi;
        if (li >= total_lines) break;
        char num[16];
        /* 1-based, alineado a la derecha */
        snprintf(num, sizeof(num), "%4d", li + 1);
        int y = text_top + vi * e->line_height;
        draw_text_c(e, num, left_offset + GUTTER_NUM_PAD,
                    y + (e->line_height - e->font_size) / 2,
                    e->theme.txt_gutter_num);
    }
}

/**
 * @brief Dibuja el cursor de texto (barra vertical) si está dentro de la vista.
 *
 * Convierte (cursor_line, cursor_col) a coordenadas de vista restando el
 * scroll; si queda fuera del área visible no dibuja nada. Si no, pinta una
 * barra fina de
 * @c CURSOR_W px de ancho y un alto de línea.
 *
 * @param e Editor. @param left_offset Desplazamiento izquierdo. @param text_top
 * Y.
 * @param visible_lines Filas visibles.
 */
static void render_cursor(Editor *e, int left_offset, int text_top,
                          int visible_lines) {
    /* Respetar el estado de parpadeo: si el cursor está en su fase "oculta",
     * no dibujar nada (el parpadeo se gestiona en editor_run). */
    if (!e->cursor_visible) return;

    /* fila del cursor en la vista */
    int vis_line = e->cursor_line - e->scroll_line;
    /* columna del cursor en la vista */
    int vis_col = e->cursor_col - e->scroll_col;
    if (vis_line < 0 || vis_line >= visible_lines || vis_col < 0)
        return; /* fuera de vista */

    int cx = left_offset + GUTTER_WIDTH + PADDING_LEFT + vis_col * e->char_w;
    int cy = text_top + vis_line * e->line_height;
    set_color_c(e->renderer, e->theme.col_cursor);
    /* barra vertical del cursor */
    fill_rect(e->renderer, cx, cy, CURSOR_W, e->line_height);
}

/**
 * @brief Orquesta el dibujado de un frame completo del editor.
 *
 * Es el director de orquesta del render. Sigue el patrón SDL de modo inmediato:
 *   1. Calcula la geometría del área de texto (offsets, líneas visibles, etc.).
 *   2. @c SDL_RenderClear con el color de fondo: borra el frame anterior.
 *   3. Si no hay pestañas, delega en ::render_empty_screen (que ya hace su
 * present) y retorna.
 *   4. Si hay archivo: actualiza la cache del lexer y dibuja, en orden de atrás
 *      hacia delante (lo de abajo se pinta primero y lo cubre lo de arriba):
 *      línea activa → selección → texto → gutter → cursor → status → scrollbar
 * → panel → navbar → pestañas → barra de búsqueda → menú.
 *   5. @c SDL_RenderPresent: muestra el frame ya compuesto (doble búfer).
 *
 * @param e Editor con todo el estado a dibujar.
 */
void render_frame(Editor *e) {
    SDL_Renderer *r = e->renderer;
    /* vaciar el registro de hit-test: se rellena al dibujar los controles de
     * este frame (ver render/ui_hit.h). */
    ui_reset(&e->ui);

    /* Pantalla de preferencias: sustituye al editor mientras está abierta. */
    if (e->settings_open) {
        render_settings_view(e);
        SDL_RenderPresent(r);
        return;
    }
    /* offset izquierdo: ancho del panel si está abierto, o el del botón si
     * cerrado */
    int left_offset = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    /* Y donde empieza el texto */
    int text_top = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    /* alto del área de texto = ventana menos las bandas de UI */
    int text_height = e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT -
                      STATUS_HEIGHT - SHORTCUT_HEIGHT;
    int visible_lines = text_height / e->line_height; /* filas que caben */
    int total_lines = (e->tab_count > 0) ? buf_line_count(e->buf) : 0;

    set_color_c(r, e->theme.col_bg);
    SDL_RenderClear(r); /* borra el frame con el color de fondo */

    if (e->tab_count == 0) {    /* sin archivos: pantalla de bienvenida */
        render_empty_screen(e); /* (hace su propio RenderPresent) */
        return;
    }

    /* re-tokenizar líneas sucias antes de dibujar texto */
    update_lexer_cache(e);

    /* resaltado de la línea activa (banda completa; solo si no hay selección)
     */
    if (!e->sel_active) {
        int vi_cursor = e->cursor_line - e->scroll_line;
        if (vi_cursor >= 0 && vi_cursor < visible_lines) {
            set_color_c(r, e->theme.col_cursor_line);
            fill_rect(r, 0, text_top + vi_cursor * e->line_height, e->win_w,
                      e->line_height);
        }
    }

    /* capas del área de edición, de atrás hacia delante */
    render_selection(e, left_offset, text_top,
                     visible_lines); /* fondo selección */
    render_text_area(e, left_offset, text_top, visible_lines,
                     total_lines); /* texto resaltado */
    render_gutter(e, left_offset, text_top, text_height, visible_lines,
                  total_lines); /* números */
    render_cursor(e, left_offset, text_top,
                  visible_lines); /* barra del cursor */

    /* barra de estado: nombre + posición del cursor (1-based) + '*' si
     * modificado */
    char status[128];
    snprintf(status, sizeof(status), " CoffeeCode | Ln %d, Col %d%s |",
             e->cursor_line + 1, e->cursor_col + 1, e->modified ? "  *" : "");
    draw_status_bar(e, status);

    /* cromo de la UI por encima del texto */
    render_scrollbar(e, left_offset);
    if (e->ftree.open)
        render_filetree(e); /* panel lateral abierto */
    else
        render_filetree_toggle_closed(e); /* solo el botón para abrirlo */
    render_navbar(e);
    render_tabbar(e);
    render_find_bar(e);
    render_menu(e); /* el menú va el último: se dibuja sobre todo lo demás */

    SDL_RenderPresent(r); /* mostrar el frame ya compuesto (doble búfer) */
}
