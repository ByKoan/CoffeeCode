#pragma once
/**
 * @file theme.h
 * @brief Paleta de colores en *runtime* (tema). Permite cambiar el aspecto del
 *        editor sin recompilar.
 *
 * Cada color de la UI vive aquí como un campo ::Color. El render lee el tema
 * activo (@c e->theme) a través de las funciones @c set_color_c / @c
 * draw_text_c (ver render_internal.h), que reciben un ::Color directamente. No
 * hay macros de color: cambiar de tema solo cambia los valores de la
 * estructura.
 *
 * ::theme_preset rellena el tema según el índice guardado en preferencias.
 * @c index 0 = oscuro (los colores originales del editor); 1 = claro.
 */
#include "lexer/lexer.h" /* Color, TOK_COUNT */

/** Nº de presets de tema disponibles. */
#define THEME_COUNT 2

/**
 * @brief Paleta de la UI. Un campo por color (mismo nombre que su macro), más
 *        los colores de los tokens de sintaxis.
 */
typedef struct {
    /* -- Editor / general -- */
    Color col_bg, col_gutter, col_cursor_line, col_cursor, col_status_bg,
        col_sel_bg, col_status_sep;
    Color txt_gutter_num, txt_status, txt_welcome_title, txt_welcome_hint;
    /* -- Navbar / menú -- */
    Color col_navbar_bg, col_navbar_btn, col_navbar_sep, col_modified_dot;
    Color col_menu_bg, col_menu_hover, col_menu_sep, col_menu_border,
        col_menu_shadow;
    Color txt_navbar_btn, txt_navbar_title;
    Color txt_menu_check, txt_menu_item, txt_menu_hint;
    /* -- Pestañas -- */
    Color col_tabbar_bg, col_tabbar_sep, col_tab_active, col_tab_accent,
        col_tab_mod_dot;
    Color txt_tab_new;
    /* -- Banda de atajos -- */
    Color col_badge_bg, col_badge_border, col_badge_shadow, col_shortcut_sep,
        col_shortcut_bg;
    Color txt_badge_key, txt_badge_label;
    /* -- Scrollbar -- */
    Color col_sb_track, col_sb_thumb, col_sb_border;
    /* -- Explorador -- */
    Color col_ftree_bg, col_ftree_hover, col_ftree_sep, col_ftree_dir,
        col_ftree_file, col_ftree_root, col_ftree_header, col_ftree_toggle;
    Color ftree_txt_dir, ftree_txt_file, ftree_txt_root;
    /* -- Barra de búsqueda -- */
    Color fb_col_bg, fb_col_border, fb_col_field, fb_col_field_focus,
        fb_col_accent, fb_col_sel;
    Color fb_txt_label, fb_txt_field, fb_txt_counter, fb_txt_noresult;
    /* -- Sintaxis (indexado por ::LexTokenType) -- */
    Color tokens[TOK_COUNT];
} Theme;

/** Devuelve el preset @p index (0 = oscuro, 1 = claro; fuera de rango =
 * oscuro). */
Theme theme_preset(int index);

/** Nombre legible del preset @p index (p. ej. "Oscuro"). */
const char *theme_name(int index);
