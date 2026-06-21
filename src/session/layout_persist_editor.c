/**
 * @file layout_persist_editor.c
 * @brief Wireado entre la struct Editor y la capa pura de persistencia de
 *        disposicion (ver session/layout_persist.h).
 *
 * Aqui viven las dos operaciones que SI conocen la struct Editor:
 *   - ::layout_save: vuelca la disposicion viva del editor a disco.
 *   - ::layout_restore: reconstruye el editor desde el fichero de disposicion.
 *
 * La ruta del fichero (@c layout.txt) sigue la MISMA convencion de datos de
 * usuario que las preferencias (@c SDL_GetPrefPath("CoffeeCode","CoffeeCode")),
 * de modo que ambos viven juntos.
 *
 * Robustez: si el fichero no existe, esta corrupto o queda vacio tras restaurar
 * (p.ej. todos los archivos guardados fueron borrados del disco),
 * ::layout_restore devuelve 0 y el editor sigue con su estado por defecto.  Tras
 * reconstruir se validan las invariantes (pestana activa por grupo, arbol
 * coherente, flotantes con grupo existente).
 */
#include "session/layout_persist.h"

#include "editor/editor.h"

#include <SDL3/SDL.h> /* SDL_GetPrefPath, SDL_free */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Verificacion en compile-time de que los topes del POD cubren los del editor.
 * Si un tope del editor crece por encima del POD, este assert rompe el build y
 * recuerda actualizar los #define de layout_persist.h. */
#if MAX_TABS > LAYOUT_MAX_TABS
#error "LAYOUT_MAX_TABS quedo por debajo de MAX_TABS"
#endif
#if DOCK_MAX_NODES > LAYOUT_MAX_NODES
#error "LAYOUT_MAX_NODES quedo por debajo de DOCK_MAX_NODES"
#endif
#if DOCK_MAX_LEAVES > LAYOUT_MAX_LEAVES
#error "LAYOUT_MAX_LEAVES quedo por debajo de DOCK_MAX_LEAVES"
#endif
#if FLOAT_MAX_PANELS > LAYOUT_MAX_FLOATS
#error "LAYOUT_MAX_FLOATS quedo por debajo de FLOAT_MAX_PANELS"
#endif

/**
 * @brief Compone la ruta de @c layout.txt en @p out (misma carpeta que settings).
 * @return 1 si se obtuvo; 0 si fallo SDL_GetPrefPath o no cabe.
 */
static int layout_file_path(char *out, size_t cap) {
    char *pref = SDL_GetPrefPath("CoffeeCode", "CoffeeCode");
    if (!pref) return 0;
    int n = snprintf(out, cap, "%slayout.txt", pref);
    SDL_free(pref);
    return n > 0 && (size_t)n < cap;
}

/* -- Guardado -------------------------------------------------------------- */

