#include "app_priv.h"

#include <ctype.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>

#include "dice.h"
#include "fog.h"

/* ---------------------------------------------------------------- clocks */

/* The clock a name means, with the complaint on the status line when it
 * means none or several. */
static int clock_named(App *a, const char *name)
{
    int idx = clock_find(a->map, name);
    if (idx == CLOCK_AMBIGUOUS) {
        char msg[96];
        snprintf(msg, sizeof msg, "\"%.20s\" could be more than one clock", name);
        app_set_status(a, msg);
    } else if (idx < 0) {
        char msg[96];
        snprintf(msg, sizeof msg, "no clock called \"%.20s\"", name);
        app_set_status(a, msg);
    }
    return idx;
}

/* :clock                 list them
 * :clock NAME SIZE       start one -- counting down under a ruleset that
 *                        does, filling up otherwise; "up" or "down" after
 *                        the size says which regardless. Or resize it.
 * :clock NAME d6         a die for the size, and it starts at the roll
 * :clock NAME off        drop it */
static void clock_command(App *a, const char *rest)
{
    Map *m = a->map;
    char msg[200];
    if (!*rest) {
        int off = 0;
        for (int i = 0; i < CLOCK_MAX && off < (int)sizeof msg - 32; i++) {
            if (!m->clocks[i].name[0]) continue;
            char one[48];
            clock_format(&m->clocks[i], one, sizeof one);
            off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%s", off ? ", " : "", one);
        }
        app_set_status(a, off ? msg : "no clocks - :clock NAME SIZE starts one");
        return;
    }

    char name[CLOCK_NAME_MAX + 8] = { 0 }, arg[16] = { 0 }, dir[8] = { 0 };
    if (sscanf(rest, "%27s %15s %7s", name, arg, dir) < 2) {
        app_set_status(a, ":clock NAME SIZE starts a clock; :clock NAME off drops it");
        return;
    }
    if (!strcmp(arg, "off")) {
        int idx = clock_named(a, name);
        if (idx < 0) return;
        snprintf(msg, sizeof msg, "clock %s dropped", m->clocks[idx].name);
        clock_drop(m, idx);
        if (a->play.clock == idx) a->play.clock = -1;
        app_note(a, msg);
        return;
    }

    /* The direction: the word if given, else what the game does, else up. */
    const Ruleset *rs = ruleset_by_name(m->ruleset);
    int down = rs && rs->countdown;
    if (dir[0]) {
        if (!strcmp(dir, "down"))    down = 1;
        else if (!strcmp(dir, "up")) down = 0;
        else { app_set_status(a, "after the size, \"up\" or \"down\" says which way it runs"); return; }
    }

    /* The size: a number, or a die, whose roll is where the clock starts. */
    const char *p = arg + (arg[0] == 'd' || arg[0] == 'D');
    int rolled = p != arg;
    char *end;
    long  size = strtol(p, &end, 10);
    if (end == p || *end || size < 1 || size > CLOCK_SIZE_MAX) {
        snprintf(msg, sizeof msg, "a clock has 1 to %d segments, or a die that many sides: :clock Dragon d6", CLOCK_SIZE_MAX);
        app_set_status(a, msg);
        return;
    }
    int had = clock_find(m, name);
    int idx = clock_start(m, name, (int)size, down);
    if (idx < 0) {
        if (clock_count(m) >= CLOCK_MAX) snprintf(msg, sizeof msg, "no room: a map holds %d clocks", CLOCK_MAX);
        else                             snprintf(msg, sizeof msg, "a clock's name starts with a letter");
        app_set_status(a, msg);
        return;
    }
    Clock *c = &m->clocks[idx];
    int roll = 0;
    if (rolled) {
        roll = dice_one((int)size);
        c->value = (uint8_t)roll;
    }
    a->play.clock = idx;
    char one[48];
    clock_format(c, one, sizeof one);
    if (had == idx)  snprintf(msg, sizeof msg, "clock %s resized", one);
    else if (rolled) snprintf(msg, sizeof msg, "clock %s started at the d%ld's %d, %s", one, size, roll,
                              down ? "counting down" : "filling up");
    else             snprintf(msg, sizeof msg, "clock %s started - :tick %s", one,
                              down ? "counts it down" : "fills a segment");
    app_note(a, msg);
}

