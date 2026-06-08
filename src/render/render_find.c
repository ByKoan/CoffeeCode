#include "render_internal.h"
#include <stdio.h>
#include <string.h>

void render_find_bar(Editor *e)
{
    if (!e->find.visible) return;

    /* Medir etiquetas con la fuente real para evitar recortes */
    int lbl1_w = 0, lbl2_w = 0, lh = 0;
    TTF_GetStringSize(e->font, "Buscar:",     0, &lbl1_w, &lh);
    TTF_GetStringSize(e->font, "Reemplazar:", 0, &lbl2_w, &lh);
    int lbl_w   = (lbl1_w > lbl2_w ? lbl1_w : lbl2_w) + 4; /* el más ancho + margen */
    int btn_lbl_w = 0;
    TTF_GetStringSize(e->font, "Reemplazar", 0, &btn_lbl_w, &lh);
    int pad      = 10;
    int gap      = 8;
    int btn_w    = btn_lbl_w + 20; /* texto + padding horizontal */
    int field_h  = 22;
    int row_gap  = 8;

    int w = pad + lbl_w + gap + 200 + gap + btn_w + pad; /* mínimo útil */
    if (w < 400) w = 400;
    int h = pad + field_h + row_gap + field_h + pad;
    int x = e->win_w - w - 12;
    int y = NAVBAR_HEIGHT + TAB_BAR_HEIGHT + 8;

    /* coordenadas derivadas */
    int lbl_x   = x + pad;
    int field_x = lbl_x + lbl_w + gap;
    int field_w = w - pad - lbl_w - gap - pad;        /* campo fila 1: ocupa todo */
    int fld2_w  = field_w - gap - btn_w;              /* campo fila 2: deja espacio al botón */
    int btn_x   = field_x + fld2_w + gap;

    int row1_y  = y + pad;
    int row2_y  = row1_y + field_h + row_gap;

    /* -- Fondo + borde -- */
    set_color(e->renderer, 0x1E,0x22,0x2A,255);
    SDL_FRect bg = {(float)x,(float)y,(float)w,(float)h};
    SDL_RenderFillRect(e->renderer, &bg);
    set_color(e->renderer, 0x3A,0x3F,0x4A,255);
    SDL_FRect borders[4] = {
        {(float)x,(float)y,(float)w,1},
        {(float)x,(float)(y+h-1),(float)w,1},
        {(float)x,(float)y,1,(float)h},
        {(float)(x+w-1),(float)y,1,(float)h}
    };
    for (int i = 0; i < 4; i++) SDL_RenderFillRect(e->renderer, &borders[i]);

    /* helper: dibuja un campo de texto */
#define DRAW_FIELD(fx, fy, fw, focused)     do {         set_color(e->renderer, (focused)?0x2A:0x25, (focused)?0x2E:0x29, (focused)?0x38:0x31, 255);         SDL_FRect _bg = {(float)(fx),(float)(fy),(float)(fw),(float)field_h};         SDL_RenderFillRect(e->renderer, &_bg);         set_color(e->renderer, (focused)?0x52:0x3A, (focused)?0x8B:0x3F, (focused)?0xD4:0x4A, 255);         SDL_FRect _b[4] = {             {(float)(fx),(float)(fy),(float)(fw),1},             {(float)(fx),(float)((fy)+field_h-1),(float)(fw),1},             {(float)(fx),(float)(fy),1,(float)field_h},             {(float)((fx)+(fw)-1),(float)(fy),1,(float)field_h}         };         for (int _i=0;_i<4;_i++) SDL_RenderFillRect(e->renderer, &_b[_i]);     } while(0)

    int focused1 = (e->find.bar_focused && e->find.replace_focused == 0);
    int focused2 = (e->find.bar_focused && e->find.replace_focused == 1);

    /* -- Fila 1: Buscar -- */
    /* etiqueta centrada verticalmente */
    draw_text(e, "Buscar:", lbl_x, row1_y + (field_h - FONT_SIZE)/2, 0x88,0x8C,0x99);
    DRAW_FIELD(field_x, row1_y, field_w, focused1);

    /* resaltado de selección en campo buscar */
    if (e->find.query_sel_start >= 0 && e->find.query_sel_end > e->find.query_sel_start
        && e->find.query_sel_end <= e->find.query_len) {
        char tmp_before[FIND_BAR_MAX];
        char tmp_sel[FIND_BAR_MAX];
        int before_len = e->find.query_sel_start;
        int sel_len    = e->find.query_sel_end - e->find.query_sel_start;
        memcpy(tmp_before, e->find.query, before_len);
        tmp_before[before_len] = '\0';
        memcpy(tmp_sel, e->find.query + before_len, sel_len);
        tmp_sel[sel_len] = '\0';
        int bw = 0, sw = 0, sh = 0;
        if (before_len > 0) TTF_GetStringSize(e->font, tmp_before, 0, &bw, &sh);
        if (sel_len   > 0) TTF_GetStringSize(e->font, tmp_sel,    0, &sw, &sh);
        int sel_x = field_x + 4 + bw;
        int sel_y = row1_y + 2;
        int sel_h = field_h - 4;
        set_color(e->renderer, 0x26, 0x4F, 0x78, 200);
        SDL_FRect sel_rect = {(float)sel_x, (float)sel_y, (float)sw, (float)sel_h};
        SDL_RenderFillRect(e->renderer, &sel_rect);
    }

    char txt1[512];
    snprintf(txt1,sizeof(txt1),"%s%s",
             e->find.query,
             (focused1 && e->find.query_sel_start < 0 && (SDL_GetTicks()/500)%2) ? "|" : "");
    draw_text(e, txt1, field_x+4, row1_y+(field_h-FONT_SIZE)/2, 220,220,220);

    if (e->find.result_line >= 0 && e->find.match_count > 0) {
        /* -- Botones ↑ ↓ + contador "X/N" a la derecha del campo buscar -- */
        int arrow_w = field_h;  /* botones cuadrados */
        int ctr_w   = 0, ctr_h = 0;
        char ctr_lbl[32];
        snprintf(ctr_lbl, sizeof(ctr_lbl), "%d/%d", e->find.match_index + 1, e->find.match_count);
        TTF_GetStringSize(e->font, ctr_lbl, 0, &ctr_w, &ctr_h);

        int btn_area_w = arrow_w + 4 + ctr_w + 4 + arrow_w + 4;
        int prev_x = field_x + field_w - btn_area_w;
        int next_x = prev_x + arrow_w + 4 + ctr_w + 4;

        /* Guardar geometría para click detection */
        e->find.prev_btn_x = prev_x; e->find.prev_btn_y = row1_y;
        e->find.prev_btn_w = arrow_w; e->find.prev_btn_h = field_h;
        e->find.next_btn_x = next_x; e->find.next_btn_y = row1_y;
        e->find.next_btn_w = arrow_w; e->find.next_btn_h = field_h;

        /* Botón ↑ (prev) */
        set_color(e->renderer, 0x2A,0x2E,0x38,255);
        SDL_FRect pbg = {(float)prev_x,(float)row1_y,(float)arrow_w,(float)field_h};
        SDL_RenderFillRect(e->renderer, &pbg);
        set_color(e->renderer, 0x3A,0x3F,0x4A,255);
        SDL_FRect pb[4] = {
            {(float)prev_x,(float)row1_y,(float)arrow_w,1},
            {(float)prev_x,(float)(row1_y+field_h-1),(float)arrow_w,1},
            {(float)prev_x,(float)row1_y,1,(float)field_h},
            {(float)(prev_x+arrow_w-1),(float)row1_y,1,(float)field_h}
        };
        for (int i=0;i<4;i++) SDL_RenderFillRect(e->renderer, &pb[i]);
        { int tw=0,th=0; TTF_GetStringSize(e->font,"↑",0,&tw,&th);
          draw_text(e,"↑", prev_x+(arrow_w-tw)/2, row1_y+(field_h-FONT_SIZE)/2, 180,200,230); }

        /* Contador X/N */
        draw_text(e, ctr_lbl,
                  prev_x + arrow_w + 4,
                  row1_y + (field_h - FONT_SIZE)/2, 97,175,239);

        /* Botón ↓ (next) */
        set_color(e->renderer, 0x2A,0x2E,0x38,255);
        SDL_FRect nbg = {(float)next_x,(float)row1_y,(float)arrow_w,(float)field_h};
        SDL_RenderFillRect(e->renderer, &nbg);
        set_color(e->renderer, 0x3A,0x3F,0x4A,255);
        SDL_FRect nb[4] = {
            {(float)next_x,(float)row1_y,(float)arrow_w,1},
            {(float)next_x,(float)(row1_y+field_h-1),(float)arrow_w,1},
            {(float)next_x,(float)row1_y,1,(float)field_h},
            {(float)(next_x+arrow_w-1),(float)row1_y,1,(float)field_h}
        };
        for (int i=0;i<4;i++) SDL_RenderFillRect(e->renderer, &nb[i]);
        { int tw=0,th=0; TTF_GetStringSize(e->font,"↓",0,&tw,&th);
          draw_text(e,"↓", next_x+(arrow_w-tw)/2, row1_y+(field_h-FONT_SIZE)/2, 180,200,230); }
    } else if (e->find.query_len > 0 && e->find.match_count == 0) {
        draw_text(e, "Sin resultados", field_x+field_w/2-30, row1_y+(field_h-FONT_SIZE)/2, 200,80,80);
    }

    /* -- Fila 2: Reemplazar -- */
    draw_text(e, "Reemplazar:", lbl_x, row2_y + (field_h - FONT_SIZE)/2, 0x88,0x8C,0x99);
    DRAW_FIELD(field_x, row2_y, fld2_w, focused2);

    /* resaltado de selección en campo reemplazar */
    if (e->find.replace_sel_start >= 0 && e->find.replace_sel_end > e->find.replace_sel_start
        && e->find.replace_sel_end <= e->find.replace_len) {
        char tmp_before[FIND_BAR_MAX];
        char tmp_sel[FIND_BAR_MAX];
        int before_len = e->find.replace_sel_start;
        int sel_len    = e->find.replace_sel_end - e->find.replace_sel_start;
        memcpy(tmp_before, e->find.replace, before_len);
        tmp_before[before_len] = '\0';
        memcpy(tmp_sel, e->find.replace + before_len, sel_len);
        tmp_sel[sel_len] = '\0';
        int bw = 0, sw = 0, sh = 0;
        if (before_len > 0) TTF_GetStringSize(e->font, tmp_before, 0, &bw, &sh);
        if (sel_len   > 0) TTF_GetStringSize(e->font, tmp_sel,    0, &sw, &sh);
        int sel_x = field_x + 4 + bw;
        int sel_y = row2_y + 2;
        int sel_h = field_h - 4;
        set_color(e->renderer, 0x26, 0x4F, 0x78, 200);
        SDL_FRect sel_rect = {(float)sel_x, (float)sel_y, (float)sw, (float)sel_h};
        SDL_RenderFillRect(e->renderer, &sel_rect);
    }

    char txt2[512];
    snprintf(txt2,sizeof(txt2),"%s%s",
             e->find.replace,
             (focused2 && e->find.replace_sel_start < 0 && (SDL_GetTicks()/500)%2) ? "|" : "");
    draw_text(e, txt2, field_x+4, row2_y+(field_h-FONT_SIZE)/2, 220,220,220);

    /* -- Botón "Reemplazar" -- */
    set_color(e->renderer, 0x2C,0x5F,0x8C,255);
    SDL_FRect btn = {(float)btn_x,(float)row2_y,(float)btn_w,(float)field_h};
    SDL_RenderFillRect(e->renderer, &btn);
    set_color(e->renderer, 0x52,0x8B,0xD4,255);
    SDL_FRect btnb[4] = {
        {(float)btn_x,(float)row2_y,(float)btn_w,1},
        {(float)btn_x,(float)(row2_y+field_h-1),(float)btn_w,1},
        {(float)btn_x,(float)row2_y,1,(float)field_h},
        {(float)(btn_x+btn_w-1),(float)row2_y,1,(float)field_h}
    };
    for (int i=0;i<4;i++) SDL_RenderFillRect(e->renderer, &btnb[i]);
    /* texto del botón centrado */
    {
        int tw=0, th=0;
        TTF_GetStringSize(e->font, "Reemplazar", 0, &tw, &th);
        int tx = btn_x + (btn_w - tw) / 2;
        int ty = row2_y + (field_h - FONT_SIZE) / 2;
        draw_text(e, "Reemplazar", tx, ty, 210,230,255);
    }

#undef DRAW_FIELD

    /* guardar geometría para click detection */
    e->find.replace_btn_x = btn_x;
    e->find.replace_btn_y = row2_y;
    e->find.replace_btn_w = btn_w;
    e->find.replace_btn_h = field_h;
    e->find.bar_x   = x;
    e->find.bar_y   = y;
    e->find.bar_w   = w;
    e->find.bar_h   = h;
    e->find.field_x = field_x;
    e->find.row1_y  = row1_y;
    e->find.row2_y  = row2_y;
    e->find.field_h = field_h;
}
