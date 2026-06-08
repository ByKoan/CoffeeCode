#pragma once
/* Header PRIVADO del modulo editor: declaraciones compartidas. No es API publica. */
#include "editor/editor.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

/* Libera buffer/lexer/undo de un tab (usada por editor_free y editor_tab_close). */
void tab_free_resources(EditorTab *t);
