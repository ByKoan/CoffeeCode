/**
 * @file app.c
 * @brief Capa de aplicacion multi-ventana: bucle de eventos, enrutado por
 *        SDL_WindowID y operaciones de desprender/fusionar ventanas.
 *
 * Cada ventana del IDE es una instancia COMPLETA de ::Editor (con su propio
 * dock, flotantes, explorador y paneles).  La App orquesta el conjunto sin
 * tocar el render ni el input de cada ventana: reutiliza @c render_frame y
 * @c input_handle_event TAL CUAL, por ventana.
 *
 * Invariante de cero regresion: con @c window_count == 1 el bucle es
 * equivalente a @c editor_run sobre la principal (un solo Editor, su input y su
 * render); el enrutado por windowID es entonces un unico chequeo trivial.
 */
#include "app/app.h"
#include "app/winhit.h"
#include "ext/ext_host.h"
#include "input/input.h"
#include "render/render.h"
#include <SDL3/SDL.h>

/* App actualmente en ejecucion (la fija app_run mientras corre).  Permite que el
 * input, que solo recibe el Editor, llegue a la App para crear/fusionar ventanas
 * sin reescribir toda la cadena de input. */
static App *g_app = NULL;

App *app_current(void) { return g_app; }

/* Enruta un evento al Editor correcto y maneja los casos multi-ventana (foco,
 * cierre de secundaria).  Interna del bucle multi-ventana. */
static void app_dispatch(App *a, SDL_Event *ev);

void app_init(App *a, Editor *primary) {
    if (!a || !primary) return;
    a->windows[0] = primary;
    for (int i = 1; i < APP_MAX_WINDOWS; i++) a->windows[i] = NULL;
    a->window_count = 1;
    a->focused = 0;
    a->prev_focused = 0;
}

/* SDL_WindowID al que pertenece un evento, o 0 si no esta ligado a una ventana
 * (p.ej. SDL_EVENT_QUIT).  SDL_Event es una union: cada tipo guarda el windowID
 * en un miembro distinto. */
static unsigned int app_event_window_id(const SDL_Event *ev) {
    switch (ev->type) {
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return ev->window.windowID;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return ev->key.windowID;
    case SDL_EVENT_TEXT_INPUT:
        return ev->text.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return ev->button.windowID;
    case SDL_EVENT_MOUSE_MOTION:
        return ev->motion.windowID;
    case SDL_EVENT_MOUSE_WHEEL:
        return ev->wheel.windowID;
    default:
        return 0; /* evento sin ventana asociada */
    }
}

/* Indice de la ventana de la App cuyo SDL_Window tiene @p wid, o -1 si ninguna.
 * Con window_count==1 es un unico chequeo barato. */
static int app_window_by_id(App *a, unsigned int wid) {
    if (wid == 0) return -1;
    for (int i = 0; i < a->window_count; i++) {
        Editor *e = a->windows[i];
        if (e && e->window && SDL_GetWindowID(e->window) == wid) return i;
    }
    return -1;
}

/* Re-apunta el host de extensiones (compartido) al buffer activo de la ventana
 * @p e, para que la API de extensiones opere sobre la ventana con foco. */
static void app_point_ext_host(Editor *e) {
    if (e && e->ext_host && e->buf)
        ext_host_set_buffer((CoffeeHost *)e->ext_host, e->buf);
}

/* Ventana destino al CERRAR la secundaria @p wi: la ULTIMA ENFOCADA distinta de
 * @p wi si sigue viva, o la principal (0) en su defecto.  Asi al cerrar una
 * secundaria sus pestanas vuelven a la ventana en la que estabas, no siempre a la
 * principal. */
static int app_merge_target_for(App *a, int wi) {
    int t = a->prev_focused;
    if (t == wi || t < 0 || t >= a->window_count) t = 0; /* fallback: principal */
    return t;
}

