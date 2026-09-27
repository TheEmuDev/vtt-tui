/* Tests: play mode: covering, sizes, selection, groups, the brush, the cursor, occupancy, terrain, coordinates, shapes, the key tables, cycling and searching. */

#include "harness.h"

/* -------------------------------------------------------------- covering */

void test_covering(void)
{
    Map *m = map_new(14, 10, "cover");
    map_fill_tiles(m, 0, 0, 13, 9, TILE_FLOOR);

    Undo u;
    undo_init(&u);

    /* Three creatures in a row, so a 3x3 cursor at (2,2) covers all three and
     * a 1x1 cursor covers exactly one. */
    Token a = { 2, 2, 1, TOKEN_PLAYER, "Aria", { { 0, "" } }, 0 };
    Token b = { 3, 2, 1, TOKEN_ENEMY,  "Bram", { { 0, "" } }, 0 };
    Token c = { 4, 2, 1, TOKEN_PLAYER, "Cara", { { 0, "" } }, 0 };
    int ai = undo_add_token(&u, m, a);
    int bi = undo_add_token(&u, m, b);
    int ci = undo_add_token(&u, m, c);

    CASE("a 1x1 cursor covers the one creature standing there");
    CHECK_EQ(tokens_covered_next(&m->tokens, 2, 2, 1, -1), ai);
    CHECK_EQ(tokens_covered_next(&m->tokens, 2, 2, 1, ai), ai);
    CHECK_EQ(tokens_covered_next(&m->tokens, 9, 9, 1, -1), -1);

    CASE("a 3x3 cursor covers all three, and enter walks the ring");
    CHECK_EQ(tokens_covered_next(&m->tokens, 2, 2, 3, -1), ai);
    CHECK_EQ(tokens_covered_next(&m->tokens, 2, 2, 3, ai), bi);
    CHECK_EQ(tokens_covered_next(&m->tokens, 2, 2, 3, bi), ci);
    CHECK_EQ(tokens_covered_next(&m->tokens, 2, 2, 3, ci), ai);   /* wraps */

    CASE("a block covering nothing offers nothing, whatever was chosen before");
    CHECK_EQ(tokens_covered_next(&m->tokens, 8, 6, 3, bi), -1);

    undo_free(&u);
    map_free(m);
}

/* The whole gesture, driven through the key handler the way a GM drives it:
 * enlarge the cursor, enter to walk the creatures under it, then a movement
 * key to settle on one. */
void test_choosing(void)
{
    Renderer r;
    App      a;

    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    if (app_open_map(&a, "tests/fixtures/crowd.vtt") != 0) {
        g_fails++;
        fprintf(stderr, "  FAIL [choosing] could not open tests/fixtures/crowd.vtt\n");
        rnd_free(&r);
        return;
    }

    Map  *m = a.map;
    Play *p = &a.play;
    int   ai = 0, bi = 1, ci = 2;      /* Aria, Bram, Cara, in file order */

    /* The parser turns \r into KEY_ENTER before the app ever sees it, so the
     * test has to hand over the same key the event loop would. */
    #define K(c)   do { Key k = { KEY_CHAR,  0, (uint32_t)(c) }; app_key(&a, k); } while (0)
    #define ENT()  do { Key k = { KEY_ENTER, 0, 0 };             app_key(&a, k); } while (0)
    #define ESC()  do { Key k = { KEY_ESC,   0, 0 };             app_key(&a, k); } while (0)

    a.screen = SCREEN_PLAY;
    a.ed.cx = 2; a.ed.cy = 2;
    p->next_size = 3;

    CASE("enter under a big cursor picks one without moving the cursor");
    ENT();
    CHECK_EQ(p->sel, ai);
    CHECK_EQ(p->grabbed, 1);
    CHECK_EQ(p->choosing, 1);
    CHECK_EQ(a.ed.cx, 2);
    CHECK_EQ(a.ed.cy, 2);

    CASE("and the cursor keeps its own size rather than the creature's");
    CHECK_EQ(play_cursor_size(p, m), 3);

    CASE("enter again walks to the next, still without moving");
    ENT();
    CHECK_EQ(p->sel, bi);
    CHECK_EQ(p->choosing, 1);
    CHECK_EQ(a.ed.cx, 2);
    CHECK_EQ(play_cursor_size(p, m), 3);

    ENT();
    CHECK_EQ(p->sel, ci);
    ENT();
    CHECK_EQ(p->sel, ai);                      /* round again */

    CASE("nothing has moved while the choice is being made");
    CHECK_EQ(m->tokens.v[ai].x, 2);
    CHECK_EQ(m->tokens.v[bi].x, 3);
    CHECK_EQ(m->tokens.v[ci].x, 4);

    CASE("a movement key settles it: the cursor takes the creature and its size");
    K('j');
    CHECK_EQ(p->choosing, 0);
    CHECK_EQ(p->sel, ai);
    CHECK_EQ(m->tokens.v[ai].y, 3);            /* it moved with the key */
    CHECK_EQ(a.ed.cx, m->tokens.v[ai].x);
    CHECK_EQ(a.ed.cy, m->tokens.v[ai].y);
    CHECK_EQ(play_cursor_size(p, m), 1);

    CASE("the offer to walk the crowd goes when it stops being true");
    CHECK(strstr(a.status, "enter for the next") == NULL);

    CASE("once settled, enter goes back to meaning put it down");
    ENT();
    CHECK_EQ(p->grabbed, 0);

    /* A 1x1 cursor over a lone creature is not a choice, so it behaves the
     * way it always did: picked up, cursor on it, enter drops. */
    CASE("one candidate is picked up outright, with no choice to make");
    p->next_size = 1;
    a.ed.cx = 3; a.ed.cy = 2;
    ENT();
    CHECK_EQ(p->sel, bi);
    CHECK_EQ(p->grabbed, 1);
    CHECK_EQ(p->choosing, 0);
    CHECK_EQ(play_cursor_size(p, m), 1);
    ENT();
    CHECK_EQ(p->grabbed, 0);

    CASE("esc while choosing stops choosing and leaves the cursor put");
    p->next_size = 3;
    a.ed.cx = 2; a.ed.cy = 2;
    ENT();
    CHECK_EQ(p->choosing, 1);
    ESC();
    CHECK_EQ(p->grabbed, 0);
    CHECK_EQ(p->choosing, 0);
    CHECK_EQ(p->sel, -1);
    CHECK_EQ(a.ed.cx, 2);
    CHECK_EQ(a.ed.cy, 2);

    CASE("an empty square under a big cursor still says so");
    a.ed.cx = 9; a.ed.cy = 6;
    ENT();
    CHECK_EQ(p->sel, -1);
    CHECK_EQ(p->grabbed, 0);

    /* Canceling has to reach back past the choice as well as the walk, or a
     * creature picked out of a crowd could not be put back. */
    CASE("esc after settling still returns the creature to where it set out");
    a.ed.cx = 2; a.ed.cy = 2;
    p->next_size = 3;
    ENT();
    ENT();                                   /* choose Bram */
    CHECK_EQ(p->sel, bi);
    int bx = m->tokens.v[bi].x, by = m->tokens.v[bi].y;
    K('j'); K('j');
    CHECK(m->tokens.v[bi].y != by);
    ESC();
    CHECK_EQ(m->tokens.v[bi].x, bx);
    CHECK_EQ(m->tokens.v[bi].y, by);
    CHECK_EQ(p->grabbed, 0);

    #undef K
    #undef ENT
    #undef ESC
    app_free(&a);
    rnd_free(&r);
}

/* ------------------------------------------------------------- sizekeys */

/* The size keys, the cursor and a selected creature all read one number, and
 * the cursor reaches everything it covers. Three bugs that were really one:
 * the cursor and the thing it acts on had drifted apart. */
void test_size_keys(void)
{
    Renderer r;
    App      a;

    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    if (app_open_map(&a, "tests/fixtures/crowd.vtt") != 0) {
        g_fails++;
        fprintf(stderr, "  FAIL [sizekeys] could not open tests/fixtures/crowd.vtt\n");
        rnd_free(&r);
        return;
    }

    Map  *m  = a.map;
    Play *p  = &a.play;
    int   di = 4;                  /* Dax, alone at (9,2) with room to grow */
    int   oi = 3;                  /* the 2x2 Ogre spanning (4..5, 6..7)   */

    #define K(c)   do { Key k = { KEY_CHAR,  0, (uint32_t)(c) }; app_key(&a, k); } while (0)
    #define ENT()  do { Key k = { KEY_ENTER, 0, 0 };             app_key(&a, k); } while (0)
    #define ESC()  do { Key k = { KEY_ESC,   0, 0 };             app_key(&a, k); } while (0)

    a.screen = SCREEN_PLAY;

    /* Being held used to disqualify a creature from being resized, which made
     * "this one is Large actually" a put-down-and-pick-up. */
    CASE("a creature can be resized while it is being carried");
    a.ed.cx = 9; a.ed.cy = 2;
    ENT();
    CHECK_EQ(p->sel, di);
    CHECK_EQ(p->grabbed, 1);

    K('3'); K('b');                          /* a count names the size */
    CHECK_EQ(m->tokens.v[di].size, 3);
    CHECK_EQ(p->grabbed, 1);                  /* still in hand */

    CASE("and the cursor is the new size at once, not after putting it down");
    CHECK_EQ(play_cursor_size(p, m), 3);

    CASE("the size setting follows, so the cursor matches after the drop too");
    CHECK_EQ(p->next_size, 3);
    ENT();
    CHECK_EQ(p->grabbed, 0);
    CHECK_EQ(play_cursor_size(p, m), 3);

    CASE("shrinking works the same way");
    K('1'); K('b');
    CHECK_EQ(m->tokens.v[di].size, 1);
    CHECK_EQ(p->next_size, 1);
    CHECK_EQ(play_cursor_size(p, m), 1);

    /* A refused resize must not move the setting either, or the cursor would
     * grow past a creature that did not -- which is the mismatch the whole
     * change is about. */
    CASE("a refused resize leaves the setting where it was");
    ESC();
    a.ed.cx = 2; a.ed.cy = 2;
    p->next_size = 1;
    ENT();                                     /* Aria, boxed in by Bram */
    CHECK_EQ(p->sel, 0);
    K('3'); K('b');
    CHECK_EQ(m->tokens.v[0].size, 1);
    CHECK_EQ(p->next_size, 1);
    CHECK(strstr(a.status, "not enough room") != NULL);
    ESC();                                     /* let go */
    ESC();                                     /* and deselect: d prefers the
                                                * selection over the cursor */
    CHECK_EQ(p->sel, -1);

    /* A 3x3 cursor at (3,5) covers (3..5, 5..7); the Ogre spans (4..5, 6..7)
     * and so sits in its lower-right. It used to be invisible to every
     * command, because the lookup only ever read the cursor's own corner. */
    CASE("a command reaches a creature merely overlapping a big cursor");
    p->next_size = 3;
    a.ed.cx = 3; a.ed.cy = 5;
    CHECK_EQ(tokens_at(&m->tokens, 3, 5), -1);        /* the old lookup missed */
    CHECK_EQ(tokens_covered_next(&m->tokens, 3, 5, 3, -1), oi);

    int before = m->tokens.n;
    K('d');
    CHECK_EQ(m->tokens.n, before - 1);
    CHECK_EQ(p->nyank, 1);
    CHECK_EQ(strcmp(p->yank[0].label, "Ogre"), 0);

    CASE("and the corner square alone still finds nothing when the cursor is small");
    p->next_size = 1;
    K('p');                                    /* put the Ogre back */
    CHECK_EQ(m->tokens.n, before);

    #undef K
    #undef ENT
    #undef ESC
    app_free(&a);
    rnd_free(&r);
}

/* ------------------------------------------------------------- selection */

/* Relative luminance, the WCAG definition, so "higher contrast" can be a
 * number in a test rather than an opinion about a screen. */
static double luminance(uint32_t c)
{
    double ch[3];
    ch[0] = ((c >> 16) & 0xFFu) / 255.0;
    ch[1] = ((c >> 8)  & 0xFFu) / 255.0;
    ch[2] = ( c        & 0xFFu) / 255.0;

    for (int i = 0; i < 3; i++)
        ch[i] = ch[i] <= 0.04045 ? ch[i] / 12.92
                                 : pow((ch[i] + 0.055) / 1.055, 2.4);

    return 0.2126 * ch[0] + 0.7152 * ch[1] + 0.0722 * ch[2];
}

double contrast(uint32_t a, uint32_t b)
{
    double la = luminance(a), lb = luminance(b);
    if (la < lb) { double t = la; la = lb; lb = t; }
    return (la + 0.05) / (lb + 0.05);
}

