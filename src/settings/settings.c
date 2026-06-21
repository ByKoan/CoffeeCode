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
    s->highlight_current_line = 1;
    s->show_shortcuts = 1;
    s->font_path[0] = '\0';
    s->background_path[0] = '\0'; /* sin imagen de fondo por defecto */
    s->background_mode = BG_MODE_NONE;
    s->background_color = SETTINGS_BG_COLOR_DEFAULT;
    s->background_opacity = SETTINGS_BG_OPACITY_DEFAULT;
    s->background_scaling = BG_SCALE_STRETCH; /* historico: imagen estirada */
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

    /* Migracion del esquema antiguo: si el archivo trae la clave legacy
     * "background_enabled" pero NO la nueva "background_mode", derivamos el
     * modo del flag al terminar (enabled=1 + ruta => imagen; si no => sin
     * fondo).  Estos centinelas recuerdan que claves aparecieron. */
    int saw_bg_mode = 0;
    int legacy_bg_enabled = -1; /* -1 = clave ausente */

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
        else if (!strcmp(key, "highlight_current_line"))
            s->highlight_current_line = atoi(val) ? 1 : 0;
        else if (!strcmp(key, "show_shortcuts"))
            s->show_shortcuts = atoi(val) ? 1 : 0;
        else if (!strcmp(key, "font_path")) {
            strncpy(s->font_path, val, sizeof s->font_path - 1);
            s->font_path[sizeof s->font_path - 1] = '\0';
        }
        else if (!strcmp(key, "background_path")) {
            strncpy(s->background_path, val, sizeof s->background_path - 1);
            s->background_path[sizeof s->background_path - 1] = '\0';
        }
        else if (!strcmp(key, "background_mode")) {
            s->background_mode = clampi(atoi(val), BG_MODE_NONE, BG_MODE_COLOR);
            saw_bg_mode = 1;
        }
        else if (!strcmp(key, "background_color"))
            /* hex 0xRRGGBB (strtoul con base 0 acepta el prefijo 0x) */
            s->background_color =
                (unsigned int)(strtoul(val, NULL, 0) & 0xFFFFFFu);
        else if (!strcmp(key, "background_opacity"))
            s->background_opacity = clampi(atoi(val), 0, 255);
        else if (!strcmp(key, "background_scaling"))
            s->background_scaling =
                clampi(atoi(val), BG_SCALE_FIT, BG_SCALE_TILE);
        /* legacy: clave del esquema antiguo (solo activado si/no) */
        else if (!strcmp(key, "background_enabled"))
            legacy_bg_enabled = atoi(val) ? 1 : 0;
    }
    fclose(f);

    /* Migracion: un settings.ini viejo no tiene "background_mode".  Si trae
     * "background_enabled=1" y hay una ruta, el fondo era una imagen; en
     * cualquier otro caso, sin fondo.  Asi los usuarios existentes conservan
     * su imagen al actualizar. */
    if (!saw_bg_mode && legacy_bg_enabled >= 0) {
        if (legacy_bg_enabled == 1 && s->background_path[0])
            s->background_mode = BG_MODE_IMAGE;
        else
            s->background_mode = BG_MODE_NONE;
    }
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
    fprintf(f, "highlight_current_line=%d\n", s->highlight_current_line);
    fprintf(f, "show_shortcuts=%d\n", s->show_shortcuts);
    fprintf(f, "font_path=%s\n", s->font_path);
    /* Fondo del area de texto (modo + parametros del modo activo). */
    fprintf(f, "background_path=%s\n", s->background_path);
    fprintf(f, "background_mode=%d\n", s->background_mode);
    fprintf(f, "background_color=0x%06X\n", s->background_color & 0xFFFFFFu);
    fprintf(f, "background_opacity=%d\n", s->background_opacity);
    fprintf(f, "background_scaling=%d\n", s->background_scaling);
    fclose(f);
}
