/**
 * @file settings.c
 * @brief Carga/guardado de las preferencias en un settings.ini (clave=valor).
 */
#include "settings/settings.h"

#include <SDL3/SDL.h> /* SDL_GetPrefPath, SDL_free */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Recorta @p v al rango [@p lo, @p hi]. */
static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void settings_defaults(Settings *s) {
    s->theme = 0;
    s->tab_width = 4;
    s->font_size = 16;
    s->autosave = 0;
    s->show_line_numbers = 1;
    s->font_path[0] = '\0';
}

/**
 * @brief Compone la ruta completa de @c settings.ini en @p out.
 *
 * @c SDL_GetPrefPath devuelve (y reserva) la carpeta de configuración por
 * usuario de la app, p. ej. @c %APPDATA%\CoffeeCode\CoffeeCode\ en Windows o
 * @c ~/.local/share/CoffeeCode/CoffeeCode/ en Linux, con la barra final.
 *
 * @return 1 si se obtuvo la ruta; 0 si falló.
 */
static int settings_path(char *out, size_t cap) {
    char *pref = SDL_GetPrefPath("CoffeeCode", "CoffeeCode");
    if (!pref) return 0;
    int n = snprintf(out, cap, "%ssettings.ini", pref);
    SDL_free(pref);
    return n > 0 && (size_t)n < cap;
}

void settings_load(Settings *s) {
    settings_defaults(s); /* base: si falta una clave, queda su default */

    char path[1024];
    if (!settings_path(path, sizeof path)) return;
    FILE *f = fopen(path, "r");
    if (!f) return; /* primera ejecución: aún no existe el archivo */

    char line[640];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue; /* línea sin '=': ignorar */
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        val[strcspn(val, "\r\n")] = '\0'; /* recortar el salto de línea */

        if (!strcmp(key, "theme"))
            s->theme = atoi(val);
        else if (!strcmp(key, "tab_width"))
            s->tab_width =
                clampi(atoi(val), SETTINGS_TAB_MIN, SETTINGS_TAB_MAX);
        else if (!strcmp(key, "font_size"))
            s->font_size =
                clampi(atoi(val), SETTINGS_FONT_MIN, SETTINGS_FONT_MAX);
        else if (!strcmp(key, "autosave"))
            s->autosave = atoi(val) ? 1 : 0;
        else if (!strcmp(key, "show_line_numbers"))
            s->show_line_numbers = atoi(val) ? 1 : 0;
        else if (!strcmp(key, "font_path")) {
            strncpy(s->font_path, val, sizeof s->font_path - 1);
            s->font_path[sizeof s->font_path - 1] = '\0';
        }
    }
    fclose(f);
}

void settings_save(const Settings *s) {
    char path[1024];
    if (!settings_path(path, sizeof path)) return;
    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "theme=%d\n", s->theme);
    fprintf(f, "tab_width=%d\n", s->tab_width);
    fprintf(f, "font_size=%d\n", s->font_size);
    fprintf(f, "autosave=%d\n", s->autosave);
    fprintf(f, "show_line_numbers=%d\n", s->show_line_numbers);
    fprintf(f, "font_path=%s\n", s->font_path);
    fclose(f);
}
