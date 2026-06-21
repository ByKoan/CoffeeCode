/**
 * @file filetree.c
 * @brief Panel explorador de archivos: escaneo de directorios, expandir /
 *        colapsar carpetas y consulta de entradas visibles.
 *
 * Las entradas viven en un @c Vec<FEntry> (sin límite fijo). El árbol se guarda
 * "aplanado": al expandir una carpeta se insertan sus hijos justo después, y al
 * colapsar se eliminan físicamente; así todas las entradas presentes son
 * visibles.
 */
#include "filetree/filetree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

/* Acceso al array de entradas (Vec<FEntry>) y su tamaño como int.
   FT(ft) se re-deriva tras cada vec_reserve por si hubo realloc. */
#define FT(ft) ((FEntry *)(ft)->entries.data)
#define FTN(ft) ((int)(ft)->entries.len)

/**
 * @brief Inicializa el explorador: panel cerrado y @c Vec<FEntry> vacío.
 *
 * Pone todo a cero, fija el ancho por defecto y "sin entrada bajo el cursor"
 * (@c hovered = -1), y construye el vector dinámico de entradas con una reserva
 * inicial. Esa reserva es solo una optimización: el vector crece sin límite
 * fijo.
 *
 * @param ft Explorador a inicializar.
 */
void ftree_init(FileTree *ft) {
    memset(ft, 0, sizeof(*ft));
    ft->open = 0;
    ft->width = FTREE_WIDTH_DEFAULT;
    ft->hovered = -1;
    vec_init(&ft->entries, sizeof(FEntry));
    vec_reserve(&ft->entries,
                FTREE_MAX_ENTRIES); /* reserva inicial (ya no es límite) */
}

/**
 * @brief Libera la memoria del heap usada por el vector de entradas.
 * @param ft Explorador a liberar.
 */
void ftree_free(FileTree *ft) {
    vec_free(&ft->entries);
}

/**
 * @brief Comparador de @c qsort para ordenar entradas dentro de un directorio.
 *
 * Las carpetas van primero (antes que los archivos); a igualdad de tipo, orden
 * alfabético sin distinguir mayúsculas/minúsculas.
 *
 * @param a Puntero a la primera @c FEntry.
 * @param b Puntero a la segunda @c FEntry.
 * @return <0 si @p a va antes, >0 si va después, 0 si equivalen.
 */
static int entry_cmp(const void *a, const void *b) {
    const FEntry *ea = (const FEntry *)a;
    const FEntry *eb = (const FEntry *)b;
    /* distinto tipo: la carpeta (FTYPE_DIR) siempre primero */
    if (ea->type != eb->type) return (ea->type == FTYPE_DIR) ? -1 : 1;
    return SDL_strcasecmp(ea->name, eb->name); /* mismo tipo: alfabético */
}

/**
 * @brief Escanea el directorio @p dirpath e inserta sus entradas (ya ordenadas)
 *        en la posición @p insert_at del vector aplanado.
 *
 * Primero recoge los hijos del directorio en un buffer temporal en pila (con un
 * tope de 1024 por escaneo), usando la API nativa de cada plataforma:
 *   - Windows: @c FindFirstFileA / @c FindNextFileA sobre el patrón "dir\\*".
 *   - POSIX: @c opendir / @c readdir; el tipo (dir vs archivo) sale de
 *     @c d_type, recurriendo a @c stat si el sistema de ficheros no lo informa
 *     (@c DT_UNKNOWN) o es un enlace simbólico (@c DT_LNK).
 * En ambos casos se omiten "." y ".." (y, en POSIX, todo lo que empiece por
 * '.'). Después ordena los hijos, hace hueco en el vector con un @c memmove
 * (desplazando a la derecha lo que hubiera tras @p insert_at) y los copia con
 * @c memcpy.
 *
 * @param ft        Explorador donde insertar.
 * @param dirpath   Ruta del directorio a escanear.
 * @param depth     Profundidad que se asigna a las entradas creadas.
 * @param insert_at Índice del vector donde insertar los hijos.
 * @return Número de entradas insertadas (0 si el directorio no se pudo abrir o
 *         está vacío, o si falló la reserva de memoria).
 */
