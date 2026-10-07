/* Change sets (docs/CONFLICTS.md): map_copy, the diff, conflicts, applying
 * whole or by box, the summary, and the preview that never touches the map. */
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "card.h"
#include "changeset.h"
#include "link.h"
#include "map.h"
#include "undo.h"
#include "util.h"

/* Cells in one order, to compare two sets of them. */
static int cell_order(const void *pa, const void *pb)
{
    const CsCell *a = pa, *b = pb;
    if (a->kind != b->kind) return a->kind - b->kind;
    if (a->y != b->y) return a->y - b->y;
    return a->x - b->x;
}

static int cells_same(const ChangeSet *a, const ChangeSet *b)
{
    if (a->ncells != b->ncells) return 0;
    size_t n = (size_t)a->ncells;
    CsCell *x = xmalloc((n + 1) * sizeof *x), *y = xmalloc((n + 1) * sizeof *y);
    memcpy(x, a->cells, n * sizeof *x);
    memcpy(y, b->cells, n * sizeof *y);
    qsort(x, n, sizeof *x, cell_order);
    qsort(y, n, sizeof *y, cell_order);
    int same = 1;
    for (size_t i = 0; i < n && same; i++)
        same = x[i].kind == y[i].kind && x[i].x == y[i].x && x[i].y == y[i].y &&
               x[i].before == y[i].before && x[i].after == y[i].after;
    free(x);
    free(y);
    return same;
}

static Token mk(int kind, int x, int y, int size, const char *label)
{
    Token t;
    memset(&t, 0, sizeof t);
    t.kind = (uint8_t)kind;
    t.x = (int16_t)x;
    t.y = (int16_t)y;
    t.size = (uint8_t)size;
    str_lcpy(t.label, label, sizeof t.label);
    return t;
}

/* The two maps say the same thing: every square, the painting, and every
 * creature, area, link, note, roll and card, whatever order they are in. */
static int map_same(const Map *a, const Map *b)
{
    if (a->w != b->w || a->h != b->h) return 0;
    size_t w = (size_t)a->w, h = (size_t)a->h;
    if (memcmp(a->tiles, b->tiles, w * h) || memcmp(a->vedges, b->vedges, (w + 1) * h) ||
        memcmp(a->hedges, b->hedges, w * (h + 1)))
        return 0;
    for (size_t i = 0; i < w * h; i++)
        if ((a->fog[i] & FOG_ID) != (b->fog[i] & FOG_ID)) return 0;
    if (a->tokens.n != b->tokens.n || a->nareas != b->nareas || a->nlinks != b->nlinks ||
        a->nnotes != b->nnotes || a->ncards != b->ncards || a->round != b->round || a->spotlight != b->spotlight)
        return 0;
    for (int i = 0; i < a->tokens.n; i++) {
        const Token *t = &a->tokens.v[i];
        int found = 0;
        for (int j = 0; j < b->tokens.n && !found; j++) found = token_equal(t, &b->tokens.v[j]);
        if (!found) return 0;
    }
    for (int i = 0; i < a->nareas; i++) {
        int j = map_area_find(b, a->areas[i].name);
        if (j < 0 || a->areas[i].x0 != b->areas[j].x0 || a->areas[i].y1 != b->areas[j].y1 ||
            a->areas[i].floor != b->areas[j].floor)
            return 0;
    }
    for (int i = 0; i < a->nlinks; i++) {
        int j = link_find(b, a->links[i].num);
        if (j < 0 || a->links[i].x[1] != b->links[j].x[1] || a->links[i].kind != b->links[j].kind) return 0;
    }
    for (int i = 0; i < a->nnotes; i++) {
        const char *t = map_note_at(b, a->notes[i].x, a->notes[i].y);
        if (!t || strcmp(t, a->notes[i].text)) return 0;
    }
    for (int i = 0; i < ROLL_MAX; i++) {
        if (!a->rolls[i].name[0]) continue;
        int found = 0;
        for (int j = 0; j < ROLL_MAX; j++)
            found |= !strcmp(a->rolls[i].name, b->rolls[j].name) && !strcmp(a->rolls[i].expr, b->rolls[j].expr);
        if (!found) return 0;
    }
    for (int i = 0; i < a->ncards; i++) {
        int j = card_find(b, a->cards[i].name);
        if (j < 0 || strcmp(a->cards[i].text, b->cards[j].text)) return 0;
    }
    return 1;
}

