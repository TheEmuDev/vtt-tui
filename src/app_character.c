/* Character templates in play mode (docs/CHARACTERS.md): i t e and i t p,
 * :character, and the picker both it and :stamp choose from. The templates
 * themselves -- files, saving, placing -- are character.c's. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "character.h"
#include "prof.h"
#include "stamp.h"

/* ---------------------------------------------------------------- picker */

/* "Crypt Ghoul  enemy 2x2  HP 12, Stress 3" */
static void character_detail(const Map *c, char *buf, size_t sz)
{
    const Token *t = character_token(c);
    int off = snprintf(buf, sz, "%s%s%s %dx%d", t->label, t->label[0] ? "  " : "",
                       token_kind_name(t->kind), t->size, t->size);
    for (int i = 0; i < t->ncounters && off > 0 && (size_t)off < sz; i++)
        off += snprintf(buf + off, sz - (size_t)off, "%s%s %d", i ? ", " : "  ",
                        t->counters[i].name, t->counters[i].max);
}

/* "3x3, 2 creatures" */
static void stamp_detail(const Map *s, char *buf, size_t sz)
{
    int n = s->tokens.n;
    snprintf(buf, sz, "%dx%d%s%d%s", s->w, s->h, n ? ", " : "", n,
             n == 1 ? " creature" : n ? " creatures" : "");
    if (!n) snprintf(buf, sz, "%dx%d", s->w, s->h);
}

/* Every saved one, each read for what the picker says of it. */
static UiPickItem *pick_items(PickWhat what, int *count)
{
    PROF_ZONE("picker.open");
    int n = what == PICK_CHARACTER ? character_list(NULL, 0) : stamp_list(NULL, 0);
    *count = 0;
    if (n <= 0) return NULL;
    char (*names)[MAP_NAME_MAX] = xmalloc((size_t)n * MAP_NAME_MAX);
    n = what == PICK_CHARACTER ? character_list(names, n) : stamp_list(names, n);
    UiPickItem *items = xmalloc(sizeof *items * (size_t)n);
    for (int i = 0; i < n; i++) {
        char err[160];
        str_lcpy(items[i].name, names[i], sizeof items[i].name);
        Map *m = what == PICK_CHARACTER ? character_load(names[i], err, sizeof err)
                                        : stamp_load(names[i], err, sizeof err);
        if (!m) str_lcpy(items[i].detail, "cannot be read", sizeof items[i].detail);
        else if (what == PICK_CHARACTER) character_detail(m, items[i].detail, sizeof items[i].detail);
        else stamp_detail(m, items[i].detail, sizeof items[i].detail);
        map_free(m);
    }
    free(names);
    *count = n;
    return items;
}

void app_pick_open(App *a, PickWhat what, int kind, const char *initial)
{
    int n;
    UiPickItem *items = pick_items(what, &n);
    if (!n) {
        app_set_status(a, what == PICK_CHARACTER
            ? "no characters saved yet - :character save keeps the creature under the cursor"
            : "no stamps yet - y copies, :stamp save NAME keeps it");
        return;
    }
    char title[64];
    if (what == PICK_STAMP) snprintf(title, sizeof title, "Stamp");
    else if (kind < 0)      snprintf(title, sizeof title, "Character");
    else                    snprintf(title, sizeof title, "Character, as %s %s",
                                     kind == TOKEN_ENEMY ? "an" : "a", token_kind_name((uint8_t)kind));
    ui_picker_open(&a->picker, title, items, n, initial);
    a->pick_what = what;
    a->pick_kind = kind;
    a->modal     = MODAL_PICKER;
    a->dirty     = 1;
    app_set_status(a, "");
}

/* Puts the named character down at the cursor. */
static void place_character(App *a, const char *name, int kind)
{
    char err[160], said[160], msg[320], at[MAP_COORD_MAX];
    Map *c = character_load(name, err, sizeof err);
    if (!c) { app_set_status(a, err); return; }
    int idx = character_place(a->map, &a->undo, c, kind, a->ed.cx, a->ed.cy, 0,
                              said, sizeof said, err, sizeof err);
    map_free(c);
    if (idx < 0) { app_set_status(a, err); return; }
    play_focus(&a->play, idx);
    const Token *t = &a->map->tokens.v[idx];
    map_coord_name(t->x, t->y, at, sizeof at);
    snprintf(msg, sizeof msg, "placed %s %.30s (%dx%d) at %s from %.40s%s%s",
             token_kind_name(t->kind), t->label[0] ? t->label : "unlabeled",
             t->size, t->size, at, name, said[0] ? " - " : "", said);
    app_note(a, msg);
}

void app_pick_key(App *a, Key k)
{
    int r = ui_picker_key(&a->picker, k);
    a->dirty = 1;
    if (!r) return;
    int  i = ui_picker_chosen(&a->picker);
    char name[UI_PICK_NAME];
    str_lcpy(name, i >= 0 ? a->picker.items[i].name : "", sizeof name);
    a->modal = MODAL_NONE;
    ui_picker_free(&a->picker);
    if (r < 0) { app_set_status(a, "canceled"); return; }

    if (a->pick_what == PICK_STAMP) app_stamp_command(a, name);
    else if (a->map && a->screen == SCREEN_PLAY) place_character(a, name, a->pick_kind);
}

/* ------------------------------------------------------------ :character */

static int target(App *a)
{
    Play *pl = &a->play;
    if (pl->sel >= 0 && pl->sel < a->map->tokens.n) return pl->sel;
    return app_token_under_cursor(a);
}

/* :character save [NAME [ROLL...]]  keep the creature under the cursor
 * :character [NAME]                 the picker, NAME typed into it */
void app_character_command(App *a, const char *rest)
{
    char msg[256], err[160];
    if (a->screen != SCREEN_PLAY || !a->map) { app_set_status(a, "characters are play mode's - F2 first"); return; }

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
        if (nw >= 2) str_lcpy(name, w[1], sizeof name);
        else character_name_from_label(a->map->tokens.v[idx].label, name, sizeof name);
        if (!name[0]) { app_set_status(a, "it has no label to name it by - :character save NAME"); return; }
        const char *rolls[8];
        for (int i = 2; i < nw; i++) rolls[i - 2] = w[i];
        if (character_save(a->map, idx, name, rolls, nw > 2 ? nw - 2 : 0, err, sizeof err) < 0) {
            app_set_status(a, err);
            return;
        }
        snprintf(msg, sizeof msg, "saved %.30s as character %s%s - i t e places it",
                 a->map->tokens.v[idx].label[0] ? a->map->tokens.v[idx].label : "the creature",
                 name, nw > 2 ? " with its rolls" : "");
        app_note(a, msg);
        return;
    }
    if (nw > 1) { app_set_status(a, ":character, :character NAME, :character save [NAME [ROLL...]]"); return; }
    app_pick_open(a, PICK_CHARACTER, -1, nw ? w[0] : "");
}