void test_selection_contrast(void)
{
    const Theme *th = &THEME_DARK;

    /* Lightening the base by a third -- what this used to do -- put the
     * selected player at 1.19:1 against the plain one, which is very nearly
     * no difference at all. */
    CASE("the selected colors are far enough from the plain ones to see");
    CHECK(contrast(th->player, th->player_sel) > 1.4);
    CHECK(contrast(th->enemy,  th->enemy_sel)  > 1.7);

    CASE("and they are still legible against the page");
    CHECK(contrast(th->player_sel, th->bg) > 10.0);
    CHECK(contrast(th->enemy_sel,  th->bg) > 10.0);

    /* The fill cannot carry it on its own: green is near the top of the
     * luminance range, so even white-green tops out under the 3:1 a UI
     * element wants. The ring is what makes up the difference, and it is
     * measured against the page rather than against the token. */
    CASE("the ring is the part that carries the contrast");
    CHECK(contrast(th->player_sel, th->bg) > 3.0 * contrast(th->player, th->player_sel));

    Map *m = map_new(12, 8, "sel");
    map_fill_tiles(m, 0, 0, 11, 7, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Play p;
    play_init(&p);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    rnd_set_clear(&r, th->fg, th->bg);

    Editor e;
    ed_init(&e, m);
    e.labels = 0;
    ed_layout(&e, m, 80, 24);

    Token a = { 4, 3, 1, TOKEN_PLAYER, "Aria", { { 0, "" } }, 0 };
    int ai = undo_add_token(&u, m, a);

    Rect area;
    grid_token_area(&e.view, 4, 3, 1, &area);

    /* Corners are left out: they are shared with the cursor's own marks, so
     * asserting on them would be testing two things at once. */
    #define RING_MID(v) do {                                                   \
        (v)[0] = rnd_at(&r, area.x, area.y - 1);                               \
        (v)[1] = rnd_at(&r, area.x, area.y + area.h);                          \
        (v)[2] = rnd_at(&r, area.x - 1, area.y);                               \
        (v)[3] = rnd_at(&r, area.x + area.w, area.y);                          \
    } while (0)

    CASE("an unselected creature has no ring");
    play_focus(&p, -1);
    rnd_begin(&r);
    play_draw(&r, m, &e, &p, th, 0, 0);
    Cell *ring[4];
    RING_MID(ring);
    for (int i = 0; i < 4; i++) {
        CHECK(ring[i] != NULL);
        CHECK(ring[i]->fg != th->player_sel);
    }

    CASE("a selected one is ringed on the grid lines around it");
    play_focus(&p, ai);
    e.cx = 0; e.cy = 0;               /* cursor elsewhere: the ring is the
                                       * only cue left, which is the case
                                       * that was failing */
    rnd_begin(&r);
    play_draw(&r, m, &e, &p, th, 0, 0);
    RING_MID(ring);
    for (int i = 0; i < 4; i++) {
        CHECK(ring[i] != NULL);
        CHECK_EQ(ring[i]->fg, th->player_sel);
        CHECK(ring[i]->attr & ATTR_BOLD);
    }

    CASE("the ring recolors the lattice rather than painting over it");
    CHECK(ring[2]->ch != ' ');         /* still a box-drawing glyph */

    CASE("an enemy is ringed in its own color, not the player's");
    Token b = { 8, 3, 1, TOKEN_ENEMY, "Bram", { { 0, "" } }, 0 };
    int bi = undo_add_token(&u, m, b);
    play_focus(&p, bi);
    grid_token_area(&e.view, 8, 3, 1, &area);
    rnd_begin(&r);
    play_draw(&r, m, &e, &p, th, 0, 0);
    RING_MID(ring);
    CHECK_EQ(ring[0]->fg, th->enemy_sel);

    #undef RING_MID
    rnd_free(&r);
    undo_free(&u);
    map_free(m);
}

/* ---------------------------------------------------------------- group */

/* Selecting several creatures with v and acting on them as one. The fixture
 * has Aria, Bram and Cara abreast at (2..4, 2), a 2x2 Ogre at (4..5, 6..7)
 * and Dax alone at (9,2). */
void test_group(void)
{
    Renderer r;
    App      a;

    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    if (app_open_map(&a, "tests/fixtures/crowd.vtt") != 0) {
        g_fails++;
        fprintf(stderr, "  FAIL [group] could not open tests/fixtures/crowd.vtt\n");
        rnd_free(&r);
        return;
    }

    Map  *m = a.map;
    Play *p = &a.play;

    #define K(c)   do { Key k = { KEY_CHAR,  0, (uint32_t)(c) }; app_key(&a, k); } while (0)
    #define ENT()  do { Key k = { KEY_ENTER, 0, 0 };             app_key(&a, k); } while (0)
    #define ESC()  do { Key k = { KEY_ESC,   0, 0 };             app_key(&a, k); } while (0)

    a.screen = SCREEN_PLAY;

    CASE("the box catches every creature whose footprint meets it");
    int idx[PLAY_GROUP_MAX];
    CHECK_EQ(play_box_tokens(m, 2, 2, 4, 2, idx, PLAY_GROUP_MAX), 3);
    CHECK_EQ(idx[0], 0); CHECK_EQ(idx[1], 1); CHECK_EQ(idx[2], 2);

    /* A creature only lapping into the box counts, the same reading the
     * cursor uses for the square it stands on. */
    CASE("a big creature lapping into the box is caught, not missed");
    CHECK_EQ(play_box_tokens(m, 5, 7, 6, 8, idx, PLAY_GROUP_MAX), 1);
    CHECK_EQ(idx[0], 3);                       /* the 2x2 Ogre */

    CASE("an empty box catches nothing");
    CHECK_EQ(play_box_tokens(m, 11, 8, 13, 9, idx, PLAY_GROUP_MAX), 0);

    CASE("v opens a box and enter carries everything in it");
    a.ed.cx = 2; a.ed.cy = 2;
    K('v');
    CHECK_EQ(p->visual, 1);
    K('l'); K('l');                            /* out to Cara */
    CHECK_EQ(a.ed.cx, 4);
    ENT();
    CHECK_EQ(p->visual, 0);
    CHECK_EQ(p->grabbed, 1);
    CHECK_EQ(p->ngroup, 3);
    CHECK_EQ(p->sel, 0);                       /* the primary is group[0] */

    CASE("they walk together");
    K('j');
    CHECK_EQ(m->tokens.v[0].y, 3);
    CHECK_EQ(m->tokens.v[1].y, 3);
    CHECK_EQ(m->tokens.v[2].y, 3);

    /* Without transparency inside the group a column could never move at
     * all: each creature is blocked by the one in front of it. */
    CASE("the group is transparent to itself, so a packed line can move");
    K('l');
    CHECK_EQ(m->tokens.v[0].x, 3);
    CHECK_EQ(m->tokens.v[1].x, 4);
    CHECK_EQ(m->tokens.v[2].x, 5);

    CASE("esc cancels the whole walk, not just the primary's");
    ESC();
    CHECK_EQ(m->tokens.v[0].x, 2); CHECK_EQ(m->tokens.v[0].y, 2);
    CHECK_EQ(m->tokens.v[1].x, 3); CHECK_EQ(m->tokens.v[1].y, 2);
    CHECK_EQ(m->tokens.v[2].x, 4); CHECK_EQ(m->tokens.v[2].y, 2);

    CASE("the group survives the cancel, so it can be walked again");
    CHECK_EQ(p->ngroup, 3);
    CHECK_EQ(p->grabbed, 0);

    /* Blocked for one is blocked for all: a formation that half-moves is not
     * a formation. Dax at (9,2) is an outsider standing in Cara's way. */
    CASE("blocked for one is blocked for all");
    a.ed.cx = 6; a.ed.cy = 2;
    ESC();                                     /* clear the group */
    CHECK_EQ(p->ngroup, 0);

    /* An enemy, because same-kind creatures step through each other on the
     * way past -- only the other side is a wall. */
    m->tokens.v[4].kind = TOKEN_ENEMY;
    m->tokens.v[4].x = 5; m->tokens.v[4].y = 2;
    a.ed.cx = 2; a.ed.cy = 2;
    K('v'); K('l'); K('l'); ENT();
    CHECK_EQ(p->ngroup, 3);
    K('l');
    CHECK_EQ(m->tokens.v[0].x, 2);             /* nobody moved */
    CHECK_EQ(m->tokens.v[1].x, 3);
    CHECK_EQ(m->tokens.v[2].x, 4);
    CHECK(strstr(a.status, "blocked") != NULL);

    CASE("the direction that is clear for all still works");
    K('j');
    CHECK_EQ(m->tokens.v[0].y, 3);
    CHECK_EQ(m->tokens.v[2].y, 3);
    ESC();
    ESC();

    #undef K
    #undef ENT
    #undef ESC
    app_free(&a);
    rnd_free(&r);
}

/* Yank, delete and paste over a whole formation. */
void test_group_yank(void)
{
    Renderer r;
    App      a;

    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    if (app_open_map(&a, "tests/fixtures/crowd.vtt") != 0) {
        g_fails++;
        fprintf(stderr, "  FAIL [groupyank] could not open the fixture\n");
        rnd_free(&r);
        return;
    }

    Map  *m = a.map;
    Play *p = &a.play;

    #define K(c)   do { Key k = { KEY_CHAR,  0, (uint32_t)(c) }; app_key(&a, k); } while (0)
    #define ESC()  do { Key k = { KEY_ESC,   0, 0 };             app_key(&a, k); } while (0)

    a.screen = SCREEN_PLAY;
    int before = m->tokens.n;

    CASE("y takes everything in the box");
    a.ed.cx = 2; a.ed.cy = 2;
    K('v'); K('l'); K('l');
    K('y');
    CHECK_EQ(p->nyank, 3);
    CHECK_EQ(p->visual, 0);                    /* the box closes behind it */
    CHECK_EQ(m->tokens.n, before);             /* a yank removes nothing */
    CHECK(strstr(a.status, "3 creatures") != NULL);

    CASE("paste stamps the formation out in the shape it was copied in");
    a.ed.cx = 2; a.ed.cy = 4;                  /* clear ground */
    K('p');
    CHECK_EQ(m->tokens.n, before + 3);
    CHECK_EQ(m->tokens.v[before + 0].x, 2);
    CHECK_EQ(m->tokens.v[before + 1].x, 3);
    CHECK_EQ(m->tokens.v[before + 2].x, 4);
    CHECK_EQ(m->tokens.v[before + 0].y, 4);
    CHECK_EQ(m->tokens.v[before + 2].y, 4);

    CASE("the copies are the new selection, ready to be walked off");
    CHECK_EQ(p->ngroup, 3);

    /* All or nothing: a paste that half-arrives leaves the GM working out
     * which half. The 2x2 Ogre at (4..5, 6..7) is in the way of the third. */
    CASE("a paste is refused outright when one creature has no room");
    int n_before = m->tokens.n;
    ESC();
    a.ed.cx = 2; a.ed.cy = 6;                  /* the 2x2 Ogre owns (4,6) */
    K('p');
    CHECK_EQ(m->tokens.n, n_before);           /* not one of them landed */
    CHECK(strstr(a.status, "formation") != NULL ||
          strstr(a.status, "in the way") != NULL);

    CASE("d removes the whole box and keeps it to paste");
    a.ed.cx = 2; a.ed.cy = 4;
    K('v'); K('l'); K('l');
    K('d');
    CHECK_EQ(m->tokens.n, n_before - 3);
    CHECK_EQ(p->nyank, 3);
    CHECK_EQ(p->ngroup, 0);
    CHECK(strstr(a.status, "removed 3 creatures") != NULL);

    CASE("and p puts them all back");
    a.ed.cx = 2; a.ed.cy = 4;
    K('p');
    CHECK_EQ(m->tokens.n, n_before);

    CASE("one creature in a box is still just one creature");
    ESC();
    a.ed.cx = 9; a.ed.cy = 2;
    K('v');
    K('y');
    CHECK_EQ(p->nyank, 1);
    CHECK(strstr(a.status, "Dax") != NULL);

    #undef K
    #undef ESC
    app_free(&a);
    rnd_free(&r);
}

/* ---------------------------------------------------------------- brush */

/* The sized cursor in build mode, and the size key both modes now share. */
void test_brush(void)
{
    Renderer r;
    App      a;

    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    if (app_open_map(&a, "tests/fixtures/crowd.vtt") != 0) {
        g_fails++;
        fprintf(stderr, "  FAIL [brush] could not open tests/fixtures/crowd.vtt\n");
        rnd_free(&r);
        return;
    }

    Map    *m = a.map;
    Editor *e = &a.ed;

    #define K(c) do { Key k = { KEY_CHAR, 0, (uint32_t)(c) }; app_key(&a, k); } while (0)

    a.screen = SCREEN_EDITOR;
    e->mode  = ED_NORMAL;

    CASE("b cycles the brush and wraps; B cycles back; a count names it");
    CHECK_EQ(e->brush, 1);
    K('b'); CHECK_EQ(e->brush, 2);
    K('b'); CHECK_EQ(e->brush, 3);
    K('b'); CHECK_EQ(e->brush, 1);
    K('B'); CHECK_EQ(e->brush, 3);
    K('2'); K('b'); CHECK_EQ(e->brush, 2);
    K('9'); K('b'); CHECK_EQ(e->brush, 3);   /* clamped to the largest */

    CASE("counts on motions survive the size key taking b");
    e->cx = 2; e->cy = 2;
    K('3'); K('j');
    CHECK_EQ(e->cy, 5);
    K('1'); K('0'); K('l');                  /* 10l: multi-digit still works */
    CHECK_EQ(e->cx, 12);

    CASE("f paints the brush's whole footprint, as one undo step");
    K('2'); K('b');
    e->cx = 6; e->cy = 6;
    map_fill_tiles(m, 0, 0, m->w - 1, m->h - 1, TILE_VOID);
    e->terrain = TILE_FLOOR;
    K('f');
    CHECK_EQ(map_tile(m, 6, 6), TILE_FLOOR);
    CHECK_EQ(map_tile(m, 7, 7), TILE_FLOOR);
    CHECK_EQ(map_tile(m, 8, 8), TILE_VOID);  /* outside the 2x2 */
    K('u');
    CHECK_EQ(map_tile(m, 6, 6), TILE_VOID);  /* one step took all four */

    CASE("a brush hanging over the edge paints the part that exists");
    e->cx = m->w - 1; e->cy = 6;
    K('3'); K('b');
    K('f');
    CHECK_EQ(map_tile(m, m->w - 1, 6), TILE_FLOOR);
    CHECK_EQ(map_tile(m, m->w - 1, 8), TILE_FLOOR);

    CASE("L walls the brush's whole east face");
    e->cx = 3; e->cy = 3;
    CHECK_EQ(e->brush, 3);
    K('L');
    CHECK_EQ(map_vedge(m, 6, 3), EDGE_WALL);
    CHECK_EQ(map_vedge(m, 6, 4), EDGE_WALL);
    CHECK_EQ(map_vedge(m, 6, 5), EDGE_WALL);
    CHECK_EQ(map_vedge(m, 6, 2), EDGE_NONE);  /* the face, not the column */
    CHECK_EQ(map_vedge(m, 6, 6), EDGE_NONE);

    CASE("the same key again takes the whole face away");
    K('L');
    CHECK_EQ(map_vedge(m, 6, 3), EDGE_NONE);
    CHECK_EQ(map_vedge(m, 6, 5), EDGE_NONE);

    CASE("a partly built face is completed rather than dismantled");
    map_set_vedge(m, 6, 4, EDGE_WALL);
    K('L');
    CHECK_EQ(map_vedge(m, 6, 3), EDGE_WALL);
    CHECK_EQ(map_vedge(m, 6, 4), EDGE_WALL);
    CHECK_EQ(map_vedge(m, 6, 5), EDGE_WALL);

    CASE("one press of a face is one undo step, whatever the brush");
    K('u');
    CHECK_EQ(map_vedge(m, 6, 3), EDGE_NONE);
    CHECK_EQ(map_vedge(m, 6, 4), EDGE_WALL);  /* the hand-laid edge survives:
                                               * undo takes back the press,
                                               * not the wall it built on */

    CASE("K walls the north face, J the south, H the west");
    map_rect_walls(m, 0, 0, m->w - 1, m->h - 1, EDGE_NONE);
    e->cx = 3; e->cy = 3;
    K('K');
    CHECK_EQ(map_hedge(m, 3, 3), EDGE_WALL);
    CHECK_EQ(map_hedge(m, 5, 3), EDGE_WALL);
    K('J');
    CHECK_EQ(map_hedge(m, 3, 6), EDGE_WALL);
    K('H');
    CHECK_EQ(map_vedge(m, 3, 3), EDGE_WALL);
    CHECK_EQ(map_vedge(m, 3, 5), EDGE_WALL);

    CASE("space clears the brush's footprint back to void together");
    map_fill_tiles(m, 0, 0, m->w - 1, m->h - 1, TILE_FLOOR);
    e->cx = 6; e->cy = 6;
    K(' ');
    CHECK_EQ(map_tile(m, 6, 6), TILE_VOID);
    CHECK_EQ(map_tile(m, 8, 8), TILE_VOID);
    CHECK_EQ(map_tile(m, 9, 9), TILE_FLOOR);

    CASE("the visual box ignores the brush -- its own two corners rule");
    K('v');
    CHECK_EQ(e->mode, ED_VISUAL);
    Key esc = { KEY_ESC, 0, 0 };
    app_key(&a, esc);
    CHECK_EQ(e->mode, ED_NORMAL);

    CASE("play mode: the same digits are counts and the same b is the size");
    a.screen = SCREEN_PLAY;
    play_focus(&a.play, -1);
    a.ed.cx = 2; a.ed.cy = 2;
    K('3'); K('l');
    CHECK_EQ(a.ed.cx, 5);
    K('2'); K('b');
    CHECK_EQ(a.play.next_size, 2);

    CASE("b on a selected creature cycles up from the size it already is");
    play_focus(&a.play, 3);                    /* the 2x2 Ogre */
    K('b');
    CHECK_EQ(m->tokens.v[3].size, 3);
    K('b');                                    /* wraps past the top */
    CHECK_EQ(m->tokens.v[3].size, 1);

    #undef K
    app_free(&a);
    rnd_free(&r);
}

