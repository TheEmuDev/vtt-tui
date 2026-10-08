/* Links from the keyboard: g l on each end makes one in build mode, g o
 * sends whoever stands on an end through it in play mode, :link changes,
 * removes, lists and jumps. What a link is and where one may go is
 * link.c's. Every message names squares and secrets the players may not
 * know, so all of them are the GM's alone -- except a trip through a link
 * everyone can see, which the table watched happen. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app_priv.h"
#include "fog.h"
#include "link.h"
#include "mapio.h"
#include "prof.h"
#include "turn.h"

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

/* ------------------------------------------------- another map: the trip */

/* g o on a link to another map: whoever stands on the end goes there,
 * keeping formation, and so do the GM and the phones. Checked whole before
 * anything moves; the map left is saved. */
static void travel(App *a, int li)
{
    PROF_ZONE("link.trip.map");
    Map *m = a->map;
    Link l = m->links[li];
    char name[32], msg[320], why[200], path[MAP_PATH_MAX + 32], err[MAPIO_ERR_MAX];
    link_name(&l, name, sizeof name);
    /* Refusals are the GM's: they name files, and the other map's places. */
#define REFUSE(...) do { snprintf(why, sizeof why, __VA_ARGS__); app_set_status_gm(a, why); return; } while (0)
    if (!link_map_path(m, l.to_map, path, sizeof path))
        REFUSE("save this map first (:w NAME) - %.30s is found beside it", l.to_map);
    struct stat sh, st;
    if (stat(m->path, &sh) == 0 && stat(path, &st) == 0 && sh.st_dev == st.st_dev && sh.st_ino == st.st_ino)
        REFUSE("%s leads to this map", name);

    /* Who goes: everyone on the end, at their offsets from its corner. */
    Token party[LINK_TRIP_MAX];
    int   idx[LINK_TRIP_MAX], n = 0;
    for (int i = 0; i < m->tokens.n; i++) {
        if (!token_meets(&m->tokens.v[i], l.x[0], l.y[0], l.size, l.size)) continue;
        if (n == LINK_TRIP_MAX) REFUSE("more than %d creatures on %s", LINK_TRIP_MAX, name);
        party[n] = m->tokens.v[i];
        party[n].x = (int16_t)(party[n].x - l.x[0]);
        party[n].y = (int16_t)(party[n].y - l.y[0]);
        idx[n++] = i;
    }
    if (!n) {                                 /* the one the table may hear */
        snprintf(why, sizeof why, "nobody on %s", name);
        if (l.secret) app_set_status_gm(a, why); else app_set_status(a, why);
        return;
    }

    Map *d = mapio_load(path, err, sizeof err);
    if (!d) REFUSE("%s leads to %.30s, which is not beside this map", name, l.to_map);
    char autosave[MAP_PATH_MAX + 16];
    mapio_autosave_path(d, autosave, sizeof autosave);
    if (mapio_autosave_newer(d->path, autosave, NULL)) {
        map_free(d);
        REFUSE("%.30s has unsaved work from a crash - open it with :e first, to recover it or let it go", l.to_map);
    }
    int ax, ay;
    if (!link_land(d, l.to_place, party, n, &ax, &ay, why, sizeof why)) {
        map_free(d);
        app_set_status_gm(a, why);
        return;
    }

    /* They leave, and the map they leave is saved; a failed save takes that
     * back -- out of the log too -- and goes nowhere. */
    undo_begin(&a->undo);
    for (int k = n - 1; k >= 0; k--) {
        turn_before_remove(m, &a->undo, idx[k]);
        undo_del_token(&a->undo, m, idx[k]);
    }
    turn_settle(m, &a->undo);
    if (mapio_save(m, m->path, err, sizeof err) != 0) {
        undo_abort(&a->undo, m);
        map_free(d);
        REFUSE("%.120s - nobody went", err);
    }
    undo_end(&a->undo);
#undef REFUSE
    char left[MAP_NAME_MAX];
    str_lcpy(left, m->name, sizeof left);
    char stem[MAP_PATH_MAX];
    path_stem(m->path, stem, sizeof stem);

    app_travel_to(a, d);
    int first = -1;
    char who[160] = "";
    for (int k = 0; k < n; k++) {
        Token t = party[k];
        t.x = (int16_t)(ax + t.x);
        t.y = (int16_t)(ay + t.y);
        t.turn = 0;                           /* the fight stays behind */
        t.init = 0;
        /* A label is kept when nobody here has it -- a paste's numbering would
         * turn "Goblin 2" into Goblin -- and numbered only when it clashes. */
        if (tokens_find_label(&d->tokens, t.label, -1) >= 0) tokens_unique_label(&d->tokens, party[k].label, t.label, sizeof t.label);
        int at = tokens_add(&d->tokens, t);
        if (first < 0) first = at;
        size_t wl = strlen(who);
        if (wl < 100) snprintf(who + wl, sizeof who - wl, "%s%.30s", k ? ", " : "", token_name(&t));
    }
    map_touch(d);
    /* The party is written where it arrived, as it was where it left: a
     * trip is never half on disk. The file was just read and had no newer
     * autosave, so nothing of anyone's is written over. */
    char saved[80] = "";
    if (mapio_save(d, d->path, err, sizeof err) != 0)
        snprintf(saved, sizeof saved, " - NOT SAVED here: %.50s, :w", err);
    else
        a->autosave_gen = d->gen;
    fog_recompute(d);
    a->ed.cx = d->tokens.v[first].x;
    a->ed.cy = d->tokens.v[first].y;
    grid_center_on(&a->ed.view, d, a->ed.cx, a->ed.cy);

    /* The way back, if there is none. */
    int back = 0;
    for (int i = 0; i < d->nlinks; i++) back |= !strcmp(d->links[i].to_map, stem);
    snprintf(msg, sizeof msg, "%s took %s from %.30s to %.30s, %.31s%s%s%.40s%s", who, name, left, d->name, l.to_place,
             saved, back ? "" : " - no way back yet: in build mode :link to ", back ? "" : stem,
             back ? "" : " PLACE makes one");
    app_note_gm(a, msg);
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
    if (l->to_map[0]) { travel(a, li); return; }

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
        int n = l->to_map[0]
            ? snprintf(one, sizeof one, "%s %s %s>%s, %s%s", i ? "," : "", name, e0, l->to_map, l->to_place,
                       l->secret ? " secret" : "")
            : snprintf(one, sizeof one, "%s %s %s%s%s%s", i ? "," : "", name, e0,
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

    /* :link to MAP PLACE -- the brush's block at the cursor, to a place in
     * another map beside this one. */
    if (!strncmp(rest, "to ", 3)) {
        if (a->screen != SCREEN_EDITOR) { app_set_status_gm(a, "links are made in build mode - F1 first"); return; }
        char to_map[64] = "", place[AREA_NAME_MAX + 16] = "", why[200];
        const char *p = rest + 3;
        while (*p == ' ') p++;
        size_t k = strcspn(p, " ");
        snprintf(to_map, sizeof to_map, "%.*s", (int)(k < sizeof to_map - 1 ? k : sizeof to_map - 1), p);
        p += k;
        while (*p == ' ') p++;
        str_lcpy(place, p, sizeof place);
        if (!to_map[0] || !place[0]) { app_set_status_gm(a, ":link to MAP PLACE - PLACE an area or a square in MAP"); return; }
        if (strlen(place) >= AREA_NAME_MAX) { app_set_status_gm(a, "no area has a name that long"); return; }
        if (link_map_check(m, to_map, place, why, sizeof why)) { app_set_status_gm(a, why); return; }
        Link nl;
        memset(&nl, 0, sizeof nl);
        nl.num  = (uint8_t)link_free_num(m);
        nl.kind = e->link_kind;
        nl.size = (uint8_t)e->brush;
        nl.x[0] = nl.x[1] = (int16_t)e->cx;
        nl.y[0] = nl.y[1] = (int16_t)e->cy;
        str_lcpy(nl.to_map, to_map, sizeof nl.to_map);
        str_lcpy(nl.to_place, place, sizeof nl.to_place);
        if (!nl.num) { app_set_status_gm(a, "the map holds all the links it can"); return; }
        const char *bad = link_problem(m, &nl);
        if (bad) { snprintf(msg, sizeof msg, "no link here: %s", bad); app_set_status_gm(a, msg); return; }
        e->link_on = 0;
        undo_begin(&a->undo);
        (void)undo_set_link(&a->undo, m, &nl);
        undo_end(&a->undo);
        char line[160];
        link_describe(&nl, line, sizeof line);
        app_note_gm(a, line);
        return;
    }

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
        if (l->to_map[0]) snprintf(msg, sizeof msg, "%s at %s - it leads to %.24s, %.31s", name, at, l->to_map, l->to_place);
        else snprintf(msg, sizeof msg, "%s at %s - :link %d again for the other end", name, at, l->num);
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
        if (l.to_map[0] && (!strcmp(words[i], "oneway") || !strcmp(words[i], "twoway") ||
                            !strcmp(words[i], "reverse"))) {
            snprintf(msg, sizeof msg, "link %d leads to another map: it has one end here, and goes one way", l.num);
            app_set_status_gm(a, msg);
            return;
        }
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
