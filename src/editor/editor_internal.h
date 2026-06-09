#pragma once
/**
 * @file editor_internal.h
 * @brief Header PRIVADO del módulo editor: declaraciones compartidas entre sus
 *        .c (editor.c, editor_tabs.c, editor_undo.c). No es API pública.
 *
 * Reúne la cabecera pública del editor y los includes de la librería estándar
 * que usan varios .c del módulo: @c stdio/stdlib/string (E/S, memoria, cadenas)
 * y
 * @c sys/stat (consulta de mtime de ficheros para la recarga de pestañas).
 */
#include "editor/editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/**
 * @brief Libera los recursos de una pestaña: buffer, cache del lexer y pila de
 *        undo (incluido el texto malloc'd de cada entrada).
 *
 * Compartida entre @c editor_free (al cerrar la app) y @c editor_tab_close (al
 * cerrar una pestaña). No modifica los punteros del @c Editor.
 *
 * @param t Pestaña cuyos recursos se liberan.
 */
void tab_free_resources(EditorTab *t);