/* ------------------------------------------------------------ cursorsize */

/* The cursor's tinted footprint, measured off the frame rather than trusted
 * from the code that drew it. */
static void cursor_extent(const Renderer *r, const Theme *th, int *w, int *h)
{
    int x0 = r->w, y0 = r->h, x1 = -1, y1 = -1;

    for (int y = 0; y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            const Cell *c = rnd_at((Renderer *)r, x, y);
            if (!c || c->bg != th->cursor_bg) continue;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
    }

    *w = (x1 < x0) ? 0 : x1 - x0 + 1;
    *h = (y1 < y0) ? 0 : y1 - y0 + 1;
}

void test_cursor_size(void)
{
    Map *m = map_new(12, 9, "cursor");
    map_fill_tiles(m, 0, 0, 11, 8, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Play p;
    play_init(&p);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 90, 24);
    rnd_set_clear(&r, THEME_DARK.fg, THEME_DARK.bg);

    Editor e;
    ed_init(&e, m);
    e.labels = 0;
    ed_layout(&e, m, 90, 24);
    e.cx = 2; e.cy = 2;

    int pw = 0, ph = 0, cw, ch;
    {   /* One tile's worth, taken from the view rather than hard-coded, so
         * this says what it means at any zoom. */
        Rect a;
        grid_token_area(&e.view, 0, 0, 1, &a);
        pw = a.w; ph = a.h;
    }

    #define FRAME() do {                              \
        rnd_begin(&r);                                \
        play_draw(&r, m, &e, &p, &THEME_DARK, 0, 0);     \
    } while (0)

    CASE("with nothing selected the cursor is the size the next token will be");
    CHECK_EQ(play_cursor_size(&p, m), 1);
    FRAME();
    cursor_extent(&r, &THEME_DARK, &cw, &ch);
    CHECK_EQ(cw, pw);
    CHECK_EQ(ch, ph);

    p.next_size = 2;
    CHECK_EQ(play_cursor_size(&p, m), 2);
    FRAME();
    cursor_extent(&r, &THEME_DARK, &cw, &ch);
    CHECK_EQ(cw, 2 * (pw + 1) - 1);
    CHECK_EQ(ch, 2 * (ph + 1) - 1);

    p.next_size = 3;
    FRAME();
    cursor_extent(&r, &THEME_DARK, &cw, &ch);
    CHECK_EQ(cw, 3 * (pw + 1) - 1);
    CHECK_EQ(ch, 3 * (ph + 1) - 1);

    /* A footprint hanging off the map is one play_can_place refuses, so the
     * cursor shows only the squares that are actually there. */
    CASE("against the edge it is clipped to the map rather than overhanging");
    e.cx = m->w - 2; e.cy = 2;
    FRAME();
    cursor_extent(&r, &THEME_DARK, &cw, &ch);
    CHECK_EQ(cw, 2 * (pw + 1) - 1);
    CHECK_EQ(play_can_place(m, e.cx, e.cy, 3, -1), 0);

    e.cx = 2; e.cy = 2;
    p.next_size = 1;

    CASE("carrying a big creature the cursor is that creature's size");
    Token big = { 2, 2, 3, TOKEN_ENEMY, "Ogre", { { 0, "" } }, 0 };
    int bi = undo_add_token(&u, m, big);
    play_focus(&p, bi);
    play_grab(&p, m, u.depth);

    CHECK_EQ(play_cursor_size(&p, m), 3);
    FRAME();
    cursor_extent(&r, &THEME_DARK, &cw, &ch);
    CHECK_EQ(cw, 3 * (pw + 1) - 1);
    CHECK_EQ(ch, 3 * (ph + 1) - 1);

    CASE("the size setting is untouched by the pickup");
    CHECK_EQ(p.next_size, 1);

    CASE("putting it down returns the cursor to the size that was set");
    p.grabbed = 0;
    CHECK_EQ(play_cursor_size(&p, m), 1);
    FRAME();
    cursor_extent(&r, &THEME_DARK, &cw, &ch);
    CHECK_EQ(cw, pw);
    CHECK_EQ(ch, ph);

    /* The four corner marks belong to the block, not to its top-left tile,
     * or a big cursor would be marked in one corner and bare in three. */
    CASE("the corner marks sit on the corners of the whole block");
    play_focus(&p, -1);
    p.next_size = 3;
    e.cx = 1; e.cy = 1;
    FRAME();
    Rect blk;
    grid_token_area(&e.view, 1, 1, 3, &blk);
    CHECK_EQ(rnd_at(&r, blk.x - 1, blk.y - 1)->fg, THEME_DARK.accent);
    CHECK_EQ(rnd_at(&r, blk.x + blk.w, blk.y - 1)->fg, THEME_DARK.accent);
    CHECK_EQ(rnd_at(&r, blk.x - 1, blk.y + blk.h)->fg, THEME_DARK.accent);
    CHECK_EQ(rnd_at(&r, blk.x + blk.w, blk.y + blk.h)->fg, THEME_DARK.accent);

    #undef FRAME
    rnd_free(&r);
    undo_free(&u);
    map_free(m);
}

/* ------------------------------------------------------------- occupancy */

void test_overlap(void)
{
    TokenList l;
    memset(&l, 0, sizeof l);

    Token a = { 4, 4, 2, TOKEN_ENEMY,  "Ogre", { { 0, "" } }, 0 };
    Token b = { 9, 1, 1, TOKEN_PLAYER, "Aria", { { 0, "" } }, 0 };
    tokens_add(&l, a);
    tokens_add(&l, b);

    CASE("a block overlaps when both axes do");
    CHECK_EQ(tokens_overlapping(&l, 4, 4, 1, -1, TOKEN_ANY_KIND), 0);
    CHECK_EQ(tokens_overlapping(&l, 5, 5, 1, -1, TOKEN_ANY_KIND), 0);
    CHECK_EQ(tokens_overlapping(&l, 3, 3, 2, -1, TOKEN_ANY_KIND), 0);  /* a corner */
    CHECK_EQ(tokens_overlapping(&l, 6, 6, 2, -1, TOKEN_ANY_KIND), -1);

    CASE("touching is not overlapping");
    CHECK_EQ(tokens_overlapping(&l, 6, 4, 1, -1, TOKEN_ANY_KIND), -1);  /* just east */
    CHECK_EQ(tokens_overlapping(&l, 3, 4, 1, -1, TOKEN_ANY_KIND), -1);  /* just west */
    CHECK_EQ(tokens_overlapping(&l, 4, 6, 1, -1, TOKEN_ANY_KIND), -1);  /* just south */

    CASE("a token can be asked about the square it is already on");
    CHECK_EQ(tokens_overlapping(&l, 4, 4, 2, 0, TOKEN_ANY_KIND), -1);
    CHECK_EQ(tokens_overlapping(&l, 4, 4, 2, 1, TOKEN_ANY_KIND), 0);

    CASE("and about one side at a time");
    CHECK_EQ(tokens_overlapping(&l, 4, 4, 2, -1, TOKEN_PLAYER), -1);
    CHECK_EQ(tokens_overlapping(&l, 4, 4, 2, -1, TOKEN_ENEMY), 0);
    CHECK_EQ(tokens_overlapping(&l, 9, 1, 1, -1, TOKEN_PLAYER), 1);
    CHECK_EQ(tokens_overlapping(&l, 9, 1, 1, -1, TOKEN_ENEMY), -1);

    tokens_free(&l);
}

void test_passing_and_stopping(void)
{
    Map *m = map_new(16, 8, "pass");
    map_fill_tiles(m, 0, 0, 15, 7, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Play p;
    play_init(&p);

    Token a  = { 2, 2, 1, TOKEN_PLAYER, "Aria", { { 0, "" } }, 0 };
    Token b  = { 4, 2, 1, TOKEN_PLAYER, "Bram", { { 0, "" } }, 0 };
    Token og = { 8, 2, 1, TOKEN_ENEMY,  "Ogre", { { 0, "" } }, 0 };
    int ai = undo_add_token(&u, m, a);
    undo_add_token(&u, m, b);
    int oi = undo_add_token(&u, m, og);
    play_focus(&p, ai);
    /* An ally is somebody you squeeze past, not a wall. */
    CASE("a creature walks through its own side");
    CHECK_EQ(token_can_move(m, &m->tokens.v[ai], 1, 0, 1, ai), 1);
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 1);
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 1);
    CHECK_EQ(m->tokens.v[ai].x, 4);                 /* standing on Bram */
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 1);
    CHECK_EQ(m->tokens.v[ai].x, 5);                 /* and out the other side */

    CASE("but not through the other one");
    while (m->tokens.v[ai].x < 7) CHECK_EQ(play_step(m, &u, &p, 1, 0), 1);
    CHECK_EQ(token_can_move(m, &m->tokens.v[ai], 1, 0, 1, ai), 0);
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 0);
    CHECK_EQ(m->tokens.v[ai].x, 7);

    CASE("and the block is mutual");
    play_focus(&p, oi);
    CHECK_EQ(token_can_move(m, &m->tokens.v[oi], -1, 0, 1, oi), 0);
    play_focus(&p, ai);
    /* A big creature is stopped by anything its whole footprint would land
     * on, not only the square its anchor would. */
    CASE("a 2x2 is stopped by a token anywhere under its footprint");
    m->tokens.v[ai].size = 2;
    m->tokens.v[ai].x = 6; m->tokens.v[ai].y = 1;
    CHECK_EQ(token_can_move(m, &m->tokens.v[ai], 1, 0, 1, ai), 0);  /* 7,1..8,2 hits 8,2 */
    m->tokens.v[ai].y = 4;
    CHECK_EQ(token_can_move(m, &m->tokens.v[ai], 1, 0, 1, ai), 1);  /* clear of it */
    m->tokens.v[ai].size = 1;

    /* Coming to rest is the strict half: passing over is fine, sharing is not. */
    CASE("blocking switched off lets a creature through anything");
    m->tokens.v[ai].x = 7; m->tokens.v[ai].y = 2;
    p.enforce_walls = 0;
    CHECK_EQ(token_can_move(m, &m->tokens.v[ai], 1, 0, 0, ai), 1);
    p.enforce_walls = 1;

    undo_free(&u);
    map_free(m);
}

/* The route has to go round what the creature cannot walk through, or the
 * ribbon would promise a way past an enemy that the movement keys refuse. */
void test_route_avoids_enemies(void)
{
    Map *m = map_new(9, 5, "route");
    map_fill_tiles(m, 0, 0, 8, 4, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Play p;
    play_init(&p);

    Token a = { 0, 2, 1, TOKEN_PLAYER, "Aria", { { 0, "" } }, 0 };
    int ai = undo_add_token(&u, m, a);
    for (int y = 1; y <= 3; y++) {
        Token e = { 4, (int16_t)y, 1, TOKEN_ENEMY, "Line", { { 0, "" } }, 0 };
        undo_add_token(&u, m, e);
    }
    play_focus(&p, ai);
    play_grab(&p, m, 0);

    /* Walk round the wall of enemies the long way, over the top. */
    for (int i = 0; i < 2; i++) { undo_begin(&u); play_step(m, &u, &p, 0, -1); undo_end(&u); }
    for (int i = 0; i < 6; i++) { undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u); }
    for (int i = 0; i < 2; i++) { undo_begin(&u); play_step(m, &u, &p, 0, 1); undo_end(&u); }
    CHECK_EQ(m->tokens.v[ai].x, 6);
    CHECK_EQ(m->tokens.v[ai].y, 2);

    CASE("the route goes round the enemies, not through them");
    CHECK(p.ntrail > 0);
    for (int i = 0; i < p.ntrail; i++)
        CHECK(!(p.trail[i].x == 4 && p.trail[i].y >= 1 && p.trail[i].y <= 3));

    CASE("so it costs more than the straight line would");
    CHECK(p.steps > 6);

    undo_free(&u);
    map_free(m);
}

