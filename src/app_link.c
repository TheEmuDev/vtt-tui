/* Links from the keyboard: g l on each end makes one in build mode, g o
 * sends whoever stands on an end through it in play mode, :link changes,
 * removes, lists and jumps. What a link is and where one may go is
 * link.c's. Every message names squares and secrets the players may not
 * know, so all of them are the GM's alone -- except a trip through a link
 * everyone can see, which the table watched happen. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "link.h"
#include "prof.h"

/* ------------------------------------------------------------ build: g l */

void app_link_mark(App *a)
{
    Editor *e = &a->ed;
    Map    *m = a->map;
    char    msg[160], at[2 * MAP_COORD_MAX + 2];

    if (!e->link_on) {
        /* The first end: checked alone, so a bad start is said now rather
         * than after walking to the other end. */
        Link l;
        memset(&l, 0, sizeof l);
        l.num = 1; l.kind = e->link_kind; l.size = e->brush;
        l.x[0] = (int16_t)e->cx;
        l.y[0] = (int16_t)e->cy;
        int s = l.size;
        if (e->cx + s > m->w || e->cy + s > m->h) { app_set_status(a, "that end runs off the map"); return; }
        for (int k = 0; k < s * s; k++)
            if (map_tile(m, e->cx + k % s, e->cy + k / s) == TILE_VOID) {
                app_set_status(a, "a link's end goes on ground, not void");
                return;
            }
        int o = link_meets(m, e->cx, e->cy, s, s, NULL);
        if (o >= 0) {
            char name[32];
            link_name(&m->links[o], name, sizeof name);
            snprintf(msg, sizeof msg, "%s is already there - :link %d remove takes it away", name, m->links[o].num);
            app_set_status(a, msg);
            return;
        }
        if (!link_free_num(m)) {
            snprintf(msg, sizeof msg, "a map holds %d links", MAP_LINKS_MAX);
            app_set_status(a, msg);
            return;
        }
        e->link_on   = 1;
        e->link_x    = e->cx;
        e->link_y    = e->cy;
        e->link_size = e->brush;
        link_end_name(&l, 0, at, sizeof at);
        snprintf(msg, sizeof msg, "%s from %s - move to the other end and g l again; esc cancels",
                 link_kind_name(e->link_kind), at);
        app_set_status(a, msg);
        return;
    }

    Link l;
    memset(&l, 0, sizeof l);
    l.num  = (uint8_t)link_free_num(m);
    l.kind = e->link_kind;
    l.size = e->link_size;
    l.x[0] = (int16_t)e->link_x; l.y[0] = (int16_t)e->link_y;
    l.x[1] = (int16_t)e->cx;     l.y[1] = (int16_t)e->cy;
    const char *why = l.num ? link_problem(m, &l) : "the map holds all the links it can";
    if (why) {
        snprintf(msg, sizeof msg, "%s - move and g l again, or esc", why);
        app_set_status(a, msg);
        return;
    }
    undo_begin(&a->undo);
    (void)undo_set_link(&a->undo, m, &l);
    undo_end(&a->undo);
    e->link_on = 0;
    char line[96];
    link_describe(&l, line, sizeof line);
    snprintf(msg, sizeof msg, "%s   :link %d oneway, secret or a kind changes it", line, l.num);
    app_note(a, msg);
}

void app_link_cancel(App *a)
{
    a->ed.link_on = 0;
    app_set_status(a, "link canceled");
}

/* ------------------------------------------------------------- play: g o */

void app_link_go(App *a, int li, int end, int enforce, int *moved_dx, int *moved_dy)
{
    Map     *m = a->map;
    LinkTrip tr;
    *moved_dx = *moved_dy = 0;
    const Link *l = &m->links[li];
    char name[32], msg[160];
    link_name(l, name, sizeof name);

    if (!link_trip(m, li, end, enforce, &tr)) {
        if (l->secret) app_set_status_gm(a, tr.why);
        else           app_set_status(a, tr.why);
        return;
    }
    PROF_ZONE("play.link");
    undo_begin(&a->undo);
    for (int i = 0; i < tr.n; i++) {
        const Token *t = &m->tokens.v[tr.idx[i]];
        undo_move_token(&a->undo, m, tr.idx[i], t->x + tr.dx, t->y + tr.dy);
    }
    undo_end(&a->undo);
    for (int i = 0; i < tr.n; i++) app_floor_note_move(a, &m->tokens.v[tr.idx[i]]);
    *moved_dx = tr.dx;
    *moved_dy = tr.dy;

    char there[2 * MAP_COORD_MAX + 2], who[48];
    link_end_name(l, 1 - end, there, sizeof there);
    if (tr.n == 1) snprintf(who, sizeof who, "%.30s takes", token_name(&m->tokens.v[tr.idx[0]]));
    else           snprintf(who, sizeof who, "%d creatures take", tr.n);
    snprintf(msg, sizeof msg, "%s %s%s to %s", who, l->secret ? "secret " : "", name, there);
    if (l->secret) app_note_gm(a, msg);
    else           app_note(a, msg);
}

/* ---------------------------------------------------------------- :link */

