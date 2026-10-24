/* The docs that describe the code as it is (CLAUDE.md, *A change is not
 * done*; docs/README.md, *Kept current*) held to it, so drift fails the
 * build instead of waiting for a reader to trip on it:
 *   - every source file is named in the first cell of a row of
 *     docs/CODEMAP.md's tables (a mention elsewhere in a row is not a row);
 *   - every path they name under src/, tests/, tools/, docs/ or web/ (globs
 *     too) exists;
 *   - every identifier written alone in backticks with an underscore --
 *     lowercase (`app_frame`, `world_event()`) or a macro (`FORMAT_VERSION`)
 *     -- is a word somewhere in the code, the tests, the tools or the
 *     Makefile, and every file named alone in backticks (`net.c`, `embed.sh`)
 *     is a file of the repo's;
 *   - a flag README.md's options block names takes the argument usage() gives
 *     it, and the docs that state the default port state NET_DEFAULT_PORT
 *     (docs/CODEMAP.md, *Facts kept in more than one place*).
 * What is checked is what can be checked without false alarms: a renamed
 * function or macro, a moved file, a file with no row. Not checked: a name
 * with no underscore (`World`, `Cell`), and whether a sentence is still
 * true -- that is every review's *Docs drift* (tools/review-brief.md). */
#include "harness.h"

#include <ctype.h>
#include <dirent.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Which docs are kept current is docs/README.md's to say (*Kept current*:
 * its table, and the files outside docs/ under it); the list is read from
 * there, never kept here as well (CLAUDE.md, *Nothing built to drift*). What
 * this file names are the exceptions, each of which must be in that list:
 * the docs whose identifiers name the code as it is today (others quote libc
 * or plans), and the two whose paths may be of things not built yet. */
static const char *const ID_DOCS[] = {
    "CLAUDE.md", "tests/CLAUDE.md", "docs/CODEMAP.md", "docs/ARCHITECTURE.md",
    "tools/review-brief.md",
};
static const char *const PLAN_DOCS[] = { "docs/ROADMAP.md", "docs/IDEAS.md" };

#define MAX_DOCS 64
/* docs/README.md's *Kept current*, as paths from the top (a .md the section
 * mentions in passing joins the list too: the section is the owner, so a
 * reworded sentence can add a page, never drop one silently): `](X.md)` is
 * docs/X.md (`](../X.md)` is X.md), a backticked `.md` or a plain one with a
 * slash is as written; docs/README.md itself too. */
static int kept_current(char docs[][128], int max)
{
    FILE *f = fopen("docs/README.md", "rb");
    if (!f) return 0;
    char line[4096];
    int in = 0, n = 0;
    snprintf(docs[n++], 128, "docs/README.md");
    while (fgets(line, sizeof line, f) && n < max) {
        if (!strncmp(line, "## ", 3)) { in = !strncmp(line, "## Kept current", 15); continue; }
        if (!in) continue;
        for (char *p = line; *p && n < max; p++) {
            char path[128] = "";
            if (p[0] == ']' && p[1] == '(') {
                char *e = strchr(p + 2, ')');
                if (!e || e - p - 2 >= 100 || strncmp(e - 3, ".md", 3)) continue;
                if (!strncmp(p + 2, "../", 3)) snprintf(path, sizeof path, "%.*s", (int)(e - p - 5), p + 5);
                else snprintf(path, sizeof path, "docs/%.*s", (int)(e - p - 2), p + 2);
            } else if (p[0] == '`' || ((p == line || p[-1] == ' ') && strchr("tsd", p[0]) && strchr(p, '/'))) {
                char *b = p + (p[0] == '`'), *e = b;
                while (*e && (isalnum((unsigned char)*e) || strchr("/._-", *e))) e++;
                while (e > b && e[-1] == '.') e--;            /* a sentence's full stop */
                if (e - b < 4 || e - b >= 100 || strncmp(e - 3, ".md", 3)) continue;
                if (p[0] != '`' && !memchr(b, '/', (size_t)(e - b))) continue;
                snprintf(path, sizeof path, "%.*s", (int)(e - b), b);
            }
            if (!path[0]) continue;
            int dup = 0;
            for (int i = 0; i < n && !dup; i++) dup = !strcmp(docs[i], path);
            if (!dup) snprintf(docs[n++], 128, "%s", path);
        }
    }
    fclose(f);
    return n;
}

static int in_list(char docs[][128], int n, const char *path)
{
    for (int i = 0; i < n; i++) if (!strcmp(docs[i], path)) return 1;
    return 0;
}

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
        if (n + 1 == cap) {
            char *more = realloc(buf, cap *= 2);
            if (!more) { free(buf); buf = NULL; }
            buf = more;
        }
    }
    fclose(f);
    if (buf) buf[n] = '\0';
    return buf;
}