/* :tick             one step towards the end, on the clock in hand
 * :tick NAME        the same on that clock, which becomes the one in hand
 * :tick NAME 2      two steps;  -1 one back;  =3 set outright;  reset to the start */
static void tick_command(App *a, const char *rest)
{
    Map *m = a->map;
    char name[CLOCK_NAME_MAX + 8] = { 0 }, arg[16] = { 0 };
    int  n = sscanf(rest, "%27s %15s", name, arg);

    /* A bare number, a signed one, or "reset" names no clock: it is the amount. */
    if (n >= 1 && (name[0] == '+' || name[0] == '-' || name[0] == '=' ||
                   (name[0] >= '0' && name[0] <= '9') || !strcmp(name, "reset"))) {
        str_lcpy(arg, name, sizeof arg);
        name[0] = '\0';
        n = 0;
    }

    int idx;
    if (name[0]) idx = clock_named(a, name);
    else {
        idx = a->play.clock;
        if (idx < 0 || idx >= CLOCK_MAX || !m->clocks[idx].name[0]) {
            app_set_status(a, clock_count(m) ? ":tick NAME - which clock?" : "no clocks - :clock NAME SIZE starts one");
            return;
        }
    }
    if (idx < 0) return;
    Clock *c = &m->clocks[idx];

    int delta = 1, set = -1;
    if (!strcmp(arg, "reset")) set = clock_start_value(c);
    else if (arg[0]) {
        const char *p = arg + (arg[0] == '=' || arg[0] == '+');
        char *end;
        long  v = strtol(p, &end, 10);
        if (end == p || *end || (arg[0] == '=' && v < 0)) {
            app_set_status(a, ":tick NAME, :tick NAME 2, :tick NAME -1, :tick NAME =3, :tick NAME reset");
            return;
        }
        if (arg[0] == '=') set = (int)v;
        else               delta = (int)v;
    }

    char msg[96];
    int  before = c->value, want;
    if (set >= 0) { want = set; if (want <= c->size) clock_set(m, &a->undo, idx, want); }
    else            clock_tick(m, &a->undo, idx, delta, &want);

    if (want > c->size || want < 0) {
        if ((want < 0) == (c->down != 0)) snprintf(msg, sizeof msg, "%s is %s", c->name, c->down ? "done" : "full");
        else                              snprintf(msg, sizeof msg, "%s is at its start", c->name);
        app_set_status(a, msg);
        return;
    }
    if (c->value == before) { app_set_status(a, "no change"); return; }

    a->play.clock = idx;
    char one[48];
    clock_format(c, one, sizeof one);
    snprintf(msg, sizeof msg, "%s%s", one, clock_done(c) ? (c->down ? " - done" : " - full") : "");
    app_note(a, msg);
}

/* ------------------------------------------------------------------ dice */

/* Rolls `p` into msg: a bare roll or a bare modifier is the ruleset's
 * action roll; "duality" asks for Daggerheart's two d12s by name on any
 * map; anything else is an expression, rules or no rules. Returns -1 with
 * the complaint already on the status line. */
static int roll_text(App *a, const char *p, char *msg, size_t msgsz, DualitySpans *sp)
{
    const Ruleset *rs = ruleset_by_name(a->map->ruleset);
    int    mod = 0, bare = 0;
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
            return -1;
        }
    }
    if (bare) {
        while (*p == ' ') p++;
        if (*p) {
            char *end;
            long v = strtol(p, &end, 10);
            while (*end == ' ') end++;
            if (end == p || *end || v < -99 || v > 99) {
                app_set_status(a, "the modifier is a number, -99 to +99");
                return -1;
            }
            mod = (int)v;
        }
        DualityRoll d;
        dice_duality(mod, &d);
        dice_duality_format(&d, msg, msgsz, sp);
        return 0;
    }

    DiceResult r;
    char err[64];
    if (dice_roll_expr(p, &r, err, sizeof err) != 0) {
        snprintf(msg, msgsz, ":roll - %s", err);
        app_set_status(a, msg);
        return -1;
    }
    dice_format(p, &r, msg, msgsz);
    return 0;
}

