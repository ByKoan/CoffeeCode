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

/* -- ftree_init ----------------------------------------------------------- */
void ftree_init(FileTree *ft) {
    memset(ft, 0, sizeof(*ft));
    ft->open = 0;
    ft->width = FTREE_WIDTH_DEFAULT;
    ft->hovered = -1;
    vec_init(&ft->entries, sizeof(FEntry));
    vec_reserve(&ft->entries, FTREE_MAX_ENTRIES); /* reserva inicial (ya no es límite) */
}

/* -- ftree_free ----------------------------------------------------------- */
void ftree_free(FileTree *ft) {
    vec_free(&ft->entries);
}

/* -- Comparador para ordenar: carpetas primero, luego alfabético ----------- */
static int entry_cmp(const void *a, const void *b) {
    const FEntry *ea = (const FEntry *)a;
    const FEntry *eb = (const FEntry *)b;
    if (ea->type != eb->type) return (ea->type == FTYPE_DIR) ? -1 : 1;
    return SDL_strcasecmp(ea->name, eb->name);
}

/* -- Insertar entradas de un directorio a partir de 'insert_at' ----------- */
/* Devuelve el número de entradas insertadas.                                 */
static int scan_dir(FileTree *ft, const char *dirpath, int depth, int insert_at) {
    /* Recogemos las entradas primero en un buffer temporal */
    FEntry tmp[1024];
    int tmp_count = 0;

#ifdef _WIN32
    char pattern[516];
    snprintf(pattern, sizeof(pattern), "%s\\*", dirpath);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (tmp_count >= 1024) break;
        FEntry *en = &tmp[tmp_count++];
        memset(en, 0, sizeof(*en));
        snprintf(en->path, sizeof(en->path), "%s\\%s", dirpath, fd.cFileName);
        strncpy(en->name, fd.cFileName, sizeof(en->name) - 1);
        en->depth = depth;
        en->type = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FTYPE_DIR : FTYPE_FILE;
        en->expanded = 0;
        en->visible = 1;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dir = opendir(dirpath);
    if (!dir) return 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue; /* ocultar archivos ocultos */
        if (tmp_count >= 1024) break;
        FEntry *en = &tmp[tmp_count++];
        memset(en, 0, sizeof(*en));
        snprintf(en->path, sizeof(en->path), "%s/%s", dirpath, de->d_name);
        strncpy(en->name, de->d_name, sizeof(en->name) - 1);
        en->depth = depth;
        en->expanded = 0;
        en->visible = 1;

        /* Determinar tipo */
        if (de->d_type == DT_DIR) {
            en->type = FTYPE_DIR;
        } else if (de->d_type == DT_UNKNOWN || de->d_type == DT_LNK) {
            struct stat st;
            if (stat(en->path, &st) == 0 && S_ISDIR(st.st_mode))
                en->type = FTYPE_DIR;
            else
                en->type = FTYPE_FILE;
        } else {
            en->type = FTYPE_FILE;
        }
    }
    closedir(dir);
#endif

    /* Ordenar: carpetas primero, luego alfabético */
    qsort(tmp, (size_t)tmp_count, sizeof(FEntry), entry_cmp);

    int to_insert = tmp_count;
    if (to_insert <= 0) return 0;

    int count = FTN(ft);
    int existing_after = count - insert_at;
    if (existing_after < 0) existing_after = 0;

    /* Crecer (sin límite fijo) y abrir hueco en insert_at */
    if (!vec_reserve(&ft->entries, (size_t)(count + to_insert))) return 0;
    if (existing_after > 0) {
        memmove(&FT(ft)[insert_at + to_insert], &FT(ft)[insert_at],
                (size_t)existing_after * sizeof(FEntry));
    }
    memcpy(&FT(ft)[insert_at], tmp, (size_t)to_insert * sizeof(FEntry));
    ft->entries.len = (size_t)(count + to_insert);

    return to_insert;
}

/* -- ftree_load ----------------------------------------------------------- */
void ftree_load(FileTree *ft, const char *dirpath) {
    ft->scroll = 0;
    ft->hovered = -1;
    strncpy(ft->root_path, dirpath, sizeof(ft->root_path) - 1);

    if (!vec_reserve(&ft->entries, 1)) return;
    ft->entries.len = 0;

    /* Entrada raíz */
    FEntry *root = FT(ft);
    memset(root, 0, sizeof(*root));
    strncpy(root->path, dirpath, sizeof(root->path) - 1);

    /* Usar solo el último componente del path como nombre */
    const char *sep = dirpath + strlen(dirpath);
    while (sep > dirpath && *(sep - 1) != '/' && *(sep - 1) != '\\')
        sep--;
    strncpy(root->name, *sep ? sep : dirpath, sizeof(root->name) - 1);

    root->depth = 0;
    root->type = FTYPE_DIR;
    root->expanded = 1;
    root->visible = 1;
    ft->entries.len = 1;

    /* Escanear nivel 1 */
    scan_dir(ft, dirpath, 1, 1);
    ft->open = 1;
}

/* -- ftree_toggle --------------------------------------------------------- */
void ftree_toggle(FileTree *ft, int index) {
    if (index < 0 || index >= FTN(ft)) return;
    FEntry *en = &FT(ft)[index];
    if (en->type != FTYPE_DIR) return;

    if (en->expanded) {
        /* Colapsar: eliminar todos los descendientes directos e indirectos */
        en->expanded = 0;
        int depth = en->depth;
        int j = index + 1;
        while (j < FTN(ft) && FT(ft)[j].depth > depth)
            j++;
        int remove = j - index - 1;
        if (remove > 0) {
            memmove(&FT(ft)[index + 1], &FT(ft)[j], (size_t)(FTN(ft) - j) * sizeof(FEntry));
            ft->entries.len -= (size_t)remove;
        }
    } else {
        /* Expandir: escanear y añadir hijos justo después.
         * IMPORTANTE: copiar path y depth antes de llamar a scan_dir porque
         * el memmove/realloc interno puede desplazar la entrada y dejar 'en' obsoleto. */
        char expand_path[512];
        int expand_depth = en->depth;
        strncpy(expand_path, en->path, sizeof(expand_path) - 1);
        expand_path[sizeof(expand_path) - 1] = '\0';
        FT(ft)[index].expanded = 1;
        scan_dir(ft, expand_path, expand_depth + 1, index + 1);
    }

    ftree_refresh_visibility(ft);
}

/* -- ftree_refresh_visibility --------------------------------------------- */
void ftree_refresh_visibility(FileTree *ft) {
    /* Como ya eliminamos físicamente los hijos al colapsar, todos los
       que quedan en el array son visibles. */
    int n = FTN(ft);
    for (int i = 0; i < n; i++)
        FT(ft)[i].visible = 1;
}

/* -- ftree_visible_count -------------------------------------------------- */
int ftree_visible_count(const FileTree *ft) {
    int n = 0;
    int total = FTN(ft);
    for (int i = 0; i < total; i++)
        if (FT(ft)[i].visible) n++;
    return n;
}

/* -- ftree_nth_visible ---------------------------------------------------- */
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