/* Every regular file under DIR, recursively: its text appended to T (NULL:
 * not wanted), its name to NAMES, a line each. */
static void add_tree(Text *t, Text *names, const char *dir)
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
            /* Not the oracle's records or the fuzzer's finds, and not the
             * reviewers' worktrees: whole copies of the repo. */
            if (strcmp(path, "tests/oracle") && strcmp(path, "tests/fuzz-found") && strcmp(path, ".claude/worktrees"))
                add_tree(t, names, path);
        } else if (S_ISREG(st.st_mode)) {
            text_add(names, e->d_name, strlen(e->d_name));
            char *s = t && st.st_size < (4 << 20) ? read_all(path) : NULL;
            if (s) { text_add(t, s, strlen(s)); free(s); }
        }
    }
    closedir(d);
}

/* A doc's text; NULL when it is not written yet (no drift), and a failure
 * counted when it is there but cannot be read (a doc unread is not one
 * checked). */
static char *read_doc(const char *path, int *bad)
{
    struct stat st;
    if (stat(path, &st) != 0) return NULL;
    char *t = read_all(path);
    if (!t) { printf("  %s is there but could not be read\n", path); (*bad)++; }
    return t;
}

/* WORD occurs in T as a whole word. */
static int has_word(const Text *t, const char *w, size_t n)
{
    for (const char *p = t->p; (p = strstr(p, w)); p++) {
        if ((p == t->p || !is_word((unsigned char)p[-1])) && !is_word((unsigned char)p[n])) return 1;
    }
    return 0;
}

/* `name`, `name()` or `MACRO_NAME`: one case throughout, an underscore
 * inside, nothing else. */
static int identifier_span(const char *s, size_t n, size_t *len)
{
    if (n > 2 && s[n - 2] == '(' && s[n - 1] == ')') n -= 2;
    if (n < 3 || !isalpha((unsigned char)s[0])) return 0;
    int (*is_case)(int) = islower((unsigned char)s[0]) ? islower : isupper;
    int under = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '_') under = 1;
        else if (!is_case((unsigned char)s[i]) && !isdigit((unsigned char)s[i])) return 0;
    }
    if (!under || s[n - 1] == '_') return 0;
    *len = n;
    return 1;
}

/* `net.c`, `embed.sh`: a file's name alone, with one of the code's
 * extensions. */
static int file_span(const char *s, size_t n)
{
    static const char *const ext[] = { ".c", ".h", ".sh", ".js", ".py" };
    if (n < 3 || !islower((unsigned char)s[0])) return 0;
    for (size_t i = 0; i < n; i++)
        if (!islower((unsigned char)s[i]) && !isdigit((unsigned char)s[i]) && !strchr("_-.", s[i])) return 0;
    for (size_t e = 0; e < sizeof ext / sizeof ext[0]; e++) {
        size_t el = strlen(ext[e]);
        if (n > el && !memcmp(s + n - el, ext[e], el)) return 1;
    }
    return 0;
}

