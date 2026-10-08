#include "scene.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "prof.h"
#include "turn.h"
#include "util.h"

int scene_name_ok(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n >= SCENE_NAME_MAX || name[0] == ' ' || name[n - 1] == ' ') return 0;
    /* save and diff are the command's own words: a scene named "save x"
     * could be saved but never put back by name. */
    for (const char *w = "save\0diff\0"; *w; w += strlen(w) + 1) {
        size_t k = strlen(w);
        if (!strncasecmp(name, w, k) && (name[k] == '\0' || name[k] == ' ')) return 0;
    }
    return strchr(name, '"') == NULL;
}

int scene_find(const Map *m, const char *name)
{
    for (int i = 0; i < m->nscenes; i++)
        if (!strcasecmp(m->scenes[i].name, name)) return i;
    return -1;
}

/* Does the creature belong to the scene's part of the map? */
static int in_scene(const Scene *sc, const Token *t)
{
    return !sc->boxed || token_meets(t, sc->x0, sc->y0, sc->x1 - sc->x0 + 1, sc->y1 - sc->y0 + 1);
}

int scene_save(Map *m, const char *name, const int *box, char *err, size_t errsz)
{
    PROF_ZONE("scene.save");
    if (!scene_name_ok(name)) {
        snprintf(err, errsz, "a scene's name is 1-%d characters, no quote, not starting with save or diff",
                 SCENE_NAME_MAX - 1);
        return -1;
    }
    int i = scene_find(m, name);
    if (i < 0) {
        if (m->nscenes >= MAP_SCENES_MAX) {
            snprintf(err, errsz, "a map holds %d scenes - :scene NAME remove makes room", MAP_SCENES_MAX);
            return -1;
        }
        i = m->nscenes++;
        memset(&m->scenes[i], 0, sizeof m->scenes[i]);
    }
    Scene *sc = &m->scenes[i];
    tokens_free(&sc->tokens);
    memset(sc, 0, sizeof *sc);
    str_lcpy(sc->name, name, sizeof sc->name);
    if (box) {
        sc->boxed = 1;
        sc->x0 = (int16_t)imin(box[0], box[2]); sc->x1 = (int16_t)imax(box[0], box[2]);
        sc->y0 = (int16_t)imin(box[1], box[3]); sc->y1 = (int16_t)imax(box[1], box[3]);
    }
    if (!box) {                        /* a boxed scene leaves the fight to the map */
        sc->round     = m->round;
        sc->spotlight = m->spotlight;
    }
    for (int k = 0; k < m->tokens.n; k++)
        if (in_scene(sc, &m->tokens.v[k])) tokens_add(&sc->tokens, m->tokens.v[k]);
    map_touch(m);
    return i;
}

void scene_remove(Map *m, int i)
{
    if (i < 0 || i >= m->nscenes) return;
    tokens_free(&m->scenes[i].tokens);
    memmove(&m->scenes[i], &m->scenes[i + 1], (size_t)(m->nscenes - i - 1) * sizeof m->scenes[0]);
    m->nscenes--;
    memset(&m->scenes[m->nscenes], 0, sizeof m->scenes[0]);
    map_touch(m);
}