static int roll_find(const Map *m, const char *name)
{
    int found = -1, hits = 0;
    for (int i = 0; i < ROLL_MAX; i++) {
        const char *n = m->rolls[i].name;
        if (!n[0] || strncasecmp(n, name, strlen(name)) != 0) continue;
        if (strlen(n) == strlen(name)) return i;
        found = i;
        hits++;
    }
    return hits > 1 ? -2 : found;
}

static void rolls_list(App *a)
{
    char msg[200];
    int  off = 0;
    for (int i = 0; i < ROLL_MAX && off < (int)sizeof msg - 24; i++) {
        if (!a->map->rolls[i].name[0]) continue;
        off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%s = %s",
                        off ? ", " : "", a->map->rolls[i].name, a->map->rolls[i].expr);
    }
    app_set_status(a, off ? msg : "no named rolls - :roll attack = 2d12+3 saves one");
}

/* :roll 2d6+3            an expression
 * :roll attack           a saved roll by name, or a prefix of it
 * :roll attack = 2d12+3  save one;  :roll attack =  forgets it */
static void roll_command(App *a, const char *rest)
{
    Map  *m = a->map;
    char  msg[160];
    const char *eq = strchr(rest, '=');

    if (eq) {
        char name[ROLL_NAME_MAX + 8] = { 0 };
        int  nl = 0;
        for (const char *p = rest; p < eq && nl + 1 < (int)sizeof name; p++)
            if (*p != ' ') name[nl++] = *p;
        name[nl] = '\0';
        const char *expr = eq + 1;
        while (*expr == ' ') expr++;

        if (!nl || !isalpha((unsigned char)name[0]) || nl >= ROLL_NAME_MAX) {
            snprintf(msg, sizeof msg, "a roll's name is one word starting with a letter, under %d characters", ROLL_NAME_MAX);
            app_set_status(a, msg);
            return;
        }
        int idx = roll_find(m, name);
        if (idx >= 0 && strcasecmp(m->rolls[idx].name, name) != 0) idx = -1;   /* a prefix is not the name */
        if (!*expr) {
            if (idx < 0) { snprintf(msg, sizeof msg, "no roll called %s", name); app_set_status(a, msg); return; }
            snprintf(msg, sizeof msg, "forgot %s", m->rolls[idx].name);
            memset(&m->rolls[idx], 0, sizeof m->rolls[idx]);
            map_touch(m);
            app_note(a, msg);
            return;
        }
        /* The expression must be something :roll would take, and the name
         * must not be one: "d6 = 2d6" would shadow every d6 forever. */
        DiceResult probe;
        char err[64];
        if (!strcmp(name, "duality") || dice_roll_expr(name, &probe, err, sizeof err) == 0) {
            app_set_status(a, "that name is already a roll of its own");
            return;
        }
        int bare = !strncmp(expr, "duality", 7) || expr[0] == '+' || expr[0] == '-';
        if (!bare && dice_roll_expr(expr, &probe, err, sizeof err) != 0) {
            snprintf(msg, sizeof msg, ":roll - %s", err);
            app_set_status(a, msg);
            return;
        }
        if (strlen(expr) >= ROLL_EXPR_MAX) { app_set_status(a, "that expression is too long to save"); return; }
        if (idx < 0)
            for (int i = 0; i < ROLL_MAX && idx < 0; i++)
                if (!m->rolls[i].name[0]) idx = i;
        if (idx < 0) { snprintf(msg, sizeof msg, "no room: a map holds %d named rolls", ROLL_MAX); app_set_status(a, msg); return; }
        str_lcpy(m->rolls[idx].name, name, sizeof m->rolls[idx].name);
        str_lcpy(m->rolls[idx].expr, expr, sizeof m->rolls[idx].expr);
        map_touch(m);
        snprintf(msg, sizeof msg, "%s = %s - :roll %s rolls it", name, expr, name);
        app_note(a, msg);
        return;
    }

    /* A name is tried only when the text is not a roll in itself, so a
     * saved roll can never shadow plain dice. */
    const char *text = rest;
    int         named = -1;
    if (*rest && isalpha((unsigned char)rest[0]) && strncmp(rest, "duality", 7) != 0) {
        DiceResult probe;
        char err[64];
        if (dice_roll_expr(rest, &probe, err, sizeof err) != 0) {
            named = roll_find(m, rest);
            if (named == -2) { snprintf(msg, sizeof msg, "\"%.20s\" could be more than one roll", rest); app_set_status(a, msg); return; }
            if (named >= 0) text = m->rolls[named].expr;
            else if (strchr(rest, ' ') == NULL) {
                snprintf(msg, sizeof msg, "no roll called %.20s - :roll %.20s = 2d6+3 would save one", rest, rest);
                app_set_status(a, msg);
                return;
            }
        }
    }

    int  off = named >= 0 ? snprintf(msg, sizeof msg, "%s: ", m->rolls[named].name) : 0;
    DualitySpans sp = { 0, 0, 0, 0 };
    if (roll_text(a, text, msg + off, sizeof msg - (size_t)off, &sp) < 0) return;
    app_note(a, msg);
    if (sp.hope_len) {
        app_status_span(a, off + sp.hope_at, sp.hope_len, a->th->hope);
        app_status_span(a, off + sp.fear_at, sp.fear_len, a->th->fear);
    }
}