/* Rellena @p d con la disposicion viva de @p e. */
void layout_capture_editor(const Editor *e, LayoutData *d) {
    if (!e || !d) return;
    layout_data_clear(d);

    /* geometria de la ventana del SO (la usan las secundarias al recrearse). */
    d->win_w = e->win_w;
    d->win_h = e->win_h;
    d->win_x = 0;
    d->win_y = 0;
    if (e->window) SDL_GetWindowPosition(e->window, &d->win_x, &d->win_y);
    d->has_win = 1;

    /* -- tamanos de la UI -- */
    d->ftree_width = e->ftree.width;
    d->ftree_open = e->ftree.open ? 1 : 0;
    d->ext_panel_w = e->ext_panel_w;
    d->ext_panel_open = e->ext_panel_open ? 1 : 0;
    d->bottom_panel_h = e->bottom_panel_h;
    d->bottom_panel_open = e->bottom_panel_open ? 1 : 0;

    /* -- foco -- */
    d->active_group = e->active_group;
    d->active_tab = e->active_tab;

    /* -- pestanas con ruta (las "Sin titulo" sin ruta no se persisten) -- */
    for (int i = 0; i < e->tab_count && d->tab_count < LAYOUT_MAX_TABS; i++) {
        const EditorTab *t = &e->tabs[i];
        if (t->filepath[0] == '\0') continue; /* sin ruta: no se guarda */
        LayoutTab *lt = &d->tabs[d->tab_count];
        strncpy(lt->path, t->filepath, sizeof(lt->path) - 1);
        lt->path[sizeof(lt->path) - 1] = '\0';
        lt->group = t->group;
        /* activa de su grupo si el indice global coincide con el guardado */
        lt->is_group_active =
            (t->group >= 0 && t->group < MAX_GROUPS &&
             e->group_active_tab[t->group] == i)
                ? 1
                : 0;
        d->tab_count++;
    }

    /* -- arbol de dock: volcar el pool tal cual -- */
    d->dock_node_count = e->dock.node_count;
    if (d->dock_node_count > LAYOUT_MAX_NODES)
        d->dock_node_count = LAYOUT_MAX_NODES;
    for (int i = 0; i < d->dock_node_count; i++) {
        const DockNode *n = &e->dock.nodes[i];
        LayoutDockNode *ln = &d->dock_nodes[i];
        ln->kind = (n->kind == DOCK_SPLIT) ? 1 : 0;
        ln->group_id = n->group_id;
        ln->orient = (n->orient == DOCK_HORIZONTAL) ? 1 : 0;
        ln->ratio = n->ratio;
        ln->child_a = n->child_a;
        ln->child_b = n->child_b;
        ln->parent = n->parent;
    }
    d->dock_root = e->dock.root;
    d->dock_focused_leaf = e->dock.focused_leaf;
    d->dock_leaf_count = e->dock.leaf_count;

    /* -- flotantes -- */
    d->float_count = e->float_count;
    if (d->float_count > LAYOUT_MAX_FLOATS) d->float_count = LAYOUT_MAX_FLOATS;
    for (int i = 0; i < d->float_count; i++) {
        const FloatPanel *fp = &e->floats[i];
        LayoutFloat *lf = &d->floats[i];
        lf->x = fp->rect.x;
        lf->y = fp->rect.y;
        lf->w = fp->rect.w;
        lf->h = fp->rect.h;
        lf->group_id = fp->group_id;
    }
}

void layout_save(const Editor *e) {
    if (!e) return;

    /* Nada interesante que guardar: ninguna pestana con ruta y editor sin
     * dividir ni flotantes -> no escribimos (el arranque por defecto basta). */
    int has_path_tab = 0;
    for (int i = 0; i < e->tab_count; i++)
        if (e->tabs[i].filepath[0]) {
            has_path_tab = 1;
            break;
        }
    if (!has_path_tab && e->dock.leaf_count <= 1 && e->float_count == 0) return;

    LayoutData d;
    layout_capture_editor(e, &d);

    static char text[64 * 1024]; /* holgado para el pool completo */
    size_t n = layout_serialize(&d, text, sizeof text);
    if (n == 0) return; /* no cabe: abandonar sin escribir */

    char path[1024];
    if (!layout_file_path(path, sizeof path)) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    fwrite(text, 1, n, f);
    fclose(f);
}

/* -- Restauracion ---------------------------------------------------------- */

/* Mapea el group_id persistido de un nodo/flotante a un group_id "vivo".
 * Tras reabrir, conservamos los group_id originales (no se remapean) porque las
 * pestanas tambien llevan su group_id original; solo hace falta comprobar que
 * el espacio de ids cabe en MAX_GROUPS, cosa que la validacion garantiza. */

/* Reconstruye el arbol de dock de @p e desde @p d.  Asume que d->dock_* ya paso
 * la validacion de coherencia de layout_parse.  Si el pool persistido esta
 * vacio (o no hay arbol), deja una sola hoja con el grupo 0 (default). */
static void layout_rebuild_dock(Editor *e, const LayoutData *d) {
    if (d->dock_node_count <= 0 || d->dock_root < 0) {
        dock_init_single(&e->dock, 0);
        return;
    }
    int nc = d->dock_node_count;
    if (nc > DOCK_MAX_NODES) nc = DOCK_MAX_NODES;
    for (int i = 0; i < nc; i++) {
        const LayoutDockNode *ln = &d->dock_nodes[i];
        DockNode *n = &e->dock.nodes[i];
        n->kind = (ln->kind == 1) ? DOCK_SPLIT : DOCK_LEAF;
        n->group_id = ln->group_id;
        n->orient = (ln->orient == 1) ? DOCK_HORIZONTAL : DOCK_VERTICAL;
        n->ratio = ln->ratio;
        n->child_a = ln->child_a;
        n->child_b = ln->child_b;
        n->parent = ln->parent;
    }
    e->dock.node_count = nc;
    e->dock.root = d->dock_root;
    e->dock.focused_leaf =
        (d->dock_focused_leaf >= 0 && d->dock_focused_leaf < nc)
            ? d->dock_focused_leaf
            : d->dock_root;
    e->dock.leaf_count = (d->dock_leaf_count > 0) ? d->dock_leaf_count : 1;
}