/* Fusiona la ventana secundaria de indice @p wi en la ventana @p into (su hoja
 * con foco) y la destruye, compactando el array de ventanas.  @p into se ajusta
 * si la compactacion lo desplaza.  No hace nada con la principal (wi==0). */
static void app_close_secondary_into(App *a, int wi, int into) {
    if (wi <= 0 || wi >= a->window_count) return; /* nunca la principal */
    if (into < 0 || into >= a->window_count || into == wi) into = 0;
    Editor *sec = a->windows[wi];
    Editor *dst = a->windows[into];

    /* fusionar sus pestanas en el destino antes de liberar nada */
    editor_merge_all(sec, dst);
    app_point_ext_host(dst);
    dst->needs_redraw = 1;

    /* liberar la secundaria (libera SOLO lo que posee: window/renderer + tabs) */
    editor_free(sec);
    free(sec);

    /* compactar el array de ventanas */
    for (int i = wi; i < a->window_count - 1; i++) a->windows[i] = a->windows[i + 1];
    a->windows[a->window_count - 1] = NULL;
    a->window_count--;

    /* el destino pasa a tener el foco (ajustando su indice si se desplazo). */
    if (into > wi) into--;
    a->focused = into;
    a->prev_focused = 0;
}

/* Cierra la secundaria @p wi fusionandola en la ultima ventana enfocada (o la
 * principal).  Atajo usado al cerrar por la X del SO / Ctrl+Q. */
static void app_close_secondary(App *a, int wi) {
    app_close_secondary_into(a, wi, app_merge_target_for(a, wi));
}

/* Indice de @p e en windows[], o -1 si no pertenece a la App. */
static int app_index_of(App *a, Editor *e) {
    for (int i = 0; i < a->window_count; i++)
        if (a->windows[i] == e) return i;
    return -1;
}

int app_window_at_global(App *a, int gx, int gy) {
    if (!a) return -1;
    /* construir los rects de pantalla de cada ventana, con la enfocada primero
     * para que gane en caso de solape (z-order). */
    WinRect rects[APP_MAX_WINDOWS];
    int order[APP_MAX_WINDOWS];
    int n = 0;
    if (a->focused >= 0 && a->focused < a->window_count) order[n++] = a->focused;
    for (int i = 0; i < a->window_count; i++)
        if (i != a->focused) order[n++] = i;

    for (int k = 0; k < n; k++) {
        Editor *e = a->windows[order[k]];
        int wx = 0, wy = 0;
        if (e && e->window) SDL_GetWindowPosition(e->window, &wx, &wy);
        WinRect r = {wx, wy, e ? e->win_w : 0, e ? e->win_h : 0};
        rects[k] = r;
    }
    int hit = win_at_point(rects, n, gx, gy);
    return (hit < 0) ? -1 : order[hit]; /* mapear de vuelta al indice real */
}

/* Crea una ventana secundaria del tamano (@p w,@p h), la coloca en la posicion
 * global (@p gx,@p gy) y la registra como ventana nueva enfocada.  Devuelve su
 * indice en windows[], o -1 si no hay sitio o SDL fallo. */
static int app_spawn_secondary_at(App *a, int w, int h, int gx, int gy) {
    if (a->window_count >= APP_MAX_WINDOWS) return -1;
    if (w < 200) w = 200; /* tamano minimo razonable de una ventana del IDE */
    if (h < 150) h = 150;
    Editor *sec = (Editor *)calloc(1, sizeof(Editor));
    if (!sec) return -1;
    if (!editor_init_secondary(sec, a->windows[0], w, h)) {
        free(sec);
        return -1;
    }
    if (sec->window) SDL_SetWindowPosition(sec->window, gx, gy);
    int wi = a->window_count++;
    a->windows[wi] = sec;
    a->prev_focused = a->focused;
    a->focused = wi;
    SDL_RaiseWindow(sec->window);
    return wi;
}