int scene_restore(Map *m, Undo *u, int i, char *err, size_t errsz)
{
    PROF_ZONE("scene.restore");
    if (i < 0 || i >= m->nscenes) { snprintf(err, errsz, "no such scene"); return -1; }
    const Scene *sc = &m->scenes[i];

    /* Checked before anything moves: the creatures staying must not be
     * under the ones coming back. */
    int outside_acting = 0;
    for (int k = 0; k < m->tokens.n; k++) {
        const Token *c = &m->tokens.v[k];
        if (in_scene(sc, c)) continue;
        if (c->turn & TURN_ACTING) outside_acting = 1;
        for (int j = 0; j < sc->tokens.n; j++) {
            const Token *s = &sc->tokens.v[j];
            if (!token_meets(c, s->x, s->y, s->size, s->size)) continue;
            char at[MAP_COORD_MAX];
            map_coord_name(c->x, c->y, at, sizeof at);
            snprintf(err, errsz, "%.30s would come back onto %.30s at %s, which the scene does not replace",
                     token_name(s), token_name(c), at);
            return -1;
        }
    }

    undo_begin(u);
    for (int k = m->tokens.n - 1; k >= 0; k--)
        if (in_scene(sc, &m->tokens.v[k])) undo_del_token(u, m, k);
    for (int j = 0; j < sc->tokens.n; j++) {
        Token t = sc->tokens.v[j];
        if (outside_acting) t.turn &= (uint8_t)~TURN_ACTING;
        undo_add_token(u, m, t);
    }
    if (!sc->boxed) {
        undo_set_round(u, m, sc->round);
        undo_set_spotlight(u, m, sc->spotlight);
    } else {
        if (m->round == 0 && turn_count(m) > 0) undo_set_round(u, m, 1);
        turn_settle(m, u);
    }
    undo_end(u);
    return sc->tokens.n;
}

/* ------------------------------------------------------------------ diff */

static void who(const Token *t, char *buf, size_t sz)
{
    if (t->label[0]) snprintf(buf, sz, "\"%s\"", t->label);
    else             snprintf(buf, sz, "(%s)", token_kind_name(t->kind));
}

static void markers(const Token *t, char *buf, size_t sz)
{
    int off = 0;
    buf[0] = '\0';
    for (int i = 0; i < t->nstatus && off >= 0 && (size_t)off < sz; i++)
        off += snprintf(buf + off, sz - (size_t)off, "%s%s", i ? " " : "", t->status[i].label);
    if (!t->nstatus) str_lcpy(buf, "none", sz);
}

static void turn_word(const Token *t, char *buf, size_t sz)
{
    if (t->turn & TURN_IN) snprintf(buf, sz, "%d%s", t->init, (t->turn & TURN_ACTING) ? " acting" : "");
    else                   snprintf(buf, sz, "%s", (t->turn & TURN_ACTING) ? "acting, outside the order" : "none");
}

/* ", " between the changes of one creature. */
static void change(FILE *out, int *any, const char *what, const char *now, const char *was)
{
    fprintf(out, "%s%s %s (was %s)", *any ? ", " : ": ", what, now, was);
    *any = 1;
}

static int changes(FILE *out, const Token *now, const Token *was)
{
    int any = 0;
    char a[160], b[160], w[48];
    who(now, w, sizeof w);
    /* The name goes out with the first change, so an unchanged creature
     * writes nothing. */
    char head[64];
    snprintf(head, sizeof head, "changed %s", w);
    int started = 0;
#define START() do { if (!started) { fputs(head, out); started = 1; } } while (0)
    if (now->size != was->size) {
        START(); snprintf(a, sizeof a, "%dx%d", now->size, now->size); snprintf(b, sizeof b, "%dx%d", was->size, was->size);
        change(out, &any, "size", a, b);
    }
    if (now->kind != was->kind) { START(); change(out, &any, "side", token_kind_name(now->kind), token_kind_name(was->kind)); }
    markers(now, a, sizeof a); markers(was, b, sizeof b);
    if (strcmp(a, b)) { START(); change(out, &any, "markers", a, b); }
    for (int i = 0; i < now->ncounters || i < was->ncounters; i++) {
        const Counter *cn = i < now->ncounters ? &now->counters[i] : NULL;
        const Counter *cw = NULL;
        for (int j = 0; j < was->ncounters && cn; j++)
            if (!strcmp(was->counters[j].name, cn->name)) cw = &was->counters[j];
        if (cn && cw && cn->value == cw->value && cn->max == cw->max) continue;
        if (cn) {
            snprintf(a, sizeof a, "%d/%d", cn->value, cn->max);
            if (cw) snprintf(b, sizeof b, "%d/%d", cw->value, cw->max); else str_lcpy(b, "none", sizeof b);
            START(); change(out, &any, cn->name, a, b);
        }
    }
    for (int j = 0; j < was->ncounters; j++) {        /* counters since taken away */
        int kept = 0;
        for (int i = 0; i < now->ncounters; i++) kept |= !strcmp(now->counters[i].name, was->counters[j].name);
        if (kept) continue;
        snprintf(b, sizeof b, "%d/%d", was->counters[j].value, was->counters[j].max);
        START(); change(out, &any, was->counters[j].name, "none", b);
    }
    if (strcmp(now->note, was->note)) {
        snprintf(a, sizeof a, "\"%s\"", now->note); snprintf(b, sizeof b, "\"%s\"", was->note);
        START(); change(out, &any, "note", a, b);
    }
    if (now->hidden != was->hidden) {
        START(); change(out, &any, "hidden", now->hidden ? "yes" : "no", was->hidden ? "yes" : "no");
    }
    turn_word(now, a, sizeof a); turn_word(was, b, sizeof b);
    if (strcmp(a, b)) { START(); change(out, &any, "turn", a, b); }
#undef START
    if (started) fputc('\n', out);
    return started;
}

