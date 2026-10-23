/* The docs that describe the code as it is (CLAUDE.md, *A feature is not
 * done*; docs/README.md, *Kept current*) held to it, so drift fails the
 * build instead of waiting for a reader to trip on it:
 *   - every source file has its row in docs/CODEMAP.md;
 *   - every path they name (src/, tests/, tools/, docs/, web/, globs too)
 *     exists;
 *   - every identifier written alone in backticks with an underscore
 *     (`app_frame`, `world_event()`) is a word somewhere in the code, the
 *     tests, the tools or the Makefile.
 * What is checked is what can be checked without false alarms: a renamed
 * function, a moved file, a file with no row. That a sentence is still true
 * is the review's (docs/HEALTH.md, *The questions*). */
#include "harness.h"

#include <ctype.h>
#include <dirent.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The docs whose identifiers name the code as it is today. */
static const char *const ID_DOCS[] = {
    "CLAUDE.md", "tests/CLAUDE.md", "docs/CODEMAP.md", "docs/ARCHITECTURE.md",
    "tools/review-brief.md",
};
/* Those and the user-facing kept-current docs, whose paths must exist. */
static const char *const PATH_DOCS[] = {
    "CLAUDE.md", "tests/CLAUDE.md", "docs/CODEMAP.md", "docs/ARCHITECTURE.md",
    "tools/review-brief.md", "README.md", "docs/AGENTS.md", "docs/README.md",
};

typedef struct { char *p; size_t n, cap; } Text;

static void text_add(Text *t, const char *s, size_t n)
{
    if (t->n + n + 2 > t->cap) {
        t->cap = (t->n + n + 2) * 2;
        t->p = realloc(t->p, t->cap);
    }
    memcpy(t->p + t->n, s, n);
    t->n += n;
    t->p[t->n++] = '\n';
    t->p[t->n] = '\0';
}

static int is_word(int c) { return isalnum(c) || c == '_'; }

/* The whole file (harness.c's slurp stops at 64 KB; maptools.c and the
 * README are larger), or NULL. */
static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 16, n = 0, got;
    char *buf = malloc(cap);
    while (buf && (got = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += got;
        if (n + 1 == cap) buf = realloc(buf, cap *= 2);
    }
    fclose(f);
    if (buf) buf[n] = '\0';
    return buf;
}

/* Every regular file under DIR, recursively, appended to T. */
static void add_tree(Text *t, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (strcmp(path, "tests/oracle") && strcmp(path, "tests/fuzz-found")) add_tree(t, path);
        } else if (S_ISREG(st.st_mode) && st.st_size < (4 << 20)) {
            char *s = read_all(path);
            if (s) { text_add(t, s, strlen(s)); free(s); }
        }
    }
    closedir(d);
}

/* WORD occurs in T as a whole word. */
static int has_word(const Text *t, const char *w, size_t n)
{
    for (const char *p = t->p; (p = strstr(p, w)); p++) {
        if ((p == t->p || !is_word((unsigned char)p[-1])) && !is_word((unsigned char)p[n])) return 1;
    }
    return 0;
}

/* `name` or `name()`: lowercase, an underscore inside, nothing else. */
static int identifier_span(const char *s, size_t n, size_t *len)
{
    if (n > 2 && s[n - 2] == '(' && s[n - 1] == ')') n -= 2;
    if (n < 3 || !islower((unsigned char)s[0])) return 0;
    int under = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '_') under = 1;
        else if (!islower((unsigned char)s[i]) && !isdigit((unsigned char)s[i])) return 0;
    }
    if (!under || s[n - 1] == '_') return 0;
    *len = n;
    return 1;
}

static void check_ids(const char *doc, const char *text, const Text *code, int *bad)
{
    int line = 1;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') { line++; continue; }
        if (*p != '`') continue;
        const char *q = p + 1;
        while (*q && *q != '`' && *q != '\n') q++;
        if (*q != '`') { p = q - 1; continue; }
        size_t len;
        if (identifier_span(p + 1, (size_t)(q - p - 1), &len)) {
            char w[128];
            if (len < sizeof w) {
                memcpy(w, p + 1, len);
                w[len] = '\0';
                if (!has_word(code, w, len)) {
                    printf("  %s:%d: `%s` is in no source, test or tool\n", doc, line, w);
                    (*bad)++;
                }
            }
        }
        p = q;
    }
}