/* ----------------------------------------------------------- the server */

/* :serve                  open the remote view, or report on the open one
 * :serve PORT             on a port of your choosing
 * :serve --stay-alive     and keep it up when the map closes
 * :serve off              close it and drop everyone
 *
 * The flag lasts as long as that server does: stopping it, or restarting it
 * on another port, starts again without it. */
static void serve_command(App *a, const char *rest)
{
    Net *net = &a->net;
    char msg[256], url[160];

    int port = 0, have_port = 0, stay = -1, off = 0, bad = 0;
    for (const char *p = rest; *p && !bad; ) {
        while (*p == ' ') p++;
        if (!*p) break;

        char word[32];
        int  n = 0;
        while (*p && *p != ' ') {
            if (n + 1 < (int)sizeof word) word[n++] = *p;
            p++;
        }
        word[n] = '\0';

        if (!strcmp(word, "off"))                  off = 1;
        else if (!strcmp(word, "--stay-alive"))    stay = 1;
        else if (!strcmp(word, "--no-stay-alive")) stay = 0;
        else if (word[0] >= '0' && word[0] <= '9') {
            char *end;
            long  v = strtol(word, &end, 10);
            if (*end || v < 0 || v > 65535) bad = 1;
            else { port = (int)v; have_port = 1; }
        }
        else bad = 1;
    }
    if (bad || (off && (have_port || stay >= 0))) {
        app_set_status(a, ":serve [PORT] [--stay-alive], or :serve off");
        return;
    }

    if (off) {
        if (!net_active(net)) { app_set_status(a, "the remote view is not on"); return; }
        int had = net_clients(net);
        net_stop(net);
        snprintf(msg, sizeof msg, "remote view off - %d client%s dropped", had, had == 1 ? "" : "s");
        app_note(a, msg);
        return;
    }

    /* Already serving, and no port named: answer, and take the flag if one
     * came with the question. Restarting would hand every player a new join
     * code for nothing. */
    if (net_active(net) && !have_port) {
        if (stay >= 0) net_set_stay(net, stay);
        net_url(net, url, sizeof url);
        snprintf(msg, sizeof msg, "serving at %s - %d client%s%s", url,
                 net_clients(net), net_clients(net) == 1 ? "" : "s",
                 net_stays(net) ? ", staying up when the map closes" : "");
        if (stay >= 0) app_note(a, msg);
        else           app_set_status(a, msg);
        return;
    }

    char err[128];
    if (net_start(net, (uint16_t)port, a->rnd, err, sizeof err) < 0) { app_set_status(a, err); return; }
    net_set_stay(net, stay > 0);
    net_set_live(net, app_remote_live(a));
    net_url(net, url, sizeof url);
    snprintf(msg, sizeof msg, "serving at %s%s", url,
             net_stays(net) ? " - staying up when the map closes" : "");
    app_note(a, msg);
}

/* ------------------------------------------------------------------- fog */

