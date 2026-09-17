#include "app_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dice.h"

/* --------------------------------------------------------- command line */

void app_exec_command(App *a, const char *line)
{
    while (*line == ' ') line++;
    if (!*line) return;

    char verb[32] = { 0 };
    int  consumed = 0;
    sscanf(line, "%31s %n", verb, &consumed);
    const char *rest = consumed > 0 ? line + consumed : "";
    while (*rest == ' ') rest++;

    Map *m = a->map;

    if (!strcmp(verb, "w") || !strcmp(verb, "write")) {
        char path[MAP_PATH_MAX];
        if (rest[0]) mapio_resolve_path(rest, path, sizeof path);
        else if (m->path[0]) str_lcpy(path, m->path, sizeof path);
        else mapio_resolve_path(m->name, path, sizeof path);
        app_save_map(a, path);
        return;
    }
    if (!strcmp(verb, "wq") || !strcmp(verb, "x")) {
        char path[MAP_PATH_MAX];
        if (m->path[0]) str_lcpy(path, m->path, sizeof path);
        else mapio_resolve_path(m->name, path, sizeof path);
        if (app_save_map(a, path) == 0) app_close_map(a);
        return;
    }
    if (!strcmp(verb, "q") || !strcmp(verb, "quit")) { app_leave_map(a); return; }
    if (!strcmp(verb, "q!")) {
        app_close_map(a);
        app_set_status(a, "closed without saving");
        return;
    }
    if (!strcmp(verb, "e") || !strcmp(verb, "edit")) {
        if (!rest[0]) { app_set_status(a, ":e needs a file name"); return; }
        char path[MAP_PATH_MAX];
        mapio_resolve_path(rest, path, sizeof path);
        app_open_map(a, path);
        return;
    }
    if (!strcmp(verb, "play"))  { a->screen = SCREEN_PLAY;   app_set_status(a, "play mode"); return; }
    if (!strcmp(verb, "build")) { a->screen = SCREEN_EDITOR; app_set_status(a, "build mode"); return; }
    if (!strcmp(verb, "name")) {
        if (!rest[0]) { app_set_status(a, ":name needs a value"); return; }
        str_lcpy(m->name, rest, sizeof m->name);
        m->modified = 1;
        app_set_status(a, "renamed");
        return;
    }
    if (!strcmp(verb, "resize")) {
        int w = 0, h = 0;
        if (sscanf(rest, "%dx%d", &w, &h) != 2 && sscanf(rest, "%d %d", &w, &h) != 2) {
            app_set_status(a, ":resize wants a width and a height");
            return;
        }
        if (map_resize(m, w, h) != 0) {
            app_set_status(a, "resize refused: out of range");
            return;
        }
        /* The history describes cells that may no longer exist. */
        undo_clear(&a->undo);
        a->ed.cx = iclamp(a->ed.cx, 0, m->w - 1);
        a->ed.cy = iclamp(a->ed.cy, 0, m->h - 1);
        ed_layout(&a->ed, m, a->rnd->w, a->rnd->h);

        char msg[96];
        snprintf(msg, sizeof msg, "resized to %dx%d (undo history cleared)", w, h);
        app_set_status(a, msg);
        return;
    }
    if (!strcmp(verb, "scale")) {
        double v = atof(rest);
        if (!(v > 0.0 && v < 100000.0)) {
            app_set_status(a, ":scale wants feet per tile, e.g. :scale 5");
            return;
        }
        m->scale_ft = v;
        m->modified = 1;
        char msg[64];
        snprintf(msg, sizeof msg, "one tile is %g ft", v);
        app_set_status(a, msg);
        return;
    }
    if (!strcmp(verb, "metric")) {
        int got = rest[0] ? dist_metric_from_name(rest) : -1;
        if (got < 0) {
            app_set_status(a, ":metric wants chebyshev, euclidean, alt or manhattan");
            return;
        }
        m->metric = got;
        m->modified = 1;
        char msg[64];
        snprintf(msg, sizeof msg, "metric: %s", dist_metric_name((DistMetric)got));
        app_set_status(a, msg);
        return;
    }
    if (!strcmp(verb, "ruleset")) {
        const Ruleset *rs = rest[0] ? ruleset_by_name(rest) : ruleset_by_name(m->ruleset);
        if (!rs) {
            char msg[128];
            int  off = snprintf(msg, sizeof msg, "unknown ruleset. try: ");
            for (int i = 0; ruleset_at(i) && off < (int)sizeof msg - 2; i++)
                off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%s",
                                i ? ", " : "", ruleset_at(i)->name);
            app_set_status(a, msg);
            return;
        }
        str_lcpy(m->ruleset, strcmp(rs->name, "none") ? rs->name : "", sizeof m->ruleset);
        m->modified = 1;
        /* The overlay's reach is read against the ruleset, so a band index
         * or a radius from the old one would mean something else now. */
        range_off(&a->play.range);
        char msg[128];
        snprintf(msg, sizeof msg, "ruleset: %s%s", rs->name,
                 rs->verified ? "" : " (range bands unverified)");
        app_note(a, msg);
        return;
    }
    if (!strcmp(verb, "roll")) {
        /* A bare roll, or a bare modifier, is the ruleset's action roll;
         * "duality" asks for Daggerheart's two d12s by name on any map;
         * anything else is an expression, rules or no rules. */
        const Ruleset *rs = ruleset_by_name(m->ruleset);
        const char *p = rest;
        int   mod = 0, bare = 0;
        size_t plen = strlen(p);
        if (plen >= 7 && !strncmp(p, "duality", 7) &&
            (plen == 7 || p[7] == ' ' || p[7] == '+' || p[7] == '-')) {
            p += 7;
            bare = 1;
        } else if (!*p || p[0] == '+' || p[0] == '-') {
            bare = 1;
            if (!rs || !rs->action_roll) {
                app_set_status(a, *p ? "a bare modifier needs a ruleset with an action roll - :roll 2d6+3"
                                     : "roll what? :roll 2d6+3, or :roll +2 under a ruleset with an action roll");
                return;
            }
        }
        char msg[160];
        DualitySpans sp = { 0, 0, 0, 0 };
        if (bare) {
            while (*p == ' ') p++;
            if (*p) {
                char *end;
                long v = strtol(p, &end, 10);
                while (*end == ' ') end++;
                if (end == p || *end || v < -99 || v > 99) {
                    app_set_status(a, "the modifier is a number, -99 to +99");
                    return;
                }
                mod = (int)v;
            }
            DualityRoll d;
            dice_duality(mod, &d);
            dice_duality_format(&d, msg, sizeof msg, &sp);
        } else {
            DiceResult r;
            char err[64];
            if (dice_roll_expr(p, &r, err, sizeof err) != 0) {
                snprintf(msg, sizeof msg, ":roll - %s", err);
                app_set_status(a, msg);
                return;
            }
            dice_format(p, &r, msg, sizeof msg);
        }
        app_note(a, msg);
        if (sp.hope_len) {
            app_status_span(a, sp.hope_at, sp.hope_len, a->th->hope);
            app_status_span(a, sp.fear_at, sp.fear_len, a->th->fear);
        }
        return;
    }
    if (!strcmp(verb, "turns")) {
        /* Bare, it reads the order out; "off" ends the fight. */
        char msg[160];
        if (!*rest) {
            turn_list(m, msg, sizeof msg);
            app_set_status(a, msg);
            return;
        }
        if (strcmp(rest, "off") != 0) { app_set_status(a, ":turns lists the order, :turns off ends the fight"); return; }
        if (turn_count(m) == 0 && turn_acting(m) < 0) { app_set_status(a, "there is no fight to end"); return; }
        int had = turn_clear(m, &a->undo);
        snprintf(msg, sizeof msg, "the fight is over - %d left the turn order", had);
        app_note(a, msg);
        return;
    }
    if (!strcmp(verb, "log")) {
        /* :log toggles; on/off say which; anything else is a file. */
        SessionLog *l = &a->slog;
        int want;
        char path[MAP_PATH_MAX];
        if (!*rest)                 { want = !slog_on(l); slog_default_path(m, path, sizeof path); }
        else if (!strcmp(rest, "on"))  { want = 1; slog_default_path(m, path, sizeof path); }
        else if (!strcmp(rest, "off")) { want = 0; path[0] = '\0'; }
        else                        { want = 1; str_lcpy(path, rest, sizeof path); }

        char msg[MAP_PATH_MAX + 32];
        if (!want) {
            if (!slog_on(l)) { app_set_status(a, "the log is already off"); return; }
            snprintf(msg, sizeof msg, "log off - %s", l->path);
            slog_close(l);
            app_set_status(a, msg);
            return;
        }
        if (slog_on(l) && !strcmp(l->path, path)) {
            snprintf(msg, sizeof msg, "already logging to %s", path);
            app_set_status(a, msg);
            return;
        }
        char err[128];
        if (slog_open(l, path, m->name, err, sizeof err) != 0) {
            app_set_status(a, err);
            return;
        }
        snprintf(msg, sizeof msg, "logging to %s", path);
        app_set_status(a, msg);
        return;
    }
    if (!strcmp(verb, "zoom")) {
        int z = atoi(rest);
        ed_set_zoom(&a->ed, m, z);
        return;
    }

    char msg[96];

    /* A coordinate rather than a verb, checked last so a verb can never lose
     * to one. Nothing else here has a digit in it, which is what makes a
     * square unmistakable -- and why the row is required: a bare column
     * letter would be :e, :w, :x or :q. */
    int jx = a->ed.cx, jy = a->ed.cy;
    if (map_coord_parse(verb, &jx, &jy)) {
        if (a->play.grabbed) {
            app_set_status(a, "put the creature down before jumping");
            return;
        }
        if (!map_in_bounds(a->map, jx, jy)) {
            char edge[MAP_COORD_MAX];
            map_coord_name(a->map->w - 1, a->map->h - 1, edge, sizeof edge);
            snprintf(msg, sizeof msg, "off the map - it ends at %s", edge);
            app_set_status(a, msg);
            return;
        }

        a->ed.cx = jx;
        a->ed.cy = jy;
        /* Centred rather than merely scrolled into view: a jump is for going
         * somewhere else, and arriving pinned against an edge shows half of
         * where you went. */
        grid_center_on(&a->ed.view, a->map, jx, jy);
        if (a->ed.mode == ED_VISUAL) a->ed.mode = ED_NORMAL;

        char at[MAP_COORD_MAX];
        map_coord_name(jx, jy, at, sizeof at);
        snprintf(msg, sizeof msg, "jumped to %s", at);
        app_set_status(a, msg);
        return;
    }

    snprintf(msg, sizeof msg, "unknown command: %.40s", verb);
    app_set_status(a, msg);
}

void app_command_key(App *a, Key k)
{
    int r = ui_prompt_key(&a->ed.cmd, k);
    if (r == 0) return;

    a->ed.mode = ED_NORMAL;
    if (r == 1) app_exec_command(a, a->ed.cmd.buf);
    else        app_set_status(a, "");
}