/* Tras mover una pestana FUERA de @p src, si @p src es una ventana SECUNDARIA
 * que quedo SIN pestanas, la cierra (sin re-fusionar: sus pestanas ya migraron).
 * La principal nunca se cierra (queda con la pantalla de bienvenida). */
static void app_close_src_if_empty(App *a, Editor *src) {
    int si = app_index_of(a, src);
    if (si <= 0) return;             /* principal o desconocido: no cerrar */
    if (src->tab_count > 0) return;  /* aun tiene pestanas: conservarla */

    /* liberar y compactar (sus pestanas ya se movieron: NADA que fusionar). */
    editor_free(src);
    free(src);
    for (int i = si; i < a->window_count - 1; i++) a->windows[i] = a->windows[i + 1];
    a->windows[a->window_count - 1] = NULL;
    a->window_count--;
    if (a->focused > si) a->focused--;
    else if (a->focused == si) a->focused = 0;
    if (a->prev_focused > si) a->prev_focused--;
    else if (a->prev_focused == si) a->prev_focused = 0;
}

int app_drop_tab_cross_window(App *a, Editor *src, int tab) {
    if (!a || !src) return 0;
    if (tab < 0 || tab >= src->tab_count) return 0;

    int si = app_index_of(a, src);
    if (si < 0) return 0; /* src no pertenece a la App: drop local */

    /* posicion GLOBAL del cursor: SDL la sigue entregando aunque el cursor salga
     * de la ventana origen porque el arrastre tiene el raton CAPTURADO. */
    float gxf = 0.0f, gyf = 0.0f;
    SDL_GetGlobalMouseState(&gxf, &gyf);
    int gx = (int)gxf, gy = (int)gyf;

    int dst_wi = app_window_at_global(a, gx, gy);

    /* Mismo origen: el llamante hace el drop local (reordenar/dock/borde). */
    if (dst_wi == si) return 0;

    if (dst_wi >= 0) {
        /* OTRA ventana existente: mover la pestana ahi, en la hoja/zona local. */
        Editor *dst = a->windows[dst_wi];
        int dwx = 0, dwy = 0;
        if (dst->window) SDL_GetWindowPosition(dst->window, &dwx, &dwy);
        editor_transfer_tab(src, tab, dst, gx - dwx, gy - dwy);
        SDL_RaiseWindow(dst->window);
        a->prev_focused = a->focused;
        a->focused = dst_wi;
        app_point_ext_host(dst);
        app_close_src_if_empty(a, src); /* puede invalidar indices: ya no se usan */
        return 1;
    }

    /* FUERA de toda ventana: tear-off a una ventana NUEVA en el cursor. */
    int w = src->win_w, h = src->win_h;
    int nwi = app_spawn_secondary_at(a, w > 0 ? w * 3 / 4 : 800,
                                     h > 0 ? h * 3 / 4 : 600, gx, gy);
    if (nwi < 0) return 0; /* sin sitio para mas ventanas: el llamante hace local */
    Editor *nw = a->windows[nwi];
    /* la pestana cae en el centro de la ventana nueva (su unica hoja). */
    editor_transfer_tab(src, tab, nw, nw->win_w / 2, nw->win_h / 2);
    app_point_ext_host(nw);
    app_close_src_if_empty(a, src);
    return 1;
}

