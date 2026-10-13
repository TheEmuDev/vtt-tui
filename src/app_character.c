/* Character templates in play mode (docs/CHARACTERS.md): :character, and
 * putting one down from the picker (app_picker.c). The templates themselves
 * -- files, saving, placing -- are character.c's. */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "character.h"
#include "floor.h"
#include "prof.h"

/* Puts the named character down at the cursor. */
void app_character_place(App *a, const char *name, int kind)
{
    char err[160], said[160], msg[320], at[MAP_COORD_MAX];
    Map *c = character_load(name, err, sizeof err);
    if (!c) { app_set_status(a, err); return; }
    int idx = character_place(a->world.map, &a->world.undo, c, kind, a->ed.cx, a->ed.cy, 0,
                              said, sizeof said, err, sizeof err);
    map_free(c);
    if (idx < 0) { app_set_status(a, err); return; }
    play_focus(&a->play, idx);
    const Token *t = &a->world.map->tokens.v[idx];
    map_coord_name(t->x, t->y, at, sizeof at);
    snprintf(msg, sizeof msg, "placed %s %.30s (%dx%d) at %s from %.40s%s%s",
             token_kind_name(t->kind), t->label[0] ? t->label : "unlabeled",
             t->size, t->size, at, name, said[0] ? " - " : "", said);
    app_note_gm(a, msg);        /* the template's name is the GM's bookkeeping */
}

/* ------------------------------------------------------------ :character */

/* The creature under the cursor, else the selected one: placing selects
 * what it placed, and the GM saving means the one they are pointing at. */
static int target(App *a)
{
    int i = app_target_token_under(a);
    if (i >= 0) return i;
    Play *pl = &a->play;
    return pl->sel >= 0 && pl->sel < a->world.map->tokens.n ? pl->sel : -1;
}

/* :character save [NAME [ROLL...]]  keep the creature under the cursor
 * :character [NAME]                 the picker, NAME typed into it */
void app_character_command(App *a, const char *rest)
{
    char msg[256], err[160];
    if (a->screen != SCREEN_PLAY || !a->world.map) { app_set_status(a, "characters are play mode's - F2 first"); return; }

    char w[10][MAP_NAME_MAX + 8];
    int  nw = 0;
    const char *p = rest;
    while (nw < 10) {
        while (*p == ' ') p++;
        if (!*p) break;
        int k = 0;
        while (*p && *p != ' ') { if (k + 1 < (int)sizeof w[0]) w[nw][k++] = *p; p++; }
        w[nw++][k] = '\0';
    }
    while (*p == ' ') p++;

    if (nw && !strcmp(w[0], "save")) {
        if (*p) { app_set_status(a, "a character carries at most eight rolls"); return; }
        int idx = target(a);
        if (idx < 0) { app_set_status(a, "no creature here to save - put the cursor on one"); return; }
        char name[MAP_NAME_MAX];
        if (nw >= 2 && strlen(w[1]) >= sizeof name) {
            snprintf(msg, sizeof msg, "a character's name is under %d characters", MAP_NAME_MAX);
            app_set_status(a, msg);
            return;
        }
        if (nw >= 2) str_lcpy(name, w[1], sizeof name);
        else character_name_from_label(a->world.map->tokens.v[idx].label, name, sizeof name);
        if (!name[0]) { app_set_status(a, "it has no label to name it by - :character save NAME"); return; }
        const char *rolls[8];
        for (int i = 2; i < nw; i++) rolls[i - 2] = w[i];
        if (character_save(a->world.map, idx, name, rolls, nw > 2 ? nw - 2 : 0, err, sizeof err) < 0) {
            app_set_status(a, err);
            return;
        }
        snprintf(msg, sizeof msg, "saved %.30s as character %s%s - i t e places it",
                 a->world.map->tokens.v[idx].label[0] ? a->world.map->tokens.v[idx].label : "the creature",
                 name, nw > 2 ? " with its rolls" : "");
        app_note(a, msg);
        return;
    }
    if (nw > 1) { app_set_status(a, ":character, :character NAME, :character save [NAME [ROLL...]]"); return; }
    app_pick_open(a, PICK_CHARACTER, -1, nw ? w[0] : "");
}

/* ---------------------------------------------------------------- :summon */

/* A band named as the GM types it: case and spaces aside, so "veryclose"
 * and "Very Close" are one. -1 for none. */
static int band_named(const Map *m, const char *text)
{
    const char *name = NULL;
    int n = range_bands(m, 0, &name);
    for (int i = 0; i < n; i++) {
        range_bands(m, i, &name);
        const char *p = text, *q = name;
        for (;;) {
            while (*p == ' ') p++;
            while (*q == ' ') q++;
            if (!*p || !*q || tolower((unsigned char)*p) != tolower((unsigned char)*q)) break;
            p++, q++;
        }
        while (*p == ' ') p++;
        if (!*p && !*q) return i;
    }
    return -1;
}