/* A small encounter: a room, two creatures, an area, a note, a fog patch. */
static Map *encounter(void)
{
    Map *m = map_new(40, 30, "enc");
    map_fill_tiles(m, 1, 1, 10, 8, TILE_FLOOR);
    map_rect_walls(m, 1, 1, 10, 8, EDGE_WALL);
    tokens_add(&m->tokens, mk(TOKEN_PLAYER, 2, 2, 1, "Aria"));
    tokens_add(&m->tokens, mk(TOKEN_ENEMY, 5, 5, 2, "Ogre"));
    map_area_set(m, "Hall", 1, 1, 10, 8);
    map_note_set(m, 3, 3, "a loose flagstone");
    str_lcpy(m->fog_patches[0].name, "Mist", FOG_NAME_MAX);
    m->modified = 0;
    return m;
}

/* What an agent's plan does on a copy: a second room joined by a door, a
 * ghoul in it, the ogre moved, a note, an area, a link, fog, a roll, a card. */
static void agent_plan(Map *c, Undo *u)
{
    undo_begin(u);
    for (int y = 1; y <= 8; y++)
        for (int x = 12; x <= 20; x++) undo_set_tile(u, c, x, y, TILE_WATER);
    for (int y = 1; y <= 8; y++) undo_set_vedge(u, c, 12, y, EDGE_WALL);
    undo_set_vedge(u, c, 11, 4, EDGE_DOOR_CLOSED);
    undo_add_token(u, c, mk(TOKEN_ENEMY, 14, 3, 1, "Ghoul"));
    undo_move_token(u, c, 1, 6, 6);
    undo_set_note(u, c, 15, 6, "the drain");
    undo_set_area(u, c, "Pool", 12, 1, 20, 8);
    Link l;
    memset(&l, 0, sizeof l);
    l.num = (uint8_t)link_free_num(c);
    l.size = 1;
    l.x[0] = 2; l.y[0] = 7; l.x[1] = 19; l.y[1] = 7;
    undo_set_link(u, c, &l);
    for (int x = 12; x <= 14; x++) undo_set_fog(u, c, x, 1, 1);
    NamedRoll r;
    memset(&r, 0, sizeof r);
    str_lcpy(r.name, "bite", sizeof r.name);
    str_lcpy(r.expr, "1d8+2", sizeof r.expr);
    undo_set_roll(u, c, 0, &r);
    card_set(c, "ghoul", "Ghoul - Tier 1 Standard");
    undo_set_round(u, c, 2);
    undo_end(u);
}