void test_occupancy_keys(void)
{
    Sandbox sb = sandbox_enter("occ");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    char path[600];
    snprintf(path, sizeof path, "%s/o.vtt", sb.dir);
    {
        FILE *f = fopen(path, "w");
        if (f) {
            fputs("VTT 2\nname Occ\nsize 12 5\nzoom 1\ntiles\n", f);
            for (int i = 0; i < 5; i++) fputs("............\n", f);
            fputs("token player 2 2 1 \"Aria\"\ntoken player 5 2 1 \"Bram\"\n"
                  "token enemy 8 2 1 \"Ogre\"\n", f);
            fclose(f);
        }
    }

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 96, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    CHECK_EQ(a.map->tokens.n, 3);

    CASE("a copy cannot be pasted onto a creature");
    press(&a, "f");                        /* Aria */
    press(&a, "y");
    press(&a, ":f3\r");                    /* Bram's square */
    press(&a, "p");
    CHECK_EQ(a.map->tokens.n, 3);
    CHECK(strstr(a.status, "already here") != NULL);

    CASE("and lands on the next square over");
    press(&a, ":g3\r");
    press(&a, "p");
    CHECK_EQ(a.map->tokens.n, 4);

    CASE("a new creature cannot be placed onto one either");
    press(&a, ":f3\r");
    press(&a, "ip");
    CHECK_EQ(a.modal, MODAL_NONE);         /* no label prompt: it never got that far */
    CHECK(strstr(a.status, "something is on it") != NULL);
    CHECK_EQ(a.map->tokens.n, 4);

    /* Passing over is fine; sharing a square is not. */
    CASE("a creature may be carried over its own side");
    press(&a, ":c3\r");
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 1);
    press(&a, "lll");
    CHECK_EQ(a.map->tokens.v[a.play.sel].x, 5);   /* standing on Bram */

    CASE("but not put down on it");
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 1);
    CHECK(strstr(a.status, "move off to put down") != NULL);

    /* Esc is a cancel rather than a drop, and the square it set out from is
     * the one square nothing can have moved onto, so it always works -- even
     * from on top of an ally, where putting down is refused. */
    CASE("but esc cancels from there, because going back is always possible");
    int sel = a.play.sel;
    press(&a, "\x1b");
    CHECK_EQ(a.play.grabbed, 0);
    CHECK_EQ(a.map->tokens.v[sel].x, 2);        /* back on C3 */
    CHECK(strstr(a.status, "canceled") != NULL);

    /* G3 holds the copy pasted above, so the first clear square is the one
     * past it. */
    CASE("carried on to a clear square it goes down");
    press(&a, "\r");
    press(&a, "lll");
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 1);                /* on Bram, refused */
    press(&a, "ll\r");
    CHECK_EQ(a.play.grabbed, 0);
    CHECK_EQ(a.map->tokens.v[sel].x, 7);

    /* Ctrl-w is the GM overruling the map, and it overrules this too. */
    CASE("with blocking off a creature can be put down anywhere");
    press(&a, "\r");
    press(&a, "h");
    CHECK_EQ(a.map->tokens.v[a.play.sel].x, 6);
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 1);           /* refused, as before */
    press(&a, "\x17");                     /* ctrl-w */
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 0);           /* now allowed */
    press(&a, "\x17");

    CASE("a creature carried nowhere still lets go on esc");
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 1);
    press(&a, "\x1b");
    CHECK_EQ(a.play.grabbed, 0);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* The number belongs where the eye already is. */
void test_move_label(void)
{
    Map *m = map_new(20, 9, "label");
    map_fill_tiles(m, 0, 0, 19, 8, TILE_FLOOR);
    str_lcpy(m->ruleset, "daggerheart", sizeof m->ruleset);

    Undo u;
    undo_init(&u);
    Play p;
    play_init(&p);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 90, 24);
    rnd_set_clear(&r, THEME_DARK.fg, THEME_DARK.bg);

    Editor e;
    ed_init(&e, m);
    e.labels = 0;
    ed_layout(&e, m, 90, 24);

    Token a = { 3, 4, 1, TOKEN_PLAYER, "Aria", { { 0, "" } }, 0 };
    int ai = undo_add_token(&u, m, a);
    play_focus(&p, ai);
    ByteBuf f;
    #define FRAME() do {                                   \
        rnd_begin(&r);                                     \
        play_draw(&r, m, &e, &p, &THEME_DARK, 0, 0);          \
        bb_init(&f, 32768);                                \
        rnd_dump(&r, &f);                                  \
        bb_putc(&f, '\0');                                 \
    } while (0)

    CASE("nothing is said about a creature standing still");
    FRAME();
    CHECK(strstr(f.data, "ft") == NULL);
    bb_free(&f);

    play_grab(&p, m, 0);
    FRAME();
    CHECK(strstr(f.data, "ft") == NULL);      /* picked up, but nowhere yet */
    bb_free(&f);

    CASE("carried four squares it says how far, and which band");
    for (int i = 0; i < 4; i++) {
        undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
        e.cx = m->tokens.v[ai].x; e.cy = m->tokens.v[ai].y;
    }
    FRAME();
    CHECK(strstr(f.data, "20 ft") != NULL);
    CHECK(strstr(f.data, "Close") != NULL);
    bb_free(&f);

    CASE("the band is the one the ruleset would name for that distance");
    const Ruleset *rs = ruleset_by_name("daggerheart");
    CHECK(rs != NULL);
    CHECK_EQ(strcmp(ruleset_band(rs, 20.0), "Close"), 0);

    CASE("one square away is melee");
    m->tokens.v[ai].x = 4;
    play_trail_sync(&p, m);
    FRAME();
    CHECK(strstr(f.data, "5 ft") != NULL);
    CHECK(strstr(f.data, "Melee") != NULL);
    bb_free(&f);

    /* Without a ruleset there is no band to name, so it says the squares
     * instead -- the thing a GM would otherwise be counting. */
    CASE("with no ruleset it gives squares and feet");
    m->ruleset[0] = '\0';
    m->tokens.v[ai].x = 6;
    play_trail_sync(&p, m);
    FRAME();
    CHECK(strstr(f.data, "3 sq") != NULL);
    CHECK(strstr(f.data, "15 ft") != NULL);
    CHECK(strstr(f.data, "Close") == NULL);
    bb_free(&f);
    str_lcpy(m->ruleset, "daggerheart", sizeof m->ruleset);

    /* The two rows around a token belong to its status markers, so the label
     * goes out to the side and they both survive. */
    CASE("it does not sit on the status marker rows");
    token_add_status(&m->tokens.v[ai], 0, "Poisoned");
    token_add_status(&m->tokens.v[ai], 1, "Marked");
    FRAME();
    Rect area;
    grid_token_area(&e.view, m->tokens.v[ai].x, m->tokens.v[ai].y,
                    m->tokens.v[ai].size, &area);
    CHECK_EQ(rnd_at(&r, area.x, area.y - 1)->ch, 'P');
    CHECK_EQ(rnd_at(&r, area.x + 1, area.y - 1)->ch, 'M');
    bb_free(&f);
    token_clear_status(&m->tokens.v[ai]);

    /* Against the right-hand edge it flips rather than being eaten by the
     * clip, the way the ruler's readout does. */
    CASE("it stays inside the viewport at either edge");
    for (int x = 0; x < m->w; x++) {
        m->tokens.v[ai].x = (int16_t)x;
        p.origin_x = 0; p.origin_y = 4;
        m->tokens.v[ai].y = 4;
        play_trail_sync(&p, m);

        rnd_begin(&r);
        play_draw(&r, m, &e, &p, &THEME_DARK, 0, 0);
        grid_ensure_visible(&e.view, m, x, 4, ED_SCROLLOFF);

        bb_init(&f, 32768);
        rnd_dump(&r, &f);
        bb_putc(&f, '\0');
        /* Every line has to fit the terminal. Counted in characters rather
         * than bytes: the frame is mostly box-drawing, three bytes a glyph. */
        for (const char *l = f.data, *nl; (nl = strchr(l, '\n')); l = nl + 1) {
            int cols = 0;
            for (const char *c = l; c < nl; c++)
                if (((unsigned char)*c & 0xC0) != 0x80) cols++;
            CHECK(cols <= 90);
        }
        bb_free(&f);
    }

    #undef FRAME
    rnd_free(&r);
    undo_free(&u);
    map_free(m);
}

void test_cancel_move(void)
{
    Sandbox sb = sandbox_enter("cancel");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    char path[600];
    snprintf(path, sizeof path, "%s/c.vtt", sb.dir);
    {
        FILE *f = fopen(path, "w");
        if (f) {
            fputs("VTT 2\nname Cancel\nsize 12 6\nzoom 1\ntiles\n", f);
            for (int i = 0; i < 6; i++) fputs("............\n", f);
            fputs("token player 2 2 1 \"Aria\"\n", f);
            fclose(f);
        }
    }

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 96, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);

    press(&a, "f");
    int sel = a.play.sel;
    CHECK_EQ(sel, 0);

    CASE("esc puts a carried creature back where it set out from");
    press(&a, "\r");
    press(&a, "lllj");
    CHECK_EQ(a.map->tokens.v[sel].x, 5);
    CHECK_EQ(a.map->tokens.v[sel].y, 3);
    press(&a, "\x1b");
    CHECK_EQ(a.play.grabbed, 0);
    CHECK_EQ(a.map->tokens.v[sel].x, 2);
    CHECK_EQ(a.map->tokens.v[sel].y, 2);
    CHECK(strstr(a.status, "C3") != NULL);

    CASE("the cursor comes back with it");
    CHECK_EQ(a.ed.cx, 2);
    CHECK_EQ(a.ed.cy, 2);

    /* The whole point of unwinding rather than stepping back: a canceled
     * move is not in the history at all. */
    CASE("and the walk leaves no trace behind it");
    CHECK_EQ(undo_can_undo(&a.undo), 0);

    CASE("the selection survives a cancel; only the carry ends");
    CHECK_EQ(a.play.sel, sel);
    CHECK_EQ(a.play.ntrail, 0);

    CASE("enter still commits, and that one does undo");
    press(&a, "\r");
    press(&a, "ll");
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 0);
    CHECK_EQ(a.map->tokens.v[sel].x, 4);
    CHECK_EQ(undo_can_undo(&a.undo), 1);
    press(&a, "u");
    CHECK_EQ(a.map->tokens.v[sel].x, 3);

    /* An edit made part way through the walk is not part of the walk, so a
     * cancel must not swallow it. */
    CASE("a marker added mid-walk survives the cancel");
    press(&a, "f");
    press(&a, "\r");
    press(&a, "ll");
    press(&a, "saPoisoned\r");
    CHECK_EQ(a.map->tokens.v[sel].nstatus, 1);
    press(&a, "ll");
    press(&a, "\x1b");
    CHECK_EQ(a.map->tokens.v[sel].nstatus, 1);

    /* Only the steps after the marker unwind -- the ones before it are behind
     * an edit the rewind will not cross, so the creature stops there. */
    CASE("it stops unwinding at the edit rather than crossing it");
    CHECK(a.map->tokens.v[sel].x > 3);

    CASE("canceling without having moved just lets go");
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 1);
    int x = a.map->tokens.v[sel].x;
    press(&a, "\x1b");
    CHECK_EQ(a.play.grabbed, 0);
    CHECK_EQ(a.map->tokens.v[sel].x, x);
    CHECK(strstr(a.status, "put down") != NULL);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

void test_delete_yanks(void)
{
    Sandbox sb = sandbox_enter("dely");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    char path[600];
    snprintf(path, sizeof path, "%s/d.vtt", sb.dir);
    {
        FILE *f = fopen(path, "w");
        if (f) {
            fputs("VTT 2\nname Del\nsize 10 5\nzoom 1\ntiles\n", f);
            for (int i = 0; i < 5; i++) fputs("..........\n", f);
            fputs("token enemy 2 2 1 \"Goblin\"\n", f);
            fclose(f);
        }
    }

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 90, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);

    /* vim's d fills the unnamed register, so d then p is how a creature moves
     * somewhere else in one go. */
    CASE("a delete fills the yank buffer");
    press(&a, "e");
    press(&a, "d");
    CHECK_EQ(a.map->tokens.n, 0);
    CHECK_EQ(a.play.nyank, 1);
    CHECK(strstr(a.status, "p puts it back") != NULL);

    CASE("and the name comes back with it, not a numbered copy");
    press(&a, ":h4\r");
    press(&a, "p");
    CHECK_EQ(a.map->tokens.n, 1);
    CHECK_EQ(strcmp(a.map->tokens.v[0].label, "Goblin"), 0);
    CHECK_EQ(a.map->tokens.v[0].x, 7);
    CHECK_EQ(a.map->tokens.v[0].y, 3);

    /* Pasting a second time is a copy of something that is now on the map
     * again, so this one does get numbered. */
    CASE("a second paste is a copy and is numbered");
    press(&a, ":b2\r");
    press(&a, "p");
    CHECK_EQ(a.map->tokens.n, 2);
    CHECK_EQ(strcmp(a.map->tokens.v[1].label, "Goblin 2"), 0);

    CASE("deleting nothing yanks nothing");
    press(&a, ":j5\r");
    play_focus(&a.play, -1);
    int before = a.play.nyank;
    press(&a, "d");
    CHECK(strstr(a.status, "no token here") != NULL);
    CHECK_EQ(a.play.nyank, before);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* ---------------------------------------------------------------- terrain */

void test_void_reads_as_void(void)
{
    /* A lone void square keeps the grid lines its floor neighbors draw, so
     * without a mark of its own it is the same picture as the floor. */
    Map *m = map_new(9, 5, "hole");
    map_fill_tiles(m, 0, 0, 8, 4, TILE_FLOOR);
    map_set_tile(m, 4, 2, TILE_VOID);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 60, 20);
    rnd_set_clear(&r, THEME_DARK.fg, THEME_DARK.bg);

    GridView g;
    memset(&g, 0, sizeof g);
    g.zoom = 1;
    g.view = rect(0, 0, 60, 20);

    rnd_begin(&r);
    grid_draw(&r, m, &g, &THEME_DARK, 0, 1, FOGV_GM);

    int hx, hy, fx, fy;
    grid_tile_interior(&g, 4, 2, &hx, &hy);   /* the hole */
    grid_tile_interior(&g, 3, 2, &fx, &fy);   /* the floor beside it */

    int mx = hx + ZOOM[g.zoom].iw / 2, my = hy + ZOOM[g.zoom].ih / 2;

    CASE("a void square is marked and the floor beside it is not");
    CHECK_EQ(rnd_at(&r, mx, my)->ch, 0x00B7u);
    CHECK_EQ(rnd_at(&r, fx + ZOOM[g.zoom].iw / 2, fy)->ch, ' ');

    /* A shade would have done it too, but not at these luminances: a
     * background dark enough to stay quiet is one nobody can see. */
    CASE("it is a mark, not a shade");
    CHECK_EQ(rnd_at(&r, mx, my)->bg, rnd_at(&r, fx, fy)->bg);

    CASE("one cell, in the middle, not a fill");
    int marked = 0;
    for (int j2 = 0; j2 < ZOOM[g.zoom].ih; j2++)
        for (int i2 = 0; i2 < ZOOM[g.zoom].iw; i2++)
            if (rnd_at(&r, hx + i2, hy + j2)->ch != ' ') marked++;
    CHECK_EQ(marked, 1);

    /* It has to recede behind the lattice rather than compete with it. */
    CASE("the mark is dimmer than the grid lines");
    CHECK_EQ(rnd_at(&r, mx, my)->fg, THEME_DARK.void_mark);
    CHECK(THEME_DARK.void_mark < THEME_DARK.grid);

    CASE("the lattice around it is untouched");
    CHECK(rnd_at(&r, hx - 1, hy)->ch != ' ');
    CHECK(rnd_at(&r, hx + ZOOM[g.zoom].iw, hy)->ch != ' ');

    CASE("ascii mode marks it too");
    rnd_begin(&r);
    grid_draw(&r, m, &g, &THEME_DARK, 1, 1, FOGV_GM);
    CHECK_EQ(rnd_at(&r, mx, my)->ch, (uint32_t)'.');

    CASE("every zoom puts the mark inside the square");
    for (int z = 0; z < ZOOM_COUNT; z++) {
        g.zoom = z;
        rnd_begin(&r);
        grid_draw(&r, m, &g, &THEME_DARK, 0, 1, FOGV_GM);
        grid_tile_interior(&g, 4, 2, &hx, &hy);
        int found = 0;
        for (int j2 = 0; j2 < ZOOM[z].ih; j2++)
            for (int i2 = 0; i2 < ZOOM[z].iw; i2++)
                if (rnd_at(&r, hx + i2, hy + j2)->ch == 0x00B7u) found++;
        CHECK_EQ(found, 1);
    }

    rnd_free(&r);
    map_free(m);
}

