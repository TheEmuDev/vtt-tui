#include "app_priv.h"
#include "card.h"

#include <ctype.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>

#include "dice.h"
#include "floor.h"
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
 * :clock NAME remove     drop it */
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
        app_set_status(a, ":clock NAME SIZE starts a clock; :clock NAME remove drops it");
        return;
    }
    if (!strcmp(arg, "off")) {
        snprintf(msg, sizeof msg, ":clock %.27s remove drops a clock", name);
        app_set_status(a, msg);
        return;
    }
    if (!strcmp(arg, "remove")) {
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
 * :roll attack = 2d12+3  save one;  :roll attack remove  removes it */
static void roll_command(App *a, const char *rest)
{
    Map  *m = a->map;
    char  msg[160];
    const char *eq = strchr(rest, '=');

    /* NAME remove: the whole name, as a prefix would remove the wrong one. */
    char gone[ROLL_NAME_MAX + 16];
    str_lcpy(gone, rest, sizeof gone);
    if (!eq && str_cut_word(gone, "remove") && !strchr(gone, ' ') && strlen(gone) < ROLL_NAME_MAX) {
        int idx = roll_find(m, gone);
        if (idx >= 0 && strcasecmp(m->rolls[idx].name, gone) != 0) idx = -1;
        if (idx < 0) { snprintf(msg, sizeof msg, "no roll called %s", gone); app_set_status(a, msg); return; }
        snprintf(msg, sizeof msg, "removed roll %s", m->rolls[idx].name);
        memset(&m->rolls[idx], 0, sizeof m->rolls[idx]);
        map_touch(m);
        app_note(a, msg);
        return;
    }

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
            snprintf(msg, sizeof msg, "%s = what? :roll %s = 2d6+3 saves it, :roll %s remove removes it", name, name, name);
            app_set_status(a, msg);
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
 * :serve --no-pings       and ignore the phones' taps (--pings takes them again)
 * :serve off              close it and drop everyone
 *
 * The flags last as long as that server does: stopping it, or restarting it
 * on another port, starts again without them. */
static void serve_command(App *a, const char *rest)
{
    Net *net = &a->net;
    char msg[256], url[160];

    int port = 0, have_port = 0, stay = -1, pings = -1, off = 0, bad = 0;
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
        else if (!strcmp(word, "--pings"))         pings = 1;
        else if (!strcmp(word, "--no-pings"))      pings = 0;
        else if (word[0] >= '0' && word[0] <= '9') {
            char *end;
            long  v = strtol(word, &end, 10);
            if (*end || v < 0 || v > 65535) bad = 1;
            else { port = (int)v; have_port = 1; }
        }
        else bad = 1;
    }
    if (bad || (off && (have_port || stay >= 0 || pings >= 0))) {
        app_set_status(a, ":serve [PORT] [--stay-alive] [--no-pings], or :serve off");
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

    /* Already serving, and no port named or the one it is on: answer, and
     * take the flag if one came with the question. Restarting would hand
     * every player a new join code and drop them for nothing. */
    if (net_active(net) && (!have_port || port == net->port)) {
        if (stay >= 0)  net_set_stay(net, stay);
        if (pings >= 0) net_set_pings(net, pings);
        net_url(net, url, sizeof url);
        snprintf(msg, sizeof msg, "serving at %s - %d client%s%s%s", url,
                 net_clients(net), net_clients(net) == 1 ? "" : "s",
                 net_stays(net) ? ", staying up when the map closes" : "",
                 net_pings_on(net) ? "" : ", pings off");
        if (stay >= 0 || pings >= 0) app_note(a, msg);
        else                         app_set_status(a, msg);
        return;
    }

    char err[128];
    if (net_start(net, (uint16_t)port, a->rnd, err, sizeof err) < 0) { app_set_status(a, err); return; }
    net_set_stay(net, stay > 0);
    net_set_pings(net, pings != 0);
    net_set_live(net, app_remote_live(a));
    net_url(net, url, sizeof url);
    snprintf(msg, sizeof msg, "serving at %s%s%s", url,
             net_stays(net) ? " - staying up when the map closes" : "",
             net_pings_on(net) ? "" : " - pings off");
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
        off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%s %s %d/%d%s%s%s%s",
                        n++ ? ", " : ": ", p->name, rev, seen, tiles,
                        p->memory ? "" : " lantern", fog_patch_soft(m, i + 1) ? " soft" : "",
                        p->disabled ? " disabled" : "",
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
 * :fog NAME remove           scrub it off the map for good */
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

    if (!strcmp(verb, "delete")) {
        snprintf(msg, sizeof msg, ":fog %.20s remove scrubs a patch off the map", name);
        app_set_status(a, msg);
        return;
    }
    /* Verbs on an existing patch. */
    if (!strcmp(verb, "clear") || !strcmp(verb, "hide") || !strcmp(verb, "disable") ||
        !strcmp(verb, "enable") || !strcmp(verb, "remove")) {
        if (!id) { snprintf(msg, sizeof msg, "no fog patch called %.20s", name); app_set_status(a, msg); return; }
        FogPatch *p = &m->fog_patches[id - 1];
        char pname[FOG_NAME_MAX];
        str_lcpy(pname, p->name, sizeof pname);
        if (!strcmp(verb, "clear") || !strcmp(verb, "hide")) {
            int n = fog_light_patch(m, &a->undo, id, !strcmp(verb, "clear"));
            snprintf(msg, sizeof msg, "%s %s - %d square%s", !strcmp(verb, "clear") ? "lit" : "darkened",
                     pname, n, n == 1 ? "" : "s");
        } else if (!strcmp(verb, "remove")) {
            fog_delete(m, id);
            if (a->ed.fog_patch == id) a->ed.fog_patch = 0;
            snprintf(msg, sizeof msg, "fog patch %s removed, painting and all", pname);
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
            app_set_status(a, ":fog NAME [reveal 0-99 | manual | memory on/off | --soft-edge | clear | hide | disable | enable | remove]");
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

int app_cmd_vbox(App *a, int *x0, int *y0, int *x1, int *y1)
{
    if (a->screen != SCREEN_EDITOR || !a->ed.cmd_from_visual) return 0;
    EdShape sh = ed_shape(a->ed.shape, a->ed.anchor_x, a->ed.anchor_y, a->ed.cx, a->ed.cy, 0);
    *x0 = sh.x0; *y0 = sh.y0; *x1 = sh.x1; *y1 = sh.y1;
    a->ed.mode = ED_NORMAL;
    return 1;
}

/* :areas lists the named areas; :area NAME names the v box (build mode),
 * or jumps to the area of that name; :area NAME remove takes the name off. */
static void area_command(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
    char msg[256], b0[MAP_COORD_MAX], b1[MAP_COORD_MAX];
    if (!strcmp(verb, "areas") || !*rest) {
        if (!m->nareas) { app_set_status_gm(a, "no named areas - v a box, then :area NAME"); return; }
        int off = snprintf(msg, sizeof msg, "areas:");
        for (int i = 0; i < m->nareas && off < (int)sizeof msg - 48; i++) {
            map_coord_name(m->areas[i].x0, m->areas[i].y0, b0, sizeof b0);
            map_coord_name(m->areas[i].x1, m->areas[i].y1, b1, sizeof b1);
            off += snprintf(msg + off, sizeof msg - (size_t)off, "%s %s %s:%s", i ? "," : "", m->areas[i].name, b0, b1);
        }
        app_set_status_gm(a, msg);
        return;
    }
    char name[AREA_NAME_MAX + 8];
    str_lcpy(name, rest, sizeof name);
    if (str_cut_word(name, "off")) {
        snprintf(msg, sizeof msg, ":area %.40s remove takes the name off", name);
        app_set_status_gm(a, msg);
        return;
    }
    int off = str_cut_word(name, "remove");
    if (!map_area_name_ok(name)) {
        snprintf(msg, sizeof msg, "an area's name is 1-%d characters, no quote or colon, and not a square", AREA_NAME_MAX - 1);
        app_set_status_gm(a, msg);
        return;
    }
    int ai = map_area_find(m, name);
    if (off) {
        if (ai < 0) { snprintf(msg, sizeof msg, "no area called %.40s", name); app_set_status_gm(a, msg); return; }
        undo_begin(&a->undo);
        undo_remove_area(&a->undo, m, name);
        undo_end(&a->undo);
        snprintf(msg, sizeof msg, "%.40s is no longer named", name);
        app_note_gm(a, msg);
        return;
    }
    int bx0, by0, bx1, by1;
    if (app_cmd_vbox(a, &bx0, &by0, &bx1, &by1)) {
        const char *fwhy = floor_problem_box(m, ai, bx0, by0, bx1, by1);
        if (fwhy) {
            snprintf(msg, sizeof msg, "%.40s is a floor, and %s", m->areas[ai].name, fwhy);
            app_set_status_gm(a, msg);
            return;
        }
        undo_begin(&a->undo);
        int ok = undo_set_area(&a->undo, m, name, bx0, by0, bx1, by1);
        undo_end(&a->undo);
        if (!ok) { snprintf(msg, sizeof msg, "a map holds %d named areas", MAP_AREAS_MAX); app_set_status_gm(a, msg); return; }
        ai = map_area_find(m, name);
        map_coord_name(m->areas[ai].x0, m->areas[ai].y0, b0, sizeof b0);
        map_coord_name(m->areas[ai].x1, m->areas[ai].y1, b1, sizeof b1);
        snprintf(msg, sizeof msg, "%.40s is %s:%s", m->areas[ai].name, b0, b1);
        app_note_gm(a, msg);
        return;
    }
    if (ai < 0) {
        snprintf(msg, sizeof msg, "no area called %.40s - in build mode, v a box and :area %.40s names it", name, name);
        app_set_status_gm(a, msg);
        return;
    }
    if (a->play.grabbed) { app_set_status_gm(a, "put the creature down before jumping"); return; }
    a->ed.cx = m->areas[ai].x0;
    a->ed.cy = m->areas[ai].y0;
    grid_center_on(&a->ed.view, m, (m->areas[ai].x0 + m->areas[ai].x1) / 2, (m->areas[ai].y0 + m->areas[ai].y1) / 2);
    snprintf(msg, sizeof msg, "jumped to %.40s", m->areas[ai].name);
    app_set_status_gm(a, msg);
}

/* ------------------------------------------------------ the command table */

static void cmd_w(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
    char path[MAP_PATH_MAX];
    if (rest[0]) mapio_resolve_path(rest, path, sizeof path);
    else if (m->path[0]) str_lcpy(path, m->path, sizeof path);
    else mapio_resolve_path(m->name, path, sizeof path);
    a->save_force = verb[strlen(verb) - 1] == '!';
    app_save_map(a, path);
    a->save_force = 0;
}

static void cmd_wq(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
    char path[MAP_PATH_MAX];
    if (m->path[0]) str_lcpy(path, m->path, sizeof path);
    else mapio_resolve_path(m->name, path, sizeof path);
    if (app_save_map(a, path) == 0) app_close_map(a);
}

static void cmd_q(App *a, const char *verb, const char *rest)
{
    app_leave_map(a);
}

static void cmd_q_bang(App *a, const char *verb, const char *rest)
{
    app_set_status(a, "closed without saving");
    app_close_map(a);
}

static void cmd_e(App *a, const char *verb, const char *rest)
{
    char path[MAP_PATH_MAX];
    /* Alone, it opens this map's file again: what is on the disk, in place
     * of what is here (asking first when there is unsaved work). */
    if (!rest[0]) {
        if (!a->map->path[0]) { app_set_status(a, ":e alone reads this map's file again, and it has none - :e NAME opens one"); return; }
        str_lcpy(path, a->map->path, sizeof path);
    }
    else mapio_resolve_path(rest, path, sizeof path);
    app_leave_map_for(a, path);
}

static void cmd_play(App *a, const char *verb, const char *rest)
{
    a->screen = SCREEN_PLAY;   app_set_status(a, "play mode");
}

static void cmd_build(App *a, const char *verb, const char *rest)
{
    a->screen = SCREEN_EDITOR; app_set_status(a, "build mode");
}

static void cmd_name(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
    if (!rest[0]) { app_set_status(a, ":name needs a value"); return; }
    str_lcpy(m->name, rest, sizeof m->name);
    map_touch(m);
    app_set_status(a, "renamed");
}

static void cmd_resize(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
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
}

static void cmd_scale(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
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
}

static void cmd_metric(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
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
}

static void cmd_ruleset(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
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
}

static void cmd_roll(App *a, const char *verb, const char *rest)
{
    roll_command(a, rest);
}

static void cmd_rolls(App *a, const char *verb, const char *rest)
{
    rolls_list(a);
}

static void cmd_turns(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
    /* Bare, it reads the order out; "end" ends the fight. */
    char msg[160];
    if (!*rest) {
        turn_list(m, msg, sizeof msg);
        app_set_status(a, msg);
        return;
    }
    if (strcmp(rest, "end") != 0) { app_set_status(a, ":turns lists the order, :turns end ends the fight"); return; }
    if (turn_count(m) == 0 && turn_acting(m) < 0) { app_set_status(a, "there is no fight to end"); return; }
    int had = turn_clear(m, &a->undo);
    snprintf(msg, sizeof msg, "the fight is over - %d left the turn order", had);
    app_note(a, msg);
}

static void cmd_notes(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
    /* Where the notes are, not what they say: this line is mirrored. */
    char msg[200];
    int  off = 0, n = 0;
    for (int i = 0; i < m->tokens.n && off < (int)sizeof msg - 28; i++) {
        const Token *t = &m->tokens.v[i];
        if (!t->note[0]) continue;
        off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%.16s", n++ ? ", " : "notes on ",
                        token_name(t));
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
}

static void cmd_fog(App *a, const char *verb, const char *rest)
{
    fog_command(a, rest);
}

static void cmd_clock(App *a, const char *verb, const char *rest)
{
    clock_command(a, rest);
}

static void cmd_tick(App *a, const char *verb, const char *rest)
{
    tick_command(a, rest);
}

static void cmd_player(App *a, const char *verb, const char *rest)
{
    /* :player preview -- the players' view on the GM's own screen. */
    if (!strncmp(rest, "floor ", 6)) { app_players_pin(a, rest + 6); return; }
    if (!strcmp(rest, "camera") || !strncmp(rest, "camera ", 7)) {
        app_players_camera_command(a, rest[6] ? rest + 7 : "");
        return;
    }
    if (strcmp(rest, "preview") != 0) {
        app_set_status(a, ":player preview shows what the players see; q returns   :player floor NAME|auto   :player camera follow|party|hold");
        return;
    }
    if (a->screen != SCREEN_PLAY) { app_set_status(a, "the players' view is play mode's - F2 first"); return; }
    a->preview = !a->preview;
    app_set_status(a, a->preview ? "previewing the players' view - q returns to yours"
                                 : "back to the GM's view");
}

static void cmd_serve(App *a, const char *verb, const char *rest)
{
    serve_command(a, rest);
}

static void cmd_agent(App *a, const char *verb, const char *rest)
{
    app_agent_command(a, rest);
}

static void cmd_character(App *a, const char *verb, const char *rest)
{
    app_character_command(a, rest);
}

static void cmd_handout(App *a, const char *verb, const char *rest)
{
    app_handout_command(a, rest);
}

static void cmd_scene(App *a, const char *verb, const char *rest)
{
    app_scene_command(a, verb, rest);
}

static void cmd_stamp(App *a, const char *verb, const char *rest)
{
    app_stamp_command(a, rest);
}

static void cmd_link(App *a, const char *verb, const char *rest)
{
    app_link_command(a, rest);
}

static void cmd_floor(App *a, const char *verb, const char *rest)
{
    app_floor_command(a, verb, rest);
}

static void cmd_ask(App *a, const char *verb, const char *rest)    { app_ask_command(a, verb, rest); }
static void cmd_jobs(App *a, const char *verb, const char *rest)   { app_jobs_command(a, rest); }
static void cmd_review(App *a, const char *verb, const char *rest) { app_review_command(a, rest); }

static void cmd_area(App *a, const char *verb, const char *rest)
{
    area_command(a, verb, rest);
}

static void cmd_mirror(App *a, const char *verb, const char *rest)
{
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
}

static void cmd_panel(App *a, const char *verb, const char *rest)
{
    Play *pl = &a->play;
    if (!*rest)                    pl->panel = !pl->panel;
    else if (!strcmp(rest, "on"))  pl->panel = 1;
    else if (!strcmp(rest, "off")) pl->panel = 0;
    else { app_set_status(a, ":panel on, :panel off, or :panel to toggle"); return; }
    app_set_status(a, pl->panel ? "turn panel on - it shows when there is a fight"
                                : "turn panel off");
}

/* :card       the selected creature's card (else the one under the cursor), whole
 * :card on    the box beside the map shows it (the default)
 * :card off   the box does not; a setting, nothing is lost */
static void cmd_card(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
    if (!strcmp(rest, "on") || !strcmp(rest, "off")) {
        a->card_box_off = !strcmp(rest, "off");
        app_set_status(a, a->card_box_off ? "card box off - :card still shows a card whole"
                                           : "card box on - the selected creature's card shows beside the map");
        return;
    }
    if (*rest) { app_set_status(a, ":card, :card on, or :card off"); return; }
    int i = app_target_token(a);
    if (i < 0) { app_set_status(a, "no creature here - :card shows the selected one's card"); return; }
    if (!card_of(m, &m->tokens.v[i])) {
        char msg[96];
        snprintf(msg, sizeof msg, "%.30s has no card - s k writes one", token_name(&m->tokens.v[i]));
        app_set_status_gm(a, msg);
        return;
    }
    a->card_token = i;
    a->card_top   = 0;
    a->modal      = MODAL_CARD;
}

static void cmd_whisper(App *a, const char *verb, const char *rest)
{
    app_whisper_command(a, rest);
}

static void cmd_players(App *a, const char *verb, const char *rest)
{
    if (*rest) { app_set_status(a, ":players lists who is watching"); return; }
    app_players_command(a);
}

static void cmd_dmg(App *a, const char *verb, const char *rest)
{
    app_damage_command(a, rest);
}

static void cmd_log(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
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
}

static void cmd_hidden(App *a, const char *verb, const char *rest)
{
    /* Who the players cannot see, and where: the GM's line alone. */
    Map *m = a->map;
    char msg[sizeof a->status];
    int  off = 0, n = 0, shown = 0;
    for (int i = 0; i < m->tokens.n; i++) n += m->tokens.v[i].hidden;
    if (!n) { app_set_status_gm(a, "nothing is hidden - s h hides the creature under the cursor"); return; }
    off = snprintf(msg, sizeof msg, "hidden:");
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        if (!t->hidden) continue;
        char at[MAP_COORD_MAX], one[64];
        map_coord_name(t->x, t->y, at, sizeof at);
        int w = snprintf(one, sizeof one, "%s %.24s %s", shown ? "," : "", token_name(t), at);
        if (off + w + 16 >= (int)sizeof msg) break;
        memcpy(msg + off, one, (size_t)w + 1);
        off += w;
        shown++;
    }
    if (shown < n) snprintf(msg + off, sizeof msg - (size_t)off, " ... %d more", n - shown);
    app_set_status_gm(a, msg);
}

static void cmd_zoom(App *a, const char *verb, const char *rest)
{
    Map *m = a->map;
    int z = atoi(rest);
    ed_set_zoom(&a->ed, m, z);
}

typedef void CmdFn(App *a, const char *verb, const char *rest);

/* Every : command by name -- a second name where a verb has another spelling
 * (:w and :write) or a plural that lists (:links). A word that is not here
 * may still be a square to jump to. */
static const struct {
    const char *name, *also;
    CmdFn      *fn;
} COMMANDS[] = {
    { "w", "write", cmd_w },
    { "w!", NULL, cmd_w },
    { "wq", "x", cmd_wq },
    { "q", "quit", cmd_q },
    { "q!", NULL, cmd_q_bang },
    { "ask", "ask!", cmd_ask },
    { "jobs", NULL, cmd_jobs },
    { "review", NULL, cmd_review },
    { "e", "edit", cmd_e },
    { "play", NULL, cmd_play },
    { "build", NULL, cmd_build },
    { "name", NULL, cmd_name },
    { "resize", NULL, cmd_resize },
    { "scale", NULL, cmd_scale },
    { "metric", NULL, cmd_metric },
    { "ruleset", NULL, cmd_ruleset },
    { "roll", NULL, cmd_roll },
    { "rolls", NULL, cmd_rolls },
    { "turns", NULL, cmd_turns },
    { "notes", NULL, cmd_notes },
    { "fog", NULL, cmd_fog },
    { "clock", NULL, cmd_clock },
    { "tick", NULL, cmd_tick },
    { "player", NULL, cmd_player },
    { "players", NULL, cmd_players },
    { "whisper", NULL, cmd_whisper },
    { "serve", NULL, cmd_serve },
    { "agent", NULL, cmd_agent },
    { "stamp", NULL, cmd_stamp },
    { "character", "characters", cmd_character },
    { "scene", "scenes", cmd_scene },
    { "handout", "handouts", cmd_handout },
    { "link", "links", cmd_link },
    { "floor", "floors", cmd_floor },
    { "area", "areas", cmd_area },
    { "mirror", NULL, cmd_mirror },
    { "panel", NULL, cmd_panel },
    { "card", NULL, cmd_card },
    { "dmg", "damage", cmd_dmg },
    { "log", NULL, cmd_log },
    { "zoom", NULL, cmd_zoom },
    { "hidden", NULL, cmd_hidden },
};

void app_exec_command(App *a, const char *line)
{
    while (*line == ' ') line++;
    if (!*line) return;

    char verb[32] = { 0 };
    int  consumed = 0;
    sscanf(line, "%31s %n", verb, &consumed);
    const char *rest = consumed > 0 ? line + consumed : "";
    while (*rest == ' ') rest++;

    for (size_t i = 0; i < sizeof COMMANDS / sizeof *COMMANDS; i++)
        if (!strcmp(verb, COMMANDS[i].name) || (COMMANDS[i].also && !strcmp(verb, COMMANDS[i].also))) {
            COMMANDS[i].fn(a, verb, rest);
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
        /* Centered rather than merely scrolled into view: a jump is for going
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

    int back = a->ed.cmd_from_stamp, review = a->ed.cmd_from_review;
    a->ed.cmd_from_stamp = a->ed.cmd_from_review = 0;
    a->ed.mode = ED_NORMAL;
    if (r == 1) app_exec_command(a, a->ed.cmd.buf);
    else        app_set_status(a, "");
    a->ed.cmd_from_visual = 0;
    if (back && a->stamp && a->map && a->screen == SCREEN_EDITOR && a->ed.mode == ED_NORMAL)
        a->ed.mode = ED_STAMP;
    if (review) app_review_resume(a);
}
