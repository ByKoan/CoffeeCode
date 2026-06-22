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
#include "ext/ext_host.h"
#include "layout/layout.h"
#include "utf8/utf8.h"
#include <stdio.h>
#include <string.h>

/* ── Constantes ─────────────────────────────────────────────────────────────
 */
#define CURSOR_W 2       /* ancho del cursor (px)                       */
#define GUTTER_NUM_PAD 4 /* sangría del número de línea en el gutter     */
/* El texto (y los numeros de linea) se suben un par de px respecto al centrado
 * exacto de la fila, para dejar hueco bajo los descenders y que el subrayado de
 * diagnostico (squiggle) no toque las letras. */
#define TEXT_TOP_LIFT 2
#define LINE_BUF_SZ 4096 /* buffer temporal por línea visible           */
#define TOKEN_CHUNK 255  /* máx. caracteres por fragmento de texto       */

/* -- Fondo del area de texto (modo color/imagen, escalado y opacidad) ------
 */

/**
 * @brief Dibuja la imagen de fondo @p tex dentro del rectangulo @p area segun
 *        el modo de escalado @p scaling, con la opacidad @p opacity (0..255).
 *
 * Comparte la logica entre el fondo real del editor y la previsualizacion de la
 * sub-pantalla "Fondos".  Recorta al area cuando el dibujo puede salirse de
 * ella (rellenar/centrar/mosaico) y restaura el alfa de la textura al terminar.
 *
 * @param r       Renderer destino.
 * @param tex     Textura de la imagen (no nula).
 * @param bw,bh   Tamano nativo de la imagen en pixeles.
 * @param area    Rectangulo destino en pixeles.
 * @param scaling Modo de encaje (::BgScale).
 * @param opacity Opacidad aplicada (0..255).
 */
static void bg_draw_image(SDL_Renderer *r, SDL_Texture *tex, int bw, int bh,
                          SDL_FRect area, int scaling, int opacity) {
    if (!tex || bw <= 0 || bh <= 0) return; /* sin imagen valida: nada */
    Uint8 a = (Uint8)(opacity < 0 ? 0 : (opacity > 255 ? 255 : opacity));
    SDL_SetTextureAlphaMod(tex, a);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);

    float AW = area.w, AH = area.h;
    float fbw = (float)bw, fbh = (float)bh;

    switch (scaling) {
    case BG_SCALE_STRETCH: { /* deformar hasta llenar el area */
        SDL_RenderTexture(r, tex, NULL, &area);
        break;
    }
    case BG_SCALE_FIT: { /* cabe entera, conserva proporcion (letterbox) */
        float s = AW / fbw;
        if (fbh * s > AH) s = AH / fbh; /* el lado limitante manda */
        float w = fbw * s, h = fbh * s;
        SDL_FRect dst = {area.x + (AW - w) / 2.0f, area.y + (AH - h) / 2.0f, w,
                         h};
        SDL_RenderTexture(r, tex, NULL, &dst);
        break;
    }
    case BG_SCALE_FILL: { /* cubre el area, recorta el sobrante (cover) */
        float s = AW / fbw;
        if (fbh * s < AH) s = AH / fbh; /* el lado mayor manda */
        float w = fbw * s, h = fbh * s;
        SDL_FRect dst = {area.x + (AW - w) / 2.0f, area.y + (AH - h) / 2.0f, w,
                         h};
        SDL_Rect clip = {(int)area.x, (int)area.y, (int)area.w, (int)area.h};
        SDL_SetRenderClipRect(r, &clip);
        SDL_RenderTexture(r, tex, NULL, &dst);
        SDL_SetRenderClipRect(r, NULL);
        break;
    }
    case BG_SCALE_CENTER: { /* tamano nativo centrado, recortado al area */
        SDL_FRect dst = {area.x + (AW - fbw) / 2.0f, area.y + (AH - fbh) / 2.0f,
                         fbw, fbh};
        SDL_Rect clip = {(int)area.x, (int)area.y, (int)area.w, (int)area.h};
        SDL_SetRenderClipRect(r, &clip);
        SDL_RenderTexture(r, tex, NULL, &dst);
        SDL_SetRenderClipRect(r, NULL);
        break;
    }
    case BG_SCALE_TILE: { /* repetir el tamano nativo cubriendo el area */
        SDL_Rect clip = {(int)area.x, (int)area.y, (int)area.w, (int)area.h};
        SDL_SetRenderClipRect(r, &clip);
        for (float ty = area.y; ty < area.y + AH; ty += fbh)
            for (float tx = area.x; tx < area.x + AW; tx += fbw) {
                SDL_FRect dst = {tx, ty, fbw, fbh};
                SDL_RenderTexture(r, tex, NULL, &dst);
            }
        SDL_SetRenderClipRect(r, NULL);
        break;
    }
    default:
        SDL_RenderTexture(r, tex, NULL, &area);
        break;
    }

    SDL_SetTextureAlphaMod(tex, 255); /* restaurar */
}

/**
 * @brief Pinta el fondo configurado dentro del rectangulo @p area.
 *
 * Despacha por @c e->settings.background_mode: sin fondo (no dibuja nada),
 * color solido (rellena con @c background_color + opacidad) o imagen (delega en
 * ::bg_draw_image con el escalado y la opacidad configurados).  Reutilizable
 * tanto para el fondo del editor como para la previsualizacion.
 */
static void bg_fill_area(Editor *e, SDL_FRect area, int see_through) {
    const Settings *s = &e->settings;
    SDL_Renderer *r = e->renderer;
    int op = s->background_opacity;
    if (op < 0) op = 0;
    if (op > 255) op = 255;

    /* BG_MODE_NONE: no se dibuja nada -> queda el color opaco del tema. */
    if (s->background_mode == BG_MODE_NONE) return;

    /* En el editor real (see_through=1) el area de texto COMPONE con el
     * escritorio: se escribe el alfa DIRECTO (BLENDMODE_NONE) con el color ya
     * premultiplicado por la opacidad, de modo que op=0 deja ver el escritorio y
     * op=255 queda solido.  En la previsualizacion (see_through=0) se mezcla
     * sobre el color del tema ya pintado (BLEND), para mostrar el efecto dentro
     * de la pantalla de Ajustes sin abrir un agujero transparente. */
    if (s->background_mode == BG_MODE_COLOR ||
        s->background_mode == BG_MODE_TRANSPARENT) {
        Uint8 R, G, B;
        if (s->background_mode == BG_MODE_COLOR) {
            R = (Uint8)((s->background_color >> 16) & 0xFF);
            G = (Uint8)((s->background_color >> 8) & 0xFF);
            B = (Uint8)(s->background_color & 0xFF);
        } else { /* TRANSPARENT: tinte con el color del tema */
            R = e->theme.col_bg.r;
            G = e->theme.col_bg.g;
            B = e->theme.col_bg.b;
        }
        if (see_through) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(r, (Uint8)(R * op / 255),
                                   (Uint8)(G * op / 255), (Uint8)(B * op / 255),
                                   (Uint8)op);
        } else {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, R, G, B, (Uint8)op);
        }
        SDL_RenderFillRect(r, &area);
        return;
    }

    if (s->background_mode == BG_MODE_IMAGE && e->background_texture) {
        if (see_through) {
            /* dejar el area transparente y dibujar la imagen premultiplicada por
             * la opacidad (colormod + alphamod = op), para que compense con el
             * escritorio segun op: op=0 -> se ve el escritorio, op=255 -> solida. */
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
            SDL_RenderFillRect(r, &area);
            SDL_SetTextureBlendMode(e->background_texture,
                                    SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            SDL_SetTextureColorMod(e->background_texture, (Uint8)op, (Uint8)op,
                                   (Uint8)op);
            bg_draw_image(r, e->background_texture, e->background_w,
                          e->background_h, area, s->background_scaling, op);
            SDL_SetTextureColorMod(e->background_texture, 255, 255, 255);
        } else {
            SDL_SetTextureBlendMode(e->background_texture, SDL_BLENDMODE_BLEND);
            bg_draw_image(r, e->background_texture, e->background_w,
                          e->background_h, area, s->background_scaling, op);
        }
    }
}

