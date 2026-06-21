/**
 * @file layout_persist.c
 * @brief Implementacion PURA de la (de)serializacion de la disposicion del
 *        editor (ver session/layout_persist.h).
 *
 * El formato de texto es linea-a-linea, ASCII puro:
 *
 *   ui ftree_width <n>
 *   ui ftree_open <0|1>
 *   ui ext_panel_w <n>
 *   ui ext_panel_open <0|1>
 *   ui bottom_panel_h <n>
 *   ui bottom_panel_open <0|1>
 *   focus active_group <n>
 *   focus active_tab <n>
 *   tabs <count>
 *   tab <group> <is_group_active> <path...>
 *   ...
 *   dock <node_count> <root> <focused_leaf> <leaf_count>
 *   node <kind> <group_id> <orient> <ratio> <child_a> <child_b> <parent>
 *   ...
 *   floats <count>
 *   float <x> <y> <w> <h> <group_id>
 *   ...
 *
 * La ruta de cada pestana es el ULTIMO campo de su linea y se toma "tal cual"
 * hasta el fin de linea, asi admite espacios.  El parser es tolerante: lineas
 * desconocidas o en blanco se ignoran; campos ausentes mantienen su default.
 * Validaciones de rango impiden desbordar los pools al reconstruir.
 */
#include "session/layout_persist.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void layout_data_clear(LayoutData *d) {
    if (!d) return;
    memset(d, 0, sizeof *d); /* todo a cero: conteos 0, flags 0, rutas vacias */
    d->dock_root = -1;        /* -1 = sin raiz (arbol vacio)                  */
    d->dock_focused_leaf = -1;
    d->active_tab = -1;       /* -1 = sin pestana activa                       */
}

/* -- Serializacion --------------------------------------------------------- */

/* Anexa con snprintf controlando el desbordamiento.  Avanza @p *off y devuelve
 * 0 si el texto no cabe (el llamante aborta y reporta "no cabe"). */
static int append(char *out, size_t cap, size_t *off, const char *fmt, ...) {
    if (*off >= cap) return 0;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + *off, cap - *off, fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if ((size_t)n >= cap - *off) return 0; /* truncado: no cabe */
    *off += (size_t)n;
    return 1;
}

size_t layout_serialize(const LayoutData *d, char *out, size_t cap) {
    if (!d || !out || cap == 0) return 0;
    size_t off = 0;
    out[0] = '\0';

#define EMIT(...) \
    do { if (!append(out, cap, &off, __VA_ARGS__)) return 0; } while (0)

    /* -- tamanos de la UI -- */
    EMIT("ui ftree_width %d\n", d->ftree_width);
    EMIT("ui ftree_open %d\n", d->ftree_open ? 1 : 0);
    EMIT("ui ext_panel_w %d\n", d->ext_panel_w);
    EMIT("ui ext_panel_open %d\n", d->ext_panel_open ? 1 : 0);
    EMIT("ui bottom_panel_h %d\n", d->bottom_panel_h);
    EMIT("ui bottom_panel_open %d\n", d->bottom_panel_open ? 1 : 0);

    /* -- foco -- */
    EMIT("focus active_group %d\n", d->active_group);
    EMIT("focus active_tab %d\n", d->active_tab);

    /* -- pestanas -- */
    EMIT("tabs %d\n", d->tab_count);
    for (int i = 0; i < d->tab_count; i++) {
        const LayoutTab *t = &d->tabs[i];
        /* La ruta va al final de la linea (admite espacios). */
        EMIT("tab %d %d %s\n", t->group, t->is_group_active ? 1 : 0, t->path);
    }

    /* -- arbol de dock -- */
    EMIT("dock %d %d %d %d\n", d->dock_node_count, d->dock_root,
         d->dock_focused_leaf, d->dock_leaf_count);
    for (int i = 0; i < d->dock_node_count; i++) {
        const LayoutDockNode *n = &d->dock_nodes[i];
        EMIT("node %d %d %d %.6f %d %d %d\n", n->kind, n->group_id, n->orient,
             n->ratio, n->child_a, n->child_b, n->parent);
    }

    /* -- flotantes -- */
    EMIT("floats %d\n", d->float_count);
    for (int i = 0; i < d->float_count; i++) {
        const LayoutFloat *f = &d->floats[i];
        EMIT("float %d %d %d %d %d\n", f->x, f->y, f->w, f->h, f->group_id);
    }

#undef EMIT
    return off;
}