static void cs_basics(void)
{
    CASE("map_copy is whole and independent: changing the copy leaves the original");
    Map *m = encounter();
    card_set(m, "ogre", "Ogre - Tier 2 Bruiser");
    Map *c = map_copy(m);
    CHECK(map_same(m, c));
    map_set_tile(c, 0, 0, TILE_HAZARD);
    c->tokens.v[0].x = 9;
    card_set(c, "ogre", "changed");
    map_note_set(c, 3, 3, "");
    CHECK_EQ(map_tile(m, 0, 0), TILE_VOID);
    CHECK_EQ(m->tokens.v[0].x, 2);
    CHECK(!strcmp(m->cards[0].text, "Ogre - Tier 2 Bruiser"));
    CHECK(map_note_at(m, 3, 3) != NULL);
    map_free(c);

    CASE("a plan on a copy: its change set, applied to the original, makes the two the same, as one undo step");
    Map *orig = map_copy(m);
    c = map_copy(m);
    Undo scratch, u;
    undo_init(&scratch);
    undo_init(&u);
    agent_plan(c, &scratch);
    ChangeSet cs;
    cs_init(&cs);
    int n = cs_diff(&cs, m, c, &scratch);
    CHECK(n > 0);
    CHECK_EQ(cs_check(&cs, m), 0);
    CHECK_EQ(m->gen, orig->gen);                          /* the diff touches nothing */
    unsigned stamp = u.stamp;
    char left[128];
    CHECK(cs_apply(&cs, m, &u, NULL, left, sizeof left) > 0);
    CHECK(!left[0]);
    CHECK(map_same(m, c));
    CHECK_EQ(u.nmarks, 1);
    CHECK(u.stamp != stamp);

    CASE("u takes the accept back whole (a card stays: cards are not in the log)");
    undo_undo(&u, m);
    int k = card_find(m, "ghoul");
    CHECK(k >= 0);
    if (k >= 0) { free(m->cards[k].text); m->cards[k] = m->cards[--m->ncards]; }
    CHECK(map_same(m, orig));

    CASE("the diff from the log equals the diff of every square");
    ChangeSet all;
    cs_init(&all);
    cs_diff(&all, orig, c, NULL);
    CHECK_EQ(all.ncells, cs.ncells);
    CHECK(cells_same(&all, &cs));
    cs_free(&all);

    CASE("the summary names what changes");
    char sum[400];
    cs_summary(&cs, NULL, sum, sizeof sum);
    CHECK(strstr(sum, "ground in M2:U9") != NULL);
    CHECK(strstr(sum, "Ghoul added") != NULL);
    CHECK(strstr(sum, "Ogre changed") != NULL);
    CHECK(strstr(sum, "area Pool added") != NULL);
    CHECK(strstr(sum, "a note at P7") != NULL);
    CHECK(strstr(sum, "round 0 -> 2") != NULL);
    int x0, y0, x1, y1;
    CHECK(cs_bounds(&cs, &x0, &y0, &x1, &y1));
    CHECK(x0 == 2 && y0 == 1 && x1 == 20 && y1 == 8);   /* from the link's near end at C8 to the pool's far corner */

    cs_free(&cs);
    map_free(c);
    map_free(orig);
    map_free(m);
    undo_free(&scratch);
    undo_free(&u);
}

static void cs_conflicts(void)
{
    Map *m = encounter();
    Map *c = map_copy(m);
    Undo scratch, u;
    undo_init(&scratch);
    undo_init(&u);
    agent_plan(c, &scratch);
    ChangeSet cs;
    cs_init(&cs);
    cs_diff(&cs, m, c, &scratch);

    CASE("what the GM changed since is a conflict, and the accept overwrites it");
    undo_set_tile(&u, m, 13, 2, TILE_HAZARD);             /* under the pool */
    CHECK_EQ(cs_check(&cs, m), 1);
    unsigned g = m->gen;
    CHECK_EQ(cs_check(&cs, m), 1);                        /* no change, no recount */
    CHECK_EQ(m->gen, g);

    CASE("a label the GM has used since is a conflict, and the accept never doubles it");
    undo_add_token(&u, m, mk(TOKEN_ENEMY, 30, 20, 1, "Ghoul"));
    CHECK_EQ(cs_check(&cs, m), 2);

    CASE("a new link takes the lowest free number at accept, never the GM's link");
    Link mine;
    memset(&mine, 0, sizeof mine);
    mine.num = 1;
    mine.size = 1;
    mine.x[0] = 25; mine.y[0] = 25; mine.x[1] = 28; mine.y[1] = 25;
    undo_set_link(&u, m, &mine);
    CHECK_EQ(cs_check(&cs, m), 2);                        /* not a conflict */

    char left[128];
    cs_apply(&cs, m, &u, NULL, left, sizeof left);
    CHECK_EQ(map_tile(m, 13, 2), TILE_WATER);
    int ghouls = 0;
    for (int i = 0; i < m->tokens.n; i++) ghouls += !strcmp(m->tokens.v[i].label, "Ghoul");
    CHECK_EQ(ghouls, 1);
    CHECK(strstr(left, "Ghoul") != NULL);
    CHECK_EQ(m->nlinks, 2);
    int j = link_find(m, 1);
    CHECK(j >= 0 && m->links[j].x[0] == 25);              /* the GM's, untouched */
    CHECK(link_find(m, 2) >= 0);                          /* the agent's, renumbered */

    CASE("painting into a fog patch deleted since is a conflict and is left out");
    cs_free(&cs);
    map_free(c);
    c = map_copy(m);
    undo_clear(&scratch);
    undo_begin(&scratch);
    undo_set_fog(&scratch, c, 30, 1, 1);
    undo_end(&scratch);
    cs_diff(&cs, m, c, &scratch);
    m->fog_patches[0].dead = 1;
    m->gen++;
    CHECK_EQ(cs_check(&cs, m), 1);
    cs_apply(&cs, m, &u, NULL, left, sizeof left);
    CHECK_EQ(m->fog[(size_t)1 * (size_t)m->w + 30] & FOG_ID, 0);
    CHECK(strstr(left, "fog patch 1") != NULL);

    cs_free(&cs);
    map_free(c);
    map_free(m);
    undo_free(&scratch);
    undo_free(&u);
}