/**
 * @brief Dibuja el fondo del area de texto del editor (bajo todo el contenido).
 *
 * Calcula el rectangulo del area de texto (igual que el render del editor) y
 * delega en ::bg_fill_area.  Con el modo "sin fondo" no toca nada, asi que el
 * editor pinta exactamente igual que antes de la migracion.
 */
void render_background_area(Editor *e) {
    int text_top = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    int text_h = e->win_h - text_top - STATUS_HEIGHT - editor_shortcut_h(e);
    if (text_h < 0) text_h = 0;
    SDL_FRect area = {0.0f, (float)text_top, (float)e->win_w, (float)text_h};
    bg_fill_area(e, area, 1); /* editor real: compone con el escritorio */
}

/**
 * @brief Indica si el editor esta en un modo de fondo "see-through" (la imagen,
 *        el color o el escritorio deben verse a traves de TODA su superficie).
 *
 * Es see-through en cualquier modo distinto de ::BG_MODE_NONE.  En ::BG_MODE_NONE
 * el editor pinta TODO opaco con el color del tema, identico a antes del fondo.
 *
 * @param e Editor. @return 1 si el chrome debe componer con el fondo, 0 si opaco.
 */
int render_is_see_through(Editor *e) {
    return e->settings.background_mode != BG_MODE_NONE;
}

/**
 * @brief Pinta el fondo configurado cubriendo TODA la ventana del editor.
 *
 * En los modos see-through (color / imagen / transparente) el fondo no se limita
 * al area de texto: cubre la ventana entera ANTES de dibujar el cromo, de modo
 * que la imagen / el color / el escritorio se vean a traves de los gutters,
 * barras de pestanas, divisores, explorador, navbar, barra de estado y panel
 * inferior.  El cromo se pinta despues semi-transparente (ver ::chrome_fill_bg)
 * para que el fondo siga asomando.  En ::BG_MODE_NONE no toca nada (cero
 * regresion: el editor queda con el color opaco del tema del ::SDL_RenderClear).
 *
 * @param e Editor.
 */
void render_background_window(Editor *e) {
    SDL_FRect area = {0.0f, 0.0f, (float)e->win_w, (float)e->win_h};
    bg_fill_area(e, area, 1); /* editor real: compone con el escritorio */
}

/**
 * @brief Rellena el fondo de una banda del cromo respetando el modo see-through.
 *
 * Es el unico punto por el que pasan los rellenos de FONDO del cromo (gutter,
 * barras de pestanas, divisores, explorador, navbar, barra de estado, panel
 * inferior).  En ::BG_MODE_NONE pinta opaco con el color @p c, exactamente igual
 * que antes (un ::set_color_c + ::fill_rect).  En los modos see-through pinta el
 * MISMO color pero semi-transparente, premultiplicado por la opacidad
 * configurada y compuesto (::SDL_BLENDMODE_BLEND_PREMULTIPLIED) sobre la capa de
 * fondo ya pintada por ::render_background_window: asi el fondo asoma a traves
 * del cromo y, donde el escritorio se ve (op<255), el alfa se conserva sin dejar
 * halos.  A mayor opacidad, mas solido el cromo; a menor, mas se ve el fondo.
 *
 * @param e       Editor.
 * @param c       Color de fondo del cromo (el mismo que se usaba opaco).
 * @param x,y,w,h Rectangulo a rellenar (px).
 */
void chrome_fill_bg(Editor *e, Color c, int x, int y, int w, int h) {
    SDL_Renderer *r = e->renderer;
    if (!render_is_see_through(e)) {
        /* modo opaco (sin fondo): identico al comportamiento de siempre */
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        set_color_c(r, c);
        fill_rect(r, x, y, w, h);
        return;
    }
    int op = e->settings.background_opacity;
    if (op < 0) op = 0;
    if (op > 255) op = 255;
    /* tinte semi-transparente premultiplicado, compuesto sobre la capa de fondo
     * (que ya esta en alfa premultiplicado tras render_background_window). */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    SDL_SetRenderDrawColor(r, (Uint8)(c.r * op / 255), (Uint8)(c.g * op / 255),
                           (Uint8)(c.b * op / 255), (Uint8)op);
    fill_rect(r, x, y, w, h);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE); /* restaurar estado */
}

void render_background_preview(Editor *e, SDL_FRect area) {
    bg_fill_area(e, area, 0); /* previsualizacion: mezcla sobre el tema */
}

/* ── Geometría del área de contenido (respeta el split de paneles) ──────────
 */

/**
 * @brief Borde derecho (X exclusiva, px) del área de contenido del editor.
 *
 * Sin división (pane_active==0) es el ancho de la ventana: el comportamiento de
 * siempre.  Con el editor dividido, devuelve el borde derecho del sub-rect del
 * panel que se está dibujando, para que texto, cursor y selección no invadan el
 * otro panel.
 *
 * @param e Editor. @return X exclusiva del borde derecho del contenido.
 */
int render_content_right(Editor *e) {
    return e->pane_active ? (e->pane_left + e->pane_width) : e->win_w;
}

/**
 * @brief Borde izquierdo (X, px) del área de contenido del editor.
 *
 * Sin división, 0 (para bandas a todo lo ancho como la línea activa).  Con
 * división, el origen del sub-rect del panel actual.
 */
int render_content_left(Editor *e) {
    return e->pane_active ? e->pane_left : 0;
}

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
    /* Premultiplicar el alfa del glifo (RGB *= A) y dibujarlo en modo de mezcla
     * premultiplicado.  En una ventana transparente, el compositor del SO usa
     * alfa premultiplicado; con el alfa recto de TTF_RenderText_Blended los
     * bordes antialias de las letras salen con halo y se ven borrosos.  Sobre un
     * fondo OPACO el resultado visible es identico al alfa recto, asi que esto no
     * cambia nada en los modos no transparentes. */
    SDL_PremultiplySurfaceAlpha(surf, false);
    /* subir a la GPU */
    SDL_Texture *tex = SDL_CreateTextureFromSurface(e->renderer, surf);
    if (tex)
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
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
    /* ancho en CARACTERES (no bytes): la columna del fin de línea es justo el
     * nº de caracteres de la línea. +1 para la celda del '\n'. */
    int dummy = 0, cols = 0;
    buf_line_col(e->buf, le, &dummy, &cols);
    return cols + 1;
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
    int text_x = left_offset + editor_gutter_w(e) + PADDING_LEFT;
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
        /* con el editor dividido, no invadir el panel contiguo */
        int right = render_content_right(e);
        if (x_start >= right) continue; /* línea fuera del panel por completo */
        if (x_end > right) x_end = right;

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
    /* fondo de la barra (opaco en BG_MODE_NONE, see-through en los demas) */
    chrome_fill_bg(e, e->theme.col_status_bg, 0, y, e->win_w, STATUS_HEIGHT);
    set_color_c(r, e->theme.col_status_sep);
    fill_rect(r, 0, y, e->win_w, 1); /* separador superior de 1 px */
    int ty = y + (STATUS_HEIGHT - e->font_size) / 2; /* centrado vertical */
    draw_text_c(e, text, 0, ty, e->theme.txt_status);

    /* Codificación, alineada a la derecha y clicable (abre el selector). */
    const char *enc = encoding_name(e->encoding);
    int ew = 0, eh = 0;
    TTF_GetStringSize(e->font, enc, 0, &ew, &eh);
    int ex = e->win_w - ew - 14;
    draw_text_c(e, enc, ex, ty, e->theme.txt_status);
    Rect encbox = {ex - 10, y, e->win_w - (ex - 10), STATUS_HEIGHT};
    ui_put(&e->ui, UI_STATUS_ENC, encbox);
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
    /* el area visible del editor descuenta tambien el panel inferior si esta
     * abierto, para que el texto de bienvenida quede centrado encima de el */
    int area_bottom = e->win_h - STATUS_HEIGHT - render_bottom_panel_height(e);
    int mid_y = area_top + (area_bottom - area_top) / 2;

    /* título dos líneas por encima del centro; cada pista una línea más abajo
     */
    draw_text_c(e, title, center_x - title_w / 2, mid_y - e->line_height * 2,
                e->theme.txt_welcome_title);
    for (int i = 0; i < 3; i++)
        draw_text_c(e, hints[i], center_x - hint_w / 2,
                    mid_y + e->line_height * i, e->theme.txt_welcome_hint);

    render_bottom_panel(e); /* panel inferior tambien sin archivo abierto */
    render_ext_panel(e); /* panel de extensiones tambien sin archivo */
    draw_status_bar(e, "  CoffeeCode");
    /* mostrar el frame (render_frame ya retornó) */
    SDL_RenderPresent(e->renderer);
}