void test_terrain_palette(void)
{
    /* Floor is the page and every other terrain is tuned to sit above it, so
     * lifting the floor would put rough and wood underneath it. That is why
     * void is told apart by a mark instead. */
    CASE("floor is the page, so the palette above it is undisturbed");
    CHECK_EQ(THEME_DARK.terrain_bg[TILE_FLOOR], THEME_DARK.bg);
    CHECK_EQ(THEME_DARK.terrain_bg[TILE_VOID], THEME_DARK.bg);

    CASE("every terrain that means something stands off the page");
    for (int i = 0; i < TILE_COUNT; i++) {
        if (i == TILE_VOID || i == TILE_FLOOR) continue;
        CHECK(THEME_DARK.terrain_bg[i] != THEME_DARK.bg);
    }

    CASE("and off each other");
    for (int a = 0; a < TILE_COUNT; a++) {
        if (a == TILE_VOID || a == TILE_FLOOR) continue;
        for (int b = a + 1; b < TILE_COUNT; b++) {
            if (b == TILE_VOID || b == TILE_FLOOR) continue;
            CHECK(THEME_DARK.terrain_bg[a] != THEME_DARK.terrain_bg[b]);
        }
    }

    /* Every kind carries a glyph of its own, so none of them rests on color
     * alone -- which is what makes the palette survive a terminal that
     * renders these tints badly. Floor is the one blank kind, and blank is
     * what floor means. */
    CASE("floor is the only kind drawn blank");
    for (int i = 0; i < TILE_COUNT; i++) {
        uint32_t glyph = grid_terrain_glyph((uint8_t)i, 0);
        if (i == TILE_FLOOR || i == TILE_VOID) CHECK_EQ(glyph, ' ');
        else                                   CHECK(glyph != ' ');
    }
}

/* ------------------------------------------------------------- coordinates */

void test_coords(void)
{
    char b[MAP_COORD_MAX];

    CASE("columns are letters and rows count from one");
    map_coord_name(0, 0, b, sizeof b);   CHECK_EQ(strcmp(b, "A1"), 0);
    map_coord_name(3, 5, b, sizeof b);   CHECK_EQ(strcmp(b, "D6"), 0);
    map_coord_name(25, 0, b, sizeof b);  CHECK_EQ(strcmp(b, "Z1"), 0);

    /* Bijective base 26: Z is followed by AA, not BA, so no column is
     * unnamed and none is named twice. */
    CASE("past Z the columns double up the way a spreadsheet does");
    map_coord_name(26, 0, b, sizeof b);  CHECK_EQ(strcmp(b, "AA1"), 0);
    map_coord_name(27, 0, b, sizeof b);  CHECK_EQ(strcmp(b, "AB1"), 0);
    map_coord_name(51, 0, b, sizeof b);  CHECK_EQ(strcmp(b, "AZ1"), 0);
    map_coord_name(52, 0, b, sizeof b);  CHECK_EQ(strcmp(b, "BA1"), 0);
    map_coord_name(701, 0, b, sizeof b); CHECK_EQ(strcmp(b, "ZZ1"), 0);
    map_coord_name(702, 0, b, sizeof b); CHECK_EQ(strcmp(b, "AAA1"), 0);

    CASE("the largest map the format allows has a name for every square");
    map_coord_name(MAP_MAX_DIM - 1, MAP_MAX_DIM - 1, b, sizeof b);
    CHECK(strlen(b) < MAP_COORD_MAX);
    CHECK_EQ(strcmp(b, "SR512"), 0);

    /* One assertion for the whole sweep: a round trip that fails on every
     * column would otherwise drown the count it is reported in. */
    CASE("a square's name reads back as the same square");
    int bad = 0;
    for (int x = 0; x < MAP_MAX_DIM && !bad; x++) {
        for (int y = 0; y < MAP_MAX_DIM; y += 37) {
            map_coord_name(x, y, b, sizeof b);
            int px = -1, py = -1;
            if (!map_coord_parse(b, &px, &py) || px != x || py != y) { bad = 1; break; }
        }
    }
    CHECK_EQ(bad, 0);

    CASE("parsing ignores case");
    int x = -1, y = -1;
    CHECK_EQ(map_coord_parse("d6", &x, &y), 1);   CHECK_EQ(x, 3); CHECK_EQ(y, 5);
    CHECK_EQ(map_coord_parse("D6", &x, &y), 1);   CHECK_EQ(x, 3); CHECK_EQ(y, 5);
    CHECK_EQ(map_coord_parse("aA12", &x, &y), 1); CHECK_EQ(x, 26); CHECK_EQ(y, 11);

    CASE("a row on its own leaves the column alone");
    x = 9; y = -1;
    CHECK_EQ(map_coord_parse("12", &x, &y), 1);
    CHECK_EQ(x, 9);                                /* untouched */
    CHECK_EQ(y, 11);

    /* Every : verb is pure letters, so a column with no row would be one.
     * Requiring the row is what keeps :e, :w, :x and :q meaning what they
     * always meant. */
    CASE("a bare column letter is not a coordinate");
    CHECK_EQ(map_coord_parse("d", &x, &y), 0);
    CHECK_EQ(map_coord_parse("e", &x, &y), 0);
    CHECK_EQ(map_coord_parse("w", &x, &y), 0);
    CHECK_EQ(map_coord_parse("q", &x, &y), 0);
    CHECK_EQ(map_coord_parse("x", &x, &y), 0);

    CASE("no existing command reads as a coordinate");
    static const char *const verbs[] = {
        "w", "write", "wq", "x", "q", "quit", "q!", "e", "edit", "play",
        "build", "name", "resize", "scale", "metric", "ruleset", "zoom",
    };
    for (size_t i = 0; i < sizeof verbs / sizeof *verbs; i++)
        CHECK_EQ(map_coord_parse(verbs[i], &x, &y), 0);

    CASE("rubbish is not a coordinate");
    CHECK_EQ(map_coord_parse("", &x, &y), 0);
    CHECK_EQ(map_coord_parse("d6x", &x, &y), 0);
    CHECK_EQ(map_coord_parse("6d", &x, &y), 0);
    CHECK_EQ(map_coord_parse("d 6", &x, &y), 0);
    CHECK_EQ(map_coord_parse("-4", &x, &y), 0);
    CHECK_EQ(map_coord_parse("d0", &x, &y), 0);    /* rows count from one */
    CHECK_EQ(map_coord_parse("abcde1", &x, &y), 0);
}

void test_jump(void)
{
    Sandbox sb = sandbox_enter("jump");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    char path[600];
    snprintf(path, sizeof path, "%s/j.vtt", sb.dir);
    {
        FILE *f = fopen(path, "w");
        CHECK(f != NULL);
        if (f) {
            fputs("VTT 2\nname Jump\nsize 30 20\nzoom 1\ntiles\n", f);
            for (int i = 0; i < 20; i++) fputs("..............................\n", f);
            fclose(f);
        }
    }

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);

    CASE(":d6 goes to column D, row 6");
    press(&a, ":d6\r");
    CHECK_EQ(a.ed.cx, 3);
    CHECK_EQ(a.ed.cy, 5);
    CHECK(strstr(a.status, "D6") != NULL);

    CASE("case does not matter");
    press(&a, ":AA3\r");
    CHECK_EQ(a.ed.cx, 26);
    CHECK_EQ(a.ed.cy, 2);

    CASE("a row on its own keeps the column, the way vim's :12 keeps yours");
    press(&a, ":12\r");
    CHECK_EQ(a.ed.cx, 26);
    CHECK_EQ(a.ed.cy, 11);

    CASE("the jump centers rather than scrolling the least it can");
    press(&a, ":a1\r");
    press(&a, ":z20\r");
    CHECK_EQ(a.ed.cx, 25);
    CHECK_EQ(a.ed.cy, 19);

    CASE("off the map says where the map ends");
    int bx = a.ed.cx, by = a.ed.cy;
    press(&a, ":zz9\r");
    CHECK_EQ(a.ed.cx, bx);
    CHECK_EQ(a.ed.cy, by);
    CHECK(strstr(a.status, "AD20") != NULL);       /* 30x20 ends at AD20 */

    /* The commands that would have lost to a coordinate. */
    CASE("a verb still beats anything that looks like a square");
    press(&a, ":e6\r");
    CHECK(strstr(a.status, "unknown") == NULL);    /* it is a jump, not an edit */
    CHECK_EQ(a.ed.cx, 4);
    CHECK_EQ(a.ed.cy, 5);
    CHECK_EQ(strcmp(a.map->name, "Jump"), 0);      /* no file was opened */

    press(&a, ":w\r");
    CHECK(strstr(a.status, "wrote") != NULL);
    press(&a, ":zoom 2\r");
    CHECK_EQ(a.ed.view.zoom, 2);
    press(&a, ":name Renamed\r");
    CHECK_EQ(strcmp(a.map->name, "Renamed"), 0);

    CASE("a bare column letter is still its command");
    press(&a, ":d\r");
    CHECK(strstr(a.status, "unknown command") != NULL);

    CASE("the readouts name squares the same way the jump does");
    press(&a, ":c4\r");
    char st[256];
    ed_status(&a.ed, a.map, st, sizeof st);
    CHECK(strstr(st, "C4") != NULL);
    CHECK(strstr(st, "2,3") == NULL);              /* the old x,y is gone */

    /* Jumping the cursor out from under a held creature would leave the two
     * in different places, so it waits. */
    CASE("a jump waits while a creature is being carried");
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    tokens_add(&a.map->tokens, (Token){ 2, 3, 1, TOKEN_ENEMY, "Ogre", { { 0, "" } }, 0 });
    a.ed.cx = 2; a.ed.cy = 3;
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 1);
    press(&a, ":a1\r");
    CHECK_EQ(a.ed.cx, 2);
    CHECK(strstr(a.status, "put the creature down") != NULL);

    CASE("and works once it is put down");
    press(&a, "\r");
    press(&a, ":a1\r");
    CHECK_EQ(a.ed.cx, 0);
    CHECK_EQ(a.ed.cy, 0);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

void test_labels(void)
{
    Sandbox sb = sandbox_enter("labels");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    char path[600];
    snprintf(path, sizeof path, "%s/l.vtt", sb.dir);
    {
        FILE *f = fopen(path, "w");
        if (f) {
            fputs("VTT 2\nname Lab\nsize 12 8\nzoom 1\ntiles\n", f);
            for (int i = 0; i < 8; i++) fputs("............\n", f);
            fclose(f);
        }
    }

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);

    CASE("labels are on to begin with, and take a row and a gutter");
    CHECK_EQ(a.ed.labels, 1);
    CHECK(a.ed.view.view.x > 0);
    CHECK_EQ(a.ed.view.view.y, 2);

    CASE("the letters and numbers are drawn");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf f;
    bb_init(&f, 32768);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr(f.data, "A   B   C") != NULL);
    bb_free(&f);

    CASE("# takes them off and gives the room back");
    int was_x = a.ed.view.view.x, was_y = a.ed.view.view.y;
    press(&a, "#");
    CHECK_EQ(a.ed.labels, 0);
    CHECK_EQ(a.ed.view.view.x, 0);
    CHECK_EQ(a.ed.view.view.y, 1);
    CHECK(a.ed.view.view.w > was_x);
    CHECK(strstr(a.status, "labels off") != NULL);

    rnd_begin(&r);
    app_draw(&a);
    bb_init(&f, 32768);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr(f.data, "A   B   C") == NULL);
    bb_free(&f);

    CASE("# puts them back");
    press(&a, "#");
    CHECK_EQ(a.ed.labels, 1);
    CHECK_EQ(a.ed.view.view.x, was_x);
    CHECK_EQ(a.ed.view.view.y, was_y);

    CASE("play mode gets the same labels");
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    rnd_begin(&r);
    app_draw(&a);
    bb_init(&f, 32768);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr(f.data, "A   B   C") != NULL);
    bb_free(&f);

    /* A jump still lands whether or not the labels are showing. */
    CASE("a jump works with the labels hidden");
    press(&a, "#");
    press(&a, ":e3\r");
    CHECK_EQ(a.ed.cx, 4);
    CHECK_EQ(a.ed.cy, 2);
    press(&a, "#");

    CASE("# on the command line is typed, not swallowed");
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    press(&a, ":name a#b\r");
    CHECK_EQ(strcmp(a.map->name, "a#b"), 0);
    CHECK_EQ(a.ed.labels, 1);

    CASE("every zoom lays labels out without crashing");
    for (int z = 0; z < ZOOM_COUNT; z++) {
        char cmd[32];
        snprintf(cmd, sizeof cmd, ":zoom %d\r", z);
        press(&a, cmd);
        for (int w = 24; w <= 120; w += 8) {
            rnd_resize(&r, w, 20);
            ed_layout(&a.ed, a.map, r.w, r.h);
            rnd_begin(&r);
            app_draw(&a);
            rnd_flush(&r, NULL);
        }
    }
    CHECK(1);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* ------------------------------------------------------------------ shapes */