static void cs_partial(void)
{
    CASE("a box accepts only the elements wholly inside it");
    Map *m = encounter();
    Map *c = map_copy(m);
    Undo scratch, u;
    undo_init(&scratch);
    undo_init(&u);
    agent_plan(c, &scratch);
    ChangeSet cs;
    cs_init(&cs);
    cs_diff(&cs, m, c, &scratch);
    CsBox box = { 12, 1, 16, 8 };                         /* the west half of the pool */
    char sum[300];
    cs_summary(&cs, &box, sum, sizeof sum);
    CHECK(strstr(sum, "Ghoul added") != NULL);
    CHECK(strstr(sum, "round 0") == NULL);                /* no square's */
    cs_apply(&cs, m, &u, &box, NULL, 0);
    CHECK_EQ(map_tile(m, 13, 2), TILE_WATER);
    CHECK_EQ(map_tile(m, 18, 2), TILE_VOID);              /* outside */
    CHECK_EQ(map_vedge(m, 12, 3), EDGE_WALL);             /* the box's west boundary */
    CHECK(token_name(&m->tokens.v[m->tokens.n - 1]) && !strcmp(m->tokens.v[m->tokens.n - 1].label, "Ghoul"));
    CHECK_EQ(m->tokens.v[1].x, 5);                        /* the ogre's move is outside */
    CHECK_EQ(m->round, 0);
    CHECK(map_area_find(m, "Pool") < 0);                  /* reaches past the box */
    CHECK_EQ(m->nlinks, 0);                               /* one end outside */

    cs_free(&cs);
    map_free(c);
    map_free(m);
    undo_free(&scratch);
    undo_free(&u);
}

