#include "store.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void store_dir(const char *sub, char *buf, size_t sz)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0]) { snprintf(buf, sz, "%s/vtt/%s", xdg, sub); return; }
    const char *home = getenv("HOME");
    snprintf(buf, sz, "%s/.local/share/vtt/%s", home && home[0] ? home : ".", sub);
}

int store_name_ok(const char *name)
{
    if (!name[0] || strlen(name) >= MAP_NAME_MAX) return 0;
    for (const char *p = name; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '-' || *p == '_'))
            return 0;
    return 1;
}

int store_path(const char *sub, const char *name, const char *ext, char *buf, size_t sz)
{
    char dir[MAP_PATH_MAX];
    store_dir(sub, dir, sizeof dir);
    int n = snprintf(buf, sz, "%s/%s%s", dir, name, ext);
    return n > 0 && (size_t)n < sz;
}

static int name_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

int store_list(const char *dir, const char *ext, char (*names)[MAP_NAME_MAX], int max)
{
    int n = 0;
    char (*all)[MAP_NAME_MAX] = store_list_all(dir, ext, &n);
    for (int i = 0; i < n && i < max; i++) str_lcpy(names[i], all[i], MAP_NAME_MAX);
    free(all);
    return n;
}

char (*store_list_all(const char *dir, const char *ext, int *n))[MAP_NAME_MAX]
{
    *n = 0;
    size_t el = strlen(ext);
    DIR *d = opendir(dir);
    if (!d) return NULL;
    /* All of them, sorted: a listing cut short is cut at the end of the
     * alphabet, not wherever the directory happened to. */
    char (*all)[MAP_NAME_MAX] = NULL;
    int cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t len = strlen(e->d_name);
        if (len <= el || strcmp(e->d_name + len - el, ext) != 0 || len - el >= MAP_NAME_MAX) continue;
        char name[MAP_NAME_MAX];
        memcpy(name, e->d_name, len - el);
        name[len - el] = '\0';
        if (!store_name_ok(name)) continue;
        if (*n == cap) {
            cap = cap ? cap * 2 : 32;
            all = xrealloc(all, (size_t)cap * MAP_NAME_MAX);
        }
        str_lcpy(all[(*n)++], name, MAP_NAME_MAX);
    }
    closedir(d);
    if (*n > 1) qsort(all, (size_t)*n, MAP_NAME_MAX, name_cmp);
    return all;
}
