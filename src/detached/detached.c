/**
 * @file detached.c
 * @brief Ventanas desprendidas: ciclo de vida (crear/cerrar) de la ventana del
 *        SO, enrutado de eventos por SDL_WindowID y render multi-ventana.
 *
 * Una ventana desprendida saca un grupo de pestanas a una ventana REAL del
 * sistema, con su propio @c SDL_Window / @c SDL_Renderer.  Aqui vive la parte que
 * toca SDL directamente (crear/destruir esos recursos, presentar en el segundo
 * renderer, identificar a que ventana pertenece un evento).  La geometria pura
 * (tira de pestanas + contenido) esta en detached_geom.c; el render del contenido
 * lo hace render_detached_window (render.c) reutilizando los helpers de dibujo; y
 * el manejo de teclado/raton de una ventana desprendida lo hace
 * editor_detached_handle_event (input_mouse.c) reutilizando los manejadores de
 * input.
 *
 * Invariante de cero regresion: con @c detached_count == 0 NADIE entra aqui.
 */
#include "editor/editor.h"
#include "render/render.h"

void editor_detach_float(Editor *e, int fi) {
    if (fi < 0 || fi >= e->float_count) return;
    if (e->detached_count >= MAX_DETACHED) return; /* sin sitio para mas ventanas */

    FloatPanel *fp = &e->floats[fi];
    int group = fp->group_id;
    int w = fp->rect.w, h = fp->rect.h; /* tamano de la ventana = el del flotante */
    if (w < 200) w = 200;               /* tamano minimo sensato para la ventana */
    if (h < 150) h = 150;

    /* crear la ventana del SO + su renderer.  Si SDL falla, conservar el flotante
     * (no se pierde el grupo) y salir. */
    SDL_Window *win = SDL_CreateWindow("CoffeeCode", w, h, SDL_WINDOW_RESIZABLE);
    if (!win) return;
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) {
        SDL_DestroyWindow(win);
        return;
    }
    SDL_SetRenderVSync(ren, 1);
    SDL_StartTextInput(win); /* habilitar eventos de texto en esta ventana */

    /* registrar la ventana desprendida con el grupo del flotante */
    DetachedWindow *dw = &e->detached[e->detached_count++];
    dw->window = win;
    dw->renderer = ren;
    dw->group_id = group;
    dw->win_w = w;
    dw->win_h = h;

    /* eliminar el FloatPanel in-window: sus pestanas siguen en tabs[] con el mismo
     * group_id, ahora mostradas por la ventana desprendida.  Compactar el z-order
     * de flotantes. */
    for (int i = fi; i < e->float_count - 1; i++) e->floats[i] = e->floats[i + 1];
    e->float_count--;

    /* la nueva ventana toma el foco del teclado */
    e->detached_focus_group = group;
    e->needs_redraw = 1;
}

int editor_detached_by_window_id(Editor *e, unsigned int window_id) {
    for (int i = 0; i < e->detached_count; i++) {
        if (!e->detached[i].window) continue;
        if (SDL_GetWindowID((SDL_Window *)e->detached[i].window) == window_id)
            return i;
    }
    return -1; /* ninguna: el evento es de la ventana principal */
}

void editor_render_detached(Editor *e) {
    if (e->detached_count <= 0) return; /* cero regresion: nada que dibujar */

    /* salvar el estado "en vivo" de la ventana principal: la pestana enfocada,
     * para reponerla tras dibujar cada ventana desprendida (render_detached_window
     * rebindeo e->buf a la pestana del grupo desprendido). */
    int main_active = e->active_tab;
    int saved_pane = e->pane_active;

    /* Salvar el registro de hit-test de la ventana PRINCIPAL: render_frame lo
     * acaba de poblar para ella; render_detached_window lo sobrescribe con la
     * geometria de cada ventana desprendida.  Sin reponerlo, un clic posterior en
     * la ventana principal consultaria geometria ajena (regresion).  El registro
     * es POD (arrays fijos), asi que se copia por valor. */
    UiRegistry saved_ui = e->ui;

    for (int i = 0; i < e->detached_count; i++) {
        DetachedWindow *dw = &e->detached[i];
        if (!dw->renderer) continue;
        render_detached_window(e, (SDL_Renderer *)dw->renderer, dw->group_id,
                               dw->win_w, dw->win_h);
    }

    /* reponer el bind y el hit-test de la ventana principal: e->buf vuelve a la
     * pestana enfocada del editor principal (render_detached_window dejo e->
     * apuntando a la pestana del ultimo grupo desprendido) y e->ui vuelve a la
     * geometria de la ventana principal. */
    e->pane_active = saved_pane;
    if (main_active >= 0 && main_active < e->tab_count)
        editor_render_bind_tab(e, main_active);
    e->ui = saved_ui;
}

void editor_detached_close(Editor *e, int di) {
    if (di < 0 || di >= e->detached_count) return;

    DetachedWindow dw = e->detached[di]; /* copia: el array se compacta abajo */
    int group = dw.group_id;

    /* quitar la ventana del array ANTES de re-acoplar (asi el re-acople ve el
     * estado sin esta ventana y el foco se reubica correctamente). */
    for (int i = di; i < e->detached_count - 1; i++)
        e->detached[i] = e->detached[i + 1];
    e->detached_count--;

    /* el foco del teclado vuelve a la ventana principal */
    if (e->detached_focus_group == group) e->detached_focus_group = -1;

    /* re-acoplar su grupo de pestanas a la ventana principal (a la hoja del dock
     * con foco).  No deja pestanas huerfanas ni grupos colgantes. */
    editor_detached_reattach_group(e, group);

    /* destruir los recursos de SDL de la ventana */
    if (dw.renderer) SDL_DestroyRenderer((SDL_Renderer *)dw.renderer);
    if (dw.window) {
        SDL_StopTextInput((SDL_Window *)dw.window);
        SDL_DestroyWindow((SDL_Window *)dw.window);
    }
    e->needs_redraw = 1;
}
