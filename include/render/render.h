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
