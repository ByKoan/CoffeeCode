#pragma once
/**
 * @file render.h
 * @brief API pública del módulo render. Solo expone ::render_frame, el punto de
 *        entrada que dibuja un frame completo; el resto de funciones de dibujo
 * son privadas del módulo (ver render_internal.h).
 */
#include "editor/editor.h"

/* SHORTCUT_HEIGHT (alto de la banda de atajos) y los helpers de dimensión
 * efectiva (gutter / atajos según preferencias) viven en editor/editor.h. */

/**
 * @brief Dibuja un frame completo del editor (clear → secciones → present).
 * @param e Editor con el estado a representar.
 */
void render_frame(Editor *e);

/**
 * @brief Dibuja UNA ventana desprendida (su tira de pestanas + el contenido de
 *        su pestana activa) en el renderer @p dr con las dimensiones
 *        (@p win_w, @p win_h) de ESA ventana, presentando al final.
 *
 * Conmuta temporalmente e->renderer/win_w/win_h al de la ventana desprendida y
 * los restaura al salir (el bind de pestana de la principal lo repone el
 * llamante).  Lo invocan editor_render_detached (cada frame) e input_mouse.c
 * (para refrescar el hit-test de la tira de pestanas antes de un clic).
 *
 * @param e     Editor.
 * @param dr    SDL_Renderer* de la ventana desprendida.
 * @param group group_id del grupo de pestanas que muestra la ventana.
 * @param win_w Ancho de la ventana (px).
 * @param win_h Alto de la ventana (px).
 */
void render_detached_window(Editor *e, SDL_Renderer *dr, int group, int win_w,
                            int win_h);
