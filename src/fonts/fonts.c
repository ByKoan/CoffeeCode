/**
 * @file fonts.c
 * @brief Escaneo de las fuentes instaladas en el sistema (ver fonts.h).
 */
#include "fonts/fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- Utilidades ------------------------------------------------------------ */

/** ¿@p s termina en @p suf, ignorando mayúsculas/minúsculas? */
static int ends_with_ci(const char *s, const char *suf) {
    size_t ls = strlen(s), lf = strlen(suf);
    if (ls < lf) return 0;
    s += ls - lf;
    for (size_t i = 0; i < lf; i++) {
        char a = s[i], b = suf[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

/** ¿@p name tiene extensión de fuente soportada (.ttf/.ttc/.otf)? */
static int has_font_ext(const char *name) {
    return ends_with_ci(name, ".ttf") || ends_with_ci(name, ".ttc") ||
           ends_with_ci(name, ".otf");
}

/** Compara dos nombres ignorando mayúsculas (para ordenar/deduplicar). */
static int name_cmp(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (ca < cb) ? -1 : 1;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

/** Añade una fuente a la lista (deduplicando por nombre; crece el array). */
static void fonts_add(FontList *l, const char *filename, const char *fullpath) {
    /* nombre = archivo sin extensión */
    char name[96];
    snprintf(name, sizeof name, "%s", filename);
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
    if (!name[0]) return;

    /* dedup por nombre (varias carpetas pueden tener la misma fuente) */
    for (int i = 0; i < l->count; i++)
        if (name_cmp(l->items[i].name, name) == 0) return;

    if (l->count == l->cap) {
        int ncap = l->cap ? l->cap * 2 : 64;
        FontEntry *ni = realloc(l->items, (size_t)ncap * sizeof *ni);
        if (!ni) return; /* sin memoria: ignorar esta fuente */
        l->items = ni;
        l->cap = ncap;
    }
    FontEntry *e = &l->items[l->count++];
    snprintf(e->name, sizeof e->name, "%s", name);
    snprintf(e->path, sizeof e->path, "%s", fullpath);
}

/* -- Escaneo recursivo por plataforma -------------------------------------- */
#ifdef _WIN32
#include <windows.h>

static void scan_dir(FontList *l, const char *dir, int depth) {
    if (depth > 6) return;
    char pattern[1024];
    snprintf(pattern, sizeof pattern, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char *name = fd.cFileName;
        if (name[0] == '.') continue; /* ".", ".." y ocultos */
        char full[1024];
        snprintf(full, sizeof full, "%s\\%s", dir, name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            scan_dir(l, full, depth + 1);
        else if (has_font_ext(name))
            fonts_add(l, name, full);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/** Añade a @p dirs las carpetas de fuentes de Windows. */
static int sys_font_dirs(char dirs[][512], int max) {
    int n = 0;
    const char *windir = getenv("WINDIR");
    if (windir && n < max) snprintf(dirs[n++], 512, "%s\\Fonts", windir);
    const char *local = getenv("LOCALAPPDATA");
    if (local && n < max)
        snprintf(dirs[n++], 512, "%s\\Microsoft\\Windows\\Fonts", local);
    return n;
}

#else
#include <dirent.h>
#include <sys/stat.h>

static void scan_dir(FontList *l, const char *dir, int depth) {
    if (depth > 6) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char full[1024];
        snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode))
            scan_dir(l, full, depth + 1);
        else if (S_ISREG(st.st_mode) && has_font_ext(de->d_name))
            fonts_add(l, de->d_name, full);
    }
    closedir(d);
}

/** Añade a @p dirs las carpetas de fuentes de Linux (sistema + usuario). */
static int sys_font_dirs(char dirs[][512], int max) {
    int n = 0;
    static const char *fixed[] = {"/usr/share/fonts", "/usr/local/share/fonts"};
    for (size_t i = 0; i < sizeof fixed / sizeof *fixed && n < max; i++)
        snprintf(dirs[n++], 512, "%s", fixed[i]);
    const char *home = getenv("HOME");
    if (home && n < max) snprintf(dirs[n++], 512, "%s/.fonts", home);
    if (home && n < max)
        snprintf(dirs[n++], 512, "%s/.local/share/fonts", home);
    return n;
}
#endif

/* -- API ------------------------------------------------------------------- */

/** Comparador de qsort: ordena fuentes por nombre. */
static int qsort_by_name(const void *a, const void *b) {
    return name_cmp(((const FontEntry *)a)->name, ((const FontEntry *)b)->name);
}

void fonts_scan(FontList *list) {
    list->items = NULL;
    list->count = 0;
    list->cap = 0;

    char dirs[8][512];
    int n = sys_font_dirs(dirs, 8);
    for (int i = 0; i < n; i++)
        scan_dir(list, dirs[i], 0);

    if (list->count > 1)
        qsort(list->items, (size_t)list->count, sizeof *list->items,
              qsort_by_name);
}

void fonts_free(FontList *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

int fonts_index_of(const FontList *list, const char *path) {
    if (!path || !path[0]) return -1;
    for (int i = 0; i < list->count; i++)
        if (strcmp(list->items[i].path, path) == 0) return i;
    return -1;
}

const char *fonts_name_for(const FontList *list, const char *path) {
    int i = fonts_index_of(list, path);
    return (i >= 0) ? list->items[i].name : "Predeterminada";
}
