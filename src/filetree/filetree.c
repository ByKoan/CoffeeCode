#include "filetree/filetree.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <dirent.h>
  #include <sys/stat.h>
#endif

/* -- ftree_init ----------------------------------------------------------- */
void ftree_init(FileTree *ft) {
    memset(ft, 0, sizeof(*ft));
    ft->open    = 0;
    ft->width   = FTREE_WIDTH_DEFAULT;
    ft->hovered = -1;
    ft->entries = (FEntry *)calloc(FTREE_MAX_ENTRIES, sizeof(FEntry));
}

/* -- ftree_free ----------------------------------------------------------- */
void ftree_free(FileTree *ft) {
    free(ft->entries);
    ft->entries = NULL;
    ft->count   = 0;
}

/* -- Comparador para ordenar: carpetas primero, luego alfabético ----------- */
static int entry_cmp(const void *a, const void *b) {
    const FEntry *ea = (const FEntry *)a;
    const FEntry *eb = (const FEntry *)b;
    if (ea->type != eb->type)
        return (ea->type == FTYPE_DIR) ? -1 : 1;
    return SDL_strcasecmp(ea->name, eb->name);
}

/* -- Insertar entradas de un directorio a partir de 'insert_at' ----------- */
/* Devuelve el número de entradas insertadas.                                 */
static int scan_dir(FileTree *ft, const char *dirpath,
                    int depth, int insert_at)
{
    if (ft->count >= FTREE_MAX_ENTRIES) return 0;

    /* Recogemos las entradas primero en un buffer temporal */
    FEntry tmp[1024];
    int    tmp_count = 0;

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
        en->depth    = depth;
        en->type     = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                       ? FTYPE_DIR : FTYPE_FILE;
        en->expanded = 0;
        en->visible  = 1;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dir = opendir(dirpath);
    if (!dir) return 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;  /* ocultar archivos ocultos */
        if (tmp_count >= 1024) break;
        FEntry *en = &tmp[tmp_count++];
        memset(en, 0, sizeof(*en));
        snprintf(en->path, sizeof(en->path), "%s/%s", dirpath, de->d_name);
        strncpy(en->name, de->d_name, sizeof(en->name) - 1);
        en->depth    = depth;
        en->expanded = 0;
        en->visible  = 1;

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

    /* Insertar en ft->entries en la posición insert_at */
    int to_insert = tmp_count;
    /* Asegurar que insert_at + to_insert + existing_after <= FTREE_MAX_ENTRIES */
    int existing_after = ft->count - insert_at;
    if (existing_after < 0) existing_after = 0;
    int available = FTREE_MAX_ENTRIES - ft->count;
    if (to_insert > available) to_insert = available;
    if (to_insert <= 0) return 0;

    /* Desplazar entradas existentes hacia adelante */
    if (existing_after > 0) {
        memmove(&ft->entries[insert_at + to_insert],
                &ft->entries[insert_at],
                (size_t)existing_after * sizeof(FEntry));
    }
    memcpy(&ft->entries[insert_at], tmp, (size_t)to_insert * sizeof(FEntry));
    ft->count += to_insert;

    return to_insert;
}

/* -- ftree_load ----------------------------------------------------------- */
void ftree_load(FileTree *ft, const char *dirpath) {
    ft->count  = 0;
    ft->scroll = 0;
    ft->hovered = -1;
    strncpy(ft->root_path, dirpath, sizeof(ft->root_path) - 1);

    /* Entrada raíz */
    FEntry *root = &ft->entries[0];
    memset(root, 0, sizeof(*root));
    strncpy(root->path, dirpath, sizeof(root->path) - 1);

    /* Usar solo el último componente del path como nombre */
    const char *sep = dirpath + strlen(dirpath);
    while (sep > dirpath && *(sep-1) != '/' && *(sep-1) != '\\') sep--;
    strncpy(root->name, *sep ? sep : dirpath, sizeof(root->name) - 1);

    root->depth    = 0;
    root->type     = FTYPE_DIR;
    root->expanded = 1;
    root->visible  = 1;
    ft->count = 1;

    /* Escanear nivel 1 */
    scan_dir(ft, dirpath, 1, 1);
    ft->open = 1;
}

/* -- ftree_toggle --------------------------------------------------------- */
void ftree_toggle(FileTree *ft, int index) {
    if (index < 0 || index >= ft->count) return;
    FEntry *en = &ft->entries[index];
    if (en->type != FTYPE_DIR) return;

    if (en->expanded) {
        /* Colapsar: eliminar todos los descendientes directos e indirectos */
        en->expanded = 0;
        int depth = en->depth;
        int j = index + 1;
        while (j < ft->count && ft->entries[j].depth > depth) j++;
        int remove = j - index - 1;
        if (remove > 0) {
            memmove(&ft->entries[index + 1],
                    &ft->entries[j],
                    (size_t)(ft->count - j) * sizeof(FEntry));
            ft->count -= remove;
        }
    } else {
        /* Expandir: escanear y añadir hijos justo después.
         * IMPORTANTE: copiar path y depth antes de llamar a scan_dir porque
         * el memmove interno puede desplazar la entrada y dejar 'en' obsoleto. */
        char expand_path[512];
        int  expand_depth = en->depth;
        strncpy(expand_path, en->path, sizeof(expand_path) - 1);
        expand_path[sizeof(expand_path) - 1] = '\0';
        ft->entries[index].expanded = 1;
        scan_dir(ft, expand_path, expand_depth + 1, index + 1);
    }

    ftree_refresh_visibility(ft);
}

/* -- ftree_refresh_visibility --------------------------------------------- */
void ftree_refresh_visibility(FileTree *ft) {
    /* Recalcula visible[] usando el estado expanded[] de los padres.
       Como ya eliminamos físicamente los hijos al colapsar, todos los
       que quedan en el array son visibles. */
    for (int i = 0; i < ft->count; i++)
        ft->entries[i].visible = 1;
}

/* -- ftree_visible_count -------------------------------------------------- */
int ftree_visible_count(const FileTree *ft) {
    int n = 0;
    for (int i = 0; i < ft->count; i++)
        if (ft->entries[i].visible) n++;
    return n;
}

/* -- ftree_nth_visible ---------------------------------------------------- */
int ftree_nth_visible(const FileTree *ft, int n) {
    int cur = 0;
    for (int i = 0; i < ft->count; i++) {
        if (ft->entries[i].visible) {
            if (cur == n) return i;
            cur++;
        }
    }
    return -1;
}