void test_shapes(void)
{
    CASE("a rectangle between two tiles includes both ends");
    EdShape b = ed_shape(ED_SHAPE_RECT, 2, 3, 5, 4, 0);
    CHECK_EQ(b.x0, 2); CHECK_EQ(b.x1, 5);
    CHECK_EQ(b.y0, 3); CHECK_EQ(b.y1, 4);
    CHECK_EQ(ed_shape_has(&b, 2, 3), 1);
    CHECK_EQ(ed_shape_has(&b, 5, 4), 1);
    CHECK_EQ(ed_shape_has(&b, 6, 4), 0);

    CASE("a reversed rectangle is the same rectangle");
    EdShape rev = ed_shape(ED_SHAPE_RECT, 5, 4, 2, 3, 0);
    CHECK_EQ(rev.x0, b.x0); CHECK_EQ(rev.x1, b.x1);
    CHECK_EQ(rev.y0, b.y0); CHECK_EQ(rev.y1, b.y1);

    /* Between two corners it spans what they enclose, which is one fewer
     * tile than a box drawn between two squares. */
    CASE("a rectangle between two corners spans the tiles they enclose");
    EdShape c = ed_shape(ED_SHAPE_RECT, 2, 2, 5, 5, 1);
    CHECK_EQ(c.x0, 2); CHECK_EQ(c.x1, 4);
    CHECK_EQ(c.y0, 2); CHECK_EQ(c.y1, 4);

    CASE("a circle holds its center and reaches its cursor");
    EdShape d = ed_shape(ED_SHAPE_CIRCLE, 10, 10, 14, 10, 0);
    CHECK_EQ(ed_shape_has(&d, 10, 10), 1);
    CHECK_EQ(ed_shape_has(&d, 14, 10), 1);      /* the tile that set the radius */
    CHECK_EQ(ed_shape_has(&d, 15, 10), 0);
    CHECK_EQ(ed_shape_radius(&d), 4);

    CASE("a circle is round, not the box around it");
    CHECK_EQ(ed_shape_has(&d, 13, 13), 0);      /* the corner of the box */
    CHECK_EQ(ed_shape_has(&d, 12, 12), 1);      /* inside the arc */

    CASE("a circle is symmetric about its center");
    for (int dy = -5; dy <= 5; dy++)
        for (int dx = -5; dx <= 5; dx++) {
            int in = ed_shape_has(&d, 10 + dx, 10 + dy);
            CHECK_EQ(ed_shape_has(&d, 10 - dx, 10 + dy), in);
            CHECK_EQ(ed_shape_has(&d, 10 + dx, 10 - dy), in);
        }

    CASE("a circle of no radius is the one tile");
    EdShape dot = ed_shape(ED_SHAPE_CIRCLE, 4, 4, 4, 4, 0);
    CHECK_EQ(ed_shape_has(&dot, 4, 4), 1);
    CHECK_EQ(ed_shape_has(&dot, 5, 4), 0);
    CHECK_EQ(ed_shape_radius(&dot), 0);

    /* Wall mode anchors on a lattice corner, so its circles sit between
     * squares and come out even across rather than odd. */
    CASE("a circle anchored on a corner is centered on the corner");
    EdShape w = ed_shape(ED_SHAPE_CIRCLE, 5, 5, 8, 5, 1);
    CHECK_EQ(ed_shape_has(&w, 4, 4), 1);        /* the four tiles round it */
    CHECK_EQ(ed_shape_has(&w, 5, 4), 1);
    CHECK_EQ(ed_shape_has(&w, 4, 5), 1);
    CHECK_EQ(ed_shape_has(&w, 5, 5), 1);
    CHECK_EQ(ed_shape_has(&w, 4, 4), ed_shape_has(&w, 5, 5));

    CASE("a circle reaching off the map is clipped, not clamped");
    EdShape edge = ed_shape(ED_SHAPE_CIRCLE, 1, 1, 6, 1, 0);
    CHECK_EQ(ed_shape_has(&edge, -3, 1), 1);    /* the shape itself is unbounded */
    CHECK(edge.x0 < 0);                          /* the map bounds it on use */
}

void test_circle_fill(void)
{
    Map *m = map_new(20, 20, "circle");
    Undo u;
    undo_init(&u);

    Editor e;
    ed_init(&e, m);
    ed_layout(&e, m, 80, 24);

    CASE("a visual circle fills a disc, not its bounding box");
    e.mode = ED_VISUAL;
    e.shape = ED_SHAPE_CIRCLE;
    e.anchor_x = 10; e.anchor_y = 10;
    e.cx = 14; e.cy = 10;
    ed_apply_tiles(&e, m, &u, TILE_FLOOR);
    CHECK_EQ(map_tile(m, 10, 10), TILE_FLOOR);
    CHECK_EQ(map_tile(m, 14, 10), TILE_FLOOR);
    CHECK_EQ(map_tile(m, 13, 13), TILE_VOID);       /* the box corner */
    CHECK_EQ(map_tile(m, 15, 10), TILE_VOID);

    CASE("it undoes as one step");
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(map_tile(m, 10, 10), TILE_VOID);
    CHECK_EQ(undo_redo(&u, m), 1);

    /* A circle reaching past the edge should paint what fits rather than
     * refusing or wrapping. */
    CASE("a circle overhanging the map paints only what is on it");
    e.anchor_x = 1; e.anchor_y = 1;
    e.cx = 5; e.cy = 1;
    ed_apply_tiles(&e, m, &u, TILE_WATER);
    CHECK_EQ(map_tile(m, 1, 1), TILE_WATER);
    CHECK_EQ(map_tile(m, 0, 0), TILE_WATER);
    CHECK_EQ(map_tile(m, 5, 1), TILE_WATER);

    CASE("a box selection still fills its box");
    e.shape = ED_SHAPE_RECT;
    e.anchor_x = 15; e.anchor_y = 15;
    e.cx = 17; e.cy = 17;
    ed_apply_tiles(&e, m, &u, TILE_ROUGH);
    for (int y = 15; y <= 17; y++)
        for (int x = 15; x <= 17; x++)
            CHECK_EQ(map_tile(m, x, y), TILE_ROUGH);

    undo_free(&u);
    map_free(m);
}

/* The point of a ring of wall is that it encloses. Flooding out from the
 * middle and finding no way past it is the only test that says so. */
static int flood_escapes(const Map *m, int sx, int sy, const EdShape *s)
{
    int  n    = m->w * m->h;
    char *seen = xcalloc((size_t)n, 1);
    int  *q    = xmalloc((size_t)n * sizeof *q);
    int   head = 0, tail = 0, escaped = 0;

    seen[sy * m->w + sx] = 1;
    q[tail++] = sy * m->w + sx;

    static const int DX[4] = { 1, -1, 0, 0 };
    static const int DY[4] = { 0, 0, 1, -1 };

    while (head < tail) {
        int cur = q[head++];
        int cx = cur % m->w, cy = cur / m->w;
        if (!ed_shape_has(s, cx, cy)) { escaped = 1; break; }

        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (!map_in_bounds(m, nx, ny)) continue;
            if (seen[ny * m->w + nx]) continue;
            if (map_blocked(m, cx, cy, DX[d], DY[d])) continue;
            seen[ny * m->w + nx] = 1;
            q[tail++] = ny * m->w + nx;
        }
    }

    free(seen);
    free(q);
    return escaped;
}

void test_circle_walls(void)
{
    Map *m = map_new(24, 24, "ring");
    map_fill_tiles(m, 0, 0, 23, 23, TILE_FLOOR);

    Undo u;
    undo_init(&u);

    CASE("a circle of wall closes all the way round");
    EdShape s = ed_shape(ED_SHAPE_CIRCLE, 12, 12, 18, 12, 1);
    ed_wall_shape(m, &u, &s, EDGE_WALL);
    CHECK_EQ(flood_escapes(m, 11, 11, &s), 0);

    CASE("it walls the boundary and nothing inside it");
    CHECK_EQ(map_blocked(m, 11, 11, 1, 0), 0);      /* the middle is open */
    CHECK_EQ(map_blocked(m, 11, 11, 0, 1), 0);

    CASE("a radius of one is still a closed ring");
    Map *tiny = map_new(9, 9, "tiny");
    map_fill_tiles(tiny, 0, 0, 8, 8, TILE_FLOOR);
    Undo tu;
    undo_init(&tu);
    EdShape one = ed_shape(ED_SHAPE_CIRCLE, 4, 4, 5, 4, 1);
    ed_wall_shape(tiny, &tu, &one, EDGE_WALL);
    CHECK_EQ(flood_escapes(tiny, 3, 3, &one), 0);
    undo_free(&tu);
    map_free(tiny);

    CASE("the whole ring undoes as one step");
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(flood_escapes(m, 11, 11, &s), 1);      /* open again */

    /* Half a circle drawn off the corner of the map: the arc that fits gets
     * laid and the rest is dropped, rather than writing past the edge. */
    CASE("a circle overhanging the map lays the arc that fits");
    Map *corner = map_new(10, 10, "corner");
    map_fill_tiles(corner, 0, 0, 9, 9, TILE_FLOOR);
    Undo cu;
    undo_init(&cu);
    EdShape off = ed_shape(ED_SHAPE_CIRCLE, 1, 1, 6, 1, 1);
    ed_wall_shape(corner, &cu, &off, EDGE_WALL);
    CHECK_EQ(undo_can_undo(&cu), 1);
    CHECK_EQ(map_vedge(corner, 6, 1), EDGE_WALL);   /* the east arc is there */
    undo_free(&cu);
    map_free(corner);

    CASE("a rectangle of wall closes too, through the same path");
    undo_clear(&u);
    EdShape box = ed_shape(ED_SHAPE_RECT, 3, 3, 8, 8, 1);
    ed_wall_shape(m, &u, &box, EDGE_WALL);
    CHECK_EQ(flood_escapes(m, 4, 4, &box), 0);

    undo_free(&u);
    map_free(m);
}

void test_shape_keys(void)
{
    Sandbox sb = sandbox_enter("shape");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "m.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/m.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);

    CASE("v selects a box, V a circle");
    press(&a, "v");
    CHECK_EQ(a.ed.mode, ED_VISUAL);
    CHECK_EQ(a.ed.shape, ED_SHAPE_RECT);
    press(&a, "\x1b");

    press(&a, "V");
    CHECK_EQ(a.ed.mode, ED_VISUAL);
    CHECK_EQ(a.ed.shape, ED_SHAPE_CIRCLE);
    CHECK(strstr(a.status, "circle") != NULL);

    /* The way v and V swap between vim's two visual modes: the other key
     * changes the shape, the same key leaves. */
    CASE("the other key swaps the shape and keeps the anchor");
    a.ed.anchor_x = 0; a.ed.anchor_y = 0;
    press(&a, "v");
    CHECK_EQ(a.ed.mode, ED_VISUAL);
    CHECK_EQ(a.ed.shape, ED_SHAPE_RECT);
    CHECK_EQ(a.ed.anchor_x, 0);
    CHECK_EQ(a.ed.anchor_y, 0);

    CASE("the same key twice leaves visual mode");
    press(&a, "v");
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    press(&a, "VV");
    CHECK_EQ(a.ed.mode, ED_NORMAL);

    CASE("the readout names the shape, and a circle's radius");
    press(&a, "V");
    a.ed.anchor_x = 0; a.ed.anchor_y = 0;
    a.ed.cx = 1; a.ed.cy = 0;
    char st[256];
    ed_status(&a.ed, a.map, st, sizeof st);
    CHECK(strstr(st, "circle r1") != NULL);
    press(&a, "\x1b");

    CASE("wall mode anchors both shapes too");
    press(&a, "w");
    CHECK_EQ(a.ed.mode, ED_WALL);
    press(&a, "V");
    CHECK_EQ(a.ed.has_anchor, 1);
    CHECK_EQ(a.ed.shape, ED_SHAPE_CIRCLE);
    press(&a, "v");
    CHECK_EQ(a.ed.has_anchor, 1);
    CHECK_EQ(a.ed.shape, ED_SHAPE_RECT);
    press(&a, "v");
    CHECK_EQ(a.ed.has_anchor, 0);

    CASE("enter lays the shape the anchor was dropped with");
    press(&a, "V");
    press(&a, "\r");
    CHECK_EQ(a.ed.has_anchor, 0);
    CHECK(strstr(a.status, "circle") != NULL);

    CASE("enter with no anchor says which keys set one");
    press(&a, "\r");
    CHECK(strstr(a.status, "v or V") != NULL);

    /* The bar has to name the shape too: v and V chose it a while ago, and
     * the anchor on screen does not spell out which one it is. */
    CASE("the trace bar names the shape enter would lay");
    press(&a, "V");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf f;
    bb_init(&f, 16384);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr(f.data, "enter circle") != NULL);
    CHECK(strstr(f.data, "enter rect") == NULL);
    bb_free(&f);

    press(&a, "v");
    rnd_begin(&r);
    app_draw(&a);
    bb_init(&f, 16384);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr(f.data, "enter rect") != NULL);
    CHECK(strstr(f.data, "enter circle") == NULL);
    bb_free(&f);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* -------------------------------------------------- key tables and the ? page */

/* The bar and the ? page read the same tables, so the tables themselves are
 * what has to be right. */
void test_keymaps(void)
{
    for (int i = 0; i < KEYS_COUNT; i++) {
        const KeyMap *km = keys_map((KeyMapId)i);

        CASE("every map has a name and rows");
        CHECK(km->name != NULL && km->name[0] != '\0');
        CHECK(km->n > 0);

        CASE("every row says what it does");
        int groups = 0, bars = 0, help = 0;
        for (int j = 0; j < km->n; j++) {
            const KeyDoc *d = &km->rows[j];
            CHECK(d->what != NULL && d->what[0] != '\0');
            if (!d->keys) { groups++; CHECK(d->bar == NULL); continue; }
            if (d->bar) bars++;
            if (strcmp(d->keys, "?") == 0) help++;
        }

        CASE("every map opens with a group heading");
        CHECK_EQ(km->rows[0].keys == NULL, 1);
        CHECK(groups > 0);

        /* Six is the cap, and the last one is ? -- ui_keybar pins the final
         * hint to the right so the way to everything else cannot be the hint
         * a narrow terminal drops. */
        CASE("no bar carries more than six hints");
        CHECK(bars <= 6);

        CASE("every map documents ?, and it is the last hint on the bar");
        CHECK_EQ(help, 1);
        CHECK_EQ(strcmp(km->rows[km->n - 1].keys, "?"), 0);
        CHECK(km->rows[km->n - 1].bar != NULL);

        /* Two rows claiming the same key in one mode means one of them is a
         * lie, and neither the bar nor the page would show which. */
        CASE("no key is documented twice in one map");
        for (int j = 0; j < km->n; j++) {
            if (!km->rows[j].keys) continue;
            for (int l = j + 1; l < km->n; l++) {
                if (!km->rows[l].keys) continue;
                CHECK(strcmp(km->rows[j].keys, km->rows[l].keys) != 0);
            }
        }
    }
}