/**
 * @brief Convierte una columna de carácter (codepoint) a un offset de byte.
 *
 * Los resaltadores de extension emiten ::CoffeeSpan en COLUMNAS DE CARACTER; la
 * cache interna (::Token) trabaja en BYTES para que el render posicione el texto
 * igual que antes.  Recorre @p line decodificando UTF-8 hasta llegar al
 * codepoint @p col (recortado al final de la línea).  Para texto ASCII (lo
 * habitual en código) byte == columna y esto es un simple avance.
 *
 * @param line      Texto de la línea (UTF-8). @param bytes Longitud en bytes.
 * @param col       Columna de carácter (codepoints) buscada.
 * @return Offset de byte correspondiente a esa columna (0..bytes).
 */
static int col_to_byte(const char *line, int bytes, uint32_t col) {
    int b = 0;
    uint32_t c = 0;
    while (b < bytes && c < col) {
        uint32_t cp;
        int n = utf8_decode(line + b, bytes - b, &cp);
        if (n <= 0) n = 1; /* defensa ante bytes inválidos */
        b += n;
        c++;
    }
    return b;
}

/**
 * @brief Vuelca @p n tramos (::CoffeeSpan, en columnas de carácter) a la línea
 *        @p lt de la cache, convirtiéndolos a ::Token (en bytes) con su color.
 *
 * @param lt        Destino en la cache (se vacía: count=0 al entrar).
 * @param line      Texto de la línea. @param bytes Longitud en bytes.
 * @param spans     Tramos en columnas de carácter. @param n Número de tramos.
 */
static void spans_to_line_tokens(LineTokens *lt, const char *line, int bytes,
                                 const CoffeeSpan *spans, int n) {
    lt->count = 0;
    for (int i = 0; i < n && lt->count < MAX_TOKENS_PER_LINE; i++) {
        int b0 = col_to_byte(line, bytes, spans[i].start_col);
        int b1 = col_to_byte(line, bytes, spans[i].start_col + spans[i].len);
        Token *t = &lt->tokens[lt->count++];
        t->col = b0;
        t->len = (b1 >= b0) ? (b1 - b0) : 0;
        t->color.r = spans[i].color.r;
        t->color.g = spans[i].color.g;
        t->color.b = spans[i].color.b;
        t->color.a = spans[i].color.a;
    }
}

/**
 * @brief Re-resalta las líneas marcadas como "sucias" usando el resaltador
 *        REGISTRADO por una extension para el lenguaje del archivo activo.
 *
 * El resaltado se cachea por línea (cada @c LineTokens guarda los tramos de una
 * línea).  Aquí se recorren todas y, las marcadas como sucias por una edición,
 * se vuelven a resaltar llamando al resaltador del host (por extension de
 * @c e->filepath).  El estado @c in_block (¿dentro de un comentario de bloque?)
 * se arrastra de una línea a la siguiente, porque un /​* abierto afecta a las
 * líneas posteriores.  Si no hay resaltador para esa extension (o no hay host),
 * las líneas quedan sin tramos (count=0) y el render las pinta en texto plano.
 *
 * Los tramos pushed por una extension (set_tokens) NO pasan por esta cache: los
 * consulta ::render_text_line por línea (tienen prioridad).
 *
 * @param e Editor (lexer y buffer activos; @c e->filepath = archivo dibujado).
 */
