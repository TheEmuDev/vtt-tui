/* Character templates in play mode (docs/CHARACTERS.md): :character, and
 * putting one down from the picker (app_picker.c). The templates themselves
 * -- files, saving, placing -- are character.c's. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "character.h"

/* Puts the named character down at the cursor. */
void app_character_place(App *a, const char *name, int kind)
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
    app_note_gm(a, msg);        /* the template's name is the GM's bookkeeping */
}

/* ------------------------------------------------------------ :character */

/* The creature under the cursor, else the selected one: placing selects
 * what it placed, and the GM saving means the one they are pointing at. */
static int target(App *a)
{
    int i = app_token_under_cursor(a);
    if (i >= 0) return i;
    Play *pl = &a->play;
    return pl->sel >= 0 && pl->sel < a->map->tokens.n ? pl->sel : -1;
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
        if (nw >= 2 && strlen(w[1]) >= sizeof name) {
            snprintf(msg, sizeof msg, "a character's name is under %d characters", MAP_NAME_MAX);
            app_set_status(a, msg);
            return;
        }
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