void test_keybar_fits(void)
{
    Renderer r;
    rnd_init(&r);

    /* The bar must never overflow, and the ? hint must survive every width a
     * terminal might be, because it is the way to everything the bar dropped. */
    for (int i = 0; i < KEYS_COUNT; i++) {
        const KeyMap *km = keys_map((KeyMapId)i);

        for (int w = 20; w <= 200; w += 3) {
            rnd_resize(&r, w, 6);
            rnd_begin(&r);
            ui_keybar(&r, &THEME_DARK, km);

            ByteBuf f;
            bb_init(&f, 4096);
            rnd_dump(&r, &f);
            bb_putc(&f, '\0');

            const char *bar = strrchr(f.data, '\n');
            CHECK(bar != NULL);
            if (bar) {
                CASE("the bar never runs past the edge");
                CHECK((int)strlen(bar + 1) <= w);
                CASE("the ? hint is there at every width");
                CHECK(strstr(f.data, "? keys") != NULL);
            }
            bb_free(&f);
        }
    }

    CASE("at eighty columns every bar keeps at least four hints");
    for (int i = 0; i < KEYS_COUNT; i++) {
        rnd_resize(&r, 80, 6);
        rnd_begin(&r);
        ui_keybar(&r, &THEME_DARK, keys_map((KeyMapId)i));

        ByteBuf f;
        bb_init(&f, 4096);
        rnd_dump(&r, &f);
        bb_putc(&f, '\0');

        int shown = 0;
        for (int j = 0; j < keys_map((KeyMapId)i)->n; j++) {
            const KeyDoc *d = &keys_map((KeyMapId)i)->rows[j];
            if (d->bar && strstr(f.data, d->bar)) shown++;
        }
        CHECK(shown >= 4);
        bb_free(&f);
    }

    rnd_free(&r);
}

void test_help_page(void)
{
    Sandbox sb = sandbox_enter("help");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "fight.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 90, 24);
    app_init(&a, NULL, &r);

    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);

    CASE("? opens the page and remembers where it came from");
    press(&a, "?");
    CHECK_EQ(a.screen, SCREEN_HELP);
    CHECK_EQ(a.help_from, SCREEN_PLAY);
    CHECK_EQ(a.help_id, KEYS_PLAY);

    CASE("the page leads with the mode you asked from");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf f;
    bb_init(&f, 32768);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    const char *play  = strstr(f.data, "Play mode");
    const char *build = strstr(f.data, "Build mode");
    CHECK(play != NULL);
    CHECK(build == NULL || play < build);      /* build is further down, if visible */

    CASE("keys the bar had no room for are on the page");
    CHECK(strstr(f.data, "cycle the bands") == NULL);   /* below the fold at 24 rows */
    bb_free(&f);

    rnd_resize(&r, 90, 100);                   /* the play page has grown past eighty rows */
    rnd_begin(&r);
    app_draw(&a);
    bb_init(&f, 65536);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr(f.data, "cycle the bands") != NULL);
    CHECK(strstr(f.data, "s a") != NULL);
    CHECK(strstr(f.data, "s d") != NULL);
    bb_free(&f);
    rnd_resize(&r, 90, 24);

    /* Scrolling has to actually move the page, not just the number. */
    CASE("ctrl-d shows something the top of the page did not");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf top;
    bb_init(&top, 32768);
    rnd_dump(&r, &top);
    bb_putc(&top, '\0');

    press(&a, "\x04");                             /* ctrl-d */
    CHECK(a.help_top > 0);
    rnd_begin(&r);
    app_draw(&a);
    bb_init(&f, 32768);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strcmp(top.data, f.data) != 0);
    CHECK(strstr(f.data, "next / previous turn") != NULL);   /* below the first fold */
    bb_free(&top);
    bb_free(&f);
    press(&a, "g");

    CASE("j and k scroll, and the top does not go negative");
    CHECK_EQ(a.help_top, 0);
    press(&a, "k");
    CHECK_EQ(a.help_top, 0);
    press(&a, "jjj");
    CHECK_EQ(a.help_top, 3);
    press(&a, "k");
    CHECK_EQ(a.help_top, 2);

    /* A scroll is a request until the page lays out: only the draw knows how
     * long the page is, which is what makes G right on the first keypress. */
    CASE("G goes to the end and stops there");
    press(&a, "G");
    rnd_begin(&r); app_draw(&a);
    int end = a.help_top;
    CHECK(end > 0);
    CHECK(end < a.help_lines);                 /* the last line stays on screen */

    press(&a, "jjjjj");
    rnd_begin(&r); app_draw(&a);
    CHECK_EQ(a.help_top, end);

    CASE("g goes back to the top");
    press(&a, "g");
    CHECK_EQ(a.help_top, 0);

    CASE("q closes it, back to where it was called from");
    press(&a, "q");
    CHECK_EQ(a.screen, SCREEN_PLAY);

    CASE("esc closes it too, and so does a second ?");
    press(&a, "?");
    CHECK_EQ(a.screen, SCREEN_HELP);
    press(&a, "\x1b");
    CHECK_EQ(a.screen, SCREEN_PLAY);
    press(&a, "?");
    press(&a, "?");
    CHECK_EQ(a.screen, SCREEN_PLAY);

    /* Carrying a creature changes enough of the keyboard to be its own page. */
    CASE("the page follows the mode, not just the screen");
    tokens_add(&a.map->tokens, (Token){ 0, 0, 1, TOKEN_ENEMY, "Ogre", { { 0, "" } }, 0 });
    a.ed.cx = 0; a.ed.cy = 0;
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 1);
    press(&a, "?");
    CHECK_EQ(a.help_id, KEYS_PLAY_GRABBED);
    press(&a, "q");

    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    press(&a, "?");
    CHECK_EQ(a.help_id, KEYS_BUILD);
    press(&a, "q");
    CHECK_EQ(a.screen, SCREEN_EDITOR);

    /* A question mark is a character you might want in a map name. */
    CASE("? on the command line is typed, not swallowed");
    press(&a, ":name a?b\r");
    CHECK_EQ(a.screen, SCREEN_EDITOR);
    CHECK_EQ(strcmp(a.map->name, "a?b"), 0);

    CASE("the page lays out on a small terminal without crashing");
    press(&a, "?");
    for (int w = 20; w <= 120; w += 7) {
        rnd_resize(&r, w, w > 40 ? 30 : 8);
        rnd_begin(&r);
        app_draw(&a);
        rnd_flush(&r, NULL);
    }
    CHECK(1);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* -------------------------------------------------- the remapped play keys */

void test_play_remap(void)
{
    Sandbox sb = sandbox_enter("remap");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "fight.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);

    /* i is insert, so p is free to mean paste the way it does everywhere. */
    CASE("i p places a player, i e an enemy");
    a.ed.cx = 0; a.ed.cy = 0;
    press(&a, "ip");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    press(&a, "Aria\r");
    CHECK_EQ(a.map->tokens.n, 1);
    CHECK_EQ(a.map->tokens.v[0].kind, TOKEN_PLAYER);

    a.ed.cx = 1; a.ed.cy = 0;
    press(&a, "ieOgre\r");
    CHECK_EQ(a.map->tokens.n, 2);
    CHECK_EQ(a.map->tokens.v[1].kind, TOKEN_ENEMY);

    CASE("a bare i does nothing until the next key says what");
    press(&a, "i");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "p player") != NULL);
    CHECK_EQ(a.map->tokens.n, 2);

    CASE("esc abandons a half-typed prefix");
    press(&a, "\x1b");
    CHECK_EQ(a.map->tokens.n, 2);
    CHECK(strstr(a.status, "canceled") != NULL);

    /* The prefix must swallow the next key rather than let it act: a
     * half-typed command turning into a different whole one is the worst
     * thing a prefix can do. */
    CASE("a prefix swallows a key that means something on its own");
    play_focus(&a.play, -1);
    press(&a, "it");
    CHECK_EQ(a.play.sel, -1);              /* t did not cycle */
    CHECK(strstr(a.status, "i wants") != NULL);

    CASE("p pastes now, and P says where it went");
    press(&a, "t");                        /* select the player */
    CHECK_EQ(a.play.sel, 0);
    press(&a, "y");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "p");
    CHECK_EQ(a.map->tokens.n, 3);
    press(&a, "P");
    CHECK_EQ(a.map->tokens.n, 3);
    CHECK(strstr(a.status, "now p") != NULL);

    CASE("c changes the label, where r used to");
    press(&a, "t");
    press(&a, "c");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    press(&a, "\025Renamed\r");
    CHECK_EQ(strcmp(a.map->tokens.v[a.play.sel].label, "Renamed"), 0);

    CASE("r cycles the range bands, where R used to");
    str_lcpy(a.map->ruleset, "daggerheart", sizeof a.map->ruleset);
    press(&a, "r");
    CHECK_EQ(a.play.range.active, 1);
    press(&a, "\x1b");
    CHECK_EQ(a.play.range.active, 0);

    /* R came back, as v did: the capital of a tool's key changes the tool's
     * variant, the way M changes the ruler's metric. */
    CASE("R cycles the range shape, a count names one, and esc keeps it");
    press(&a, "R");
    CHECK_EQ(a.play.range.shape, RANGE_CONE);
    CHECK(strstr(a.status, "range shape: cone") != NULL);
    CHECK_EQ(a.play.range.active, 0);           /* a setting: it shows nothing itself */
    press(&a, "4R");
    CHECK_EQ(a.play.range.shape, RANGE_SQUARE);
    press(&a, "R");
    CHECK_EQ(a.play.range.shape, RANGE_CIRCLE);  /* wraps */
    press(&a, "3R");
    press(&a, "r");
    CHECK_EQ(a.play.range.active, 1);
    CHECK_EQ(a.play.range.shape, RANGE_LINE);
    press(&a, "\x1b");
    CHECK_EQ(a.play.range.active, 0);
    CHECK_EQ(a.play.range.shape, RANGE_LINE);
    press(&a, "1R");

    /* The overlay's band index or radius only means something against the
     * ruleset it was set under, so changing the ruleset takes it off rather
     * than leaving a highlight that reads as something else. */
    CASE("2r names the second band, and :ruleset takes the overlay off");
    press(&a, "2r");
    CHECK_EQ(a.play.range.active, 1);
    CHECK_EQ(a.play.range.band, 1);
    press(&a, ":ruleset none\r");
    CHECK_EQ(a.play.range.active, 0);
    press(&a, "20r");
    CHECK_EQ(a.play.range.active, 1);
    CHECK_EQ(a.play.range.radius, 20);
    press(&a, ":ruleset daggerheart\r");
    CHECK_EQ(a.play.range.active, 0);

    CASE("s a adds a marker, s c colors, s d drops");
    press(&a, "t");
    int sel = a.play.sel;
    press(&a, "saPoisoned\r");
    CHECK_EQ(a.map->tokens.v[sel].nstatus, 1);
    uint8_t before = a.play.status_color;
    press(&a, "sc");
    CHECK(a.play.status_color != before);
    press(&a, "sd");
    CHECK_EQ(a.map->tokens.v[sel].nstatus, 0);

    CASE("s with a key it does not know says what it wanted");
    press(&a, "sz");
    CHECK(strstr(a.status, "s wants") != NULL);

    CASE("the retired keys name where they went");
    const char *gone = "aAvVPRS";
    for (const char *c = gone; *c; c++) {
        char keys[2] = { *c, '\0' };
        app_set_status(&a, "");
        press(&a, keys);
        CHECK(a.status[0] != '\0');
    }

    /* A count typed in build must not arrive in play as a multiplier. */
    CASE("a half-typed count does not survive the mode switch");
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    press(&a, "12");
    CHECK(a.ed.count > 0);
    app_key(&a, f2);
    CHECK_EQ(a.ed.count, 0);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* ------------------------------------------------- cycling and searching */