/* "Melee, Very Close, Close ..." for the hint a wrong band gets. */
static void band_list(const Map *m, char *buf, size_t sz)
{
    const char *name = NULL;
    int n = range_bands(m, 0, &name), off = 0;
    buf[0] = '\0';
    for (int i = 0; i < n && off < (int)sz - 1; i++) {
        range_bands(m, i, &name);
        off += snprintf(buf + off, sz - (size_t)off, "%s%s", i ? ", " : "", name);
    }
}

void app_summon_end(App *a)
{
    Summon *s = &a->summon;
    if (!s->tpl) return;
    map_free(s->tpl);
    s->tpl = NULL;
    a->play.next_size = s->saved_size;
    a->play.range     = s->saved_range;
}

void app_summon_status(const App *a, char *buf, size_t bufsz)
{
    const Summon *s  = &a->summon;
    const Play   *pl = &a->play;
    const Token  *t  = character_token(s->tpl);
    const Token  *from = pl->range.token >= 0 && pl->range.token < a->world.map->tokens.n
                       ? &a->world.map->tokens.v[pl->range.token] : NULL;
    char count[48];
    if (s->any) snprintf(count, sizeof count, "%d placed", s->placed);
    else        snprintf(count, sizeof count, "%d of %d to place", s->left, s->left + s->placed);
    snprintf(buf, bufsz, "SUMMONING  %.30s %dx%d from %.30s  %s%s",
             t->label[0] ? t->label : s->name, t->size, t->size,
             from ? token_name(from) : "here", count, s->hidden ? "  hidden" : "");
}

/* :summon [hidden] [N|any] TEMPLATE [BAND]
 * Lights the band round the creature the cursor means and hands the GM the
 * template to put down inside it (docs/SUMMON.md). */
void app_summon_command(App *a, const char *rest)
{
    static const char USAGE[] = ":summon [hidden] [N|any] TEMPLATE [BAND] - :summon 3 lackey far";
    char msg[256], err[160];
    if (a->screen != SCREEN_PLAY || !a->world.map) { app_set_status(a, "summoning is play mode's - F2 first"); return; }
    Map  *m  = a->world.map;
    Play *pl = &a->play;
    if (app_summoning(a)) { app_set_status(a, "already summoning - esc stops it"); return; }
    if (pl->grabbed || pl->visual || a->ruler.active) {
        app_set_status(a, "esc first - put down what the cursor is holding");
        return;
    }

    char w[4][MAP_NAME_MAX + 8];
    int  nw = 0;
    const char *p = rest;
    while (nw < 4) {
        while (*p == ' ') p++;
        if (!*p) break;
        int k = 0;
        while (*p && *p != ' ') { if (k + 1 < (int)sizeof w[0]) w[nw][k++] = *p; p++; }
        w[nw++][k] = '\0';
    }
    int at = 0, hidden = 0, any = 0, count = 1;
    if (at < nw && !strcmp(w[at], "hidden")) { hidden = 1; at++; }
    /* A count only when a name follows it: a template may be called 3. */
    if (at + 1 < nw && !strcmp(w[at], "any")) { any = 1; at++; }
    else if (at + 1 < nw && w[at][strspn(w[at], "0123456789")] == '\0') {
        count = atoi(w[at]);
        if (count < 1 || count > PLAY_GROUP_MAX) {
            snprintf(msg, sizeof msg, "summon 1 to %d at a time, or any", PLAY_GROUP_MAX);
            app_set_status(a, msg);
            return;
        }
        at++;
    }
    if (at >= nw) { app_set_status(a, USAGE); return; }
    const char *name = w[at++];

    /* What is left is the band: the words still in w, then the line's rest. */
    char band[64] = "";
    for (int i = at; i < nw; i++) {
        size_t off = strlen(band);
        snprintf(band + off, sizeof band - off, "%s%s", off ? " " : "", w[i]);
    }
    while (*p == ' ') p++;
    if (*p) {
        size_t off = strlen(band);
        snprintf(band + off, sizeof band - off, "%s%s", off ? " " : "", p);
    }

    int from = app_target_token(a);
    if (from < 0) { app_set_status(a, "no creature here to summon from - put the cursor on one"); return; }

    int reach;
    if (range_bands(m, 0, NULL)) {
        reach = band[0] ? band_named(m, band) : 0;
        if (reach < 0) {
            char names[128];
            band_list(m, names, sizeof names);
            snprintf(msg, sizeof msg, "no band called %.20s - %s", band, names);
            app_set_status(a, msg);
            return;
        }
    } else {
        /* No bands: a distance in the map's units, in whole squares. */
        char *end = NULL;
        double units = band[0] ? strtod(band, &end) : m->scale_ft;
        if (band[0] && (*end || !(units > 0) || !isfinite(units))) {
            snprintf(msg, sizeof msg, "no ruleset, so the range is a distance: :summon %.20s 30", name);
            app_set_status(a, msg);
            return;
        }
        double squares = units / m->scale_ft;
        reach = squares >= RANGE_RADIUS_MAX ? RANGE_RADIUS_MAX : squares < 1 ? 1 : (int)squares;
    }

    Map *tpl = character_load(name, err, sizeof err);
    if (!tpl) { app_set_status(a, err); return; }

    Summon *s = &a->summon;
    memset(s, 0, sizeof *s);
    s->tpl    = tpl;
    str_lcpy(s->name, name, sizeof s->name);
    s->left   = any ? 0 : count;
    s->any    = any;
    s->hidden = hidden;
    s->stamp  = a->world.undo.stamp;
    s->saved_size  = pl->next_size;
    s->saved_range = pl->range;
    pl->next_size  = character_token(tpl)->size;
    range_circle(&pl->range, m, from, reach);
    app_set_status(a, "");      /* the mode's line and its bar say the rest */
}

