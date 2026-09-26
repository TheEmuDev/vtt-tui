/* The control channel's requests: an agent reading, and editing, the map the
 * GM has open. ctl.c moves the bytes; this reads the lines and runs them
 * against the App. The language and its rules are docs/CONTROL.md's. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "maptools.h"
#include "prof.h"
#include "util.h"

/* ----------------------------------------------------------------- words */

#define CTL_WORDS    12
#define CTL_WORD_MAX 256

/* A line split into words: spaces between, "..." one word with \" and \\
 * inside. Returns how many, or -1 with why in err. */
static int split_words(const char *line, char w[CTL_WORDS][CTL_WORD_MAX], char *err, size_t errsz)
{
    int n = 0;
    const char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (!*p) break;
        if (n == CTL_WORDS) { snprintf(err, errsz, "more than %d words", CTL_WORDS); return -1; }
        size_t k = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) p++;
                if (k + 1 == CTL_WORD_MAX) { snprintf(err, errsz, "a word over %d characters", CTL_WORD_MAX - 1); return -1; }
                w[n][k++] = *p++;
            }
            if (*p != '"') { snprintf(err, errsz, "a quote is not closed"); return -1; }
            p++;
            if (*p && *p != ' ' && *p != '\t' && *p != '\r') {
                snprintf(err, errsz, "a closing quote runs into the next word");
                return -1;
            }
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != '\r') {
                if (*p == '"') { snprintf(err, errsz, "a quote in the middle of a word"); return -1; }
                if (k + 1 == CTL_WORD_MAX) { snprintf(err, errsz, "a word over %d characters", CTL_WORD_MAX - 1); return -1; }
                w[n][k++] = *p++;
            }
        }
        w[n][k] = '\0';
        n++;
    }
    return n;
}

/* ----------------------------------------------------------------- state */

static const char *screen_name(Screen s)
{
    switch (s) {
    case SCREEN_MENU:    return "menu";
    case SCREEN_BROWSER: return "file browser";
    case SCREEN_EDITOR:  return "build";
    case SCREEN_PLAY:    return "play";
    case SCREEN_HELP:    return "help";
    }
    return "?";
}

static const char *mode_name(EdMode m)
{
    switch (m) {
    case ED_NORMAL:  return "normal";
    case ED_WALL:    return "wall";
    case ED_VISUAL:  return "visual";
    case ED_COMMAND: return "command line";
    }
    return "?";
}

const char *app_ctl_busy(const App *a)
{
    if (!a->map)                         return "no map is open";
    if (a->screen == SCREEN_PLAY)        return "the GM is in play mode - edits are build mode's";
    if (a->screen != SCREEN_EDITOR)      return "the GM is not in build mode";
    if (a->modal == MODAL_PROMPT)        return "the GM is answering a prompt";
    if (a->modal != MODAL_NONE)          return "a question is open on the GM's screen";
    if (a->ed.mode == ED_COMMAND)        return "the GM is typing a : command";
    if (a->pending || a->ed.pending_g)   return "the GM is part way through a key";
    return NULL;
}

/* ----------------------------------------------------------------- reads */

static void do_status(App *a, FILE *out)
{
    const Map *m = a->map;
    if (m) {
        fprintf(out, "map %s  %dx%d\n", m->name, m->w, m->h);
        fprintf(out, "file %s%s\n", m->path[0] ? m->path : "(never saved)",
                m->modified ? "  unsaved changes" : "");
    }
    else fputs("map none open\n", out);
    if (a->screen == SCREEN_EDITOR) fprintf(out, "screen build, %s mode\n", mode_name(a->ed.mode));
    else                            fprintf(out, "screen %s\n", screen_name(a->screen));
    fprintf(out, "undo %d back, %d forward\n", a->undo.depth, a->undo.nmarks - a->undo.depth);
    const char *busy = app_ctl_busy(a);
    fprintf(out, "edits %s%s\n", busy ? "not now: " : "taken", busy ? busy : "");
}

/* An optional last word that must be `json`. */
static int want_json(char w[][CTL_WORD_MAX], int n, int at, char *err, size_t errsz)
{
    if (n <= at) return 0;
    if (n == at + 1 && !strcmp(w[at], "json")) return 1;
    snprintf(err, errsz, "%.20s takes nothing but json after it", w[0]);
    return -1;
}

/* ------------------------------------------------------------------- run */