/* -- Parseo ---------------------------------------------------------------- */

/* Copia la linea apuntada por @p p (hasta '\n' o NUL) en @p buf, recortando el
 * salto.  Devuelve el puntero al inicio de la SIGUIENTE linea (o al NUL). */
static const char *read_line(const char *p, char *buf, size_t cap) {
    size_t i = 0;
    while (*p && *p != '\n') {
        if (i + 1 < cap) buf[i++] = *p;
        p++;
    }
    buf[i] = '\0';
    /* recortar un posible '\r' final (ficheros con CRLF) */
    if (i > 0 && buf[i - 1] == '\r') buf[i - 1] = '\0';
    if (*p == '\n') p++; /* saltar el salto de linea */
    return p;
}

/* Salta espacios/tabs iniciales. */
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

int layout_parse(const char *text, LayoutData *out) {
    if (!out) return 0;
    layout_data_clear(out);
    if (!text) return 0;

    int saw_anything = 0; /* 1 si parseamos algo persistible coherente */

    char line[640];
    const char *p = text;
    while (*p) {
        p = read_line(p, line, sizeof line);
        const char *s = skip_ws(line);
        if (*s == '\0') continue; /* linea en blanco */

        /* -- tamanos de la UI: "ui <clave> <valor>" -- */
        if (strncmp(s, "ui ", 3) == 0) {
            char key[64];
            int val;
            if (sscanf(s + 3, "%63s %d", key, &val) == 2) {
                if (!strcmp(key, "ftree_width"))
                    out->ftree_width = val;
                else if (!strcmp(key, "ftree_open"))
                    out->ftree_open = val ? 1 : 0;
                else if (!strcmp(key, "ext_panel_w"))
                    out->ext_panel_w = val;
                else if (!strcmp(key, "ext_panel_open"))
                    out->ext_panel_open = val ? 1 : 0;
                else if (!strcmp(key, "bottom_panel_h"))
                    out->bottom_panel_h = val;
                else if (!strcmp(key, "bottom_panel_open"))
                    out->bottom_panel_open = val ? 1 : 0;
                saw_anything = 1;
            }
            continue;
        }

        /* -- foco: "focus <clave> <valor>" -- */
        if (strncmp(s, "focus ", 6) == 0) {
            char key[64];
            int val;
            if (sscanf(s + 6, "%63s %d", key, &val) == 2) {
                if (!strcmp(key, "active_group"))
                    out->active_group = val;
                else if (!strcmp(key, "active_tab"))
                    out->active_tab = val;
            }
            continue;
        }

        /* -- cabecera de pestanas: "tabs <count>" (informativa; el conteo real
         *    lo lleva tab_count al ir anexando lineas "tab"). -- */
        if (strncmp(s, "tabs ", 5) == 0) {
            continue; /* el conteo se deriva de las lineas "tab" */
        }

        /* -- una pestana: "tab <group> <is_active> <path...>" -- */
        if (strncmp(s, "tab ", 4) == 0) {
            const char *q = skip_ws(s + 4);
            int group = 0, is_active = 0;
            int consumed = 0;
            if (sscanf(q, "%d %d %n", &group, &is_active, &consumed) >= 2 &&
                consumed > 0) {
                const char *path = q + consumed; /* resto = ruta (con espacios) */
                if (*path && out->tab_count < LAYOUT_MAX_TABS) {
                    LayoutTab *t = &out->tabs[out->tab_count];
                    strncpy(t->path, path, sizeof(t->path) - 1);
                    t->path[sizeof(t->path) - 1] = '\0';
                    t->group = group;
                    t->is_group_active = is_active ? 1 : 0;
                    out->tab_count++;
                    saw_anything = 1;
                }
            }
            continue;
        }

        /* -- cabecera del dock: "dock <count> <root> <focused> <leaf_count>" -- */
        if (strncmp(s, "dock ", 5) == 0) {
            int nc = 0, root = -1, foc = -1, lc = 0;
            if (sscanf(s + 5, "%d %d %d %d", &nc, &root, &foc, &lc) == 4) {
                out->dock_root = root;
                out->dock_focused_leaf = foc;
                out->dock_leaf_count = lc;
                /* dock_node_count lo deriva el numero de lineas "node"; aqui no
                 * lo fijamos para tolerar discrepancias. */
            }
            continue;
        }

        /* -- un nodo: "node <kind> <gid> <orient> <ratio> <ca> <cb> <parent>" -- */
        if (strncmp(s, "node ", 5) == 0) {
            int kind = 0, gid = 0, orient = 0, ca = -1, cb = -1, parent = -1;
            float ratio = 0.5f;
            if (sscanf(s + 5, "%d %d %d %f %d %d %d", &kind, &gid, &orient,
                       &ratio, &ca, &cb, &parent) == 7 &&
                out->dock_node_count < LAYOUT_MAX_NODES) {
                LayoutDockNode *n = &out->dock_nodes[out->dock_node_count];
                n->kind = kind;
                n->group_id = gid;
                n->orient = orient;
                n->ratio = ratio;
                n->child_a = ca;
                n->child_b = cb;
                n->parent = parent;
                out->dock_node_count++;
                saw_anything = 1;
            }
            continue;
        }

        /* -- cabecera de flotantes: "floats <count>" -- */
        if (strncmp(s, "floats ", 7) == 0) {
            continue; /* el conteo se deriva de las lineas "float" */
        }

        /* -- un flotante: "float <x> <y> <w> <h> <group_id>" -- */
        if (strncmp(s, "float ", 6) == 0) {
            int x = 0, y = 0, w = 0, h = 0, gid = 0;
            if (sscanf(s + 6, "%d %d %d %d %d", &x, &y, &w, &h, &gid) == 5 &&
                out->float_count < LAYOUT_MAX_FLOATS) {
                LayoutFloat *f = &out->floats[out->float_count];
                f->x = x;
                f->y = y;
                f->w = w;
                f->h = h;
                f->group_id = gid;
                out->float_count++;
                saw_anything = 1;
            }
            continue;
        }

        /* cualquier otra linea: ignorar (tolerancia a formatos futuros) */
    }

    /* Derivar dock_node_count de las lineas "node" efectivamente leidas. */
    /* (out->dock_node_count ya quedo con ese valor al ir anexando.) */

    /* -- Validacion de coherencia del arbol de dock --------------------------
     * Si hay nodos, la raiz debe indexar uno valido y todo indice de hijo/padre
     * debe caer en rango o ser -1.  Ante cualquier inconsistencia, se descarta
     * SOLO el arbol (no las pestanas ni los tamanos): el wireado caera a un
     * arbol por defecto reconstruido desde los grupos de las pestanas. */
    if (out->dock_node_count > 0) {
        int bad = 0;
        if (out->dock_root < 0 || out->dock_root >= out->dock_node_count)
            bad = 1;
        for (int i = 0; !bad && i < out->dock_node_count; i++) {
            const LayoutDockNode *n = &out->dock_nodes[i];
            if (n->child_a != -1 &&
                (n->child_a < 0 || n->child_a >= out->dock_node_count))
                bad = 1;
            if (n->child_b != -1 &&
                (n->child_b < 0 || n->child_b >= out->dock_node_count))
                bad = 1;
            if (n->parent != -1 &&
                (n->parent < 0 || n->parent >= out->dock_node_count))
                bad = 1;
        }
        if (bad) {
            /* invalidar el arbol persistido; el resto se conserva */
            out->dock_node_count = 0;
            out->dock_root = -1;
            out->dock_focused_leaf = -1;
            out->dock_leaf_count = 0;
        }
    }

    return saw_anything ? 1 : 0;
}
