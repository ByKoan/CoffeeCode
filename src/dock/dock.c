/**
 * @file dock.c
 * @brief Logica PURA del arbol de dock: construccion, mutacion, layout y hit.
 *
 * No depende de SDL ni de la struct Editor, asi que se compila y prueba en
 * headless (ver test/dock/test_dock.c).  El render y el input la alimentan con
 * la geometria del area del editor y enrutan por los rects que devuelve.
 */
#include "dock/dock.h"

/* -- Helpers internos del pool -------------------------------------------- */

/** Reserva un nodo del pool y devuelve su indice, o DOCK_NONE si esta lleno. */
static int dock_alloc_node(DockTree *t) {
    if (t->node_count >= DOCK_MAX_NODES) return DOCK_NONE;
    return t->node_count++;
}

/** El indice @p n apunta a un nodo valido del pool? */
static int dock_node_valid(const DockTree *t, int n) {
    return n >= 0 && n < t->node_count;
}

/** Recorta @p v al rango [@p lo, @p hi] (con hi degenerado tolerado). */
static int clamp_int(int v, int lo, int hi) {
    if (hi < lo) hi = lo;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

/* ===========================================================================
 *  Construccion y mutacion del arbol
 * =========================================================================== */

void dock_init_single(DockTree *t, int group_id) {
    t->node_count = 0;
    int r = dock_alloc_node(t); /* siempre 0: pool recien vaciado */
    DockNode *n = &t->nodes[r];
    n->kind = DOCK_LEAF;
    n->group_id = group_id;
    n->orient = DOCK_VERTICAL;
    n->ratio = 0.5f;
    n->child_a = DOCK_NONE;
    n->child_b = DOCK_NONE;
    n->parent = DOCK_NONE;
    t->root = r;
    t->focused_leaf = r;
    t->leaf_count = 1;
}

int dock_split_leaf(DockTree *t, int leaf, DockOrient orient, int new_group_id) {
    if (!dock_node_valid(t, leaf)) return DOCK_NONE;
    if (t->nodes[leaf].kind != DOCK_LEAF) return DOCK_NONE; /* solo hojas */
    if (t->leaf_count >= DOCK_MAX_LEAVES) return DOCK_NONE;  /* tope de hojas */
    /* Hacen falta dos nodos nuevos: la hoja-original-clonada y la hoja nueva.
     * El nodo @p leaf se reconvierte en SPLIT en su sitio (conserva su indice y
     * su padre, asi no hay que reenganchar al abuelo). */
    int a = dock_alloc_node(t); /* hoja con el group_id original */
    if (a == DOCK_NONE) return DOCK_NONE;
    int b = dock_alloc_node(t); /* hoja nueva */
    if (b == DOCK_NONE) {
        t->node_count--; /* deshacer la reserva de 'a' */
        return DOCK_NONE;
    }

    int orig_group = t->nodes[leaf].group_id;

    /* child_a: hoja con el contenido original */
    t->nodes[a].kind = DOCK_LEAF;
    t->nodes[a].group_id = orig_group;
    t->nodes[a].child_a = DOCK_NONE;
    t->nodes[a].child_b = DOCK_NONE;
    t->nodes[a].parent = leaf;

    /* child_b: hoja nueva */
    t->nodes[b].kind = DOCK_LEAF;
    t->nodes[b].group_id = new_group_id;
    t->nodes[b].child_a = DOCK_NONE;
    t->nodes[b].child_b = DOCK_NONE;
    t->nodes[b].parent = leaf;

    /* el nodo original pasa a ser el SPLIT padre de ambas hojas */
    t->nodes[leaf].kind = DOCK_SPLIT;
    t->nodes[leaf].orient = orient;
    t->nodes[leaf].ratio = 0.5f;
    t->nodes[leaf].child_a = a;
    t->nodes[leaf].child_b = b;
    /* parent del split = el que tenia la hoja original (sin cambios) */

    /* si la hoja dividida tenia el foco, pasa a la hoja con el contenido
     * original (a); el llamante puede re-enfocar la nueva si lo desea */
    if (t->focused_leaf == leaf) t->focused_leaf = a;

    t->leaf_count++;
    return b;
}

/**
 * @brief Compacta el pool eliminando los nodos @p dead_a y @p dead_b.
 *
 * Tras colapsar un split sobran dos indices (el split y la hoja eliminada).
 * Para mantener el pool denso (sin huecos) movemos el ULTIMO nodo a cada hueco
 * y reparamos las referencias (parent/child_a/child_b/root/focused_leaf) que
 * apuntaban al nodo movido.  Se eliminan de mayor a menor para no invalidar el
 * segundo hueco al mover sobre el primero.
 */
static void dock_compact_remove(DockTree *t, int dead_a, int dead_b) {
    int dead[2];
    /* ordenar descendente: eliminar primero el indice mayor */
    if (dead_a >= dead_b) { dead[0] = dead_a; dead[1] = dead_b; }
    else                  { dead[0] = dead_b; dead[1] = dead_a; }

    for (int k = 0; k < 2; k++) {
        int hole = dead[k];
        int last = t->node_count - 1;
        if (hole != last) {
            /* mover 'last' al hueco */
            t->nodes[hole] = t->nodes[last];
            int moved = hole; /* nuevo indice del nodo que estaba en 'last' */
            /* reparar el padre del nodo movido (su child_a/b apuntaba a last) */
            int p = t->nodes[moved].parent;
            if (dock_node_valid(t, p)) {
                if (t->nodes[p].child_a == last) t->nodes[p].child_a = moved;
                if (t->nodes[p].child_b == last) t->nodes[p].child_b = moved;
            }
            /* reparar los hijos del nodo movido (su parent apuntaba a last) */
            if (t->nodes[moved].kind == DOCK_SPLIT) {
                int ca = t->nodes[moved].child_a;
                int cb = t->nodes[moved].child_b;
                if (dock_node_valid(t, ca)) t->nodes[ca].parent = moved;
                if (dock_node_valid(t, cb)) t->nodes[cb].parent = moved;
            }
            /* reparar raiz y foco si apuntaban al nodo movido */
            if (t->root == last) t->root = moved;
            if (t->focused_leaf == last) t->focused_leaf = moved;
            /* si el OTRO hueco pendiente era 'last', ahora vive en 'hole' */
            if (k == 0 && dead[1] == last) dead[1] = hole;
        }
        t->node_count--;
    }
}

int dock_remove_leaf(DockTree *t, int leaf) {
    if (!dock_node_valid(t, leaf)) return 0;
    if (t->nodes[leaf].kind != DOCK_LEAF) return 0;
    if (leaf == t->root) return 0; /* unica hoja: no se elimina */

    int split = t->nodes[leaf].parent;
    if (!dock_node_valid(t, split) || t->nodes[split].kind != DOCK_SPLIT)
        return 0; /* arbol mal formado: defensivo */

    /* el hermano = el otro hijo del split */
    int sib = (t->nodes[split].child_a == leaf) ? t->nodes[split].child_b
                                                : t->nodes[split].child_a;
    if (!dock_node_valid(t, sib)) return 0;

    int grandparent = t->nodes[split].parent;

    /* El hermano sube a ocupar el lugar del split: hereda el padre del split. */
    t->nodes[sib].parent = grandparent;
    if (grandparent == DOCK_NONE) {
        /* el split era la raiz: el hermano pasa a ser la nueva raiz */
        t->root = sib;
    } else {
        if (t->nodes[grandparent].child_a == split)
            t->nodes[grandparent].child_a = sib;
        if (t->nodes[grandparent].child_b == split)
            t->nodes[grandparent].child_b = sib;
    }

    /* si la hoja eliminada o el split tenian el foco, pasarlo a una hoja viva
     * del subarbol del hermano (la primera hoja en preorden) */
    if (t->focused_leaf == leaf || t->focused_leaf == split) {
        int n = sib;
        while (dock_node_valid(t, n) && t->nodes[n].kind == DOCK_SPLIT)
            n = t->nodes[n].child_a;
        t->focused_leaf = n;
    }

    t->leaf_count--;
    /* compactar el pool quitando 'leaf' y 'split' (ambos sobran) */
    dock_compact_remove(t, leaf, split);
    return 1;
}

int dock_leaf_by_group(const DockTree *t, int group_id) {
    for (int i = 0; i < t->node_count; i++)
        if (t->nodes[i].kind == DOCK_LEAF && t->nodes[i].group_id == group_id)
            return i;
    return DOCK_NONE;
}

int dock_alloc_group_id(const DockTree *t) {
    for (int g = 0; g < DOCK_MAX_LEAVES; g++)
        if (dock_leaf_by_group(t, g) == DOCK_NONE) return g;
    return DOCK_NONE;
}

/* ===========================================================================
 *  Layout
 * =========================================================================== */

/**
 * @brief Reparte el area de un split entre sus dos hijos segun ratio/orient.
 *
 * Deja al menos ::DOCK_LEAF_MIN px a cada hijo cuando el area lo permite; si el
 * area es demasiado pequena para dos minimos, reparte a medias (degenerado).
 *
 * @param area  Area del split.
 * @param orient Orientacion del split.
 * @param ratio Fraccion [0,1] para child_a.
 * @param[out] ra Area resultante de child_a.
 * @param[out] rb Area resultante de child_b.
 */
static void dock_split_areas(DockRect area, DockOrient orient, float ratio,
                             DockRect *ra, DockRect *rb) {
    if (orient == DOCK_VERTICAL) {
        int total = area.w;
        int a_w = (int)(total * ratio + 0.5f);
        /* respetar el minimo por hijo cuando hay espacio para dos */
        if (total >= 2 * DOCK_LEAF_MIN) {
            a_w = clamp_int(a_w, DOCK_LEAF_MIN, total - DOCK_LEAF_MIN);
        } else {
            a_w = total / 2; /* demasiado estrecho: a medias */
        }
        ra->x = area.x;      ra->y = area.y;
        ra->w = a_w;         ra->h = area.h;
        rb->x = area.x + a_w; rb->y = area.y;
        rb->w = total - a_w;  rb->h = area.h;
    } else {
        int total = area.h;
        int a_h = (int)(total * ratio + 0.5f);
        if (total >= 2 * DOCK_LEAF_MIN) {
            a_h = clamp_int(a_h, DOCK_LEAF_MIN, total - DOCK_LEAF_MIN);
        } else {
            a_h = total / 2;
        }
        ra->x = area.x;      ra->y = area.y;
        ra->w = area.w;      ra->h = a_h;
        rb->x = area.x;      rb->y = area.y + a_h;
        rb->w = area.w;      rb->h = total - a_h;
    }
}

/** Recorrido recursivo que escribe el rect de cada hoja en out_rects. */
static void dock_walk_rects(const DockTree *t, int node, DockRect area,
                            DockLeafRect *out, int max_out, int *count) {
    if (!dock_node_valid(t, node)) return;
    if (t->nodes[node].kind == DOCK_LEAF) {
        if (*count < max_out) {
            out[*count].group_id = t->nodes[node].group_id;
            out[*count].node = node;
            out[*count].rect = area;
            (*count)++;
        }
        return;
    }
    /* SPLIT: repartir y recursar */
    DockRect ra, rb;
    dock_split_areas(area, t->nodes[node].orient, t->nodes[node].ratio, &ra, &rb);
    dock_walk_rects(t, t->nodes[node].child_a, ra, out, max_out, count);
    dock_walk_rects(t, t->nodes[node].child_b, rb, out, max_out, count);
}

int dock_compute_leaf_rects(const DockTree *t, DockRect area,
                            DockLeafRect *out_rects, int max_out) {
    int count = 0;
    dock_walk_rects(t, t->root, area, out_rects, max_out, &count);
    return count;
}

void dock_child_areas(const DockTree *t, int node, DockRect area, DockRect *ra,
                      DockRect *rb) {
    if (!dock_node_valid(t, node) || t->nodes[node].kind != DOCK_SPLIT) {
        *ra = area;
        *rb = area;
        return;
    }
    dock_split_areas(area, t->nodes[node].orient, t->nodes[node].ratio, ra, rb);
}

/* ===========================================================================
 *  Hit-test y arrastre de divisores internos
 * =========================================================================== */

/** Recorrido recursivo que busca el split bajo el cursor; -1 si ninguno. */
static int dock_walk_hit(const DockTree *t, int node, DockRect area, int mx,
                         int my, DockOrient *orient) {
    if (!dock_node_valid(t, node)) return DOCK_NONE;
    if (t->nodes[node].kind == DOCK_LEAF) return DOCK_NONE;

    DockRect ra, rb;
    dock_split_areas(area, t->nodes[node].orient, t->nodes[node].ratio, &ra, &rb);

    /* franja agarrable centrada en el borde entre child_a y child_b */
    if (t->nodes[node].orient == DOCK_VERTICAL) {
        int edge_x = rb.x; /* borde = inicio de child_b */
        if (my >= area.y && my < area.y + area.h &&
            mx >= edge_x - DOCK_DIVIDER_GRAB && mx <= edge_x + DOCK_DIVIDER_GRAB) {
            if (orient) *orient = DOCK_VERTICAL;
            return node;
        }
    } else {
        int edge_y = rb.y; /* borde = inicio de child_b */
        if (mx >= area.x && mx < area.x + area.w &&
            my >= edge_y - DOCK_DIVIDER_GRAB && my <= edge_y + DOCK_DIVIDER_GRAB) {
            if (orient) *orient = DOCK_HORIZONTAL;
            return node;
        }
    }

    /* no es este divisor: bajar al hijo cuyo sub-area contiene el cursor */
    int hit = dock_walk_hit(t, t->nodes[node].child_a, ra, mx, my, orient);
    if (hit != DOCK_NONE) return hit;
    return dock_walk_hit(t, t->nodes[node].child_b, rb, mx, my, orient);
}

int dock_hit_divider(const DockTree *t, DockRect area, int mx, int my,
                     DockOrient *orient) {
    return dock_walk_hit(t, t->root, area, mx, my, orient);
}

/** Localiza el area concreta del nodo @p target recorriendo desde la raiz. */
static int dock_area_of(const DockTree *t, int node, DockRect area, int target,
                        DockRect *out) {
    if (!dock_node_valid(t, node)) return 0;
    if (node == target) { *out = area; return 1; }
    if (t->nodes[node].kind == DOCK_LEAF) return 0;
    DockRect ra, rb;
    dock_split_areas(area, t->nodes[node].orient, t->nodes[node].ratio, &ra, &rb);
    if (dock_area_of(t, t->nodes[node].child_a, ra, target, out)) return 1;
    return dock_area_of(t, t->nodes[node].child_b, rb, target, out);
}

void dock_apply_divider(DockTree *t, DockRect area, int split, int mx, int my) {
    if (!dock_node_valid(t, split) || t->nodes[split].kind != DOCK_SPLIT) return;
    DockRect sa; /* area concreta de este split */
    if (!dock_area_of(t, t->root, area, split, &sa)) return;

    if (t->nodes[split].orient == DOCK_VERTICAL) {
        if (sa.w <= 0) return;
        int a_w = clamp_int(mx - sa.x, DOCK_LEAF_MIN, sa.w - DOCK_LEAF_MIN);
        if (sa.w < 2 * DOCK_LEAF_MIN) a_w = sa.w / 2;
        t->nodes[split].ratio = (float)a_w / (float)sa.w;
    } else {
        if (sa.h <= 0) return;
        int a_h = clamp_int(my - sa.y, DOCK_LEAF_MIN, sa.h - DOCK_LEAF_MIN);
        if (sa.h < 2 * DOCK_LEAF_MIN) a_h = sa.h / 2;
        t->nodes[split].ratio = (float)a_h / (float)sa.h;
    }
}
