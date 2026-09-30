/* The picker: one list to choose from with type-to-filter and tab
 * completion (ui.c's UiPicker), for the character templates (i t e, i t p,
 * :character), the stamps (:stamp), the scenes (:scene) and the handouts
 * (:handout). What each list holds and what choosing does are here. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "card.h"
#include "character.h"
#include "prof.h"
#include "scene.h"
#include "stamp.h"
#include "store.h"

/* "Crypt Ghoul  enemy 2x2  HP 12, Stress 3" */
static void character_detail(const Map *c, char *buf, size_t sz)
{
    const Token *t = character_token(c);
    /* Its card's first line says more than its label: "Acid Burrower -
     * Tier 1 Solo". The picker filters on it too, so "solo" finds them. */
    char what[TOKEN_LABEL_MAX + 64];
    const char *card = card_of(c, t);
    if (card) card_first_line(card, what, sizeof what);
    else      str_lcpy(what, t->label, sizeof what);
    int off = snprintf(buf, sz, "%s%s%s %dx%d", what, what[0] ? "  " : "",
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

/* Every saved one, each read for what the picker says of it; a map's scenes
 * are in hand already. */
static UiPickItem *pick_items(const App *a, PickWhat what, int *count)
{
    PROF_ZONE("picker.open");
    if (what == PICK_HANDOUT) return app_handout_items(count);
    if (what == PICK_SCENE) {
        const Map *m = a->map;
        *count = m ? m->nscenes : 0;
        if (!*count) return NULL;
        UiPickItem *items = xmalloc(sizeof *items * (size_t)*count);
        for (int i = 0; i < *count; i++) {
            str_lcpy(items[i].name, m->scenes[i].name, sizeof items[i].name);
            scene_describe(m, i, items[i].detail, sizeof items[i].detail);
        }
        return items;
    }
    char dir[MAP_PATH_MAX];
    if (what == PICK_CHARACTER) character_dir(dir, sizeof dir);
    else                        stamp_dir(dir, sizeof dir);
    int n;
    char (*names)[MAP_NAME_MAX] = store_list_all(dir, ".vtt", &n);
    *count = 0;
    if (n <= 0) return NULL;
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
    UiPickItem *items = pick_items(a, what, &n);
    if (!n) {
        if (what == PICK_SCENE) app_set_status_gm(a, "no scenes - :scene save NAME keeps the creatures as they stand");
        else if (what == PICK_HANDOUT) {
            char dir[MAP_PATH_MAX], msg[MAP_PATH_MAX + 64];
            store_dir("handouts", dir, sizeof dir);
            snprintf(msg, sizeof msg, "no handouts - write NAME.txt in %.200s, or :handout say TEXT", dir);
            app_set_status_gm(a, msg);
        }
        else app_set_status(a, what == PICK_CHARACTER
            ? "no characters saved yet - :character save keeps the creature under the cursor"
            : "no stamps yet - y copies, :stamp save NAME keeps it");
        return;
    }
    char title[64];
    if (what == PICK_STAMP)      snprintf(title, sizeof title, "Stamp");
    else if (what == PICK_SCENE) snprintf(title, sizeof title, "Scene to put back");
    else if (what == PICK_HANDOUT) snprintf(title, sizeof title, "Handout to show the players");
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
    else if (a->pick_what == PICK_SCENE) { if (a->map) app_scene_restore(a, name); }
    else if (a->pick_what == PICK_HANDOUT) app_handout_show(a, name);
    else if (a->map && a->screen == SCREEN_PLAY) app_character_place(a, name, a->pick_kind);
}