static int scan_dir(FileTree *ft, const char *dirpath, int depth,
                    int insert_at) {
    /* recoger primero en un buffer temporal en el heap.
     * FEntry pesa ~784 bytes, así que 1024 en pila serían ~803 KB —
     * suficiente para provocar stack overflow en carpetas anidadas. */
    FEntry *tmp = (FEntry *)malloc(1024 * sizeof(FEntry));
    if (!tmp) return 0;
    int tmp_count = 0;

#ifdef _WIN32
    /* patrón de búsqueda "dir\*" que exige la API Win32 de enumeración */
    char pattern[516];
    snprintf(pattern, sizeof(pattern), "%s\\*", dirpath);
    WIN32_FIND_DATAA fd;
    HANDLE h =
        FindFirstFileA(pattern, &fd); /* abre la enumeración del directorio */
    if (h == INVALID_HANDLE_VALUE) { free(tmp); return 0; } /* directorio inaccesible */
    do {
        /* saltar las entradas especiales "." (actual) y ".." (padre) */
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (tmp_count >= 1024) break; /* tope del buffer temporal */
        FEntry *en = &tmp[tmp_count++];
        memset(en, 0, sizeof(*en));
        /* ruta completa = dirpath + '\' + nombre */
        snprintf(en->path, sizeof(en->path), "%s\\%s", dirpath, fd.cFileName);
        strncpy(en->name, fd.cFileName, sizeof(en->name) - 1);
        en->depth = depth;
        /* el atributo DIRECTORY distingue carpeta de archivo */
        en->type = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                       ? FTYPE_DIR
                       : FTYPE_FILE;
        en->expanded = 0;
        en->visible = 1;
    } while (FindNextFileA(h, &fd)); /* siguiente entrada hasta agotar */
    FindClose(h); /* cerrar el handle de enumeración */
#else
    DIR *dir = opendir(dirpath);
    if (!dir) { free(tmp); return 0; } /* directorio inaccesible */
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue; /* ocultar archivos ocultos */
        if (tmp_count >= 1024) break;       /* tope del buffer temporal */
        FEntry *en = &tmp[tmp_count++];
        memset(en, 0, sizeof(*en));
        /* ruta completa = dirpath + '/' + nombre */
        snprintf(en->path, sizeof(en->path), "%s/%s", dirpath, de->d_name);
        strncpy(en->name, de->d_name, sizeof(en->name) - 1);
        en->depth = depth;
        en->expanded = 0;
        en->visible = 1;

        if (de->d_type == DT_DIR) {
            en->type = FTYPE_DIR; /* el dirent ya dice que es carpeta */
        } else if (de->d_type == DT_UNKNOWN || de->d_type == DT_LNK) {
            /* el FS no informa el tipo, o es symlink: resolverlo con stat */
            struct stat st;
            en->type = (stat(en->path, &st) == 0 && S_ISDIR(st.st_mode))
                           ? FTYPE_DIR
                           : FTYPE_FILE;
        } else {
            en->type = FTYPE_FILE;
        }
    }
    closedir(dir);
#endif

    /* ordenar los hijos recogidos: carpetas primero, luego alfabético */
    qsort(tmp, (size_t)tmp_count, sizeof(FEntry), entry_cmp);

    int to_insert = tmp_count;
    if (to_insert <= 0) { free(tmp); return 0; } /* directorio vacío */

    int count = FTN(ft);
    int existing_after =
        count - insert_at; /* entradas que hay tras insert_at */
    if (existing_after < 0) existing_after = 0;

    /* crecer (sin límite fijo) y abrir hueco en insert_at */
    if (!vec_reserve(&ft->entries, (size_t)(count + to_insert))) {
        free(tmp);
        return 0;
    }
    /* desplazar a la derecha lo que había tras insert_at, dejando el hueco */
    if (existing_after > 0)
        memmove(&FT(ft)[insert_at + to_insert], &FT(ft)[insert_at],
                (size_t)existing_after * sizeof(FEntry));
    /* volcar los hijos ordenados en el hueco */
    memcpy(&FT(ft)[insert_at], tmp, (size_t)to_insert * sizeof(FEntry));
    ft->entries.len = (size_t)(count + to_insert);

    free(tmp);
    return to_insert;
}

/**
 * @brief Carga un directorio raíz en el árbol y lo deja con su primer nivel
 *        expandido y el panel abierto.
 *
 * Vacía el vector, crea la entrada raíz (profundidad 0, expandida) cuyo nombre
 * es el último componente de @p dirpath, y escanea ese directorio para insertar
 * el primer nivel de hijos justo detrás.
 *
 * @param ft      Explorador a (re)cargar.
 * @param dirpath Ruta absoluta del directorio raíz.
 */
void ftree_load(FileTree *ft, const char *dirpath) {
    ft->scroll = 0;
    ft->hovered = -1;
    strncpy(ft->root_path, dirpath, sizeof(ft->root_path) - 1);

    if (!vec_reserve(&ft->entries, 1)) return; /* asegurar sitio para la raíz */
    ft->entries.len = 0;                       /* vaciar el árbol anterior */

    /* entrada raíz (su nombre es el último componente de la ruta) */
    FEntry *root = FT(ft);
    memset(root, 0, sizeof(*root));
    strncpy(root->path, dirpath, sizeof(root->path) - 1);
    /* retroceder desde el final hasta el último separador para aislar el nombre
     */
    const char *sep = dirpath + strlen(dirpath);
    while (sep > dirpath && *(sep - 1) != '/' && *(sep - 1) != '\\')
        sep--;
    /* si quedó algo tras el separador úsalo; si no, la ruta entera */
    strncpy(root->name, *sep ? sep : dirpath, sizeof(root->name) - 1);
    root->depth = 0;
    root->type = FTYPE_DIR;
    root->expanded = 1;
    root->visible = 1;
    ft->entries.len = 1;

    scan_dir(ft, dirpath, 1, 1); /* primer nivel */
    ft->open = 1;
}

