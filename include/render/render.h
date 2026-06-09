#pragma once
/**
 * @file render.h
 * @brief API pública del módulo render. Solo expone ::render_frame, el punto de
 *        entrada que dibuja un frame completo; el resto de funciones de dibujo
 * son privadas del módulo (ver render_internal.h).
 */
#include "editor/editor.h"

/* Altura de la banda de atajos de teclado entre el editor y la status bar.
 * A 0 la banda queda oculta (no se reserva espacio ni se ve). */
#define SHORTCUT_HEIGHT 0

/**
 * @brief Dibuja un frame completo del editor (clear → secciones → present).
 * @param e Editor con el estado a representar.
 */
void render_frame(Editor *e);