static void list_links(App *a)
{
    Map *m = a->map;
    char msg[sizeof a->status];
    if (!m->nlinks) {
        app_set_status_gm(a, "no links - in build mode, g l on one end and g l on the other makes one");
        return;
    }
    /* As many as the status line holds, whole, and how many more: :link N
     * jumps to any of them. */
    int off = snprintf(msg, sizeof msg, "%d link%s:", m->nlinks, m->nlinks == 1 ? "" : "s");
    int i = 0;
    for (; i < m->nlinks; i++) {
        const Link *l = &m->links[i];
        char name[32], e0[2 * MAP_COORD_MAX + 2], e1[2 * MAP_COORD_MAX + 2], one[96];
        link_name(l, name, sizeof name);
        link_end_name(l, 0, e0, sizeof e0);
        link_end_name(l, 1, e1, sizeof e1);
        int n = snprintf(one, sizeof one, "%s %s %s%s%s%s", i ? "," : "", name, e0,
                         l->oneway ? ">" : "-", e1, l->secret ? " secret" : "");
        if (off + n + 24 >= (int)sizeof msg && i + 1 < m->nlinks) break;
        if (off + n >= (int)sizeof msg) break;
        memcpy(msg + off, one, (size_t)n + 1);
        off += n;
    }
    if (i < m->nlinks) snprintf(msg + off, sizeof msg - (size_t)off, " ... %d more", m->nlinks - i);
    app_set_status_gm(a, msg);
}

void app_link_command(App *a, const char *rest)
{
    Map    *m = a->map;
    Editor *e = &a->ed;
    char    msg[160];
    if (!*rest) { list_links(a); return; }

    char words[8][16];
    int  nw = 0;
    for (const char *p = rest; *p && nw < 8; ) {
        while (*p == ' ') p++;
        size_t n = strcspn(p, " ");
        if (!n) break;
        snprintf(words[nw++], sizeof words[0], "%.*s", (int)(n < 15 ? n : 15), p);
        p += n;
    }

    /* :link portal -- the kind the next g l makes. */
    int kind = link_kind_from_name(words[0]);
    if (kind >= 0 && nw == 1) {
        e->link_kind = (uint8_t)kind;
        snprintf(msg, sizeof msg, "g l makes %s now", link_kind_name((uint8_t)kind));
        app_set_status_gm(a, msg);
        return;
    }

    char *end;
    long  num = strtol(words[0], &end, 10);
    int   li  = *end ? -1 : link_find(m, (int)num);
    if (li < 0) {
        snprintf(msg, sizeof msg, "no link %.15s - :links lists them; :link stairs, ladder, trapdoor or portal "
                 "picks what g l makes", words[0]);
        app_set_status_gm(a, msg);
        return;
    }

    /* :link 3 -- the cursor to its first end, or from there to the other. */
    if (nw == 1) {
        if (a->play.grabbed) { app_set_status_gm(a, "put the creature down before jumping"); return; }
        const Link *l = &m->links[li];
        int to = e->cx == l->x[0] && e->cy == l->y[0] ? 1 : 0;
        e->cx = l->x[to];
        e->cy = l->y[to];
        grid_center_on(&e->view, m, e->cx, e->cy);
        char name[32], at[2 * MAP_COORD_MAX + 2];
        link_name(l, name, sizeof name);
        link_end_name(l, to, at, sizeof at);
        snprintf(msg, sizeof msg, "%s at %s - :link %d again for the other end", name, at, l->num);
        app_set_status_gm(a, msg);
        return;
    }

    Link l = m->links[li];
    if (nw == 2 && !strcmp(words[1], "off")) {
        snprintf(msg, sizeof msg, ":link %d remove takes a link away", l.num);
        app_set_status_gm(a, msg);
        return;
    }
    if (nw == 2 && !strcmp(words[1], "remove")) {
        char name[32];
        link_name(&l, name, sizeof name);
        undo_begin(&a->undo);
        undo_remove_link(&a->undo, m, l.num);
        undo_end(&a->undo);
        snprintf(msg, sizeof msg, "%s is gone", name);
        app_note_gm(a, msg);
        return;
    }
    for (int i = 1; i < nw; i++) {
        int k = link_kind_from_name(words[i]);
        if (k >= 0)                             l.kind = (uint8_t)k;
        else if (!strcmp(words[i], "oneway"))   l.oneway = 1;
        else if (!strcmp(words[i], "twoway"))   l.oneway = 0;
        else if (!strcmp(words[i], "secret"))   l.secret = 1;
        else if (!strcmp(words[i], "seen"))     l.secret = 0;
        else if (!strcmp(words[i], "reverse")) {
            int16_t x = l.x[0], y = l.y[0];
            l.x[0] = l.x[1]; l.y[0] = l.y[1];
            l.x[1] = x;      l.y[1] = y;
        } else {
            snprintf(msg, sizeof msg, "not something a link is: %.15s - a kind, oneway, twoway, reverse, "
                     "secret, seen or remove", words[i]);
            app_set_status_gm(a, msg);
            return;
        }
    }
    undo_begin(&a->undo);
    (void)undo_set_link(&a->undo, m, &l);
    undo_end(&a->undo);
    char line[96];
    link_describe(&l, line, sizeof line);
    app_note_gm(a, line);
}