/**
 * @brief Expande o colapsa la entrada de índice @p index (si es un directorio).
 *
 * Alterna el estado de la carpeta:
 *   - Colapsar: elimina físicamente del vector todos sus descendientes, que son
 *     las entradas contiguas con @c depth mayor que la suya, con un @c memmove
 * que cierra el hueco.
 *   - Expandir: escanea el directorio e inserta sus hijos justo después. Antes
 * de llamar a @c scan_dir se copian @c path y @c depth a variables locales,
 *     porque @c scan_dir puede hacer crecer el vector (realloc) y desplazar las
 *     entradas (memmove), dejando obsoleto el puntero @c en.
 * No hace nada si el índice es inválido o la entrada no es una carpeta.
 *
 * @param ft    Explorador.
 * @param index Índice de la entrada a alternar.
 */
void ftree_toggle(FileTree *ft, int index) {
    if (index < 0 || index >= FTN(ft)) return;
    FEntry *en = &FT(ft)[index];
    if (en->type != FTYPE_DIR)
        return; /* solo las carpetas se expanden/colapsan */

    if (en->expanded) {
        /* colapsar: eliminar todos los descendientes (depth mayor) */
        en->expanded = 0;
        int depth = en->depth;
        int j = index + 1;
        /* avanzar j mientras las entradas sean descendientes (más profundas) */
        while (j < FTN(ft) && FT(ft)[j].depth > depth)
            j++;
        int remove = j - index - 1; /* nº de descendientes a eliminar */
        if (remove > 0) {
            /* cerrar el hueco trayendo hacia atrás lo que había tras los hijos
             */
            memmove(&FT(ft)[index + 1], &FT(ft)[j],
                    (size_t)(FTN(ft) - j) * sizeof(FEntry));
            ft->entries.len -= (size_t)remove;
        }
    } else {
        /* expandir: copiar path/depth ANTES de scan_dir, cuyo realloc/memmove
           puede desplazar la entrada y dejar 'en' obsoleto. */
        char expand_path[512];
        int expand_depth = en->depth;
        strncpy(expand_path, en->path, sizeof(expand_path) - 1);
        expand_path[sizeof(expand_path) - 1] = '\0';
        FT(ft)[index].expanded = 1; /* re-indexar: 'en' podría ya no valer */
        scan_dir(ft, expand_path, expand_depth + 1, index + 1);
    }

    ftree_refresh_visibility(ft);
}

/**
 * @brief Recalcula la visibilidad de todas las entradas.
 *
 * Como los hijos de las carpetas colapsadas se eliminan FÍSICAMENTE del vector
 * al colapsar (no se ocultan), todo lo que permanece en el vector es visible.
 * Por eso basta con marcar @c visible = 1 en cada entrada.
 *
 * @param ft Explorador.
 */
void ftree_refresh_visibility(FileTree *ft) {
    /* los hijos colapsados ya se eliminaron físicamente: todo lo que queda es
     * visible */
    int n = FTN(ft);
    for (int i = 0; i < n; i++)
        FT(ft)[i].visible = 1;
}

/**
 * @brief Cuenta cuántas entradas están marcadas como visibles.
 * @param ft Explorador.
 * @return Número de entradas con @c visible distinto de 0.
 */
int ftree_visible_count(const FileTree *ft) {
    int n = 0;
    int total = FTN(ft);
    for (int i = 0; i < total; i++)
        if (FT(ft)[i].visible) n++;
    return n;
}

/**
 * @brief Devuelve el índice (en el vector) de la entrada visible número @p n.
 *
 * Recorre las entradas contando solo las visibles hasta llegar a la n-ésima.
 * Sirve para traducir una fila pintada en pantalla a su entrada real en el
 * vector.
 *
 * @param ft Explorador.
 * @param n  Posición entre las visibles (0-based).
 * @return Índice en el vector, o -1 si no hay tantas entradas visibles.
 */
int ftree_nth_visible(const FileTree *ft, int n) {
    int cur = 0;
    int total = FTN(ft);
    for (int i = 0; i < total; i++) {
        if (FT(ft)[i].visible) {
            if (cur == n) return i;
            cur++;
        }
    }
    return -1;
}