/* Why the template may not go down at the cursor, or NULL when it may. */
static const char *summon_blocked(const App *a)
{
    const Map   *m    = a->world.map;
    const Play  *pl   = &a->play;
    int          size = character_token(a->summon.tpl)->size;
    const Token *from = &m->tokens.v[pl->range.token];
    int          fl   = floor_at(m, from->x, from->y);
    if (a->ed.cx + size > m->w || a->ed.cy + size > m->h) return "off the map";
    for (int y = a->ed.cy; y < a->ed.cy + size; y++)
        for (int x = a->ed.cx; x < a->ed.cx + size; x++) {
            if (!map_walkable(m, x, y))                           return "off the map";
            if (!range_contains(&pl->range, m, x, y))             return "outside the range";
            if (floor_at(m, x, y) != fl)                          return "on another floor";
        }
    if (!play_can_place(m, a->ed.cx, a->ed.cy, size, -1)) return "a creature is in the way";
    return NULL;
}

static void summon_place(App *a)
{
    PROF_ZONE("summon.place");
    Summon *s = &a->summon;
    Map    *m = a->world.map;
    char msg[320], said[160], err[160], at[MAP_COORD_MAX];

    const char *why = summon_blocked(a);
    if (why) {
        snprintf(msg, sizeof msg, "not here: %s", why);
        app_set_status_gm(a, msg);      /* where the GM is trying is the GM's */
        return;
    }
    int idx = character_place(m, &a->world.undo, s->tpl, -1, a->ed.cx, a->ed.cy, s->hidden,
                              said, sizeof said, err, sizeof err);
    if (idx < 0) { app_set_status_gm(a, err); return; }
    s->stamp = a->world.undo.stamp;
    s->placed++;
    if (!s->any) s->left--;

    const Token *t = &m->tokens.v[idx];
    map_coord_name(t->x, t->y, at, sizeof at);
    snprintf(msg, sizeof msg, "summoned %s %.30s at %s%s%s%s",
             token_kind_name(t->kind), token_name(t), at, s->hidden ? ", hidden" : "",
             said[0] ? " - " : "", said);
    app_note_gm(a, msg);        /* it names a creature, and it may be hidden */
    if (!s->any && s->left == 0) app_summon_end(a);
}

void app_summon_key(App *a, Key k)
{
    Summon *s = &a->summon;
    char msg[96];
    a->ed.count = 0;
    if (k.kind == KEY_ENTER) { summon_place(a); return; }
    if (k.kind == KEY_ESC) {
        int placed = s->placed;
        app_summon_end(a);
        snprintf(msg, sizeof msg, placed ? "summoning stopped - %d placed" : "summoning canceled", placed);
        app_set_status_gm(a, msg);      /* a count of creatures, some perhaps hidden */
        return;
    }
    if (k.kind == KEY_CHAR && k.mods == 0 && k.ch == 'u') {
        /* Only the summon's own: the log must be as the last placement left it. */
        if (!s->placed || a->world.undo.stamp != s->stamp || !undo_undo(&a->world.undo, a->world.map)) {
            app_set_status_gm(a, "nothing of this summon to take back");
            return;
        }
        s->stamp = a->world.undo.stamp;
        s->placed--;
        if (!s->any) s->left++;
        app_note_gm(a, "took one back");
        return;
    }
    app_set_status_gm(a, "summoning: enter places, u takes one back, esc stops");
}
