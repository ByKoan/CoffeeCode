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
    s->background_gallery_count = 0;          /* galeria vacia por defecto   */
    s->background_gallery[0][0] = '\0';
    /* Vista godbolt del hover (generica para cualquier extension). */
    s->hover_arrows = 1;
    s->hover_frame = 1;
    s->hover_notes = 1;
    s->hover_ir_mode = 0;
}

int settings_gallery_index_of(const Settings *s, const char *path) {
    if (!path || !path[0]) return -1;
    for (int i = 0; i < s->background_gallery_count; i++)
        if (strcmp(s->background_gallery[i], path) == 0) return i;
    return -1;
}

int settings_gallery_add(Settings *s, const char *path) {
    if (!path || !path[0]) return -1;          /* ruta vacia: ignorar       */
    int existing = settings_gallery_index_of(s, path);
    if (existing >= 0) return existing;        /* dedup: ya esta             */
    if (s->background_gallery_count >= BG_GALLERY_MAX) return -1; /* llena    */
    int i = s->background_gallery_count++;
    strncpy(s->background_gallery[i], path, sizeof s->background_gallery[i] - 1);
    s->background_gallery[i][sizeof s->background_gallery[i] - 1] = '\0';
    return i;
}

void settings_gallery_remove(Settings *s, int index) {
    if (index < 0 || index >= s->background_gallery_count) return;
    /* desplazar las siguientes una posicion hacia atras (compactar) */
    for (int i = index; i < s->background_gallery_count - 1; i++)
        memcpy(s->background_gallery[i], s->background_gallery[i + 1],
               sizeof s->background_gallery[i]);
    s->background_gallery_count--;
    s->background_gallery[s->background_gallery_count][0] = '\0';
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
        else if (!strcmp(key, "hover_arrows"))
            s->hover_arrows = atoi(val) ? 1 : 0;
        else if (!strcmp(key, "hover_frame"))
            s->hover_frame = atoi(val) ? 1 : 0;
        else if (!strcmp(key, "hover_notes"))
            s->hover_notes = atoi(val) ? 1 : 0;
        else if (!strcmp(key, "hover_ir_mode"))
            s->hover_ir_mode = clampi(atoi(val), 0, 4);
        else if (!strcmp(key, "font_path")) {
            strncpy(s->font_path, val, sizeof s->font_path - 1);
            s->font_path[sizeof s->font_path - 1] = '\0';
        }
        else if (!strcmp(key, "background_path")) {
            strncpy(s->background_path, val, sizeof s->background_path - 1);
            s->background_path[sizeof s->background_path - 1] = '\0';
        }
        else if (!strcmp(key, "background_mode")) {
            s->background_mode =
                clampi(atoi(val), BG_MODE_NONE, BG_MODE_TRANSPARENT);
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
        else if (!strcmp(key, "bg_gallery_count")) {
            /* el numero de entradas se acota; cada ruta se lee por su clave */
            s->background_gallery_count =
                clampi(atoi(val), 0, BG_GALLERY_MAX);
        }
        else if (!strncmp(key, "bg_gallery_", 11) &&
                 key[11] >= '0' && key[11] <= '9') {
            /* clave "bg_gallery_<n>": guardar la ruta en su indice si cabe */
            int idx = atoi(key + 11);
            if (idx >= 0 && idx < BG_GALLERY_MAX && val[0]) {
                strncpy(s->background_gallery[idx], val,
                        sizeof s->background_gallery[idx] - 1);
                s->background_gallery[idx][sizeof s->background_gallery[idx] - 1] =
                    '\0';
            }
        }
        /* legacy: clave del esquema antiguo (solo activado si/no) */
        else if (!strcmp(key, "background_enabled"))
            legacy_bg_enabled = atoi(val) ? 1 : 0;
    }
    fclose(f);

    /* Coherencia de la galeria: el contador podria ser mayor que las rutas
     * realmente presentes (settings.ini editado a mano o truncado).  Recortar
     * al primer hueco vacio para no exponer entradas basura. */
    {
        int valid = 0;
        while (valid < s->background_gallery_count &&
               s->background_gallery[valid][0])
            valid++;
        s->background_gallery_count = valid;
    }

    /* Migracion suave: si hay una imagen activa que no esta en la galeria,
     * agregarla (asi los usuarios que ya eligieron un fondo no lo pierden y
     * aparece en la nueva cuadricula). */
    if (s->background_path[0])
        settings_gallery_add(s, s->background_path);

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
    fprintf(f, "hover_arrows=%d\n", s->hover_arrows);
    fprintf(f, "hover_frame=%d\n", s->hover_frame);
    fprintf(f, "hover_notes=%d\n", s->hover_notes);
    fprintf(f, "hover_ir_mode=%d\n", s->hover_ir_mode);
    fprintf(f, "font_path=%s\n", s->font_path);
    /* Fondo del area de texto (modo + parametros del modo activo). */
    fprintf(f, "background_path=%s\n", s->background_path);
    fprintf(f, "background_mode=%d\n", s->background_mode);
    fprintf(f, "background_color=0x%06X\n", s->background_color & 0xFFFFFFu);
    fprintf(f, "background_opacity=%d\n", s->background_opacity);
    fprintf(f, "background_scaling=%d\n", s->background_scaling);
    /* Galeria de fondos: contador + una clave por ruta. */
    fprintf(f, "bg_gallery_count=%d\n", s->background_gallery_count);
    for (int i = 0; i < s->background_gallery_count; i++)
        fprintf(f, "bg_gallery_%d=%s\n", i, s->background_gallery[i]);
    fclose(f);
}