void test_cycle_tracks(void)
{
    Map *m = map_new(12, 6, "cycle");
    map_fill_tiles(m, 0, 0, 11, 5, TILE_FLOOR);

    Play p;
    play_init(&p);

    CASE("cycling an empty map selects nothing and says so");
    CHECK_EQ(play_cycle(&p, m, 1, PLAY_ANY_KIND), 0);
    CHECK_EQ(p.sel, -1);

    /* Interleaved on purpose: a track has to skip over the other kind rather
     * than stop at it. */
    Token a = { 0, 0, 1, TOKEN_PLAYER, "Aria",  { { 0, "" } }, 0 };
    Token b = { 2, 0, 1, TOKEN_ENEMY,  "Ogre",  { { 0, "" } }, 0 };
    Token c = { 4, 0, 1, TOKEN_PLAYER, "Bram",  { { 0, "" } }, 0 };
    Token d = { 6, 0, 1, TOKEN_ENEMY,  "Goblin",{ { 0, "" } }, 0 };
    tokens_add(&m->tokens, a);
    tokens_add(&m->tokens, b);
    tokens_add(&m->tokens, c);
    tokens_add(&m->tokens, d);

    CASE("the all track visits every token in order and wraps");
    play_focus(&p, -1);
    const int all[] = { 0, 1, 2, 3, 0 };
    for (int i = 0; i < 5; i++) {
        CHECK_EQ(play_cycle(&p, m, 1, PLAY_ANY_KIND), 1);
        CHECK_EQ(p.sel, all[i]);
    }

    CASE("the player track skips the enemies");
    play_focus(&p, -1);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_PLAYER), 1);
    CHECK_EQ(p.sel, 0);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_PLAYER), 1);
    CHECK_EQ(p.sel, 2);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_PLAYER), 1);
    CHECK_EQ(p.sel, 0);          /* wrapped past both enemies */

    CASE("the enemy track skips the players");
    play_focus(&p, -1);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_ENEMY), 1);
    CHECK_EQ(p.sel, 1);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_ENEMY), 1);
    CHECK_EQ(p.sel, 3);

    CASE("shift runs a track backwards");
    play_focus(&p, -1);
    CHECK_EQ(play_cycle(&p, m, -1, TOKEN_ENEMY), 1);
    CHECK_EQ(p.sel, 3);
    CHECK_EQ(play_cycle(&p, m, -1, TOKEN_ENEMY), 1);
    CHECK_EQ(p.sel, 1);
    CHECK_EQ(play_cycle(&p, m, -1, PLAY_ANY_KIND), 1);
    CHECK_EQ(p.sel, 0);

    /* Switching tracks should pick up near where you were looking, not at the
     * top of the list. */
    CASE("a track switch carries on from where the selection is");
    p.sel = 2;                                /* Bram, a player */
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_ENEMY), 1);
    CHECK_EQ(p.sel, 3);                       /* the enemy just after him */

    CASE("an empty track leaves the selection where it was");
    Map *only = map_new(6, 6, "only");
    map_fill_tiles(only, 0, 0, 5, 5, TILE_FLOOR);
    tokens_add(&only->tokens, b);
    Play q;
    play_init(&q);
    play_focus(&q, 0);
    CHECK_EQ(play_cycle(&q, only, 1, TOKEN_PLAYER), 0);
    CHECK_EQ(q.sel, 0);
    map_free(only);

    CASE("a track of one comes back round to itself");
    play_focus(&p, 0);
    CHECK_EQ(play_cycle(&p, m, 1, TOKEN_PLAYER), 1);
    CHECK_EQ(p.sel, 2);

    /* ------------------------------------------------------------ search */

    CASE("a search finds a label by any part of it, in any case");
    play_focus(&p, -1);
    CHECK_EQ(play_find(&p, m, "gob", 1), 1);
    CHECK_EQ(p.sel, 3);
    play_focus(&p, -1);
    CHECK_EQ(play_find(&p, m, "ARI", 1), 1);
    CHECK_EQ(p.sel, 0);
    play_focus(&p, -1);
    CHECK_EQ(play_find(&p, m, "ra", 1), 1);   /* Bram, mid-label */
    CHECK_EQ(p.sel, 2);

    CASE("a search that matches nothing leaves the selection alone");
    play_focus(&p, 1);
    CHECK_EQ(play_find(&p, m, "dragon", 1), 0);
    CHECK_EQ(p.sel, 1);

    CASE("repeating walks the matches and wraps, both ways");
    Token e2 = { 8, 0, 1, TOKEN_ENEMY, "Goblin 2", { { 0, "" } }, 0 };
    tokens_add(&m->tokens, e2);
    play_focus(&p, -1);
    CHECK_EQ(play_find(&p, m, "goblin", 1), 1);
    CHECK_EQ(p.sel, 3);
    CHECK_EQ(play_find(&p, m, NULL, 1), 1);
    CHECK_EQ(p.sel, 4);
    CHECK_EQ(play_find(&p, m, NULL, 1), 1);
    CHECK_EQ(p.sel, 3);                       /* wrapped */
    CHECK_EQ(play_find(&p, m, NULL, -1), 1);
    CHECK_EQ(p.sel, 4);

    CASE("an empty needle repeats the last search rather than matching all");
    play_focus(&p, -1);
    CHECK_EQ(play_find(&p, m, "", 1), 1);
    CHECK_EQ(p.sel, 3);
    CHECK_EQ(strcmp(p.search, "goblin"), 0);

    /* An unlabeled token has nothing to match, and must not be swept up by
     * an empty-looking search. */
    CASE("an unlabeled token matches nothing");
    Token bare = { 10, 0, 1, TOKEN_ENEMY, "", { { 0, "" } }, 0 };
    tokens_add(&m->tokens, bare);
    play_focus(&p, -1);
    CHECK_EQ(play_find(&p, m, "z", 1), 0);

    CASE("searching with nothing ever searched for finds nothing");
    Play fresh;
    play_init(&fresh);
    CHECK_EQ(play_find(&fresh, m, NULL, 1), 0);
    CHECK_EQ(fresh.sel, -1);

    map_free(m);
}

void test_cycle_keys(void)
{
    Sandbox sb = sandbox_enter("cyc");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "fight.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);

    Token p1 = { 0, 0, 1, TOKEN_PLAYER, "Aria", { { 0, "" } }, 0 };
    Token e1 = { 1, 0, 1, TOKEN_ENEMY,  "Ogre", { { 0, "" } }, 0 };
    Token p2 = { 0, 1, 1, TOKEN_PLAYER, "Bram", { { 0, "" } }, 0 };
    tokens_add(&a.map->tokens, p1);
    tokens_add(&a.map->tokens, e1);
    tokens_add(&a.map->tokens, p2);

    CASE("t walks every token, T walks back");
    press(&a, "t"); CHECK_EQ(a.play.sel, 0);
    press(&a, "t"); CHECK_EQ(a.play.sel, 1);
    press(&a, "T"); CHECK_EQ(a.play.sel, 0);

    CASE("f walks the friendlies, e the enemies");
    press(&a, "\x1b");
    press(&a, "f"); CHECK_EQ(a.play.sel, 0);
    press(&a, "f"); CHECK_EQ(a.play.sel, 2);
    press(&a, "e"); CHECK_EQ(a.play.sel, 1);
    press(&a, "E"); CHECK_EQ(a.play.sel, 1);   /* only one enemy: back to itself */

    CASE("the cursor follows the selection so the creature can be seen");
    CHECK_EQ(a.ed.cx, a.map->tokens.v[a.play.sel].x);
    CHECK_EQ(a.ed.cy, a.map->tokens.v[a.play.sel].y);

    CASE("the status line names what was landed on");
    CHECK(strstr(a.status, "Ogre") != NULL);

    CASE("/ opens a prompt and finds by part of a label");
    press(&a, "/");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    press(&a, "bra\r");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(a.play.sel, 2);
    CHECK(strstr(a.status, "Bram") != NULL);

    CASE("a search with no match says so and keeps the selection");
    press(&a, "/dragon\r");
    CHECK_EQ(a.play.sel, 2);
    CHECK(strstr(a.status, "no token matching") != NULL);

    CASE("n and N repeat the last search without retyping it");
    press(&a, "/o\r");                          /* Ogre */
    CHECK_EQ(a.play.sel, 1);
    press(&a, "n");
    CHECK_EQ(a.play.sel, 1);                    /* the only match, wrapped */
    press(&a, "N");
    CHECK_EQ(a.play.sel, 1);

    CASE("n before any search says what to press");
    Play saved = a.play;
    a.play.search[0] = '\0';
    press(&a, "n");
    CHECK(strstr(a.status, "/ finds") != NULL);
    a.play = saved;

    /* The three keys are only cycles in play mode; build mode has its own use
     * for the letters and must not lose it. */
    CASE("the cycle keys stay out of build mode");
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    CHECK_EQ(a.screen, SCREEN_EDITOR);
    int before = a.play.sel;
    press(&a, "v");
    CHECK_EQ(a.play.sel, before);
    CHECK_EQ(a.ed.mode, ED_VISUAL);
    press(&a, "\x1b");

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* Esc backs out of one thing at a time, and the range overlay is anchored to
 * a creature, so it goes when the focus does. */
void test_play_focus(void)
{
    Sandbox sb = sandbox_enter("focus");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "fight.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    CHECK_EQ(app_open_map(&a, path), 0);
    str_lcpy(a.map->ruleset, "daggerheart", sizeof a.map->ruleset);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    CHECK_EQ(a.screen, SCREEN_PLAY);

    Token one = { 0, 0, 1, TOKEN_ENEMY, "One", { { 0, "" } }, 0 };
    Token two = { 1, 1, 1, TOKEN_ENEMY, "Two", { { 0, "" } }, 0 };
    tokens_add(&a.map->tokens, one);
    tokens_add(&a.map->tokens, two);

    press(&a, "\t");                        /* focus the first token */
    CHECK_EQ(a.play.sel, 0);
    press(&a, "r");
    CASE("r anchors the overlay to the token in focus");
    CHECK_EQ(a.play.range.active, 1);
    CHECK_EQ(a.play.range.token, 0);

    CASE("esc takes the overlay off without dropping the selection");
    press(&a, "\x1b");
    CHECK_EQ(a.play.range.active, 0);
    CHECK_EQ(a.play.sel, 0);

    CASE("a second esc then lets the token go");
    press(&a, "\x1b");
    CHECK_EQ(a.play.sel, -1);

    CASE("tabbing to another creature resets the overlay");
    press(&a, "\tr");
    CHECK_EQ(a.play.range.active, 1);
    CHECK_EQ(a.play.range.token, 0);
    press(&a, "\t");
    CHECK_EQ(a.play.sel, 1);
    CHECK_EQ(a.play.range.active, 0);

    /* An overlay dropped on bare ground belongs to nobody, so moving the
     * focus about should leave it alone. */
    CASE("an overlay anchored to a tile survives a change of focus");
    press(&a, "\x1b");                      /* deselect */
    CHECK_EQ(a.play.sel, -1);
    a.ed.cx = 5; a.ed.cy = 5;
    press(&a, "r");
    CHECK_EQ(a.play.range.active, 1);
    CHECK_EQ(a.play.range.token, -1);
    press(&a, "\t");
    CHECK_EQ(a.play.sel, 0);
    CHECK_EQ(a.play.range.active, 1);

    CASE("esc still cancels a tile overlay");
    press(&a, "\x1b");
    CHECK_EQ(a.play.range.active, 0);

    /* Held first, overlay second, selection last: esc unwinds one at a time. */
    CASE("esc puts a held creature down before touching the overlay");
    a.ed.cx = a.map->tokens.v[0].x;
    a.ed.cy = a.map->tokens.v[0].y;
    play_select_at(&a.play, a.map, a.ed.cx, a.ed.cy, 1);
    CHECK_EQ(a.play.sel, 0);
    press(&a, "r");
    CHECK_EQ(a.play.range.active, 1);
    press(&a, "\r");                        /* pick it up */
    CHECK_EQ(a.play.grabbed, 1);
    CHECK_EQ(a.play.ntrail, 1);
    press(&a, "\x1b");
    CHECK_EQ(a.play.grabbed, 0);
    CHECK_EQ(a.play.ntrail, 0);
    CHECK_EQ(a.play.range.active, 1);

    CASE("dropping with enter clears the trail too");
    press(&a, "\rl");
    CHECK_EQ(a.play.grabbed, 1);
    CHECK_EQ(a.play.ntrail, 2);
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 0);
    CHECK_EQ(a.play.ntrail, 0);

    /* Steps used to be pushed with no mark closing them, so u reached past
     * them to the batch underneath and took the whole token off the map. */
    CASE("undo after a move takes back the step, not the creature");
    int ntok = a.map->tokens.n;
    a.ed.cx = a.map->tokens.v[0].x;
    a.ed.cy = a.map->tokens.v[0].y;
    play_select_at(&a.play, a.map, a.ed.cx, a.ed.cy, 1);
    press(&a, "\r");
    int ox = a.map->tokens.v[0].x;
    int dir = ox > 0 ? -1 : 1;               /* the fixture map is only 2 wide */
    press(&a, dir < 0 ? "h" : "l");
    CHECK_EQ(a.map->tokens.v[0].x, ox + dir);
    CHECK_EQ(a.play.steps, 1);
    press(&a, "u");
    CHECK_EQ(a.map->tokens.n, ntok);
    CHECK_EQ(a.map->tokens.v[0].x, ox);

    CASE("and the trail retreats with it");
    CHECK_EQ(a.play.ntrail, 1);
    CHECK_EQ(a.play.steps, 0);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

void test_status_draw(void)
{
    Map *m = map_new(8, 8, "draw");
    map_fill_tiles(m, 0, 0, 7, 7, TILE_FLOOR);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 60, 24);

    GridView g;
    memset(&g, 0, sizeof g);
    g.zoom = 1;                            /* a 1x1 token is three cells wide */
    g.view = rect(0, 0, 60, 24);

    Token t;
    memset(&t, 0, sizeof t);
    t.x = 2; t.y = 2; t.size = 1; t.kind = TOKEN_ENEMY;
    str_lcpy(t.label, "G", sizeof t.label);

    Rect a;
    grid_token_area(&g, t.x, t.y, t.size, &a);

    CASE("an unmarked token draws no markers");
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 0);
    CHECK_EQ(rnd_at(&r, a.x, a.y - 1)->ch, ' ');

    /* Above the token, never on it: the label has to stay readable. */
    CASE("markers sit on the boundary row, not on the token");
    token_add_status(&t, 0, "Poisoned");
    token_add_status(&t, 3, "Marked");
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 0);
    CHECK_EQ(rnd_at(&r, a.x, a.y - 1)->ch, 'P');
    CHECK_EQ(rnd_at(&r, a.x + 1, a.y - 1)->ch, 'M');
    CHECK_EQ(rnd_at(&r, a.x, a.y)->ch, ' ');        /* the token row is untouched */

    CASE("each marker takes its own color from the palette");
    CHECK_EQ(rnd_at(&r, a.x, a.y - 1)->fg, THEME_DARK.status[0]);
    CHECK_EQ(rnd_at(&r, a.x + 1, a.y - 1)->fg, THEME_DARK.status[3]);

    /* Three cells is fewer than a token can carry, so the fourth has to go
     * somewhere: the row below. */
    CASE("markers past the top edge continue underneath");
    token_add_status(&t, 4, "Burning");
    token_add_status(&t, 5, "Slowed");
    CHECK_EQ(t.nstatus, 4);
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 0);
    CHECK_EQ(rnd_at(&r, a.x + 2, a.y - 1)->ch, 'B');
    CHECK_EQ(rnd_at(&r, a.x, a.y + a.h)->ch, 'S');

    CASE("a wider token fits them all on one row");
    g.zoom = 2;
    grid_token_area(&g, t.x, t.y, t.size, &a);
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 0);
    CHECK_EQ(rnd_at(&r, a.x + 3, a.y - 1)->ch, 'S');

    CASE("ascii mode keeps letters and swaps the dot");
    g.zoom = 1;
    token_clear_status(&t);
    token_add_status(&t, 0, "Poisoned");
    token_add_status(&t, 1, "");
    grid_token_area(&g, t.x, t.y, t.size, &a);
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 1);
    CHECK_EQ(rnd_at(&r, a.x, a.y - 1)->ch, 'P');
    CHECK_EQ(rnd_at(&r, a.x + 1, a.y - 1)->ch, '*');

    rnd_free(&r);
    map_free(m);
}