/* 1 si alguna hoja viva del arbol de dock usa @p group_id. */
static int dock_group_exists(const Editor *e, int group_id) {
    for (int i = 0; i < e->dock.node_count; i++)
        if (e->dock.nodes[i].kind == DOCK_LEAF &&
            e->dock.nodes[i].group_id == group_id)
            return 1;
    return 0;
}

/* 1 si algun flotante vivo usa @p group_id. */
static int float_group_exists(const Editor *e, int group_id) {
    for (int i = 0; i < e->float_count; i++)
        if (e->floats[i].group_id == group_id) return 1;
    return 0;
}

int layout_apply_editor(Editor *e, const LayoutData *dd) {
    if (!e || !dd) return 0;
    LayoutData d = *dd; /* copia local: el resto del codigo usa 'd' por valor */

    /* -- Reabrir cada archivo guardado (saltando los que ya no existan) ------
     * editor_tab_open coloca la pestana en e->active_group, asi que fijamos el
     * grupo destino ANTES de cada apertura.  Recordamos el indice global de la
     * pestana que debe quedar activa por grupo. */
    int want_active_for_group[MAX_GROUPS];
    for (int g = 0; g < MAX_GROUPS; g++) want_active_for_group[g] = -1;

    int opened = 0;
    for (int i = 0; i < d.tab_count; i++) {
        const LayoutTab *lt = &d.tabs[i];
        if (lt->group < 0 || lt->group >= MAX_GROUPS) continue; /* gid invalido */
        /* saltar archivos que ya no existen en disco */
        FILE *chk = fopen(lt->path, "rb");
        if (!chk) continue;
        fclose(chk);
        if (e->tab_count >= MAX_TABS) break;

        e->active_group = lt->group; /* la nueva pestana ira a este grupo */
        int before = e->tab_count;
        editor_tab_open(e, lt->path);
        if (e->tab_count > before) {
            int idx = e->tab_count - 1; /* la recien creada es la ultima */
            /* editor_tab_open la coloca en e->active_group; forzar su grupo por
             * si acaso (idempotente) y registrar si debe ser la activa. */
            e->tabs[idx].group = lt->group;
            if (lt->is_group_active) want_active_for_group[lt->group] = idx;
            opened++;
        }
    }

    if (opened == 0) {
        /* Todos los archivos guardados desaparecieron: caer a default sin tocar
         * nada mas (el editor se queda como arranco). */
        return 0;
    }

    /* -- Tamanos de la UI -- */
    if (d.ftree_width > 0) e->ftree.width = d.ftree_width;
    e->ftree.open = d.ftree_open ? 1 : 0;
    if (d.ext_panel_w > 0) e->ext_panel_w = d.ext_panel_w;
    e->ext_panel_open = d.ext_panel_open ? 1 : 0;
    if (d.bottom_panel_h > 0) e->bottom_panel_h = d.bottom_panel_h;
    e->bottom_panel_open = d.bottom_panel_open ? 1 : 0;

    /* -- Arbol de dock -- */
    layout_rebuild_dock(e, &d);

    /* -- Flotantes -- */
    e->float_count = 0;
    for (int i = 0; i < d.float_count && e->float_count < FLOAT_MAX_PANELS;
         i++) {
        const LayoutFloat *lf = &d.floats[i];
        if (lf->group_id < 0 || lf->group_id >= MAX_GROUPS) continue;
        FloatPanel *fp = &e->floats[e->float_count++];
        fp->rect.x = lf->x;
        fp->rect.y = lf->y;
        fp->rect.w = (lf->w >= FLOAT_MIN_W) ? lf->w : FLOAT_MIN_W;
        fp->rect.h = (lf->h >= FLOAT_MIN_H) ? lf->h : FLOAT_MIN_H;
        fp->group_id = lf->group_id;
    }

    /* -- Validacion de invariantes tras reconstruir -------------------------
     * 1. Toda pestana cuyo grupo NO exista (ni hoja del dock ni flotante) se
     *    reasigna al grupo activo, para no dejar buffers huerfanos invisibles. */
    int fallback_group =
        (e->active_group >= 0 && e->active_group < MAX_GROUPS &&
         (dock_group_exists(e, e->active_group) ||
          float_group_exists(e, e->active_group)))
            ? e->active_group
            : (e->dock.root >= 0 ? e->dock.nodes[e->dock.root].group_id : 0);
    /* si la raiz es un SPLIT, su group_id no es el de una hoja; buscar la
     * primera hoja del arbol como fallback seguro */
    if (e->dock.root >= 0 && e->dock.nodes[e->dock.root].kind != DOCK_LEAF) {
        for (int i = 0; i < e->dock.node_count; i++)
            if (e->dock.nodes[i].kind == DOCK_LEAF) {
                fallback_group = e->dock.nodes[i].group_id;
                break;
            }
    }
    for (int i = 0; i < e->tab_count; i++) {
        int g = e->tabs[i].group;
        if (g < 0 || g >= MAX_GROUPS ||
            (!dock_group_exists(e, g) && !float_group_exists(e, g)))
            e->tabs[i].group = fallback_group;
    }

    /* 2. Fijar la pestana activa de cada grupo a partir de lo deseado, y
     *    reparar las invariantes con la logica del editor. */
    for (int g = 0; g < MAX_GROUPS; g++) {
        if (want_active_for_group[g] >= 0 &&
            want_active_for_group[g] < e->tab_count &&
            e->tabs[want_active_for_group[g]].group == g)
            e->group_active_tab[g] = want_active_for_group[g];
        /* reparar: deja un indice valido del grupo o -1 si quedo vacio */
        e->group_active_tab[g] = editor_group_valid_active_tab(e, g);
    }

    /* 3. active_group debe ser un grupo con al menos una pestana; si no, caer
     *    al fallback. */
    if (e->active_group < 0 || e->active_group >= MAX_GROUPS ||
        e->group_active_tab[e->active_group] < 0)
        e->active_group = fallback_group;
    if (e->active_group < 0 || e->active_group >= MAX_GROUPS)
        e->active_group = 0;

    /* 4. active_tab = la pestana activa del grupo enfocado. */
    int at = e->group_active_tab[e->active_group];
    if (at < 0 || at >= e->tab_count) {
        /* el grupo enfocado quedo vacio: buscar cualquier grupo con pestana */
        for (int g = 0; g < MAX_GROUPS; g++)
            if (e->group_active_tab[g] >= 0) {
                e->active_group = g;
                at = e->group_active_tab[g];
                break;
            }
    }
    if (at < 0 || at >= e->tab_count) return 0; /* sin pestanas validas: default */

    /* Hacer activa la pestana elegida via editor_tab_open (publico): al estar
     * ya abierta, redirige e->buf y los espejos de cursor/scroll a su memoria
     * viva sin recargar el archivo de cero.  Conserva e->active_group. */
    e->active_group = e->tabs[at].group;
    editor_tab_open(e, e->tabs[at].filepath);
    editor_update_lexer(e, 0);
    e->needs_redraw = 1;
    return 1;
}

int layout_restore(Editor *e) {
    if (!e) return 0;

    /* Leer el fichero entero. */
    char path[1024];
    if (!layout_file_path(path, sizeof path)) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0; /* no hay disposicion guardada: arranque por defecto */

    static char text[64 * 1024];
    size_t rd = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[rd] = '\0';

    /* El fichero puede ser una SESION multi-ventana (con bloques "window N") o el
     * formato antiguo de una sola ventana.  session_parse cubre ambos: aqui solo
     * restauramos la ventana principal (la 0); las secundarias las recrea la capa
     * de aplicacion (app_layout_restore).  Asi layout_restore sigue valido para el
     * arranque sin App (init aislado): cero regresion. */
    LayoutSession s;
    if (!session_parse(text, &s)) return 0; /* corrupto o vacio: default */
    return layout_apply_editor(e, &s.windows[0]);
}

int session_file_path(char *out, size_t cap) {
    return layout_file_path(out, cap);
}