static void cs_preview(void)
{
    CASE("the preview shows the change and puts every byte back: no gen, no undo, no modified");
    Map *m = encounter();
    Map *c = map_copy(m);
    Undo scratch;
    undo_init(&scratch);
    agent_plan(c, &scratch);
    ChangeSet cs;
    cs_init(&cs);
    cs_diff(&cs, m, c, &scratch);

    Map *snap = map_copy(m);
    unsigned gen = m->gen;
    cs_show(&cs, m, 0, 0, m->w - 1, m->h - 1);
    CHECK_EQ(map_tile(m, 13, 2), TILE_WATER);
    CHECK_EQ(m->tokens.n, 3);
    CHECK(map_note_at(m, 15, 6) != NULL);
    CHECK_EQ(m->nlinks, 1);
    CHECK_EQ(m->tokens.v[1].x, 6);                        /* the ogre where the plan put it */
    cs_unshow(&cs, m);
    CHECK(map_same(m, snap));
    CHECK(!memcmp(m->fog, snap->fog, (size_t)m->w * (size_t)m->h));
    CHECK_EQ(m->gen, gen);
    CHECK_EQ(m->modified, 0);

    CASE("only the cells in the window are swapped; the rest are never read");
    cs_show(&cs, m, 0, 0, 9, 9);                          /* the pool's first block only */
    CHECK_EQ(map_tile(m, 13, 2), TILE_WATER);             /* block 0 reaches x 15 */
    CHECK_EQ(map_tile(m, 18, 2), TILE_VOID);              /* block 1: off the window */
    cs_unshow(&cs, m);
    CHECK(map_same(m, snap));

    CASE("after the GM edits, the preview is rebuilt on the live map");
    map_note_set(m, 30, 20, "the GM's");
    cs_show(&cs, m, 0, 0, m->w - 1, m->h - 1);
    CHECK(map_note_at(m, 30, 20) != NULL && map_note_at(m, 15, 6) != NULL);
    cs_unshow(&cs, m);
    CHECK_EQ(m->nnotes, 2);

    cs_free(&cs);
    map_free(snap);
    map_free(c);
    map_free(m);
    undo_free(&scratch);
}

/* Random plans on a copy: the change set from the log agrees with the full
 * diff, and applying it makes the original the copy. */
static void cs_differential(void)
{
    CASE("random plans: the log's diff equals the full diff, and applying it gives the copy");
    unsigned seed = 12345;
    int ok_same = 1, ok_apply = 1;
    for (int round = 0; round < 60; round++) {
        Map *m = encounter();
        Map *c = map_copy(m);
        Undo scratch, u;
        undo_init(&scratch);
        undo_init(&u);
        undo_begin(&scratch);
        for (int k = 0; k < 40; k++) {
            seed = seed * 1103515245u + 12345u;
            int r = (int)((seed >> 16) % 7), x = (int)((seed >> 3) % 40), y = (int)((seed >> 9) % 30);
            switch (r) {
            case 0: undo_set_tile(&scratch, c, x, y, (uint8_t)(1 + (seed >> 20) % 6)); break;
            case 1: undo_set_vedge(&scratch, c, x, y, (uint8_t)((seed >> 20) % EDGE_COUNT)); break;
            case 2: undo_set_hedge(&scratch, c, x, y, (uint8_t)((seed >> 20) % EDGE_COUNT)); break;
            case 3: undo_set_fog(&scratch, c, x, y, (uint8_t)((seed >> 20) % 2)); break;
            case 4: if (!tokens_at(&c->tokens, x, y)) {} else {
                        char lab[16];
                        snprintf(lab, sizeof lab, "C%d", k + round * 100);
                        undo_add_token(&scratch, c, mk(TOKEN_ENEMY, x, y, 1, lab));
                    }
                    break;
            case 5: if (c->tokens.n) undo_del_token(&scratch, c, (int)((seed >> 20) % (unsigned)c->tokens.n)); break;
            case 6: undo_set_note(&scratch, c, x, y, (seed >> 20) % 2 ? "x" : ""); break;
            }
        }
        undo_end(&scratch);
        ChangeSet a, b;
        cs_init(&a);
        cs_init(&b);
        cs_diff(&a, m, c, &scratch);
        cs_diff(&b, m, c, NULL);
        if (!cells_same(&a, &b)) ok_same = 0;
        cs_apply(&a, m, &u, NULL, NULL, 0);
        if (!map_same(m, c)) ok_apply = 0;
        cs_free(&a);
        cs_free(&b);
        map_free(c);
        map_free(m);
        undo_free(&scratch);
        undo_free(&u);
    }
    CHECK(ok_same);
    CHECK(ok_apply);
}

void test_changeset(void)
{
    cs_basics();
    cs_conflicts();
    cs_partial();
    cs_preview();
    cs_differential();
}
