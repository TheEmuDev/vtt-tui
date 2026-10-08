/* Change sets (docs/CONFLICTS.md): map_copy, the diff, conflicts, applying
 * whole or by box, the summary, and the preview that never touches the map. */
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "card.h"
#include "changeset.h"
#include "checkpoint.h"
#include "fog.h"
#include "scene.h"
#include "stamp.h"
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
            case 4: if (tokens_at(&c->tokens, x, y) < 0) {
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

static void cs_edges_and_turns(void)
{
    CASE("boundaries on the map's far edges: diffed by log and in full, previewed back byte for byte, taken by a box's east and south sides");
    Map *m = map_new(20, 10, "edges");
    map_fill_tiles(m, 0, 0, 19, 9, TILE_FLOOR);
    Map *c = map_copy(m);
    Undo scratch, u;
    undo_init(&scratch);
    undo_init(&u);
    undo_begin(&scratch);
    undo_set_vedge(&scratch, c, 20, 9, EDGE_WALL);                /* x == w */
    undo_set_hedge(&scratch, c, 19, 10, EDGE_WALL);               /* y == h */
    undo_set_vedge(&scratch, c, 6, 2, EDGE_DOOR_CLOSED);          /* a box's east side, x1 + 1 */
    undo_set_hedge(&scratch, c, 3, 5, EDGE_WINDOW);               /* its south side, y1 + 1 */
    undo_end(&scratch);
    ChangeSet cs, all;
    cs_init(&cs);
    cs_init(&all);
    cs_diff(&cs, m, c, &scratch);
    cs_diff(&all, m, c, NULL);
    CHECK_EQ(cs.ncells, 4);
    CHECK(cells_same(&cs, &all));
    Map *snap = map_copy(m);
    cs_show(&cs, m, 0, 0, m->w - 1, m->h - 1);
    CHECK_EQ(map_vedge(m, 20, 9), EDGE_WALL);
    CHECK_EQ(map_hedge(m, 19, 10), EDGE_WALL);
    cs_unshow(&cs, m);
    CHECK(map_same(m, snap));
    CsBox box = { 2, 1, 5, 4 };
    cs_apply(&cs, m, &u, &box, NULL, 0);
    CHECK_EQ(map_vedge(m, 6, 2), EDGE_DOOR_CLOSED);
    CHECK_EQ(map_hedge(m, 3, 5), EDGE_WINDOW);
    CHECK_EQ(map_vedge(m, 20, 9), EDGE_NONE);                     /* outside */
    cs_free(&cs);
    cs_free(&all);
    map_free(snap);
    map_free(c);

    CASE("an unlabeled creature that moved is one gone and one come, and lands where the plan put it");
    tokens_add(&m->tokens, mk(TOKEN_ENEMY, 1, 1, 1, ""));
    c = map_copy(m);
    undo_clear(&scratch);
    undo_begin(&scratch);
    undo_move_token(&scratch, c, 0, 8, 8);
    undo_end(&scratch);
    cs_diff(&cs, m, c, &scratch);
    CHECK_EQ(cs.ntoks, 2);
    cs_apply(&cs, m, &u, NULL, NULL, 0);
    CHECK(map_same(m, c));
    CHECK_EQ(tokens_at(&m->tokens, 8, 8), 0);
    cs_free(&cs);
    map_free(c);
    map_free(m);

    CASE("two creatures of one label pair in their order: removing another leaves no change on them");
    m = map_new(20, 10, "dups");
    map_fill_tiles(m, 0, 0, 19, 9, TILE_FLOOR);
    tokens_add(&m->tokens, mk(TOKEN_PLAYER, 0, 0, 1, "Aria"));
    tokens_add(&m->tokens, mk(TOKEN_ENEMY, 2, 0, 1, "X"));
    tokens_add(&m->tokens, mk(TOKEN_ENEMY, 4, 0, 1, "X"));
    c = map_copy(m);
    undo_clear(&scratch);
    undo_begin(&scratch);
    undo_del_token(&scratch, c, 0);
    undo_end(&scratch);
    cs_diff(&cs, m, c, &scratch);
    CHECK_EQ(cs.ntoks, 1);
    cs_free(&cs);
    map_free(c);
    map_free(m);

    CASE("one creature holds the turn: a plan's turn lands only where nobody else has it");
    m = map_new(20, 10, "turns");
    map_fill_tiles(m, 0, 0, 19, 9, TILE_FLOOR);
    Token aria = mk(TOKEN_PLAYER, 0, 0, 1, "Aria"), ogre = mk(TOKEN_ENEMY, 10, 5, 1, "Ogre"), bob = mk(TOKEN_PLAYER, 1, 0, 1, "Bob");
    aria.turn = TURN_IN | TURN_ACTING;
    ogre.turn = TURN_IN;
    bob.turn  = TURN_IN;
    tokens_add(&m->tokens, aria);
    tokens_add(&m->tokens, ogre);
    tokens_add(&m->tokens, bob);
    c = map_copy(m);
    undo_clear(&scratch);
    undo_begin(&scratch);                                       /* the plan passes the turn to the Ogre */
    Token a2 = c->tokens.v[0], o2 = c->tokens.v[1];
    a2.turn = TURN_IN;
    o2.turn = TURN_IN | TURN_ACTING;
    undo_edit_token(&scratch, c, 0, a2);
    undo_edit_token(&scratch, c, 1, o2);
    undo_end(&scratch);
    cs_diff(&cs, m, c, &scratch);
    Token b2 = m->tokens.v[2], a3 = m->tokens.v[0];             /* the GM passes it to Bob meanwhile */
    a3.turn = TURN_IN;
    b2.turn = TURN_IN | TURN_ACTING;
    undo_edit_token(&u, m, 0, a3);
    undo_edit_token(&u, m, 2, b2);
    cs_apply(&cs, m, &u, NULL, NULL, 0);
    int acting = 0;
    for (int i = 0; i < m->tokens.n; i++) acting += (m->tokens.v[i].turn & TURN_ACTING) != 0;
    CHECK_EQ(acting, 1);
    CHECK((m->tokens.v[2].turn & TURN_ACTING) != 0);           /* the GM's turn stands */
    map_free(m);

    CASE("a box holding the plan's new actor and not the old one leaves the old one acting");
    m = map_new(20, 10, "turns");
    map_fill_tiles(m, 0, 0, 19, 9, TILE_FLOOR);
    tokens_add(&m->tokens, aria);
    tokens_add(&m->tokens, ogre);
    tokens_add(&m->tokens, bob);
    CsBox obox = { 9, 4, 11, 6 };
    cs_apply(&cs, m, &u, &obox, NULL, 0);
    acting = 0;
    for (int i = 0; i < m->tokens.n; i++) acting += (m->tokens.v[i].turn & TURN_ACTING) != 0;
    CHECK_EQ(acting, 1);
    CHECK((m->tokens.v[0].turn & TURN_ACTING) != 0);
    cs_free(&cs);
    map_free(c);
    map_free(m);
    undo_free(&scratch);
    undo_free(&u);
}

static void cs_small_parts(void)
{
    Map *m = encounter();
    NamedRoll r;
    memset(&r, 0, sizeof r);
    str_lcpy(r.name, "bite", sizeof r.name);
    str_lcpy(r.expr, "1d6", sizeof r.expr);
    m->rolls[0] = r;
    card_set(m, "ghoul", "old text");
    Map *c = map_copy(m);
    Undo scratch, u;
    undo_init(&scratch);
    undo_init(&u);
    undo_begin(&scratch);
    undo_set_area(&scratch, c, "Hall", 1, 1, 12, 8);              /* grows */
    str_lcpy(r.expr, "1d8", sizeof r.expr);
    undo_set_roll(&scratch, c, 0, &r);
    undo_set_round(&scratch, c, 3);
    undo_end(&scratch);
    card_set(c, "ghoul", "new text");
    Link l;
    memset(&l, 0, sizeof l);
    l.num = 1; l.size = 1;
    l.x[0] = 2; l.y[0] = 2; l.x[1] = 30; l.y[1] = 20;
    undo_set_link(&scratch, c, &l);
    ChangeSet cs;
    cs_init(&cs);
    cs_diff(&cs, m, c, &scratch);

    CASE("the summary does not number a new link: its number is given at accept");
    char sum[300];
    cs_summary(&cs, NULL, sum, sizeof sum);
    CHECK(strstr(sum, "a stairs link added") != NULL);
    CHECK(strstr(sum, "link 1") == NULL);

    CASE("the preview numbers a new link as the accept will, past one the GM made since");
    Link g = l;
    g.x[0] = 25; g.y[0] = 25; g.x[1] = 28; g.y[1] = 25;
    map_fill_tiles(m, 0, 0, m->w - 1, m->h - 1, TILE_FLOOR);
    link_put(m, &g);
    cs_show(&cs, m, 0, 0, m->w - 1, m->h - 1);
    CHECK_EQ(m->nlinks, 2);
    int nums = m->nlinks == 2 ? m->links[0].num + m->links[1].num : 0;
    CHECK_EQ(nums, 3);                                            /* 1 and 2, not 1 and 1 */
    cs_unshow(&cs, m);
    CHECK_EQ(m->nlinks, 1);

    CASE("conflicts in the small parts: an area, a roll, a card and the round changed since");
    CHECK_EQ(cs_check(&cs, m), 0);
    map_area_set(m, "Hall", 1, 1, 9, 8);
    CHECK_EQ(cs_check(&cs, m), 1);
    str_lcpy(m->rolls[0].expr, "2d4", sizeof m->rolls[0].expr);
    m->round = 1;
    m->gen++;
    CHECK_EQ(cs_check(&cs, m), 3);
    card_set(m, "ghoul", "edited by the GM");           /* touches nothing: seen by cards_gen */
    CHECK_EQ(cs_check(&cs, m), 4);

    cs_free(&cs);
    map_free(c);
    map_free(m);
    undo_free(&scratch);
    undo_free(&u);
}

static void cs_checkpoint(void)
{
    CASE("no checkpoint: an edit notes nothing");
    Map *m = encounter();
    Undo u;
    undo_init(&u);
    CHECK(m->cp == NULL && m->cp_saved == NULL);
    undo_begin(&u);
    undo_set_tile(&u, m, 0, 0, TILE_WATER);
    undo_end(&u);
    CHECK(m->cp == NULL);

    CASE("a checkpoint sees every edit through the log, the far edges, fog painted and fog deleted, undo and redo, and only the blocks written");
    for (int y = 1; y <= 3; y++)
        for (int x = 30; x <= 32; x++) map_fog_set(m, x, y, 1);
    checkpoint_start(m);
    Map *base = map_copy(m);
    CHECK(base->cp == NULL);                                  /* a copy never carries it */
    undo_begin(&u);
    undo_set_tile(&u, m, 5, 5, TILE_WATER);
    undo_set_vedge(&u, m, m->w, 29, EDGE_WALL);               /* x == w */
    undo_set_hedge(&u, m, 39, m->h, EDGE_WALL);               /* y == h */
    undo_set_fog(&u, m, 20, 20, 1);
    undo_end(&u);
    undo_begin(&u);
    for (int y = 16; y <= 18; y++)
        for (int x = 0; x <= 3; x++) undo_set_tile(&u, m, x, y, TILE_ROUGH);
    undo_end(&u);
    undo_undo(&u, m);                                         /* back, and forward again */
    undo_redo(&u, m);
    fog_delete(m, 1);
    Checkpoint *cp = m->cp;
    CHECK(cp->nsaved >= 4 && cp->nsaved <= 8);                /* a handful, of 3x2 blocks */
    ChangeSet a, b;
    cs_init(&a);
    cs_init(&b);
    checkpoint_changes(m, &a);
    cs_diff(&b, base, m, NULL);
    CHECK(cells_same(&a, &b));
    CHECK(a.ncells > 0);

    CASE("the small parts and what only counts as changed: cards, scenes, clocks");
    undo_move_token(&u, m, 0, 3, 3);
    map_note_set(m, 6, 6, "the GM's note");
    map_area_set(m, "Annex", 20, 20, 25, 25);
    m->round = 4;
    card_set(m, "ghoul", "a card");
    char err[64];
    scene_save(m, "Before", NULL, err, sizeof err);
    str_lcpy(m->clocks[0].name, "Doom", sizeof m->clocks[0].name);
    m->clocks[0].size = 6;
    checkpoint_changes(m, &a);
    char sum[400];
    cs_summary(&a, NULL, sum, sizeof sum);
    CHECK(strstr(sum, "Aria changed") != NULL);
    CHECK(strstr(sum, "a note at G7") != NULL);
    CHECK(strstr(sum, "area Annex added") != NULL);
    CHECK(strstr(sum, "round 0 -> 4") != NULL);
    CHECK(strstr(sum, "cards changed") != NULL);
    CHECK(strstr(sum, "scenes changed") != NULL);
    CHECK(strstr(sum, "clocks changed") != NULL);

    CASE("sight's bits and the previews leave no change: LIT, SEEN and RIM are masked, a swap is no write");
    checkpoint_start(m);
    for (int i = 0; i < m->w * m->h; i++) m->fog[i] |= FOG_LIT | FOG_SEEN | FOG_RIM;
    Map *c = map_copy(m);
    Undo s2;
    undo_init(&s2);
    undo_begin(&s2);
    for (int x = 0; x < 30; x++) undo_set_tile(&s2, c, x, 25, TILE_HAZARD);
    undo_end(&s2);
    ChangeSet pv;
    cs_init(&pv);
    cs_diff(&pv, m, c, &s2);
    cs_show(&pv, m, 0, 0, m->w - 1, m->h - 1);
    cs_unshow(&pv, m);
    CHECK_EQ(checkpoint_changes(m, &a), 0);
    CHECK_EQ(m->cp->nsaved, 0);
    cs_free(&pv);
    map_free(c);
    undo_free(&s2);

    CASE("a request rolled back leaves no change");
    undo_begin(&u);
    undo_set_tile(&u, m, 9, 9, TILE_HAZARD);
    undo_abort(&u, m);
    CHECK_EQ(checkpoint_changes(m, &a), 0);

    CASE("painting deleted before the start, then undone: no change is reported, since the map and the log disagreed");
    checkpoint_stop(m);
    undo_clear(&u);
    undo_begin(&u);
    undo_set_fog(&u, m, 35, 25, 2);
    undo_set_fog(&u, m, 36, 25, 2);
    undo_end(&u);
    str_lcpy(m->fog_patches[1].name, "Smoke", FOG_NAME_MAX);
    fog_delete(m, 2);                                         /* round the log */
    checkpoint_start(m);
    undo_undo(&u, m);                                         /* paints over nothing: 0 -> 0 */
    CHECK_EQ(checkpoint_changes(m, &a), 0);

    CASE("one cell written twice in a batch before the start and undone after: its start value is the live one");
    undo_clear(&u);
    undo_begin(&u);
    undo_set_tile(&u, m, 30, 22, TILE_WATER);
    undo_set_tile(&u, m, 30, 22, TILE_ROUGH);
    undo_end(&u);
    checkpoint_start(m);
    undo_undo(&u, m);
    checkpoint_changes(m, &a);
    CHECK_EQ(a.ncells, 1);
    CHECK(a.ncells == 1 && a.cells[0].before == TILE_ROUGH && a.cells[0].after == TILE_VOID);

    CASE("a fog setting changed is reported");
    checkpoint_start(m);
    m->fog_patches[0].memory = (uint8_t)!m->fog_patches[0].memory;
    checkpoint_changes(m, &a);
    cs_summary(&a, NULL, sum, sizeof sum);
    CHECK(strstr(sum, "fog settings changed") != NULL);
    m->fog_patches[0].memory = (uint8_t)!m->fog_patches[0].memory;

    CASE("starting again counts from now");
    undo_begin(&u);
    undo_set_tile(&u, m, 7, 7, TILE_BRUSH);
    undo_end(&u);
    CHECK(checkpoint_changes(m, &a) > 0);
    checkpoint_start(m);
    CHECK_EQ(checkpoint_changes(m, &a), 0);

    CASE("a log older than a resize, undone with a checkpoint running, notes nothing off the map");
    undo_clear(&u);
    undo_begin(&u);
    undo_set_tile(&u, m, 39, 29, TILE_WATER);
    undo_end(&u);
    Map *small = map_copy(m);
    map_resize(small, 20, 10);
    checkpoint_start(small);
    undo_undo(&u, small);                                     /* ASan watches the store */
    CHECK_EQ(checkpoint_changes(small, &a), 0);
    map_free(small);

    CASE("a resize is reported, writes after it save nothing, and a new start fits the new size");
    map_resize(m, 50, 30);
    undo_clear(&u);                                           /* as the app does after a resize */
    undo_begin(&u);
    undo_set_tile(&u, m, 45, 5, TILE_WATER);
    undo_end(&u);
    checkpoint_changes(m, &a);
    cs_summary(&a, NULL, sum, sizeof sum);
    CHECK(strstr(sum, "resized from 40x30 to 50x30") != NULL);
    checkpoint_start(m);
    undo_begin(&u);
    undo_set_tile(&u, m, 49, 29, TILE_WATER);
    undo_end(&u);
    CHECK_EQ(checkpoint_changes(m, &a), 1);

    cs_free(&a);
    cs_free(&b);
    map_free(base);
    map_free(m);                                              /* frees the checkpoint */
    undo_free(&u);
}

/* Random edits through the log -- recorded, undone, redone, rolled back --
 * and fog deleted: what the checkpoint says changed is what a full diff
 * against a copy taken at the start says. */
static void cs_checkpoint_differential(void)
{
    CASE("random edits, undos, redos and rollbacks: the checkpoint's cells are the full diff's");
    unsigned seed = 777;
    int ok = 1;
    for (int round = 0; round < 40; round++) {
        Map *m = encounter();
        Undo u;
        undo_init(&u);
        undo_begin(&u);                                       /* a history from before the start */
        for (int x = 0; x < 10; x++) undo_set_tile(&u, m, x, 12, TILE_WOOD);
        for (int x = 0; x < 10; x++) undo_set_fog(&u, m, x, 13, 1);
        undo_end(&u);
        if (round % 2) fog_delete(m, 1);                      /* the log and the map now disagree */
        checkpoint_start(m);
        Map *base = map_copy(m);
        for (int k = 0; k < 60; k++) {
            seed = seed * 1103515245u + 12345u;
            int r = (int)((seed >> 16) % 8), x = (int)((seed >> 3) % 41), y = (int)((seed >> 9) % 31);
            uint8_t v = (uint8_t)((seed >> 20) % 7);
            switch (r) {
            case 0: case 1: case 2: case 3:
                undo_begin(&u);
                for (int j = 0; j < 1 + (int)v; j++) {
                    int xx = imin(x + j / 2, 40), yy = imin(y, 30);      /* each cell twice */
                    switch ((r + j) % 4) {
                    case 0: undo_set_tile(&u, m, xx, yy, v % TILE_COUNT); break;
                    case 1: undo_set_vedge(&u, m, xx, yy, v % EDGE_COUNT); break;
                    case 2: undo_set_hedge(&u, m, xx, yy, v % EDGE_COUNT); break;
                    case 3: undo_set_fog(&u, m, xx, yy, (uint8_t)(v % 2)); break;
                    }
                }
                if (v == 5) undo_abort(&u, m); else undo_end(&u);
                break;
            case 4: case 5: undo_undo(&u, m); break;
            case 6: undo_redo(&u, m); break;
            case 7: if (v == 0) fog_delete(m, 1); break;
            }
        }
        ChangeSet a, b;
        cs_init(&a);
        cs_init(&b);
        checkpoint_changes(m, &a);
        cs_diff(&b, base, m, NULL);
        if (!cells_same(&a, &b) || a.ntoks != b.ntoks) ok = 0;
        cs_free(&a);
        cs_free(&b);
        map_free(base);
        map_free(m);
        undo_free(&u);
    }
    CHECK(ok);
}

/* The writers that build on the log -- a change set's accept, a stamp --
 * seen by a checkpoint like any edit. */
static void cs_checkpoint_writers(void)
{
    CASE("an accept and a stamp, with a checkpoint running: the checkpoint's cells are the full diff's");
    Map *m = encounter();
    Map *c = map_copy(m);
    Undo scratch, u;
    undo_init(&scratch);
    undo_init(&u);
    agent_plan(c, &scratch);
    ChangeSet cs;
    cs_init(&cs);
    cs_diff(&cs, m, c, &scratch);
    checkpoint_start(m);
    Map *base = map_copy(m);
    cs_apply(&cs, m, &u, NULL, NULL, 0);
    Map *st = stamp_copy(m, 7, 1, 10, 4);                     /* a corner of the room, nobody in it */
    char err[80];
    CHECK(st && stamp_place(m, &u, st, 25, 18, err, sizeof err) == 1);
    ChangeSet a, b;
    cs_init(&a);
    cs_init(&b);
    checkpoint_changes(m, &a);
    cs_diff(&b, base, m, NULL);
    CHECK(cells_same(&a, &b));
    CHECK(a.ncells > 0);
    cs_free(&a);
    cs_free(&b);
    cs_free(&cs);
    map_free(st);
    map_free(base);
    map_free(c);
    map_free(m);
    undo_free(&scratch);
    undo_free(&u);
}

void test_changeset(void)
{
    cs_checkpoint_writers();
    cs_checkpoint();
    cs_checkpoint_differential();
    cs_edges_and_turns();
    cs_small_parts();
    cs_basics();
    cs_conflicts();
    cs_partial();
    cs_preview();
    cs_differential();
}
