/**
 * @file bg_core.c
 * @brief Implementacion de la geometria pura del fondo (ver bg_core.h).
 */
#include "render/bg_core.h"
#include "settings/settings.h" /* BgScale */

BgRect bg_image_dst(int scale, int ax, int ay, int aw, int ah, int iw, int ih,
                    int *out_needs_clip) {
    BgRect d = {(float)ax, (float)ay, 0.0f, 0.0f};
    int needs_clip = 0;

    /* Area o imagen degeneradas: nada que dibujar. */
    if (aw <= 0 || ah <= 0 || iw <= 0 || ih <= 0) {
        if (out_needs_clip) *out_needs_clip = 0;
        return d;
    }

    switch (scale) {
    case BG_SCALE_STRETCH:
        /* Deforma hasta llenar el area por completo. */
        d.w = (float)aw;
        d.h = (float)ah;
        break;

    case BG_SCALE_CENTER: {
        /* Tamano nativo, centrado; puede ser mayor que el area (recorta). */
        d.w = (float)iw;
        d.h = (float)ih;
        d.x = (float)ax + ((float)aw - (float)iw) * 0.5f;
        d.y = (float)ay + ((float)ah - (float)ih) * 0.5f;
        if (iw > aw || ih > ah) needs_clip = 1;
        break;
    }

    case BG_SCALE_FIT: {
        /* Cabe entera: escala = min(escala_x, escala_y), centrada. */
        float sx = (float)aw / (float)iw;
        float sy = (float)ah / (float)ih;
        float s = sx < sy ? sx : sy;
        d.w = (float)iw * s;
        d.h = (float)ih * s;
        d.x = (float)ax + ((float)aw - d.w) * 0.5f;
        d.y = (float)ay + ((float)ah - d.h) * 0.5f;
        break;
    }

    case BG_SCALE_FILL:
    default: {
        /* Cubre el area: escala = max(escala_x, escala_y), centrada y
         * recortada al area (el lado sobrante se sale). */
        float sx = (float)aw / (float)iw;
        float sy = (float)ah / (float)ih;
        float s = sx > sy ? sx : sy;
        d.w = (float)iw * s;
        d.h = (float)ih * s;
        d.x = (float)ax + ((float)aw - d.w) * 0.5f;
        d.y = (float)ay + ((float)ah - d.h) * 0.5f;
        needs_clip = 1;
        break;
    }
    }

    if (out_needs_clip) *out_needs_clip = needs_clip;
    return d;
}

void bg_tile_count(int aw, int ah, int iw, int ih, int *out_cols,
                   int *out_rows) {
    int cols = 0, rows = 0;
    if (aw > 0 && ah > 0 && iw > 0 && ih > 0) {
        /* Division con redondeo hacia arriba para cubrir el ultimo borde. */
        cols = (aw + iw - 1) / iw;
        rows = (ah + ih - 1) / ih;
    }
    if (out_cols) *out_cols = cols;
    if (out_rows) *out_rows = rows;
}