static void fog_list(App *a)
{
    const Map *m = a->map;
    char msg[200];
    int  off = snprintf(msg, sizeof msg, "fog %s", m->fog_on ? "on" : "off");
    int  n = 0;
    for (int i = 0; i < FOG_PATCH_MAX && off < (int)sizeof msg - 40; i++) {
        const FogPatch *p = &m->fog_patches[i];
        if (!p->name[0] || p->dead) continue;
        int seen, tiles = fog_count(m, i + 1, &seen);
        char rev[12];
        if (p->reveal == FOG_REVEAL_MANUAL) str_lcpy(rev, "manual", sizeof rev);
        else                                snprintf(rev, sizeof rev, "r%d", p->reveal);
        off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%s %s %d/%d%s%s%s",
                        n++ ? ", " : ": ", p->name, rev, seen, tiles,
                        p->memory ? "" : " lantern", p->disabled ? " disabled" : "",
                        i + 1 == a->ed.fog_patch ? " *" : "");
    }
    if (!n) str_lcpy(msg, "no fog - :fog NAME starts a patch, then g f paints it in build mode", sizeof msg);
    app_set_status(a, msg);
}

/* :fog                       list the patches, the one g f paints marked *
 * :fog on | off              the master switch; painting is kept either way
 * :fog --soft-edge           the half-lit rim, for every patch that follows the map
 * :fog all [N]               a patch over the whole map
 * :fog NAME [N | manual]     make NAME the patch g f paints, creating it; its reveal
 * :fog NAME memory on|off    does lit ground stay drawn once the party leaves
 * :fog NAME --soft-edge      that patch's own rim setting; --no-soft-edge
 * :fog NAME clear | hide     light the whole patch, or put it back in the dark
 * :fog NAME disable | enable keep the painting, stop it hiding / start again
 * :fog NAME delete           scrub it off the map for good */
