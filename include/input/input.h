#pragma once
/**
 * @file input.h
 * @brief API pública del módulo de entrada: un único punto de entrada para
 *        procesar eventos de SDL.
 *
 * @note SDL para recién llegados. SDL entrega la interacción del usuario
 * (teclado, ratón, redimensionado de ventana, cierre...) como una cola de
 * @c SDL_Event. El editor lee esos eventos en su bucle principal y los pasa
 * a ::input_handle_event, que actúa de despachador: mira el tipo de evento y
 * lo enruta al manejador adecuado. Toda la lógica interna (manejadores de
 * teclado, ratón, búsqueda...) es privada al módulo; aquí solo se expone la
 * función de despacho.
 */
#include "editor/editor.h"

/**
 * @brief Procesa un evento de SDL aplicándolo al estado del editor.
 *
 * Es el despachador principal del módulo de entrada: examina @c ev->type y lo
 * reparte (teclado, texto, ratón, resize, quit). Antes de despachar consulta
 * los modificadores globales (Ctrl/Shift) para los manejadores que los usan.
 *
 * @param e  Editor sobre el que se aplica el evento (su estado puede mutar:
 *           cursor, selección, scroll, flag @c needs_redraw, etc.).
 * @param ev Evento de SDL ya leído de la cola. SDL_Event es una unión: según
 *           @c ev->type se accede a un miembro distinto (@c ev->key,
 *           @c ev->text, @c ev->button, @c ev->window...).
 */
void input_handle_event(Editor *e, SDL_Event *ev);
