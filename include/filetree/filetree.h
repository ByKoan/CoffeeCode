#pragma once
#include "structs/vec.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

/* -- Dimensiones del panel ------------------------------------------------- */
#define FTREE_WIDTH_DEFAULT 220 /* ancho cuando está visible               */
#define FTREE_MIN_WIDTH 80      /* no se puede colapsar más que esto        */
#define FTREE_ITEM_H 22         /* altura de cada fila                      */
#define FTREE_INDENT 14         /* sangría por nivel de profundidad         */
#define FTREE_ICON_W 16         /* espacio para icono de carpeta/archivo    */
#define FTREE_TOGGLE_BTN_W 18   /* ancho del botón « / » en el borde        */

/* -- Número máximo de entradas --------------------------------------------- */
#define FTREE_MAX_ENTRIES 2048

/* -- Tipo de entrada ------------------------------------------------------- */
typedef enum { FTYPE_DIR = 0, FTYPE_FILE = 1 } FEntryType;

/* -- Una entrada del árbol -------------------------------------------------
 * Cada entrada es un archivo o carpeta del árbol "aplanado". La jerarquía se
 * representa solo con `depth`: los hijos de una carpeta son las entradas que la
 * siguen en el vector con depth mayor. */
typedef struct {
    char path[512];  /* ruta absoluta                               */
    char name[256];  /* nombre de archivo / carpeta                 */
    int depth;       /* profundidad (0 = raíz)                      */
    FEntryType type; /* FTYPE_DIR o FTYPE_FILE                      */
    int expanded;    /* 1 si el directorio está expandido           */
    int visible;     /* 1 si debe pintarse (padres expandidos)      */
} FEntry;

/* -- Estado del explorador ------------------------------------------------- */
typedef struct {
    int open;            /* 1 = panel visible               */
    int width;           /* ancho actual en px               */
    int scroll;          /* líneas desplazadas               */
    int hovered;         /* índice bajo el cursor, -1=ninguno*/
    Vec entries;         /* Vec<FEntry>: entradas del árbol  */
    char root_path[512]; /* carpeta raíz cargada             */

    /* arrastrar el borde para redimensionar */
    int dragging_border; /* 1 mientras se arrastra el borde derecho   */
    int drag_start_x;    /* X del ratón al empezar a arrastrar (px)   */
    int drag_start_w;    /* ancho del panel al empezar a arrastrar    */
} FileTree;

/* -- API pública ----------------------------------------------------------- */

/* Inicializa la estructura (panel cerrado) — aloja entries en el heap */
void ftree_init(FileTree *ft);

/* Libera la memoria del heap */
void ftree_free(FileTree *ft);

/* Carga un directorio raíz en el árbol */
void ftree_load(FileTree *ft, const char *dirpath);

/* Expande o colapsa una entrada directorio */
void ftree_toggle(FileTree *ft, int index);

/* Recalcula qué entradas son visibles según el estado de expansión */
void ftree_refresh_visibility(FileTree *ft);

/* Devuelve el índice de la entrada visible número n (0-based) */
int ftree_nth_visible(const FileTree *ft, int n);

/* Cuenta cuántas entradas son visibles */
int ftree_visible_count(const FileTree *ft);

/* Accesores: `entries` es ahora un Vec<FEntry> (sin límite fijo) */
static inline int ftree_count(const FileTree *ft) {
    return (int)ft->entries.len;
}
static inline FEntry *ftree_entry(const FileTree *ft, int i) {
    return (FEntry *)vec_at(&ft->entries, (size_t)i);
}