int scene_diff(FILE *out, const Map *m, int i)
{
    const Scene *sc = &m->scenes[i];
    int lines = 0, n = m->tokens.n;
    char *used = xcalloc((size_t)(n > 0 ? n : 1), 1);
    char w[48], at[MAP_COORD_MAX], to[MAP_COORD_MAX];
    for (int j = 0; j < sc->tokens.n; j++) {
        const Token *was = &sc->tokens.v[j];
        int k = -1;
        for (int c = 0; c < n && k < 0; c++)
            if (!used[c] && in_scene(sc, &m->tokens.v[c]) && token_same_key(&m->tokens.v[c], was)) k = c;
        who(was, w, sizeof w);
        map_coord_name(was->x, was->y, at, sizeof at);
        if (k < 0) { fprintf(out, "gone %s at %s\n", w, at); lines++; continue; }
        used[k] = 1;
        const Token *now = &m->tokens.v[k];
        if (now->x != was->x || now->y != was->y) {
            map_coord_name(now->x, now->y, to, sizeof to);
            fprintf(out, "moved %s %s -> %s\n", w, at, to);
            lines++;
        }
        lines += changes(out, now, was);
    }
    for (int c = 0; c < n; c++) {
        if (used[c] || !in_scene(sc, &m->tokens.v[c])) continue;
        who(&m->tokens.v[c], w, sizeof w);
        map_coord_name(m->tokens.v[c].x, m->tokens.v[c].y, at, sizeof at);
        fprintf(out, "new %s at %s\n", w, at);
        lines++;
    }
    free(used);
    if (!sc->boxed) {
        if (m->round != sc->round) { fprintf(out, "round %d (was %d)\n", m->round, sc->round); lines++; }
        if (m->spotlight != sc->spotlight) {
            fprintf(out, "spotlight %s (was %s)\n", m->spotlight == SPOTLIGHT_GM ? "gm" : "players",
                    sc->spotlight == SPOTLIGHT_GM ? "gm" : "players");
            lines++;
        }
    }
    return lines;
}

void scene_describe(const Map *m, int i, char *buf, size_t sz)
{
    const Scene *sc = &m->scenes[i];
    int off = snprintf(buf, sz, "%d creature%s", sc->tokens.n, sc->tokens.n == 1 ? "" : "s");
    if (!sc->boxed && sc->round > 0 && off > 0 && (size_t)off < sz)
        off += snprintf(buf + off, sz - (size_t)off, ", round %d", sc->round);
    if (sc->boxed && off > 0 && (size_t)off < sz) {
        char a[MAP_COORD_MAX], b[MAP_COORD_MAX];
        map_coord_name(sc->x0, sc->y0, a, sizeof a);
        map_coord_name(sc->x1, sc->y1, b, sizeof b);
        snprintf(buf + off, sz - (size_t)off, ", %s:%s", a, b);
    }
}
