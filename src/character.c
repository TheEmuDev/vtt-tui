#include "character.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "card.h"
#include "mapio.h"
#include "play.h"
#include "prof.h"
#include "stamp.h"
#include "store.h"
#include "util.h"

void character_dir(char *buf, size_t sz)
{
    store_dir("characters", buf, sz);
}

void character_name_from_label(const char *label, char *out, size_t outsz)
{
    char root[TOKEN_LABEL_MAX];
    token_label_root(label, root, sizeof root);

    size_t k = 0;
    int    dash = 0;
    for (const char *p = root; *p && k + 1 < outsz && k + 1 < MAP_NAME_MAX; p++) {
        if (isalnum((unsigned char)*p)) {
            if (dash && k) out[k++] = '-';
            if (k + 1 < outsz) out[k++] = *p;
            dash = 0;
        } else {
            dash = 1;
        }
    }
    while (k > 0 && out[k - 1] == '-') k--;     /* the bound can cut after a dash */
    out[k] = '\0';
}

static int path_of(const char *name, char *buf, size_t sz)
{
    return store_path("characters", name, ".vtt", buf, sz);
}

/* What a template's creature is, apart from any one fight. */
static void fresh(Token *t)
{
    token_clear_status(t);
    t->turn   = 0;
    t->init   = 0;
    t->hidden = 0;
    t->x = t->y = 0;
    for (int i = 0; i < t->ncounters; i++) t->counters[i].value = t->counters[i].max;
}

static int roll_named(const Map *m, const char *name)
{
    for (int i = 0; i < ROLL_MAX; i++)
        if (m->rolls[i].name[0] && !strcasecmp(m->rolls[i].name, name)) return i;
    return -1;
}

int character_save(const Map *m, int idx, const char *name,
                   const char *const *rolls, int nrolls, char *err, size_t errsz)
{
    if (!store_name_ok(name)) { snprintf(err, errsz, "a character's name is letters, digits, - and _"); return -1; }
    if (idx < 0 || idx >= m->tokens.n) { snprintf(err, errsz, "no creature to save"); return -1; }
    Token t = m->tokens.v[idx];
    fresh(&t);
    /* Without its copy number: placing numbers it again, as a paste does. */
    char root[TOKEN_LABEL_MAX];
    token_label_root(t.label, root, sizeof root);
    str_lcpy(t.label, root, sizeof t.label);

    Map *c = map_new(t.size, t.size, name);
    for (int i = 0; i < t.size * t.size; i++) c->tiles[i] = TILE_FLOOR;
    /* Its card goes with it; a name with no card on this map, nowhere. */
    const char *card = card_of(m, &t);
    if (card) card_set(c, t.card, card);
    else      t.card[0] = '\0';
    tokens_add(&c->tokens, t);
    int kept = 0;
    for (int r = 0; r < nrolls; r++) {
        int i = roll_named(m, rolls[r]);
        if (i < 0) {
            snprintf(err, errsz, "no roll called %.20s on this map - :rolls lists them", rolls[r]);
            map_free(c);
            return -1;
        }
        if (roll_named(c, m->rolls[i].name) >= 0) continue;   /* named twice */
        c->rolls[kept++] = m->rolls[i];
    }

    char path[MAP_PATH_MAX], dir[MAP_PATH_MAX];
    int  rc = -1;
    if (!path_of(name, path, sizeof path)) snprintf(err, errsz, "the character's path is too long");
    else {
        character_dir(dir, sizeof dir);
        dir_make(dir);
        rc = mapio_write(c, path, err, errsz);
    }
    map_free(c);
    return rc;
}

Map *character_load(const char *name, char *err, size_t errsz)
{
    char path[MAP_PATH_MAX], why[160];
    if (!store_name_ok(name) || !path_of(name, path, sizeof path)) {
        snprintf(err, errsz, "no character called %.40s", name);
        return NULL;
    }
    Map *c = mapio_load(path, why, sizeof why);
    if (!c) { snprintf(err, errsz, "no character called %.40s", name); return NULL; }
    if (c->tokens.n != 1) {
        snprintf(err, errsz, "%.40s holds %d creatures - a character is one", name, c->tokens.n);
        map_free(c);
        return NULL;
    }
    fresh(&c->tokens.v[0]);
    return c;
}

int character_list(char (*names)[MAP_NAME_MAX], int max)
{
    char dir[MAP_PATH_MAX];
    character_dir(dir, sizeof dir);
    return store_list(dir, ".vtt", names, max);
}

int character_place(Map *m, Undo *u, const Map *tpl, int kind, int x, int y, int hidden,
                    char *said, size_t saidsz, char *err, size_t errsz)
{
    PROF_ZONE("character.place");
    said[0] = '\0';
    Token t = *character_token(tpl);
    if (!play_can_place(m, x, y, t.size, -1)) {
        char at[MAP_COORD_MAX];
        map_coord_name(x, y, at, sizeof at);
        snprintf(err, errsz, "no room for a %dx%d %.30s at %s%s", t.size, t.size,
                 t.label[0] ? t.label : "creature", at,
                 tokens_overlapping(&m->tokens, x, y, t.size, -1, TOKEN_ANY_KIND) >= 0
                     ? " - something is on it" : "");
        return -1;
    }
    t.x = (int16_t)x;
    t.y = (int16_t)y;
    if (kind >= 0) t.kind = (uint8_t)kind;
    t.hidden = hidden ? 1 : 0;
    tokens_unique_label(&m->tokens, character_token(tpl)->label, t.label, sizeof t.label);

    undo_begin(u);
    int idx = undo_add_token(u, m, t);
    size_t off = 0;
    /* Its card, when the map has none by that name; the map's own is kept,
     * as its rolls are. A card is not in the undo history: one brought by a
     * placing that is undone stays on the map, named by nobody. */
    const char *card = card_of(tpl, &t);
    if (card) {
        int ci = card_find(m, t.card);
        char buf[96] = "";
        if (ci >= 0 && strcmp(m->cards[ci].text, card) != 0)
            snprintf(buf, sizeof buf, "kept this map's card %s", t.card);
        else if (ci < 0 && card_set(m, t.card, card) < 0)
            snprintf(buf, sizeof buf, "no room for card %s", t.card);
        if (buf[0]) { snprintf(said, saidsz, "%s", buf); off = strlen(said); }
    }
    for (int r = 0; r < ROLL_MAX; r++) {
        const NamedRoll *nr = &tpl->rolls[r];
        if (!nr->name[0]) continue;
        int i = roll_named(m, nr->name);
        const char *why = NULL;
        char buf[96];
        if (i >= 0) {
            if (strcmp(m->rolls[i].expr, nr->expr) != 0) {
                snprintf(buf, sizeof buf, "kept this map's %s = %s", m->rolls[i].name, m->rolls[i].expr);
                why = buf;
            }
        } else {
            int slot = -1;
            for (int k = 0; k < ROLL_MAX && slot < 0; k++)
                if (!m->rolls[k].name[0]) slot = k;
            if (slot >= 0) undo_set_roll(u, m, slot, nr);
            else { snprintf(buf, sizeof buf, "no room for roll %s", nr->name); why = buf; }
        }
        if (why && off + 4 < saidsz) {
            int w = snprintf(said + off, saidsz - off, "%s%s", off ? ", " : "", why);
            if (w > 0) off += (size_t)w < saidsz - off ? (size_t)w : saidsz - off - 1;
        }
    }
    undo_end(u);
    return idx;
}