static void update_lexer_cache(Editor *e) {
    CoffeeHost *host = (CoffeeHost *)e->ext_host;
    /* ¿hay resaltador registrado para la extension de este archivo? */
    int has_hl = host && ext_host_has_highlighter(host, e->filepath);
    int in_block = 0; /* ¿venimos dentro de un comentario de bloque? */
    for (int li = 0; li < lexer_cache_count(e->lex); li++) {
        if (!*lexer_cache_dirty_at(e->lex, li)) continue; /* ya limpia */
        LineTokens *lt = lexer_cache_line(e->lex, li);
        if (!has_hl) {
            lt->count = 0; /* sin resaltador: texto plano */
            *lexer_cache_dirty_at(e->lex, li) = 0;
            continue;
        }
        char line_buf[LINE_BUF_SZ];
        int bytes = get_line_text(e, li, line_buf, sizeof(line_buf));
        CoffeeSpan spans[MAX_TOKENS_PER_LINE];
        int out_block = 0;
        int n = ext_host_highlight_line(host, e->filepath, line_buf, bytes,
                                        in_block, spans, MAX_TOKENS_PER_LINE,
                                        &out_block);
        if (n < 0) n = 0; /* defensa: tratar como sin tramos */
        spans_to_line_tokens(lt, line_buf, bytes, spans, n);
        in_block = out_block; /* encadenar el estado de bloque */
        *lexer_cache_dirty_at(e->lex, li) = 0;
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

/** Ancho de display (celdas) del rango de bytes line[b0, b1). */
static int count_cols(const char *line, int b0, int b1) {
    int w = 0, i = b0;
    while (i < b1) {
        uint32_t cp;
        int n = utf8_decode(line + i, b1 - i, &cp);
        w += utf8_cp_width(cp);
        i += n;
    }
    return w;
}

/**
 * @brief Dibuja el rango de bytes line[b0, b1) cuyo primer byte está en la
 *        columna de caracteres @p col0, recortando por el scroll horizontal.
 *
 * Las columnas cuentan CARACTERES, no bytes, de modo que el texto multibyte
 * (acentos, emojis) queda alineado con el cursor (1 carácter = 1 celda de
 * @c char_w px). Salta los caracteres ocultos a la izquierda por el scroll.
 */
static void draw_seg(Editor *e, const char *line, int b0, int b1, int col0,
                     int text_x, int y, Color c) {
    if (b1 <= b0) return;
    int sc = e->scroll_col;
    int col = col0, b = b0;
    /* avanzar (sin dibujar) por los caracteres ocultos por el scroll */
    while (b < b1 && col < sc) {
        uint32_t cp;
        int n = utf8_decode(line + b, b1 - b, &cp);
        col += utf8_cp_width(cp);
        b += n;
    }
    if (b < b1) {
        int x = text_x + (col - sc) * e->char_w;
        /* Con el editor dividido, recortar el fragmento al borde derecho del
         * panel para que el texto no invada el panel contiguo.  Sin división el
         * borde es e->win_w y este recorte no quita nada. */
        int right = render_content_right(e);
        if (e->char_w > 0 && right > x) {
            int max_cols = (right - x) / e->char_w;
            if (max_cols <= 0) return; /* no cabe ni un carácter */
            /* recortar b1 a max_cols caracteres de display desde b */
            int bb = b, cc = 0;
            while (bb < b1 && cc < max_cols) {
                uint32_t cp;
                int n = utf8_decode(line + bb, b1 - bb, &cp);
                cc += utf8_cp_width(cp);
                bb += n;
            }
            b1 = bb;
        }
        draw_substr(e, line, b, b1 - b, x, y, c);
    }
}

/* Convierte una columna en CODEPOINTS del buffer (las que da el LSP/cliente,
 * con el tab contando como 1) a una columna de DISPLAY (tabs expandidos a
 * tab_width, anchos CJK = 2), igual que el texto que se dibuja.  Sin esto, en
 * lineas con tabs el subrayado caeria desplazado a la izquierda (sobre la
 * indentacion). */
static int buffer_cp_to_display_col(Editor *e, int line, uint32_t cp_target) {
    const Buffer *b = e->buf;
    if (line < 0 || line >= buf_line_count(b)) return 0;
    size_t start = buf_line_offset(b, line);
    size_t total = buf_length(b);
    int tw = (e->settings.tab_width > 0) ? e->settings.tab_width : 4;

    /* reconstruir los bytes crudos de la linea (sin expandir) para decodificar */
    char raw[LINE_BUF_SZ];
    int rb = 0;
    for (size_t i = start; i < total && rb < (int)sizeof(raw) - 1; ++i) {
        char c = buf_char_at(b, i);
        if (c == '\n') break;
        raw[rb++] = c;
    }
    raw[rb] = '\0';

    int disp = 0;       /* columna de display acumulada */
    uint32_t cp = 0;    /* codepoints del buffer ya consumidos */
    int bi = 0;
    while (bi < rb && cp < cp_target) {
        if (raw[bi] == '\t') {
            disp += tw - (disp % tw); /* tab: salta a la siguiente parada */
            bi += 1;
        } else {
            uint32_t u;
            int nb = utf8_decode(raw + bi, rb - bi, &u);
            if (nb <= 0) nb = 1;
            disp += utf8_cp_width(u); /* ancho de display (CJK/emoji = 2) */
            bi += nb;
        }
        cp += 1;
    }
    /* si el rango va mas alla del fin de linea, seguir contando como display
     * 1:1 (columnas virtuales tras el ultimo caracter). */
    if (cp < cp_target) disp += (int)(cp_target - cp);
    return disp;
}

/* Subrayados ondulados (squiggles) de diagnostico de la linea @p li, dibujados
 * DESPUES del texto.  Posiciona el rango con buffer_cp_to_display_col para que
 * el trazo caiga bajo los mismos caracteres que se ven (tabs incluidos). */
static void render_text_underlines(Editor *e, int li, int text_x, int y) {
    if (!e->ext_host || !e->buf) return;
    CoffeeHost *host = (CoffeeHost *)e->ext_host;
    CoffeeUnderline ul[16];
    int n = ext_host_range_underlines(host, e->buf, (size_t)li, ul,
                                      (int)(sizeof(ul) / sizeof(ul[0])));
    if (n <= 0) return;
    int sc = e->scroll_col;
    int cw = (e->char_w > 0 ? e->char_w : 8);
    const int AMP = 2, STEP = 3, THICK = 2; /* onda suave, trazo de 2 px */
    int y_top = y + e->line_height - AMP - THICK; /* pegado a la base de la fila */
    for (int k = 0; k < n; ++k) {
        int col0 = buffer_cp_to_display_col(e, li, ul[k].start_col);
        int col1 = buffer_cp_to_display_col(e, li, ul[k].end_col);
        if (col1 <= col0) col1 = col0 + 1; /* rango de 0 ancho: una celda */
        if (col1 <= sc) continue;          /* todo oculto a la izquierda */
        if (col0 < sc) col0 = sc;          /* recorte por scroll horizontal */
        int x0 = text_x + (col0 - sc) * cw;
        int x1 = text_x + (col1 - sc) * cw;
        if (x1 <= x0) continue;
        CoffeeColor c = ul[k].color;
        SDL_SetRenderDrawColor(e->renderer, c.r, c.g, c.b, 255); /* trazo solido */
        for (int t = 0; t < THICK; ++t) { /* THICK pasadas apiladas = grosor */
            int up = 0;
            for (int x = x0; x < x1; x += STEP) {
                int xn = (x + STEP < x1) ? x + STEP : x1;
                float ya = up ? (float)(y_top + t) : (float)(y_top + AMP + t);
                float yb = up ? (float)(y_top + AMP + t) : (float)(y_top + t);
                SDL_RenderLine(e->renderer, (float)x, ya, (float)xn, yb);
                up = !up;
            }
        }
    }
}

/* Info del texto fantasma (inline hint) de una linea: lo pone una extension
 * (p.ej. el LSP de Vex con el valor de sizeof<T>).  Se INSERTA tras el codigo
 * (antes de un comentario //) empujando lo que sigue a la derecha. */
typedef struct {
    const char *text; /**< texto del hint (NULL si no hay). */
    CoffeeColor color;
    int has;       /**< 1 si la linea tiene hint. */
    int code_end;  /**< byte del texto de linea donde insertar (tras el codigo). */
    int hint_cols; /**< columnas que ocupa el hint (texto + separacion). */
} InlineHintInfo;

static InlineHintInfo get_inline_hint(Editor *e, int li, const char *line,
                                      int line_bytes) {
    InlineHintInfo h;
    h.text = NULL;
    h.has = 0;
    h.code_end = line_bytes;
    h.hint_cols = 0;
    if (!e->ext_host || !e->buf) return h;
    CoffeeHost *host = (CoffeeHost *)e->ext_host;
    const char *text = NULL;
    CoffeeColor c;
    if (!ext_host_inline_hint(host, e->buf, (size_t)li, &text, &c)) return h;
    if (!text || !text[0]) return h;
    h.text = text;
    h.color = c;
    h.has = 1;
    /* punto de insercion = tras el codigo, antes de un comentario // (fuera de
     * cadenas), recortando el espacio en blanco previo al comentario. */
    int code_end = line_bytes;
    int in_str = 0;
    char q = 0;
    for (int b = 0; b + 1 < line_bytes; ++b) {
        char ch = line[b];
        if (in_str) {
            if (ch == q && (b == 0 || line[b - 1] != '\\')) in_str = 0;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            in_str = 1;
            q = ch;
            continue;
        }
        if (ch == '/' && line[b + 1] == '/') {
            code_end = b;
            break;
        }
    }
    while (code_end > 0 && (line[code_end - 1] == ' ' || line[code_end - 1] == '\t'))
        --code_end;
    h.code_end = code_end;
    h.hint_cols = (int)strlen(text) + 2; /* 1 columna de separacion a cada lado */
    return h;
}

/* Dibuja line[from..to) en color @p c desde la columna *col, INSERTANDO el hint
 * (y desplazando lo que sigue) cuando el segmento cruza @c h->code_end.  Asi el
 * valor queda tras la expresion y el comentario se empuja a la derecha, sin
 * solaparse.  Actualiza *col y marca *done cuando inserta el hint. */
static void draw_seg_hint(Editor *e, const char *line, int from, int to, int *col,
                          int text_x, int text_y, Color c, const InlineHintInfo *h,
                          int *done) {
    int cw = (e->char_w > 0 ? e->char_w : 8);
    int d = from;
    if (h->has && !*done && from <= h->code_end && to > h->code_end) {
        if (d < h->code_end) { /* parte del codigo previa al punto */
            draw_seg(e, line, d, h->code_end, *col, text_x, text_y, c);
            *col += count_cols(line, d, h->code_end);
            d = h->code_end;
        }
        Color hc = {h->color.r, h->color.g, h->color.b,
                    (unsigned char)(h->color.a ? h->color.a : 255)};
        int hx = text_x + (*col + 1 - e->scroll_col) * cw; /* 1 col de separacion */
        draw_substr(e, h->text, 0, (int)strlen(h->text), hx, text_y, hc);
        *col += h->hint_cols;
        *done = 1;
    }
    if (d < to) { /* resto del segmento (ya desplazado si se inserto el hint) */
        draw_seg(e, line, d, to, *col, text_x, text_y, c);
        *col += count_cols(line, d, to);
    }
}

/* Dibuja el hint al final del texto cuando la linea no tenia comentario (no se
 * inserto en medio): va tras el ultimo caracter. */
static void draw_trailing_hint(Editor *e, int col, int text_x, int text_y,
                               const InlineHintInfo *h, int done) {
    if (!h->has || done) return;
    int cw = (e->char_w > 0 ? e->char_w : 8);
    Color hc = {h->color.r, h->color.g, h->color.b,
                (unsigned char)(h->color.a ? h->color.a : 255)};
    int hx = text_x + (col + 1 - e->scroll_col) * cw;
    draw_substr(e, h->text, 0, (int)strlen(h->text), hx, text_y, hc);
}

/**
 * @brief Dibuja una línea de texto con el resaltado aportado por las extensiones.
 *
 * Obtiene el texto de la línea y elige el origen de los tramos coloreados en
 * este orden de prioridad:
 *   1. Tramos PUSHED por una extension (set_tokens, p.ej. semantic tokens de un
 *      LSP) para esta línea del buffer activo: tienen prioridad.
 *   2. Tramos cacheados por el resaltador SINCRONO registrado para el lenguaje
 *      del archivo (ver ::update_lexer_cache).
 *   3. Si no hay ni unos ni otros: texto plano en el color por defecto del tema.
 *
 * Recorre los tramos dibujando: los huecos sin tramo en color por defecto y cada
 * tramo en SU color (decidido por la extension).  Todo se ajusta al scroll
 * horizontal (@c scroll_col): las columnas a la izquierda del scroll se
 * recortan.
 *
 * @param e      Editor. @param li Índice de línea. @param y Y de la fila (px).
 * @param text_x X del primer carácter de texto (px).
 */
static void render_text_line(Editor *e, int li, int y, int text_x) {
    char line_buf[LINE_BUF_SZ];
    /* texto expandido (bytes UTF-8); line_bytes = longitud en BYTES */
    int line_bytes = get_line_text(e, li, line_buf, sizeof(line_buf));
    /* centrado vertical en la fila */
    int text_y = y + (e->line_height - e->font_size) / 2 - TEXT_TOP_LIFT;
    Color def = e->theme.tokens[TOK_DEFAULT]; /* color de texto por defecto */

    /* Elegir el origen de los tramos: (1) pushed por extension, (2) cache. */
    LineTokens pushed; /* almacen local si los tramos vienen pushed */
    LineTokens *lt = NULL;
    CoffeeHost *host = (CoffeeHost *)e->ext_host;
    if (host && ext_host_has_pushed_tokens(host, e->buf)) {
        CoffeeSpan spans[MAX_TOKENS_PER_LINE];
        int n = ext_host_line_tokens(host, e->buf, (size_t)li, spans,
                                     MAX_TOKENS_PER_LINE);
        if (n >= 0) { /* esta línea tiene tramos pushed (n>=0; 0 = vacía) */
            spans_to_line_tokens(&pushed, line_buf, line_bytes, spans, n);
            lt = &pushed;
        }
    }
    if (!lt) /* sin pushed para esta línea: usar la cache del resaltador */
        lt = (li < lexer_cache_count(e->lex)) ? lexer_cache_line(e->lex, li)
                                              : NULL;

    /* Info del inline hint (valor comptime, etc.): se INSERTA tras el codigo,
     * empujando el comentario; en lineas sin comentario va al final. */
    InlineHintInfo hint = get_inline_hint(e, li, line_buf, line_bytes);

    /* Sin tramos: dibujar la línea entera en color por defecto. */
    if (!lt || lt->count == 0) {
        int col = 0, hint_done = 0;
        draw_seg_hint(e, line_buf, 0, line_bytes, &col, text_x, text_y, def,
                      &hint, &hint_done);
        draw_trailing_hint(e, col, text_x, text_y, &hint, hint_done);
        render_text_underlines(e, li, text_x, y); /* squiggles encima */
        return;
    }

    /* Con tramos: en orden, los huecos (sin tramo) en color por defecto y cada
     * tramo con SU color. Los tramos vienen en BYTES; aquí se posicionan por
     * COLUMNAS de carácter (draw_seg) para alinear multibyte.
     * `drawn` = byte ya cubierto; `col` = su columna de caracteres. */
    int drawn = 0, col = 0, hint_done = 0;
    for (int ti = 0; ti < lt->count; ti++) {
        Token *tok = &lt->tokens[ti];
        int tok_start = tok->col;
        int tok_end = tok->col + tok->len;
        if (tok_start > line_bytes) tok_start = line_bytes;
        if (tok_end > line_bytes) tok_end = line_bytes;
        if (tok_start < drawn) tok_start = drawn; /* defensivo: solapes */

        /* hueco antes del tramo (color por defecto), insertando el hint si cae */
        if (drawn < tok_start) {
            draw_seg_hint(e, line_buf, drawn, tok_start, &col, text_x, text_y,
                          def, &hint, &hint_done);
            drawn = tok_start;
        }
        /* el tramo, con su color (insertando el hint si cae en su rango) */
        if (drawn < tok_end) {
            draw_seg_hint(e, line_buf, drawn, tok_end, &col, text_x, text_y,
                          tok->color, &hint, &hint_done);
            drawn = tok_end;
        }
    }

    /* texto restante tras el último tramo (en color por defecto) */
    if (drawn < line_bytes)
        draw_seg_hint(e, line_buf, drawn, line_bytes, &col, text_x, text_y, def,
                      &hint, &hint_done);

    /* si no habia comentario, el hint no se inserto: ponerlo tras el texto */
    draw_trailing_hint(e, col, text_x, text_y, &hint, hint_done);
    /* subrayados de diagnostico (squiggles) por encima del texto de la linea */
    render_text_underlines(e, li, text_x, y);
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
    int text_x = left_offset + editor_gutter_w(e) + PADDING_LEFT;
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
    if (!e->settings.show_line_numbers)
        return; /* gutter oculto: nada que pintar */
    /* fondo del gutter (opaco en BG_MODE_NONE, see-through en los demas) */
    chrome_fill_bg(e, e->theme.col_gutter, left_offset, text_top, GUTTER_WIDTH,
                   text_height);

    for (int vi = 0; vi < visible_lines; vi++) {
        int li = e->scroll_line + vi;
        if (li >= total_lines) break;
        int y = text_top + vi * e->line_height;
        /* Marcador de gutter de una extension (set_gutter_marker): si esta linea
         * lo tiene, sustituye al numero de linea (queda mas limpio, estilo icono
         * de error del LSP).  Sin marcador (caso por defecto) se dibuja el numero
         * como siempre: cero regresion. */
        if (render_ext_gutter_marker(e, li, left_offset, y)) continue;
        char num[16];
        /* 1-based, alineado a la derecha */
        snprintf(num, sizeof(num), "%4d", li + 1);
        draw_text_c(e, num, left_offset + GUTTER_NUM_PAD,
                    y + (e->line_height - e->font_size) / 2 - TEXT_TOP_LIFT,
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

    int cx =
        left_offset + editor_gutter_w(e) + PADDING_LEFT + vis_col * e->char_w;
    int cy = text_top + vis_line * e->line_height;
    /* con el editor dividido, no pintar el cursor si cae fuera del panel */
    if (cx >= render_content_right(e)) return;
    set_color_c(e->renderer, e->theme.col_cursor);
    /* barra vertical del cursor */
    fill_rect(e->renderer, cx, cy, CURSOR_W, e->line_height);
}

/**
 * @brief Dibuja las capas del contenido del editor (línea activa, selección,
 *        texto, gutter, cursor) para la pestaña que el editor tiene "en vivo".
 *
 * Aísla el dibujado del CONTENIDO para reutilizarlo tanto en el editor sin
 * dividir como en cada panel del editor dividido.  La banda activa horizontal
 * va de @p left_offset (incluido el gutter) a @p content_right; el resto de la
 * geometría se pasa explícita.
 *
 * @param e             Editor (lee buf/cursor/scroll/selección "en vivo").
 * @param left_offset   X donde empieza el área de este panel (px).
 * @param content_right X exclusiva del borde derecho del panel (px).
 * @param text_top      Y de la primera fila de texto (px).
 * @param text_height   Alto del área de texto (px).
 * @param visible_lines Filas que caben.
 * @param total_lines   Total de líneas del buffer.
 */
static void render_content_layers(Editor *e, int left_offset, int content_right,
                                  int text_top, int text_height,
                                  int visible_lines, int total_lines) {
    SDL_Renderer *r = e->renderer;
    /* resaltado de la línea activa (banda completa del panel) */
    if (e->settings.highlight_current_line && !e->sel_active) {
        int vi_cursor = e->cursor_line - e->scroll_line;
        if (vi_cursor >= 0 && vi_cursor < visible_lines) {
            int band_left = render_content_left(e);
            set_color_c(r, e->theme.col_cursor_line);
            fill_rect(r, band_left, text_top + vi_cursor * e->line_height,
                      content_right - band_left, e->line_height);
        }
    }
    /* fondos de linea decorados por extensiones (set_line_background): se pintan
     * ANTES de la seleccion y el texto, como banda completa del panel.  Sin
     * decoraciones (caso por defecto) este bucle no pinta nada. */
    render_ext_line_backgrounds(e, content_right, text_top, visible_lines,
                                total_lines);
    render_selection(e, left_offset, text_top, visible_lines);
    render_text_area(e, left_offset, text_top, visible_lines, total_lines);
    render_gutter(e, left_offset, text_top, text_height, visible_lines,
                  total_lines);
    render_cursor(e, left_offset, text_top, visible_lines);
}

/** Dibuja los divisores internos del árbol (líneas entre hojas hermanas). */
static void render_dock_dividers(Editor *e, int node, DockRect area) {
    if (node < 0 || node >= e->dock.node_count) return;
    DockNode *n = &e->dock.nodes[node];
    if (n->kind != DOCK_SPLIT) return;

    /* repartir el área con la MISMA matemática que el layout (sin duplicarla) */
    DockRect ra, rb;
    dock_child_areas(&e->dock, node, area, &ra, &rb);

    /* divisor entre hojas: see-through como el resto del cromo */
    if (n->orient == DOCK_VERTICAL)
        chrome_fill_bg(e, e->theme.col_tabbar_sep, rb.x - 1, area.y, 2,
                       area.h); /* línea vertical */
    else
        chrome_fill_bg(e, e->theme.col_tabbar_sep, area.x, rb.y - 1, area.w,
                       2); /* línea horizontal */

    render_dock_dividers(e, n->child_a, ra);
    render_dock_dividers(e, n->child_b, rb);
}

/**
 * @brief Dibuja las hojas del editor dividido recorriendo el árbol de dock.
 *
 * Computa el rect de cada hoja con dock_compute_leaf_rects y, para cada una,
 * enlaza su pestaña activa "en vivo" (sin recargar del disco), activa el
 * override de área (e->pane_*) con el sub-rect de contenido de la hoja (bajo su
 * propia barra de pestañas), dibuja su barra de pestañas y su contenido
 * recortado, y resalta con un acento la hoja enfocada.  Al final vuelve a
 * enlazar la pestaña de la hoja con foco para que e-> termine reflejándola.
 * Asume que el llamante ya guardó el estado de la hoja enfocada con
 * editor_tab_save_state.
 *
 * @param e    Editor (dock.leaf_count > 1).
 * @param area Área total del editor a repartir (px).
 */
static void render_split_panes(Editor *e, DockRect area) {
    SDL_Renderer *r = e->renderer;

    DockLeafRect leaves[DOCK_MAX_LEAVES];
    int nleaves = dock_compute_leaf_rects(&e->dock, area, leaves, DOCK_MAX_LEAVES);

    for (int li = 0; li < nleaves; li++) {
        int g = leaves[li].group_id;
        DockRect lr = leaves[li].rect; /* rect total de la hoja (incluye tabbar) */
        /* indice VALIDO del grupo: solo dibuja una pestana que pertenezca a esta
         * hoja (nunca el buffer de otro grupo si el indice guardado quedo obsoleto) */
        int idx = editor_group_valid_active_tab(e, g);

        /* geometría del contenido de la hoja: bajo su barra de pestañas */
        int text_top = lr.y + TAB_BAR_HEIGHT;
        int text_height = lr.h - TAB_BAR_HEIGHT;
        if (text_height < 0) text_height = 0;
        int pane_left = lr.x;
        int pane_right = lr.x + lr.w;
        int visible_lines = (e->line_height > 0) ? text_height / e->line_height : 0;

        if (idx >= 0 && idx < e->tab_count) {
            editor_render_bind_tab(e, idx); /* e->buf -> pestaña de la hoja */

            /* override del área de contenido de esta hoja */
            e->pane_active = 1;
            e->pane_left = pane_left;
            e->pane_top = text_top;
            e->pane_width = pane_right - pane_left;
            e->pane_height = text_height;

            /* re-tokenizar las líneas sucias de ESTA hoja (cada pestaña tiene su
             * propia cache; el update previo solo cubrió la de la hoja enfocada) */
            update_lexer_cache(e);

            int total_lines = buf_line_count(e->buf);
            render_content_layers(e, pane_left, pane_right, text_top, text_height,
                                  visible_lines, total_lines);
        }

        /* barra de pestañas propia de la hoja (solo las pestañas de este grupo) */
        render_tabbar_group(e, g, lr.y, pane_left, pane_right);

        /* acento en la hoja enfocada: una línea fina sobre su contenido */
        if (g == e->active_group) {
            set_color_c(r, e->theme.col_tab_accent);
            fill_rect(r, pane_left, text_top, pane_right - pane_left, 2);
        }
    }

    e->pane_active = 0; /* fin del override */

    /* divisores internos entre hojas hermanas */
    render_dock_dividers(e, e->dock.root, area);

    /* dejar e-> reflejando la hoja con foco (su estado ya estaba guardado) */
    editor_render_bind_tab(e, e->group_active_tab[e->active_group]);
}

/** Ultimo componente de una ruta (nombre de archivo), o la ruta entera si no
 *  tiene separadores.  Local a render.c (render_ui.c tiene su propia copia). */
static const char *float_basename(const char *path) {
    if (!path || !path[0]) return "Sin título";
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    return base[0] ? base : path;
}

void render_floats(Editor *e) {
    if (e->float_count <= 0) return; /* sin flotantes: nada que dibujar */
    SDL_Renderer *r = e->renderer;

    /* z-order ascendente: floats[0] al fondo, el ultimo al frente */
    for (int fi = 0; fi < e->float_count; fi++) {
        FloatPanel *fp = &e->floats[fi];
        int focused = (e->float_count - 1 == fi); /* el del frente = enfocado */
        DockRect frame = {fp->rect.x, fp->rect.y, fp->rect.w, fp->rect.h};

        /* sombra suave detras del marco (desplazada) */
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        set_color(r, 0, 0, 0, 0x60);
        fill_rect(r, frame.x + 4, frame.y + 4, frame.w, frame.h);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        /* fondo del marco */
        set_color_c(r, e->theme.col_bg);
        fill_rect(r, frame.x, frame.y, frame.w, frame.h);

        /* barra de titulo */
        Rect tb = float_titlebar_rect(fp);
        set_color_c(r, focused ? e->theme.col_tab_active : e->theme.col_tabbar_bg);
        fill_rect(r, tb.x, tb.y, tb.w, tb.h);
        if (focused) { /* acento superior en el flotante enfocado */
            set_color_c(r, e->theme.col_tab_accent);
            fill_rect(r, tb.x, tb.y, tb.w, 2);
        }

        /* titulo = nombre de la pestana activa del flotante */
        int idx = e->group_active_tab[fp->group_id];
        const char *name = "Sin título";
        if (idx >= 0 && idx < e->tab_count)
            name = float_basename(e->tabs[idx].filepath);
        {
            /* el clip del titulo deja sitio a los tres botones de la derecha */
            SDL_Rect clip = {tb.x + 6, tb.y, tb.w - FLOAT_BTN_SZ * 3 - 20, tb.h};
            SDL_SetRenderClipRect(r, &clip);
            draw_text(e, name, tb.x + 6, tb.y + (tb.h - e->font_size) / 2, 0xCC,
                      0xCC, 0xDD);
            SDL_SetRenderClipRect(r, NULL);
        }

        /* boton desprender (recuadro con flecha hacia arriba "^": a una ventana
         * del SO), boton acoplar (flecha hacia abajo "v") y boton cerrar "x" */
        Rect detach_b = float_detach_rect(fp);
        set_color_c(r, e->theme.col_tabbar_sep);
        stroke_rect(r, detach_b.x, detach_b.y, detach_b.w, detach_b.h);
        draw_text(e, "^", detach_b.x + 5,
                  detach_b.y + (detach_b.h - e->font_size) / 2, 0xAA, 0xAA, 0xBB);
        Rect dock_b = float_dock_rect(fp);
        set_color_c(r, e->theme.col_tabbar_sep);
        stroke_rect(r, dock_b.x, dock_b.y, dock_b.w, dock_b.h);
        draw_text(e, "v", dock_b.x + 5, dock_b.y + (dock_b.h - e->font_size) / 2,
                  0xAA, 0xAA, 0xBB);
        Rect close_b = float_close_rect(fp);
        draw_text(e, "×", close_b.x + 4,
                  close_b.y + (close_b.h - e->font_size) / 2, 0xAA, 0xAA, 0xBB);

        /* tira de pestanas del flotante (solo las de su grupo) */
        Rect tabbar = float_tabbar_rect(fp);
        render_tabbar_group(e, fp->group_id, tabbar.y, tabbar.x,
                            tabbar.x + tabbar.w);

        /* contenido: enlazar su pestana activa y dibujar con override de area */
        Rect content = float_content_rect(fp);
        if (idx >= 0 && idx < e->tab_count && content.h > 0) {
            editor_render_bind_tab(e, idx);
            e->pane_active = 1;
            e->pane_left = content.x;
            e->pane_top = content.y;
            e->pane_width = content.w;
            e->pane_height = content.h;
            update_lexer_cache(e); /* re-tokenizar las lineas sucias de esta hoja */
            int visible_lines =
                (e->line_height > 0) ? content.h / e->line_height : 0;
            int total_lines = buf_line_count(e->buf);
            render_content_layers(e, content.x, content.x + content.w, content.y,
                                  content.h, visible_lines, total_lines);
            e->pane_active = 0;
        }

        /* marco/borde exterior (acento si enfocado) */
        set_color_c(r, focused ? e->theme.col_tab_accent : e->theme.col_tabbar_sep);
        stroke_rect(r, frame.x, frame.y, frame.w, frame.h);

        /* esquina de redimension (un par de lineas en diagonal) */
        Rect rz = float_resize_rect(fp);
        set_color_c(r, e->theme.col_tabbar_sep);
        for (int g = 4; g < FLOAT_RESIZE_SZ; g += 4) {
            fill_rect(r, rz.x + FLOAT_RESIZE_SZ - g, rz.y + FLOAT_RESIZE_SZ - 2, g,
                      1);
        }
    }

    /* dejar e-> reflejando la pestana del grupo con foco (su estado ya guardado
     * por el llamante antes de render_floats) */
    int fg = e->group_active_tab[e->active_group];
    if (fg >= 0 && fg < e->tab_count) editor_render_bind_tab(e, fg);
}

/**
 * @brief Dibuja UNA ventana desprendida (su tira de pestanas + el contenido de
 *        su pestana activa) en el renderer pasado, con sus propias dimensiones.
 *
 * Entra/sale con el estado del Editor INTACTO salvo lo que restaura el llamante
 * (editor_render_detached salva renderer/tamano/bind y los repone tras cada
 * ventana).  Aqui se conmuta temporalmente e->renderer/e->win_w/e->win_h a los de
 * la ventana desprendida para que los helpers de dibujo (draw_text, fill_rect,
 * render_tabbar_group, render_content_layers) operen sobre ELLA, y se fija el
 * override de area (pane_*) al rect de contenido de la ventana.
 *
 * @param e Editor.
 * @param dr Renderer de la ventana desprendida.
 * @param group  group_id del grupo de pestanas que muestra la ventana.
 * @param win_w  Ancho de la ventana (px).
 * @param win_h  Alto de la ventana (px).
 */
void render_detached_window(Editor *e, SDL_Renderer *dr, int group, int win_w,
                            int win_h) {
    /* conmutar el renderer/tamano del Editor a los de la ventana desprendida */
    SDL_Renderer *saved_r = e->renderer;
    int saved_w = e->win_w, saved_h = e->win_h;
    e->renderer = dr;
    e->win_w = win_w;
    e->win_h = win_h;

    /* vaciar el hit-test: render_tabbar_group registra aqui la geometria de las
     * pestanas de la ventana desprendida, que el input consulta para enrutar el
     * clic en su tira. */
    ui_reset(&e->ui);

    /* fondo: limpiar opaco con el color del tema y luego aplicar el modo de
     * fondo (color / imagen / transparente) igual que la ventana principal.
     * e->renderer, e->win_w y e->win_h ya estan conmutados a esta ventana, asi
     * que render_background_window compone sobre el area correcta. */
    set_color_c(dr, e->theme.col_bg);
    SDL_RenderClear(dr);
    render_background_window(e);

    /* indice VALIDO de la pestana activa del grupo (nunca la de otro grupo) */
    int idx = editor_group_valid_active_tab(e, group);

    Rect content = detached_content_rect(win_w, win_h);

    if (idx >= 0 && idx < e->tab_count && content.h > 0) {
        editor_render_bind_tab(e, idx); /* e->buf -> pestana activa del grupo */

        /* override del area de contenido: pane_left==0 (sin panel lateral en una
         * ventana desprendida), pane_top bajo la tira de pestanas. */
        e->pane_active = 1;
        e->pane_left = content.x;
        e->pane_top = content.y;
        e->pane_width = content.w;
        e->pane_height = content.h;

        update_lexer_cache(e); /* re-tokenizar lineas sucias de esta pestana */

        int visible_lines = (e->line_height > 0) ? content.h / e->line_height : 0;
        int total_lines = buf_line_count(e->buf);
        render_content_layers(e, content.x, content.x + content.w, content.y,
                              content.h, visible_lines, total_lines);

        e->pane_active = 0;
    }

    /* tira de pestanas del grupo en el borde superior (a todo el ancho) */
    Rect tabbar = detached_tabbar_rect(win_w, win_h);
    render_tabbar_group(e, group, tabbar.y, tabbar.x, tabbar.x + tabbar.w);

    SDL_RenderPresent(dr);

    /* restaurar el renderer/tamano del Editor (el bind lo restaura el llamante) */
    e->renderer = saved_r;
    e->win_w = saved_w;
    e->win_h = saved_h;
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

    /* Pantalla de preferencias: sustituye al editor mientras esta abierta.
     * Si ademas esta abierta la sub-pantalla "Fondos", se dibuja esa. */
    if (e->settings_open) {
        if (e->background_view_open)
            render_background_view(e);
        else
            render_settings_view(e);
        SDL_RenderPresent(r);
        return;
    }
    /* offset izquierdo: ancho del panel si está abierto, o el del botón si
     * cerrado */
    int left_offset = e->ftree.open ? e->ftree.width : FTREE_TOGGLE_BTN_W;
    /* Y donde empieza el texto */
    int text_top = NAVBAR_HEIGHT + TAB_BAR_HEIGHT;
    /* alto del área de texto = ventana menos las bandas de UI (incluido el
     * panel inferior si esta abierto, para que el texto no quede tapado). */
    int text_height = e->win_h - NAVBAR_HEIGHT - TAB_BAR_HEIGHT -
                      STATUS_HEIGHT - editor_shortcut_h(e) -
                      render_bottom_panel_height(e);
    int visible_lines = text_height / e->line_height; /* filas que caben */
    int total_lines = (e->tab_count > 0) ? buf_line_count(e->buf) : 0;

    /* Limpieza del frame: SIEMPRE opaca con el color del tema.  El area de texto
     * que deba ser see-through (modos Color/Imagen/Transparente) se vuelve a
     * pintar a continuacion en render_background_area sobrescribiendo su alfa,
     * de modo que nunca queda un agujero transparente fuera del area de texto. */
    set_color_c(r, e->theme.col_bg);
    SDL_RenderClear(r);

    /* Fondo a VENTANA COMPLETA segun el modo activo (sin fondo / color / imagen /
     * transparente).  En los modos see-through la imagen/color/escritorio cubre
     * TODA la ventana ANTES del cromo, que luego se pinta semi-transparente
     * (chrome_fill_bg) para dejar asomar el fondo por gutters, barras, divisores,
     * explorador, navbar, status y panel inferior.  En BG_MODE_NONE no toca nada
     * y queda el color opaco del tema del RenderClear (cero regresion). */
    render_background_window(e);

    if (e->tab_count == 0) {    /* sin archivos: pantalla de bienvenida */
        render_empty_screen(e); /* (hace su propio RenderPresent) */
        return;
    }

    /* re-tokenizar líneas sucias antes de dibujar texto */
    update_lexer_cache(e);

    /* Guardar el estado de la pestana enfocada antes de barajar vistas para los
     * sub-paneles (split y/o flotantes).  Se restaura al final del frame.  Con
     * cero flotantes y sin division esto es inocuo (vuelca y recarga la misma). */
    int have_floats = (e->float_count > 0);
    if (e->dock.leaf_count > 1 || have_floats || e->detached_count > 0)
        editor_tab_save_state(e);

    /* Si el foco esta en un flotante (su group_id no es ninguna hoja del dock),
     * el area del dock debe dibujar la pestana de la hoja del dock con foco, no
     * la del flotante.  Se enlaza temporalmente esa pestana para el dibujo del
     * dock; render_floats y el cierre del frame rebindan despues. */
    int focus_in_dock = (dock_leaf_by_group(&e->dock, e->active_group) != DOCK_NONE);
    int dock_draw_tab = e->active_tab; /* por defecto, la enfocada */
    /* 1 = la hoja del dock con foco tiene una pestana valida que dibujar.  Con el
     * foco dentro del dock siempre la hay; con el foco en un flotante depende de
     * que la hoja del dock no se haya quedado vacia. */
    int dock_has_tab = 1;
    if (!focus_in_dock) {
        /* el foco esta en un flotante: el dock dibuja la pestana de SU hoja con
         * foco.  Usar el indice VALIDO del grupo (que pertenece a la hoja): si la
         * hoja se quedo vacia (su unica pestana se fue al flotante) di es -1 y NO
         * se dibuja su contenido, evitando pintar el buffer del flotante en el
         * area del dock (bug del buffer compartido). */
        int dg = e->dock.nodes[e->dock.focused_leaf].group_id;
        int di = editor_group_valid_active_tab(e, dg);
        if (di >= 0 && di < e->tab_count)
            dock_draw_tab = di;
        else
            dock_has_tab = 0; /* hoja del dock vacia: area en blanco */
    }

    if (e->dock.leaf_count > 1) {
        /* Editor dividido: cada hoja dibuja sus pestañas y su contenido en su
         * sub-rect.  render_split_panes recorre solo las hojas del dock (ignora
         * los grupos flotantes) y restaura la pestana del grupo con foco. */
        render_split_panes(e, editor_dock_area(e));
    } else {
        /* Editor sin dividir: comportamiento de siempre (pane_active==0).  Si el
         * foco esta en un flotante, dibujar el contenido del dock con su hoja
         * (recalculando total_lines para ESA pestana, no la del flotante). */
        int dl = total_lines;
        if (!focus_in_dock && dock_has_tab) {
            editor_render_bind_tab(e, dock_draw_tab);
            update_lexer_cache(e); /* tokens de la pestana del dock */
            dl = buf_line_count(e->buf);
        }
        /* Dibujar el contenido del area del dock solo si hay una pestana suya que
         * mostrar.  Si el foco esta en un flotante y la hoja del dock se quedo
         * vacia, el area queda en blanco (el fondo ya esta limpio) en vez de
         * pintar el buffer del flotante. */
        if (focus_in_dock || dock_has_tab) {
            /* resaltado de la línea activa (banda completa; solo si está activado
             * en preferencias y no hay selección) */
            if (e->settings.highlight_current_line && !e->sel_active) {
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
                             dl); /* texto resaltado */
            render_gutter(e, left_offset, text_top, text_height, visible_lines,
                          dl); /* numeros de linea */
            render_cursor(e, left_offset, text_top,
                          visible_lines); /* barra del cursor */
        }

        /* si se dibujo el dock con la pestana de su hoja (foco en flotante),
         * rebindar la pestana enfocada para que el status bar la refleje. */
        if (!focus_in_dock && dock_has_tab) {
            editor_render_bind_tab(e, e->group_active_tab[e->active_group]);
            update_lexer_cache(e);
        }
    }

    /* barra de estado: nombre + posición del cursor (1-based) + '*' si
     * modificado */
    char status[384];
    if (e->ext_status[0])
        /* una extension fijo un mensaje (CoffeeApi::set_status): mostrarlo */
        snprintf(status, sizeof(status), " CoffeeCode | Ln %d, Col %d%s | %s",
                 e->cursor_line + 1, e->cursor_col + 1, e->modified ? "  *" : "",
                 e->ext_status);
    else
        snprintf(status, sizeof(status), " CoffeeCode | Ln %d, Col %d%s |",
                 e->cursor_line + 1, e->cursor_col + 1, e->modified ? "  *" : "");
    draw_status_bar(e, status);

    /* cromo de la UI por encima del texto.  Con el editor dividido, la barra de
     * pestañas y la scrollbar globales se omiten: cada panel ya dibujó su propia
     * tira de pestañas en render_split_panes, y una scrollbar global a todo lo
     * alto no representaría a un único panel. */
    if (e->dock.leaf_count <= 1) render_scrollbar(e, left_offset);
    if (e->ftree.open)
        render_filetree(e); /* panel lateral abierto */
    else
        render_filetree_toggle_closed(e); /* solo el botón para abrirlo */
    render_bottom_panel(e); /* panel inferior (Salida/Logs/Terminal) */
    render_ext_panel(e); /* panel de extensiones, bajo la navbar */
    render_navbar(e);
    if (e->dock.leaf_count <= 1) render_tabbar(e);
    render_floats(e);   /* paneles flotantes: overlay ENCIMA del dock */
    render_float_dock_guide(e); /* guia de re-acople de un flotante arrastrado */
    render_find_bar(e);
    render_menu(e); /* el menú va el último: se dibuja sobre todo lo demás */
    render_enc_popup(e); /* selector de codificación, por encima de todo */
    render_tab_drag(e);  /* guia del arrastre de pestañas, sobre todo lo demás */
    render_drag_window_highlight(e); /* multi-ventana: marco de "soltar aqui" */

    SDL_RenderPresent(r); /* mostrar el frame ya compuesto (doble búfer) */
}