static void fog_command(App *a, const char *rest)
{
    Map *m = a->map;
    char msg[160];
    if (!*rest) { fog_list(a); return; }

    char w1[32] = { 0 }, w2[32] = { 0 }, w3[32] = { 0 };
    sscanf(rest, "%31s %31s %31s", w1, w2, w3);

    if (!strcmp(w1, "on") || !strcmp(w1, "off")) {
        if (w2[0]) { app_set_status(a, ":fog on, :fog off"); return; }
        m->fog_on = !strcmp(w1, "on");
        map_touch(m);
        app_note(a, m->fog_on ? "fog on" : "fog off - the painting is kept; :fog on brings it back");
        return;
    }
    if (!strcmp(w1, "--soft-edge") || !strcmp(w1, "--no-soft-edge")) {
        m->fog_soft_edge = !strcmp(w1, "--soft-edge");
        map_touch(m);
        app_note(a, m->fog_soft_edge ? "soft edge on for every patch that follows the map"
                                     : "soft edge off for every patch that follows the map");
        return;
    }

    int all = !strcmp(w1, "all");
    const char *name = all ? "All" : w1;

    /* "all" is the one patch found by its exact name, never a prefix: with
     * a patch called Allies it must not paint the whole map into that. */
    int id = all ? 0 : fog_find(m, name);
    if (all)
        for (int i = 0; i < FOG_PATCH_MAX && !id; i++)
            if (!m->fog_patches[i].dead && !strcmp(m->fog_patches[i].name, "All")) id = i + 1;
    if (id < 0) {
        snprintf(msg, sizeof msg, "\"%.20s\" could be more than one patch", name);
        app_set_status(a, msg);
        return;
    }
    const char *verb = w2;
    const char *arg  = w3;

    /* Verbs on an existing patch. */
    if (!strcmp(verb, "clear") || !strcmp(verb, "hide") || !strcmp(verb, "disable") ||
        !strcmp(verb, "enable") || !strcmp(verb, "delete")) {
        if (!id) { snprintf(msg, sizeof msg, "no fog patch called %.20s", name); app_set_status(a, msg); return; }
        FogPatch *p = &m->fog_patches[id - 1];
        char pname[FOG_NAME_MAX];
        str_lcpy(pname, p->name, sizeof pname);
        if (!strcmp(verb, "clear") || !strcmp(verb, "hide")) {
            int n = fog_light_patch(m, &a->undo, id, !strcmp(verb, "clear"));
            snprintf(msg, sizeof msg, "%s %s - %d square%s", !strcmp(verb, "clear") ? "lit" : "darkened",
                     pname, n, n == 1 ? "" : "s");
        } else if (!strcmp(verb, "delete")) {
            fog_delete(m, id);
            if (a->ed.fog_patch == id) a->ed.fog_patch = 0;
            snprintf(msg, sizeof msg, "fog patch %s deleted, painting and all", pname);
        } else {
            p->disabled = !strcmp(verb, "disable");
            map_touch(m);
            snprintf(msg, sizeof msg, "fog patch %s %s", pname,
                     p->disabled ? "disabled - it hides nothing until :fog NAME enable" : "enabled");
        }
        app_note(a, msg);
        return;
    }

    /* Otherwise it is the patch g f paints, created if need be, with any
     * setting that came with it -- checked before anything is created, so
     * a typo does not leave a stray patch behind. */
    long reveal = -2;
    if (!strcmp(verb, "memory")) {
        if (strcmp(arg, "on") && strcmp(arg, "off")) { app_set_status(a, ":fog NAME memory on, or off"); return; }
    } else if (!strcmp(verb, "manual") || !strcmp(verb, "--soft-edge") || !strcmp(verb, "--no-soft-edge") || !verb[0]) {
    } else {
        char *end;
        reveal = strtol(verb, &end, 10);
        if (*end || reveal < 0 || reveal > 99) {
            app_set_status(a, ":fog NAME [reveal 0-99 | manual | memory on/off | --soft-edge | clear | hide | disable | enable | delete]");
            return;
        }
    }
    int created = 0;
    if (!id) {
        id = fog_create(m, name);
        if (id == 0) { snprintf(msg, sizeof msg, "no room: a map holds %d fog patches", FOG_PATCH_MAX); app_set_status(a, msg); return; }
        if (id < 0)  { app_set_status(a, "a fog patch's name is one word, starting with a letter, under 16 characters"); return; }
        created = 1;
    }
    FogPatch *p = &m->fog_patches[id - 1];

    if (!strcmp(verb, "memory")) {
        p->memory = !strcmp(arg, "on");
        if (!p->memory) fog_forget(m, id);
    }
    else if (!strcmp(verb, "--soft-edge") || !strcmp(verb, "--no-soft-edge")) p->soft_edge = (int8_t)!strcmp(verb, "--soft-edge");
    else if (!strcmp(verb, "manual"))                                   p->reveal = FOG_REVEAL_MANUAL;
    else if (reveal >= 0)                                               p->reveal = (int8_t)reveal;
    map_touch(m);
    a->ed.fog_patch = id;

    if (all) {
        undo_begin(&a->undo);
        for (int y = 0; y < m->h; y++)
            for (int x = 0; x < m->w; x++)
                if ((fog_at(m, x, y) & FOG_ID) != (uint8_t)id) fog_paint(m, &a->undo, x, y, id);
        undo_end(&a->undo);
    }
    int switched = 0;
    if (created && !m->fog_on) { m->fog_on = 1; switched = 1; }

    char rev[16];
    if (p->reveal == FOG_REVEAL_MANUAL) str_lcpy(rev, "lit by hand only", sizeof rev);
    else                                snprintf(rev, sizeof rev, "reveal %d", p->reveal);
    snprintf(msg, sizeof msg, "fog patch %s%s: %s, memory %s%s%s", p->name, created ? " made" : "",
             rev, p->memory ? "on" : "off",
             all ? " - over the whole map" : created ? " - g f paints it in build mode" : "",
             switched ? "; fog on" : "");
    app_note(a, msg);
}

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
        app_set_status(a, "closed without saving");
        app_close_map(a);
        return;
    }
    if (!strcmp(verb, "e") || !strcmp(verb, "edit")) {
        if (!rest[0]) { app_set_status(a, ":e needs a file name"); return; }
        char path[MAP_PATH_MAX];
        mapio_resolve_path(rest, path, sizeof path);
        app_leave_map_for(a, path);
        return;
    }
    if (!strcmp(verb, "play"))  { a->screen = SCREEN_PLAY;   app_set_status(a, "play mode"); return; }
    if (!strcmp(verb, "build")) { a->screen = SCREEN_EDITOR; app_set_status(a, "build mode"); return; }
    if (!strcmp(verb, "name")) {
        if (!rest[0]) { app_set_status(a, ":name needs a value"); return; }
        str_lcpy(m->name, rest, sizeof m->name);
        map_touch(m);
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
        map_touch(m);
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
        map_touch(m);
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
        map_touch(m);
        /* The overlay's reach is read against the ruleset, so a band index
         * or a radius from the old one would mean something else now. */
        range_off(&a->play.range);
        char msg[128];
        snprintf(msg, sizeof msg, "ruleset: %s%s", rs->name,
                 rs->verified ? "" : " (range bands unverified)");
        app_note(a, msg);
        return;
    }
    if (!strcmp(verb, "roll"))  { roll_command(a, rest); return; }
    if (!strcmp(verb, "rolls")) { rolls_list(a); return; }
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
    if (!strcmp(verb, "notes")) {
        /* Where the notes are, not what they say: this line is mirrored. */
        char msg[200];
        int  off = 0, n = 0;
        for (int i = 0; i < m->tokens.n && off < (int)sizeof msg - 28; i++) {
            const Token *t = &m->tokens.v[i];
            if (!t->note[0]) continue;
            off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%.16s", n++ ? ", " : "notes on ",
                            t->label[0] ? t->label : token_kind_name(t->kind));
        }
        for (int i = 0; i < m->nnotes && off < (int)sizeof msg - 28; i++) {
            char at[MAP_COORD_MAX];
            map_coord_name(m->notes[i].x, m->notes[i].y, at, sizeof at);
            off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%s", n++ ? ", " : "notes on ", at);
        }
        int total = m->nnotes;
        for (int i = 0; i < m->tokens.n; i++) total += m->tokens.v[i].note[0] != '\0';
        if (n < total && off < (int)sizeof msg - 8) snprintf(msg + off, sizeof msg - (size_t)off, ", ...");
        app_set_status(a, n ? msg : "no notes - s n writes one on a creature or a square");
        return;
    }
    if (!strcmp(verb, "fog"))   { fog_command(a, rest);   return; }
    if (!strcmp(verb, "clock")) { clock_command(a, rest); return; }
    if (!strcmp(verb, "tick"))  { tick_command(a, rest);  return; }
    if (!strcmp(verb, "player")) {
        /* :player preview -- the players' view on the GM's own screen. */
        if (strcmp(rest, "preview") != 0) { app_set_status(a, ":player preview shows what the players see; q returns"); return; }
        if (a->screen != SCREEN_PLAY) { app_set_status(a, "the players' view is play mode's - F2 first"); return; }
        a->preview = !a->preview;
        app_set_status(a, a->preview ? "previewing the players' view - q returns to yours"
                                     : "back to the GM's view");
        return;
    }
    if (!strcmp(verb, "serve")) { serve_command(a, rest); return; }
    if (!strcmp(verb, "mirror")) {
        /* A second window on this machine, running the watcher against our
         * own server, which is started if it is not. It is detached so it
         * outlives nothing of ours but the server itself. */
        Net *net = &a->net;
        if (!net_active(net)) {
            char err[128];
            if (net_start(net, 0, a->rnd, err, sizeof err) < 0) { app_set_status(a, err); return; }
            net_set_live(net, app_remote_live(a));
        }
        char msg[192];
        if (app_spawn_mirror(a, msg, sizeof msg) < 0) { app_set_status(a, msg); return; }
        app_note(a, msg);
        return;
    }
    if (!strcmp(verb, "panel")) {
        Play *pl = &a->play;
        if (!*rest)                    pl->panel = !pl->panel;
        else if (!strcmp(rest, "on"))  pl->panel = 1;
        else if (!strcmp(rest, "off")) pl->panel = 0;
        else { app_set_status(a, ":panel on, :panel off, or :panel to toggle"); return; }
        app_set_status(a, pl->panel ? "turn panel on - it shows when there is a fight"
                                    : "turn panel off");
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