/* Runs one line. 0, or -1 with why in err. */
static int run_line(App *a, char w[][CTL_WORD_MAX], int n, FILE *out, char *err, size_t errsz)
{
    Map *m = a->map;
    const char *v = w[0];

    if (!strcmp(v, "status")) {
        if (n > 1) { snprintf(err, errsz, "status takes nothing after it"); return -1; }
        do_status(a, out);
        return 0;
    }
    if (!strcmp(v, "dump")) {
        int x0 = 0, y0 = 0, x1 = m->w - 1, y1 = m->h - 1;
        if (n > 2) { snprintf(err, errsz, "dump takes one region, like B2:K12"); return -1; }
        if (n == 2 && !maptools_region(m, w[1], &x0, &y0, &x1, &y1)) {
            snprintf(err, errsz, "%.40s is not a region on this map, like B2:K12", w[1]);
            return -1;
        }
        maptools_dump(out, m, x0, y0, x1, y1);
        return 0;
    }
    if (!strcmp(v, "describe")) {
        int j = want_json(w, n, 1, err, errsz);
        if (j < 0) return -1;
        maptools_describe(out, m, j);
        return 0;
    }
    if (!strcmp(v, "check")) {
        int j = want_json(w, n, 1, err, errsz);
        if (j < 0) return -1;
        maptools_check_map(out, m, j);
        return 0;
    }
    snprintf(err, errsz, "unknown request %.40s", v);
    return -1;
}

char *app_ctl_exec(App *a, const char *req, size_t *len)
{
    PROF_ZONE("ctl");
    char  *body = NULL;
    size_t blen = 0;
    FILE  *out  = open_memstream(&body, &blen);
    if (!out) return NULL;

    char verdict[320] = "ok";
    int  lineno = 0;
    const char *p = req;
    if (strlen(req) != *len) {
        snprintf(verdict, sizeof verdict, "error: a nul byte in the request");
        p = "";
    }
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t      ll  = end ? (size_t)(end - p) : strlen(p);
        lineno++;

        char line[1024], err[200] = "";
        char w[CTL_WORDS][CTL_WORD_MAX];
        int  n = -1;
        if (ll >= sizeof line) snprintf(err, sizeof err, "the line is over %zu characters", sizeof line - 1);
        else {
            memcpy(line, p, ll);
            line[ll] = '\0';
            n = split_words(line, w, err, sizeof err);
        }
        p = end ? end + 1 : p + ll;

        if (n == 0 || (n > 0 && w[0][0] == '#')) continue;
        if (n > 0 && !a->map && strcmp(w[0], "status") != 0) snprintf(err, sizeof err, "no map is open");
        if (!err[0] && run_line(a, w, n, out, err, sizeof err) == 0) continue;
        snprintf(verdict, sizeof verdict, "error: line %d: %s", lineno, err);
        break;
    }
    fclose(out);

    /* The verdict, then the report; an error sends only what it says. */
    int    ok = !strcmp(verdict, "ok");
    size_t vl = strlen(verdict);
    size_t total = vl + 1 + (ok ? blen : 0);
    char  *ans = malloc(total + 1);
    if (!ans) { free(body); return NULL; }
    memcpy(ans, verdict, vl);
    ans[vl] = '\n';
    if (ok && blen) memcpy(ans + vl + 1, body, blen);
    ans[total] = '\0';
    free(body);
    *len = total;
    return ans;
}

/* ------------------------------------------------------------- :agent */

void app_agent_command(App *a, const char *rest)
{
    Ctl *c = &a->ctl;
    char msg[CTL_PATH_MAX + 64];
    if (!*rest) {
        if (!ctl_active(c)) app_set_status_gm(a, "the agent channel is off - :agent on opens it");
        else {
            snprintf(msg, sizeof msg, "agent channel on at %s - %u request%s so far", c->path,
                     c->requests, c->requests == 1 ? "" : "s");
            app_set_status_gm(a, msg);
        }
        return;
    }
    if (!strcmp(rest, "off")) {
        if (!ctl_active(c)) { app_set_status_gm(a, "the agent channel is already off"); return; }
        ctl_stop(c);
        app_note_gm(a, "agent channel off");
        return;
    }
    if (strcmp(rest, "on") != 0) { app_set_status_gm(a, ":agent on, :agent off, or :agent to ask"); return; }
    if (ctl_active(c)) {
        snprintf(msg, sizeof msg, "the agent channel is already on at %s", c->path);
        app_set_status_gm(a, msg);
        return;
    }
    char err[CTL_PATH_MAX + 64];
    if (ctl_start(c, err, sizeof err) < 0) { app_set_status_gm(a, err); return; }
    snprintf(msg, sizeof msg, "agent channel on - vtt --ctl talks to this map; u undoes what it does");
    app_note_gm(a, msg);
}