static void check_ids(const char *doc, const char *text, const Text *code, const Text *names, int *bad)
{
    int line = 1;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') { line++; continue; }
        if (*p != '`') continue;
        const char *q = p + 1;
        while (*q && *q != '`' && *q != '\n') q++;
        if (*q != '`') { p = q - 1; continue; }
        size_t len, n = (size_t)(q - p - 1);
        if (file_span(p + 1, n) && n < 120) {
            char want[128];
            snprintf(want, sizeof want, "\n%.*s\n", (int)n, p + 1);
            if (!strstr(names->p, want)) {
                printf("  %s:%d: `%.*s` is no file of the repo's\n", doc, line, (int)n, p + 1);
                (*bad)++;
            }
        } else if (identifier_span(p + 1, n, &len)) {
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

/* The flags a usage line names, "--flag ARG" (ARG "" when the next word is
 * prose, not an upper-case or bracketed argument), one a line of `out`:
 * from main.c's usage() strings when `quoted`, else from README.md's
 * options block. Only lines that begin with the flag are read. */
static void flag_args(const char *text, int quoted, Text *out)
{
    for (const char *l = text; l && *l; ) {
        const char *end = strchr(l, '\n');
        if (!end) end = l + strlen(l);
        const char *p = l;
        while (p < end && *p == ' ') p++;
        if (quoted && *p == '"') p++;
        if (p + 4 < end && p[0] == ' ' && p[1] == ' ' && p[2] == '-' && p[3] == '-') p += 2;
        else if (quoted || p != l + 2) p = NULL;
        if (p && p[0] == '-' && p[1] == '-') {
            const char *f = p;
            while (p < end && *p != ' ' && *p != '\\' && *p != '"') p++;
            size_t fl = (size_t)(p - f);
            while (p < end && *p == ' ') p++;
            const char *a = p;
            if (p < end && (isupper((unsigned char)*p) || *p == '[')) while (p < end && *p != ' ' && *p != '\\' && *p != '"') p++;
            char line[128];
            int n = snprintf(line, sizeof line, "%.*s %.*s", (int)fl, f, (int)(p - a), a);
            if (n > 0 && n < (int)sizeof line) text_add(out, line, (size_t)n);
        }
        l = *end ? end + 1 : end;
    }
}

/* The length of the flag that opens a line of flag_args ("--flag ARG"). */
static size_t flag_len(const char *s) { const char *sp = strchr(s, ' '); return sp ? (size_t)(sp - s) : strlen(s); }

void test_docs(void)
{
    Text code = { 0 }, names = { 0 };
    text_add(&names, "", 0);             /* every name between two newlines */
    add_tree(&code, &names, "src");
    add_tree(&code, &names, "tests");
    add_tree(&code, &names, "tools");
    add_tree(NULL, &names, "web");
    add_tree(NULL, &names, "docs");
    add_tree(NULL, &names, ".claude");   /* the hooks the repo carries */
    text_add(&names, "Makefile", 8);
    char *mk = read_all("Makefile");
    if (mk) { text_add(&code, mk, strlen(mk)); free(mk); }

    CASE("every source file has its row in docs/CODEMAP.md");
    {
        char *cm = read_all("docs/CODEMAP.md");
        CHECK(cm != NULL);
        /* The first cell of every table row: `| cell | ...`. */
        Text cells = { 0 };
        text_add(&cells, "", 0);
        for (const char *l = cm; l && *l; ) {
            const char *end = strchr(l, '\n');
            if (!end) end = l + strlen(l);
            if (l[0] == '|' && l[1] == ' ') {
                const char *bar = memchr(l + 2, '|', (size_t)(end - l - 2));
                if (bar) text_add(&cells, l + 2, (size_t)(bar - l - 2));
            }
            l = *end ? end + 1 : end;
        }
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
            /* The name itself, whole (`map.c` is not met by `xmap.c`), in
             * a row's first cell. */
            if (!has_word(&cells, want, n)) {
                printf("  src/%s has no row in docs/CODEMAP.md\n", want);
                bad++;
            }
        }
        if (d) closedir(d);
        CHECK_EQ(bad, 0);
        free(cells.p);
        free(cm);
    }

    char kept[MAX_DOCS][128];
    int nkept = kept_current(kept, MAX_DOCS);
    CASE("the kept-current docs are read from docs/README.md, and this file's exceptions are among them");
    CHECK(nkept >= 10);
    for (size_t i = 0; i < sizeof ID_DOCS / sizeof ID_DOCS[0]; i++)
        if (!in_list(kept, nkept, ID_DOCS[i])) { printf("  %s is not in docs/README.md's *Kept current*\n", ID_DOCS[i]); CHECK(0); }
    for (size_t i = 0; i < sizeof PLAN_DOCS / sizeof PLAN_DOCS[0]; i++)
        if (!in_list(kept, nkept, PLAN_DOCS[i])) { printf("  %s is not in docs/README.md's *Kept current*\n", PLAN_DOCS[i]); CHECK(0); }
    for (int i = 0; i < nkept; i++) {
        struct stat st;
        if (stat(kept[i], &st) != 0) { printf("  docs/README.md lists %s, which is not there\n", kept[i]); CHECK(0); }
    }

    CASE("every identifier the code-describing docs name in backticks is in the code");
    {
        int bad = 0;
        for (size_t i = 0; i < sizeof ID_DOCS / sizeof ID_DOCS[0]; i++) {
            char *t = read_doc(ID_DOCS[i], &bad);
            if (!t) continue;
            check_ids(ID_DOCS[i], t, &code, &names, &bad);
            free(t);
        }
        CHECK_EQ(bad, 0);
    }

    CASE("every path the kept-current docs name exists");
    {
        int bad = 0;
        for (int i = 0; i < nkept; i++) {
            int plan = 0;
            for (size_t k = 0; k < sizeof PLAN_DOCS / sizeof PLAN_DOCS[0]; k++) plan |= !strcmp(kept[i], PLAN_DOCS[k]);
            if (plan) continue;                 /* a path of something not built yet is no drift */
            char *t = read_doc(kept[i], &bad);
            if (!t) continue;
            check_paths(kept[i], t, &bad);
            free(t);
        }
        CHECK_EQ(bad, 0);
    }

    /* README's options block is a summary of usage() in plainer words
     * (CODEMAP, *Facts kept in more than one place*): the words may differ,
     * a flag's argument may not. --watch HOST:PORT outlived the join code
     * there (CODEMAP, *Drift found*, 2026-10-24). */
    CASE("a flag README.md's options block names takes the argument usage() gives it");
    {
        char *mc = read_all("src/main.c"), *rd = read_all("README.md");
        CHECK(mc != NULL && rd != NULL);
        const char *blk = rd ? strstr(rd, "\nvtt [options]") : NULL;
        CHECK(blk != NULL);
        Text u = { 0 }, r = { 0 };
        /* usage()'s body only: another "  --x" string in main.c is not the help. */
        char *ub = mc ? strstr(mc, "\nstatic void usage(void)") : NULL;
        char *ue = ub ? strstr(ub, "\n}") : NULL;
        CHECK(ue != NULL);
        if (ue) { *ue = '\0'; flag_args(ub, 1, &u); }
        if (blk) {
            const char *fence = strstr(blk, "\n```");
            size_t n = fence ? (size_t)(fence - blk) : strlen(blk);
            char *b = malloc(n + 1);
            memcpy(b, blk, n);
            b[n] = '\0';
            flag_args(b, 0, &r);
            free(b);
        }
        int bad = 0, both = 0;
        for (const char *x = r.p; x && *x; x = strchr(x, '\n') + 1) {
            size_t xl = (size_t)(strchr(x, '\n') - x), fl = flag_len(x);
            for (const char *y = u.p; y && *y; y = strchr(y, '\n') + 1) {
                size_t yl = (size_t)(strchr(y, '\n') - y);
                if (flag_len(y) != fl || memcmp(x, y, fl)) continue;
                both++;
                if (xl != yl || memcmp(x, y, xl)) {
                    printf("  README.md has `%.*s`, usage() `%.*s`\n", (int)xl, x, (int)yl, y);
                    bad++;
                }
                break;
            }
        }
        CHECK(both >= 20);                    /* the parse found the flags */
        CHECK_EQ(bad, 0);
        free(u.p);
        free(r.p);
        free(mc);
        free(rd);
    }

    /* The default port is NET_DEFAULT_PORT's; the docs that say it must say
     * this one (CODEMAP, *Facts kept in more than one place*). */
    CASE("the docs that state the default port state NET_DEFAULT_PORT");
    {
        static const char *const PORT_DOCS[] = { "README.md", "docs/REMOTE.md", "tests/CLAUDE.md" };
        char port[16];
        snprintf(port, sizeof port, "%d", NET_DEFAULT_PORT);
        for (size_t i = 0; i < sizeof PORT_DOCS / sizeof PORT_DOCS[0]; i++) {
            char *t = read_all(PORT_DOCS[i]);
            CHECK(t != NULL);
            if (t && !strstr(t, port)) { printf("  %s does not say port %s\n", PORT_DOCS[i], port); CHECK(0); }
            /* And no stale copy beside it: every "port N" and ":N" (an
             * address's port; 4-5 digits) is this one, but for two that are
             * not the default: a file's line (`net.c:1050`, a source
             * extension before the colon; `gm.local:7794` is still read)
             * and a port the doc itself has the GM name (`:serve 7777`). The
             * bare copies ("the port is 7793") are not read: CODEMAP's facts
             * row says *none* for them. */
            for (const char *p = t; p && *p; p++) {
                const char *d = !strncmp(p, "port ", 5) ? p + 5 : *p == ':' ? p + 1 : NULL;
                if (!d) continue;
                size_t n = 0;
                while (isdigit((unsigned char)d[n])) n++;
                if (n < 4 || n > 5 || isalnum((unsigned char)d[n])) continue;
                if (*p == ':') {
                    /* A source file's extension, not a dotted host's last label. */
                    static const char *const EXT[] = { "c", "h", "sh", "py", "js", "md", "html", "txt" };
                    const char *b = p;
                    while (b > t && isalpha((unsigned char)b[-1])) b--;
                    int file = 0;
                    for (size_t e = 0; b < p && b > t && b[-1] == '.' && e < sizeof EXT / sizeof EXT[0]; e++)
                        file |= (size_t)(p - b) == strlen(EXT[e]) && !strncmp(b, EXT[e], (size_t)(p - b));
                    if (file) continue;
                }
                char named[24];
                snprintf(named, sizeof named, ":serve %.*s", (int)n, d);
                const char *s = strstr(t, named);
                if (s && !isdigit((unsigned char)s[strlen(named)])) continue;
                if (n != strlen(port) || strncmp(d, port, n)) {
                    printf("  %s says %.*s, the default port is %s\n", PORT_DOCS[i], (int)(d - p) + (int)n, p, port);
                    CHECK(0);
                }
            }
            free(t);
        }
    }
    free(code.p);
    free(names.p);
}