static void check_paths(const char *doc, const char *text, int *bad)
{
    static const char *const roots[] = { "src/", "tests/", "tools/", "docs/", "web/" };
    int line = 1;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') { line++; continue; }
        if (p > text && (is_word((unsigned char)p[-1]) || p[-1] == '/' || p[-1] == '.' || p[-1] == '$')) continue;
        size_t r = 0;
        for (; r < sizeof roots / sizeof roots[0]; r++)
            if (!strncmp(p, roots[r], strlen(roots[r]))) break;
        if (r == sizeof roots / sizeof roots[0]) continue;
        const char *q = p;
        while (is_word((unsigned char)*q) || *q == '/' || *q == '.' || *q == '-' || *q == '*') q++;
        while (q > p && (q[-1] == '.' || q[-1] == '-')) q--;     /* a sentence's full stop */
        char path[512];
        size_t n = (size_t)(q - p);
        if (n <= strlen(roots[r]) || n >= sizeof path) { p = q - 1; continue; }
        memcpy(path, p, n);
        path[n] = '\0';
        glob_t g;
        int found = glob(path, 0, NULL, &g) == 0 && g.gl_pathc > 0;
        globfree(&g);
        if (!found) {
            printf("  %s:%d: %s does not exist\n", doc, line, path);
            (*bad)++;
        }
        p = q - 1;
    }
}

void test_docs(void)
{
    Text code = { 0 };
    add_tree(&code, "src");
    add_tree(&code, "tests");
    add_tree(&code, "tools");
    char *mk = read_all("Makefile");
    if (mk) { text_add(&code, mk, strlen(mk)); free(mk); }

    CASE("every source file has its row in docs/CODEMAP.md");
    {
        char *cm = read_all("docs/CODEMAP.md");
        CHECK(cm != NULL);
        DIR *d = opendir("src");
        CHECK(d != NULL);
        int bad = 0;
        struct dirent *e;
        while (cm && d && (e = readdir(d))) {
            size_t n = strlen(e->d_name);
            if (n < 3 || e->d_name[n - 2] != '.' || (e->d_name[n - 1] != 'c' && e->d_name[n - 1] != 'h')) continue;
            /* A header beside its .c is named by the .c's row (`net.c/h`). */
            char stem[256];
            snprintf(stem, sizeof stem, "%.*s", (int)(n - 2), e->d_name);
            char c_name[300];
            snprintf(c_name, sizeof c_name, "src/%s.c", stem);
            struct stat st;
            const char *want = e->d_name;
            if (e->d_name[n - 1] == 'h' && stat(c_name, &st) == 0) continue;
            /* The name itself, whole: `map.c` is not met by `xmap.c`. */
            if (!has_word(&(Text){ cm, strlen(cm), 0 }, want, n)) {
                printf("  src/%s has no row in docs/CODEMAP.md\n", want);
                bad++;
            }
        }
        if (d) closedir(d);
        CHECK_EQ(bad, 0);
        free(cm);
    }

    CASE("every identifier the code-describing docs name in backticks is in the code");
    {
        int bad = 0;
        for (size_t i = 0; i < sizeof ID_DOCS / sizeof ID_DOCS[0]; i++) {
            char *t = read_all(ID_DOCS[i]);
            if (!t) continue;              /* a doc not written yet is no drift */
            check_ids(ID_DOCS[i], t, &code, &bad);
            free(t);
        }
        CHECK_EQ(bad, 0);
    }

    CASE("every path the kept-current docs name exists");
    {
        int bad = 0;
        for (size_t i = 0; i < sizeof PATH_DOCS / sizeof PATH_DOCS[0]; i++) {
            char *t = read_all(PATH_DOCS[i]);
            if (!t) continue;
            check_paths(PATH_DOCS[i], t, &bad);
            free(t);
        }
        CHECK_EQ(bad, 0);
    }
    free(code.p);
}