void app_detach_float_to_window(App *a, Editor *e, int fi) {
    if (!a || !e) return;
    if (a->window_count >= APP_MAX_WINDOWS) return; /* sin sitio para mas ventanas */
    if (fi < 0 || fi >= e->float_count) return;

    int group = e->floats[fi].group_id;
    int w = e->floats[fi].rect.w;
    int h = e->floats[fi].rect.h;

    /* crear la ventana NUEVA como un IDE completo (Editor secundario) */
    Editor *sec = (Editor *)calloc(1, sizeof(Editor));
    if (!sec) return;
    if (!editor_init_secondary(sec, a->windows[0], w, h)) {
        free(sec); /* SDL fallo: conservar el flotante */
        return;
    }

    /* mover las pestanas del grupo del flotante a la ventana nueva */
    editor_transfer_group(e, group, sec);

    /* eliminar el FloatPanel in-window de @p e (sus pestanas ya estan en sec) */
    if (fi >= 0 && fi < e->float_count) {
        for (int i = fi; i < e->float_count - 1; i++) e->floats[i] = e->floats[i + 1];
        e->float_count--;
    }
    editor_float_gc_empty(e);
    e->needs_redraw = 1;

    /* registrar la ventana nueva y darle el foco */
    int wi = a->window_count++;
    a->windows[wi] = sec;
    a->focused = wi;
    app_point_ext_host(sec);
    SDL_RaiseWindow(sec->window);
}

void app_run(App *a) {
    if (!a || a->window_count < 1) return;
    g_app = a;

    SDL_Event ev;
    /* el bucle vive mientras la ventana principal este viva */
    while (a->windows[0] && a->windows[0]->running) {
        if (SDL_WaitEventTimeout(&ev, 16)) {
            app_dispatch(a, &ev);
            while (SDL_PollEvent(&ev)) app_dispatch(a, &ev);
        }

        /* tareas periodicas + render POR CADA ventana */
        for (int i = 0; i < a->window_count; i++) {
            Editor *e = a->windows[i];
            if (!e) continue;
            editor_frame_tasks(e);
            if (e->needs_redraw) {
                render_frame(e);
                if (e->detached_count > 0) editor_render_detached(e);
                e->needs_redraw = 0;
            }
        }

        /* cerrar secundarias que pidieron salir desde su propio input (running=0
         * por Ctrl+Q): fusionarlas a la principal. */
        for (int i = a->window_count - 1; i >= 1; i--)
            if (a->windows[i] && !a->windows[i]->running)
                app_close_secondary(a, i);
    }

    g_app = NULL;
}

static void app_dispatch(App *a, SDL_Event *ev) {
    /* SDL_EVENT_QUIT: cerrar la app entera (lo dispara SDL al no quedar ventanas
     * o por peticion explicita).  Lo trata la principal. */
    if (ev->type == SDL_EVENT_QUIT) {
        a->windows[0]->running = 0;
        return;
    }

    int wi = app_window_by_id(a, app_event_window_id(ev));
    if (wi < 0) {
        /* evento sin ventana resoluble: con una sola ventana, va a la principal
         * (cero regresion); con varias, a la enfocada. */
        wi = (a->window_count == 1) ? 0 : a->focused;
        if (wi < 0 || wi >= a->window_count) wi = 0;
    }
    Editor *e = a->windows[wi];
    if (!e) return;

    /* Cierre de una ventana por la X del SO: la principal termina la app; una
     * secundaria se fusiona de vuelta a la principal. */
    if (ev->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (wi == 0) {
            a->windows[0]->running = 0;
        } else {
            app_close_secondary(a, wi);
        }
        return;
    }

    /* Cambio de foco de teclado: actualizar la ventana enfocada y re-apuntar el
     * host de extensiones a su buffer activo. */
    if (ev->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        if (wi != a->focused) a->prev_focused = a->focused; /* recordar la previa */
        a->focused = wi;
        app_point_ext_host(e);
        /* tambien dejamos que el input lo procese (no hace dano). */
    }

    /* todo lo demas: el input de ESA ventana, tal cual (mismo codigo). */
    input_handle_event(e, ev);
}

void app_free(App *a) {
    if (!a) return;
    /* liberar SOLO las secundarias (la principal la libera el llamante). */
    for (int i = a->window_count - 1; i >= 1; i--) {
        if (a->windows[i]) {
            editor_free(a->windows[i]);
            free(a->windows[i]);
            a->windows[i] = NULL;
        }
    }
    a->window_count = 1;
    a->focused = 0;
    a->prev_focused = 0;
}
