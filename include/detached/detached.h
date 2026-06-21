#pragma once
/**
 * @file detached.h
 * @brief Ventanas desprendidas: un grupo de pestanas mostrado en su propia
 *        ventana REAL del sistema operativo (estilo "Move into New Window").
 *
 * Una ventana desprendida (::DetachedWindow) es un grupo de pestanas (un
 * group_id) sacado de la ventana principal a una ventana del SO independiente,
 * con su propio @c SDL_Window y @c SDL_Renderer.  Las pestanas siguen viviendo
 * en el array compartido @c Editor.tabs[] (una pestana pertenece a la ventana
 * desprendida si @c tab.group == @c DetachedWindow.group_id); su pestana activa
 * la lleva @c group_active_tab[group_id] y el cursor/scroll por pestana ya viven
 * en cada @c EditorTab, igual que los flotantes.
 *
 * A diferencia de un flotante (overlay DENTRO de la ventana principal), una
 * ventana desprendida es una ventana del SO aparte: se mueve, minimiza y cierra
 * con el gestor de ventanas, y tiene su propio bucle de eventos enrutado por
 * @c SDL_WindowID.  Muestra SOLO ese grupo: una tira de pestanas simple arriba y
 * el contenido de la pestana activa debajo (sin dock anidado ni flotantes
 * dentro, v1).
 *
 * Invariante de cero regresion: con @c detached_count == 0 (caso normal) NADIE
 * consulta estos campos ni recorre estos caminos; el bucle, el input y el render
 * de la ventana principal son EXACTAMENTE los de siempre.  Toda la logica
 * multi-ventana esta gateada por @c detached_count > 0 / @c windowID distinto del
 * de la ventana principal.
 *
 * Este modulo expone una parte PURA (sin SDL ni Editor) que se prueba en
 * headless: el calculo de la geometria del area de contenido de una ventana
 * desprendida a partir de su tamano.  El resto (crear/destruir la ventana,
 * enrutar eventos, render en el segundo renderer) vive acoplado al Editor.
 */
#include "render/ui_hit.h" /* Rect (independiente de SDL/Editor) */

/** Maximo de ventanas desprendidas simultaneas. */
#define MAX_DETACHED 4

/* Alto (px) de la tira de pestanas de una ventana desprendida.  Igual que la
 * barra de pestanas del editor (TAB_BAR_HEIGHT == 28); se define aqui para no
 * acoplar este header al de editor.  Si aquella cambiara, ajustar esta. */
#define DETACHED_TABBAR_H 28

/**
 * @brief Una ventana desprendida: su ventana/renderer del SO y el grupo de
 *        pestanas que muestra.
 *
 * @c window y @c renderer son recursos del SO propios de esta ventana (creados
 * con @c SDL_CreateWindow / @c SDL_CreateRenderer y destruidos al cerrarla).
 * @c group_id identifica el grupo de pestanas (las que tienen tab.group ==
 * group_id).  @c win_w / @c win_h son el tamano actual de ESTA ventana en
 * pixeles (se actualizan al redimensionarla).  Se declara @c window y
 * @c renderer como @c void* para no arrastrar SDL a quien solo necesite el
 * group_id; los .c que tocan SDL los castean a @c SDL_Window* / @c
 * SDL_Renderer*.
 */
typedef struct {
    void *window;   /**< SDL_Window* de esta ventana del SO        */
    void *renderer; /**< SDL_Renderer* ligado a @c window           */
    int group_id;   /**< id del grupo de pestanas que muestra       */
    int win_w;      /**< ancho actual de la ventana (px)            */
    int win_h;      /**< alto actual de la ventana (px)             */
} DetachedWindow;

/* ===========================================================================
 *  Geometria pura (sin SDL/Editor): se prueba en headless
 * =========================================================================== */

/**
 * @brief Rectangulo de la tira de pestanas de una ventana desprendida de tamano
 *        (@p win_w, @p win_h): franja superior a todo el ancho.
 *
 * Funcion PURA: solo aritmetica de enteros.  El render la usa para dibujar la
 * tira y el input para enrutar el clic.
 *
 * @param win_w Ancho de la ventana (px).
 * @param win_h Alto de la ventana (px).
 * @return Rect de la tira de pestanas (px).
 */
Rect detached_tabbar_rect(int win_w, int win_h);

/**
 * @brief Rectangulo del area de CONTENIDO de una ventana desprendida de tamano
 *        (@p win_w, @p win_h): todo lo que queda bajo la tira de pestanas.
 *
 * Funcion PURA.  El render dibuja el texto aqui y el input mapea pixel->columna
 * sobre este rect (via el override @c pane_* del Editor).
 *
 * @param win_w Ancho de la ventana (px).
 * @param win_h Alto de la ventana (px).
 * @return Rect del area de contenido (px); alto recortado a >= 0.
 */
Rect detached_content_rect(int win_w, int win_h);
