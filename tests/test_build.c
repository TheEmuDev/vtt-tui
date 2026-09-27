/* Tests: build mode: walls, doors and edges, terrain, the brush, shapes, stamps. */

#include "harness.h"

/* ------------------------------------------------------- doors and terrain */

void test_edges(void)
{
    Map *m = map_new(9, 9, "edges");
    map_fill_tiles(m, 0, 0, 8, 8, TILE_FLOOR);

    /* Movement and sight are separate questions, and each kind answers them
     * differently. This is the whole point of having kinds at all. */
    CASE("each boundary kind stops what it should");
    const struct { uint8_t kind; int stops_move; int stops_sight; } K[] = {
        { EDGE_NONE,          0, 0 },
        { EDGE_WALL,          1, 1 },
        { EDGE_DOOR_CLOSED,   1, 1 },
        { EDGE_DOOR_OPEN,     0, 0 },
        { EDGE_WINDOW,        1, 0 },   /* see through, cannot walk through */
        { EDGE_SECRET_CLOSED, 1, 1 },
        { EDGE_SECRET_OPEN,   0, 0 },
    };
    for (size_t i = 0; i < sizeof K / sizeof *K; i++) {
        map_set_vedge(m, 5, 4, K[i].kind);
        CHECK_EQ(map_edge_blocked(m, 4, 4, 1, 0), K[i].stops_move);
        CHECK_EQ(map_edge_opaque(m, 4, 4, 1, 0), K[i].stops_sight);
        CHECK_EQ(map_blocked(m, 4, 4, 1, 0), K[i].stops_move);
        CHECK_EQ(sight_blocked(m, 4, 4, 6, 4), K[i].stops_sight);
    }
    map_set_vedge(m, 5, 4, EDGE_NONE);

    CASE("a window is the one you can shoot through but not walk through");
    map_set_vedge(m, 5, 4, EDGE_WINDOW);
    CHECK_EQ(map_blocked(m, 4, 4, 1, 0), 1);
    CHECK_EQ(sight_blocked(m, 4, 4, 8, 4), 0);
    map_set_vedge(m, 5, 4, EDGE_NONE);

    CASE("only doors toggle");
    CHECK_EQ(edge_is_door(EDGE_DOOR_CLOSED), 1);
    CHECK_EQ(edge_is_door(EDGE_SECRET_OPEN), 1);
    CHECK_EQ(edge_is_door(EDGE_WALL), 0);
    CHECK_EQ(edge_is_door(EDGE_WINDOW), 0);
    CHECK_EQ(edge_toggled(EDGE_DOOR_CLOSED), EDGE_DOOR_OPEN);
    CHECK_EQ(edge_toggled(EDGE_DOOR_OPEN), EDGE_DOOR_CLOSED);
    CHECK_EQ(edge_toggled(EDGE_SECRET_CLOSED), EDGE_SECRET_OPEN);
    CHECK_EQ(edge_toggled(EDGE_WALL), EDGE_WALL);      /* a wall is a wall */

    CASE("opening a door opens the way through it");
    map_set_vedge(m, 5, 4, EDGE_DOOR_CLOSED);
    CHECK_EQ(map_blocked(m, 4, 4, 1, 0), 1);
    map_set_vedge(m, 5, 4, edge_toggled(map_vedge(m, 5, 4)));
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_DOOR_OPEN);
    CHECK_EQ(map_blocked(m, 4, 4, 1, 0), 0);
    CHECK_EQ(sight_blocked(m, 4, 4, 6, 4), 0);

    CASE("every kind survives a round trip through its file character");
    for (int k = 0; k < EDGE_COUNT; k++)
        CHECK_EQ(edge_from_file_char(edge_file_char((uint8_t)k)), k);
    for (int k = 0; k < TILE_COUNT; k++)
        CHECK_EQ(tile_from_file_char(tile_file_char((uint8_t)k)), k);

    CASE("horizontal walls written before doors existed still load");
    CHECK_EQ(edge_from_file_char('-'), EDGE_WALL);
    CHECK_EQ(edge_from_file_char('?'), -1);
    CHECK_EQ(tile_from_file_char('?'), -1);

    map_free(m);
}

void test_terrain(void)
{
    Map *m = map_new(8, 8, "terrain");

    /* Terrain is decoration. Anything that is not void is map, and the map
     * does not decide what difficult ground costs. */
    CASE("every terrain is walkable; only void is not");
    for (int k = 0; k < TILE_COUNT; k++) {
        map_set_tile(m, 3, 3, (uint8_t)k);
        CHECK_EQ(map_walkable(m, 3, 3), k != TILE_VOID);
    }

    CASE("terrain does not affect movement or sight");
    map_fill_tiles(m, 0, 0, 7, 7, TILE_HAZARD);
    CHECK_EQ(map_blocked(m, 3, 3, 1, 0), 0);
    CHECK_EQ(sight_blocked(m, 0, 3, 7, 3), 0);
    map_fill_tiles(m, 0, 0, 7, 7, TILE_WATER);
    CHECK_EQ(map_blocked(m, 3, 3, 1, 0), 0);

    /* Grid lines mark walkable ground, so they must follow terrain and not
     * just plain floor. */
    CASE("grid lines show on every terrain, not only on floor");
    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 40, 20);
    GridView g;
    memset(&g, 0, sizeof g);
    g.zoom = 0;
    g.view = rect(0, 0, 40, 20);

    for (int k = TILE_FLOOR; k < TILE_COUNT; k++) {
        Map *t = map_new(3, 3, "t");
        map_fill_tiles(t, 0, 0, 2, 2, (uint8_t)k);
        rnd_begin(&r);
        grid_draw(&r, t, &g, &THEME_DARK, 0, 1, FOGV_GM);
        CHECK_EQ(rnd_at(&r, 0, 0)->ch, 0x250Cu);      /* the map's outer corner */
        map_free(t);
    }

    CASE("void draws its mark and no terrain, whatever the palette");
    Map *v = map_new(3, 3, "v");
    rnd_begin(&r);
    grid_draw(&r, v, &g, &THEME_DARK, 0, 1, FOGV_GM);
    int drawn = 0, marks = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint32_t ch = rnd_at(&r, x, y)->ch;
            if (ch == ' ') continue;
            drawn++;
            if (ch == 0x00B7u) marks++;
        }
    CHECK_EQ(marks, 9);
    CHECK_EQ(drawn, marks);
    map_free(v);

    rnd_free(&r);
    map_free(m);
}

void test_secret_doors(void)
{
    Map *m = map_new(5, 5, "secret");
    map_fill_tiles(m, 0, 0, 4, 4, TILE_FLOOR);
    map_set_vedge(m, 2, 2, EDGE_SECRET_CLOSED);

    Map *w = map_new(5, 5, "wall");
    map_fill_tiles(w, 0, 0, 4, 4, TILE_FLOOR);
    map_set_vedge(w, 2, 2, EDGE_WALL);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 40, 20);
    GridView g;
    memset(&g, 0, sizeof g);
    g.zoom = 1;
    g.view = rect(0, 0, 40, 20);

    /* The point of a secret door is that nobody reading the screen in play
     * can tell it from a wall. Not by glyph, and not by color. */
    CASE("in play mode a secret door is pixel for pixel a wall");
    rnd_begin(&r);
    grid_draw(&r, m, &g, &THEME_DARK, 0, 0, FOGV_GM);
    Cell secret[40 * 20];
    memcpy(secret, r.back, sizeof secret);

    rnd_begin(&r);
    grid_draw(&r, w, &g, &THEME_DARK, 0, 0, FOGV_GM);
    int same = 1;
    for (int i = 0; i < 40 * 20; i++)
        if (secret[i].ch != r.back[i].ch || secret[i].fg != r.back[i].fg ||
            secret[i].bg != r.back[i].bg || secret[i].attr != r.back[i].attr)
            same = 0;
    CHECK_EQ(same, 1);

    CASE("in build mode it is marked, so the GM can see their own door");
    rnd_begin(&r);
    grid_draw(&r, m, &g, &THEME_DARK, 0, 1, FOGV_GM);
    int differs = 0;
    for (int i = 0; i < 40 * 20; i++)
        if (secret[i].ch != r.back[i].ch || secret[i].fg != r.back[i].fg)
            differs++;
    CHECK(differs > 0);

    CASE("but it blocks exactly like a wall either way");
    CHECK_EQ(map_blocked(m, 1, 2, 1, 0), map_blocked(w, 1, 2, 1, 0));
    CHECK_EQ(sight_blocked(m, 1, 2, 3, 2), sight_blocked(w, 1, 2, 3, 2));

    rnd_free(&r);
    map_free(m);
    map_free(w);
}

void test_edge_tools(void)
{
    Map *m = map_new(9, 9, "tools");
    map_fill_tiles(m, 0, 0, 8, 8, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Editor e;
    ed_init(&e, m);
    ed_layout(&e, m, 80, 24);

    CASE("a fresh editor lays walls on plain floor");
    CHECK_EQ(e.material, EDGE_WALL);
    CHECK_EQ(e.terrain, TILE_FLOOR);

    CASE("the selectors cycle and wrap past the eraser");
    for (int i = 0; i < EDGE_COUNT * 2; i++) {
        ed_cycle_material(&e);
        CHECK(e.material != EDGE_NONE);
        CHECK(e.material < EDGE_COUNT);
    }
    for (int i = 0; i < TILE_COUNT * 2; i++) {
        ed_cycle_terrain(&e);
        CHECK(e.terrain != TILE_VOID);
        CHECK(e.terrain < TILE_COUNT);
    }

    CASE("a face takes the selected material, and drops it when pressed again");
    e.material = EDGE_WINDOW;
    e.cx = 4; e.cy = 4;
    ed_toggle_edge(&e, m, &u, 1, 0);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_WINDOW);
    ed_toggle_edge(&e, m, &u, 1, 0);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_NONE);

    CASE("a different material replaces rather than clears");
    e.material = EDGE_WALL;
    ed_toggle_edge(&e, m, &u, 1, 0);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_WALL);
    e.material = EDGE_DOOR_CLOSED;
    ed_toggle_edge(&e, m, &u, 1, 0);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_DOOR_CLOSED);

    CASE("the pen lays whatever is selected");
    e.material = EDGE_WINDOW;
    e.mode = ED_WALL;
    e.wx = 1; e.wy = 1;
    e.pen = 1;
    ed_wall_step(&e, m, &u, 1, 0, 3);
    for (int x = 1; x < 4; x++) CHECK_EQ(map_hedge(m, x, 1), EDGE_WINDOW);
    e.mode = ED_NORMAL;
    e.pen = 0;

    CASE("painting uses the selected terrain");
    e.terrain = TILE_WATER;
    e.cx = 6; e.cy = 6;
    ed_apply_tiles(&e, m, &u, e.terrain);
    CHECK_EQ(map_tile(m, 6, 6), TILE_WATER);
    ed_toggle_tile(&e, m, &u);
    CHECK_EQ(map_tile(m, 6, 6), TILE_VOID);
    ed_toggle_tile(&e, m, &u);
    CHECK_EQ(map_tile(m, 6, 6), TILE_WATER);

    /* Opening an ordinary door beside a secret one must not give the secret
     * away, so the two are toggled by separate requests. */
    CASE("doors and secret doors toggle separately");
    e.cx = 4; e.cy = 4;
    map_set_vedge(m, 4, 4, EDGE_DOOR_CLOSED);      /* west face */
    map_set_vedge(m, 5, 4, EDGE_SECRET_CLOSED);    /* east face */

    CHECK_EQ(ed_toggle_doors(&e, m, &u, 0), 1);
    CHECK_EQ(map_vedge(m, 4, 4), EDGE_DOOR_OPEN);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_SECRET_CLOSED);   /* untouched */

    CHECK_EQ(ed_toggle_doors(&e, m, &u, 1), 1);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_SECRET_OPEN);

    CASE("toggling a door is undoable");
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(map_vedge(m, 5, 4), EDGE_SECRET_CLOSED);

    CASE("a tile with no doors reports none, and changes nothing");
    e.cx = 8; e.cy = 8;
    CHECK_EQ(ed_toggle_doors(&e, m, &u, 0), 0);
    CHECK_EQ(ed_toggle_doors(&e, m, &u, 1), 0);

    undo_free(&u);
    map_free(m);
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

/* ---------------------------------------------------------------- stamps */

/* A map's squares, boundaries, creatures and notes as text, for comparing. */
static char *stamp_text(const Map *m)
{
    size_t n;
    char  *t = tool_text(m, 0, 0, m->w - 1, m->h - 1, &n);
    return t;
}

/* A small asymmetric piece: an L of wall with a door in it, a window, water
 * in one corner, a 2x2 creature and a note, so every turn is told apart. */
static Map *stamp_fixture(void)
{
    Map *s = map_new(4, 3, "piece");
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 4; x++) map_set_tile(s, x, y, TILE_FLOOR);
    map_set_tile(s, 3, 0, TILE_WATER);
    map_set_tile(s, 0, 2, TILE_VOID);
    map_set_vedge(s, 0, 0, EDGE_WALL);
    map_set_vedge(s, 0, 1, EDGE_DOOR_CLOSED);
    map_set_hedge(s, 0, 0, EDGE_WALL);
    map_set_hedge(s, 1, 0, EDGE_WALL);
    map_set_hedge(s, 2, 3, EDGE_WINDOW);
    map_set_vedge(s, 4, 2, EDGE_WALL);
    Token t;
    memset(&t, 0, sizeof t);
    t.x = 1; t.y = 1; t.size = 2; t.kind = TOKEN_ENEMY;
    str_lcpy(t.label, "Ogre", sizeof t.label);
    tokens_add(&s->tokens, t);
    map_note_set(s, 3, 0, "well");
    return s;
}

void test_stamps(void)
{
    char err[160];
    Map *s = stamp_fixture();
    char *orig = stamp_text(s);

    CASE("turning: each quarter as the goldens have it, four quarters the start again");
    static const char *const names[] = { "stamp-turn0", "stamp-turn1", "stamp-turn2", "stamp-turn3" };
    for (int q = 0; q < 4; q++) {
        Map *t = stamp_turned(s, q);
        CHECK_EQ(t->w, q % 2 ? 3 : 4);
        char *txt = stamp_text(t);
        golden_bytes(names[q], txt, strlen(txt));
        free(txt);
        map_free(t);
    }
    {
        Map *t = s;
        Map *owned[4];
        for (int q = 0; q < 4; q++) { owned[q] = stamp_turned(t, 1); t = owned[q]; }
        char *txt = stamp_text(t);
        CHECK_EQ(strcmp(txt, orig), 0);
        free(txt);
        for (int q = 0; q < 4; q++) map_free(owned[q]);
        Map *back = stamp_turned(s, -1), *fwd = stamp_turned(s, 3);
        char *a = stamp_text(back), *b = stamp_text(fwd);
        CHECK_EQ(strcmp(a, b), 0);                          /* -1 is 3 */
        free(a); free(b); map_free(back); map_free(fwd);
    }

    CASE("mirroring: as the golden has it, twice the start again");
    {
        Map *m1 = stamp_mirrored(s), *m2 = stamp_mirrored(m1);
        char *a = stamp_text(m1), *b = stamp_text(m2);
        golden_bytes("stamp-mirror", a, strlen(a));
        CHECK_EQ(strcmp(b, orig), 0);
        free(a); free(b); map_free(m1); map_free(m2);
    }

    CASE("mirroring moves a creature by its size: a 2x2 at the west edge of 4 wide lands at x 2");
    {
        Map *one = map_new(4, 2, "one");
        Token t;
        memset(&t, 0, sizeof t);
        t.size = 2;
        tokens_add(&one->tokens, t);
        Map *mm = stamp_mirrored(one);
        CHECK(mm->tokens.v[0].x == 2 && mm->tokens.v[0].y == 0);
        map_free(mm);
        map_free(one);
    }

    CASE("an empty box has no outline: a box of corners in one line lays nothing");
    {
        Map *m = map_new(6, 4, "m");
        Undo u;
        undo_init(&u);
        EdShape line = ed_shape(ED_SHAPE_RECT, 2, 1, 2, 3, 1);   /* two corners, one column */
        ed_wall_shape(m, &u, &line, EDGE_WALL);
        CHECK_EQ(u.nmarks, 0);
        for (int y = 0; y < 4; y++) CHECK_EQ(map_vedge(m, 2, y), EDGE_NONE);
        EdShape box = ed_shape(ED_SHAPE_RECT, 1, 1, 3, 3, 1);    /* a 2x2 between corners */
        ed_wall_shape(m, &u, &box, EDGE_WALL);
        CHECK(map_vedge(m, 1, 1) == EDGE_WALL && map_vedge(m, 3, 2) == EDGE_WALL &&
              map_hedge(m, 2, 1) == EDGE_WALL && map_hedge(m, 1, 3) == EDGE_WALL);
        CHECK(map_vedge(m, 2, 1) == EDGE_NONE);
        undo_free(&u);
        map_free(m);
    }

    CASE("copying: squares, every boundary round them, creatures wholly inside, notes; fresh creatures");
    {
        Map *m = map_new(8, 6, "m");
        Map *p = NULL;
        Undo u;
        undo_init(&u);
        for (int y = 0; y < 6; y++)
            for (int x = 0; x < 8; x++) map_set_tile(m, x, y, TILE_FLOOR);
        CHECK_EQ(stamp_place(m, &u, s, 2, 2, err, sizeof err), 1);
        m->tokens.v[0].turn = TURN_IN | TURN_ACTING;
        token_add_status(&m->tokens.v[0], 1, "Poisoned");
        p = stamp_copy(m, 2, 2, 5, 4);
        CHECK(p != NULL);
        if (p) {
            CHECK_EQ(p->tokens.n, 1);
            CHECK(p->tokens.v[0].turn == 0 && p->tokens.v[0].nstatus == 0);
            CHECK_EQ(map_vedge(p, 0, 1), EDGE_DOOR_CLOSED);
            CHECK_EQ(map_hedge(p, 2, 3), EDGE_WINDOW);
            CHECK(map_note_at(p, 3, 0) != NULL);
            map_free(p);
        }
        p = stamp_copy(m, 2, 2, 3, 4);                 /* cuts the Ogre in half: left out */
        CHECK(p && p->tokens.n == 0);
        map_free(p);
        undo_free(&u);
        map_free(m);
    }

    CASE("placing: see-through void and blank boundaries, one undo step, labels kept apart");
    {
        Map *m = map_new(10, 8, "m");
        Undo u;
        undo_init(&u);
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 10; x++) map_set_tile(m, x, y, TILE_BRUSH);
        map_set_hedge(m, 5, 5, EDGE_WALL);             /* under the stamp, where it has no boundary */
        Token og;
        memset(&og, 0, sizeof og);
        og.x = 9; og.y = 7; og.size = 1;
        str_lcpy(og.label, "Ogre", sizeof og.label);
        tokens_add(&m->tokens, og);
        char *before = stamp_text(m);
        CHECK_EQ(stamp_place(m, &u, s, 3, 3, err, sizeof err), 1);
        CHECK_EQ(map_tile(m, 3, 5), TILE_BRUSH);        /* the stamp's void square: the map's */
        CHECK_EQ(map_tile(m, 6, 3), TILE_WATER);
        CHECK_EQ(map_hedge(m, 5, 5), EDGE_WALL);        /* see-through boundary */
        CHECK_EQ(map_vedge(m, 3, 4), EDGE_DOOR_CLOSED);
        CHECK_EQ(m->tokens.n, 2);
        CHECK_EQ(strcmp(m->tokens.v[1].label, "Ogre 2"), 0);
        CHECK_EQ(u.nmarks, 1);
        undo_undo(&u, m);
        char *after = stamp_text(m);
        CHECK_EQ(strcmp(before, after), 0);
        free(before); free(after);

        CASE("placing is refused whole: off the map, a creature on void or on another, notes full");
        before = stamp_text(m);
        CHECK_EQ(stamp_place(m, &u, s, 7, 3, err, sizeof err), 0);
        CHECK(strstr(err, "runs off the map") != NULL);
        Map *hole = stamp_copy(s, 0, 0, 3, 2);
        map_set_tile(hole, 1, 2, TILE_VOID);            /* the Ogre's square in the stamp goes see-through */
        map_set_tile(m, 4, 5, TILE_VOID);               /* and the map has no ground there */
        CHECK_EQ(stamp_place(m, &u, hole, 3, 3, err, sizeof err), 0);
        CHECK(strstr(err, "Ogre would stand on void at E6") != NULL);
        map_set_tile(m, 4, 5, TILE_BRUSH);
        CHECK_EQ(stamp_place(m, &u, s, 8, 5, err, sizeof err), 0);
        CHECK(strstr(err, "runs off the map") != NULL);
        m->tokens.v[0].x = 4; m->tokens.v[0].y = 4;     /* where the stamp's Ogre would go */
        CHECK_EQ(stamp_place(m, &u, s, 3, 3, err, sizeof err), 0);
        CHECK(strstr(err, "would land on Ogre") != NULL);
        m->tokens.v[0].x = 9; m->tokens.v[0].y = 7;
        for (int i = 0; m->nnotes < MAP_NOTES_MAX; i++) map_note_set(m, i % 10, i / 10, "x");
        map_note_set(m, 6, 3, "");                       /* the stamp's note square is free */
        map_note_set(m, 9, 7, "y");
        CHECK_EQ(stamp_place(m, &u, s, 3, 3, err, sizeof err), 0);
        CHECK(strstr(err, "no room") != NULL);
        map_free(hole);
        free(before);
        undo_free(&u);
        map_free(m);
    }

    CASE("the preview: shown for a draw and put back exactly, touching nothing");
    {
        Map *m = map_new(10, 8, "m");
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 10; x++) map_set_tile(m, x, y, TILE_BRUSH);
        Token og;
        memset(&og, 0, sizeof og);
        og.x = 0; og.y = 0; og.size = 1;
        tokens_add(&m->tokens, og);
        char *before = stamp_text(m);
        unsigned gen = m->gen, shape = m->tokens.shape;
        StampShow sv;
        stamp_show(m, s, 8, 6, &sv);                    /* hangs off the corner */
        CHECK_EQ(map_tile(m, 8, 6), TILE_FLOOR);          /* the stamp's corner, clipped */
        CHECK_EQ(map_vedge(m, 8, 7), EDGE_DOOR_CLOSED);
        CHECK_EQ(m->tokens.n, 1);                        /* its Ogre would hang off the map: not shown */
        stamp_unshow(m, &sv);
        char *after = stamp_text(m);
        CHECK_EQ(strcmp(before, after), 0);
        CHECK(m->gen == gen && m->tokens.shape == shape);
        stamp_show(m, s, 2, 2, &sv);                    /* its note shows with it, and goes */
        CHECK(map_note_at(m, 5, 2) && !strcmp(map_note_at(m, 5, 2), "well"));
        stamp_unshow(m, &sv);
        CHECK(map_note_at(m, 5, 2) == NULL);
        CHECK_EQ(m->nnotes, 0);
        free(before); free(after);
        map_free(m);
    }

    CASE("files: saved by name, listed, loaded back the same; names are never paths");
    {
        Sandbox sb = sandbox_enter("stamps");
        CHECK_EQ(stamp_name_ok("pillar-row_2"), 1);
        CHECK_EQ(stamp_name_ok("../x"), 0);
        CHECK_EQ(stamp_name_ok(""), 0);
        CHECK_EQ(stamp_name_ok("a b"), 0);
        CHECK_EQ(stamp_save(s, "../evil", err, sizeof err), -1);
        CHECK_EQ(stamp_save(s, "Piece", err, sizeof err), 0);
        CHECK_EQ(stamp_save(s, "Altar", err, sizeof err), 0);
        char listed[4][MAP_NAME_MAX];
        CHECK_EQ(stamp_list(listed, 4), 2);
        CHECK(!strcmp(listed[0], "Altar") && !strcmp(listed[1], "Piece"));
        Map *l = stamp_load("Piece", err, sizeof err);
        CHECK(l != NULL);
        if (l) {
            CHECK_EQ(strcmp(l->name, "Piece"), 0);    /* the file says its name */
            str_lcpy(l->name, s->name, sizeof l->name);
            char *a = stamp_text(l);
            CHECK_EQ(strcmp(a, orig), 0);
            if (strcmp(a, orig)) fprintf(stderr, "%s\n---\n%s", a, orig);
            free(a);
            map_free(l);
        }
        CHECK(stamp_load("Nothing", err, sizeof err) == NULL);
        CASE("a listing cut short is the start of the alphabet, not the directory's order");
        char nm[16];
        for (int i = 20; i >= 1; i--) {
            snprintf(nm, sizeof nm, "b%02d", i);
            stamp_save(s, nm, err, sizeof err);
        }
        char few[3][MAP_NAME_MAX];
        CHECK_EQ(stamp_list(few, 3), 22);
        CHECK(!strcmp(few[0], "Altar") && !strcmp(few[1], "Piece") && !strcmp(few[2], "b01"));
        CHECK(strstr(err, "no stamp called Nothing") != NULL);
        char dir[600], cmd[700];
        stamp_dir(dir, sizeof dir);
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", sb.dir);
        sandbox_leave(&sb);
        if (system(cmd) != 0) { }
    }

    free(orig);
    map_free(s);
}

void test_stamp_keys(void)
{
    Sandbox sb = sandbox_enter("stampkeys");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 12, 8));
    if (!a.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.map;

    CASE("p with nothing in hand says how to get something");
    press(&a, "p");
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    CHECK(strstr(a.status, "nothing to paste") != NULL);

    CASE("y copies the box; p shows it on the cursor without changing the map; p again puts it down");
    map_set_tile(m, 1, 1, TILE_WATER);
    map_set_vedge(m, 1, 1, EDGE_WALL);
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "vly");
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    CHECK(a.stamp && a.stamp->w == 2 && a.stamp->h == 1);
    CHECK(strstr(a.status, "copied 2x1") != NULL);
    int depth = a.undo.depth;
    unsigned gen = m->gen;
    press(&a, "p5l3j");
    CHECK_EQ(a.ed.mode, ED_STAMP);
    CHECK(a.ed.cx == 7 && a.ed.cy == 4);          /* vl left the cursor on C2 */
    rnd_begin(&r);
    app_draw(&a);                                   /* the preview draws, and goes */
    CHECK_EQ(map_tile(m, 7, 4), TILE_FLOOR);
    CHECK_EQ(m->gen, gen);
    CHECK_EQ(a.undo.depth, depth);
    press(&a, "p");
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    CHECK_EQ(map_tile(m, 7, 4), TILE_WATER);
    CHECK_EQ(map_vedge(m, 7, 4), EDGE_WALL);
    CHECK_EQ(a.undo.depth, depth + 1);
    CHECK(strstr(a.status, "stamped the copy 2x1 at H5") != NULL);
    press(&a, "u");
    CHECK_EQ(map_tile(m, 7, 4), TILE_FLOOR);

    CASE("r and | turn and mirror the stamp in hand; enter places; esc puts it away placing nothing");
    press(&a, "pr");
    CHECK(a.stamp->w == 1 && a.stamp->h == 2);
    CHECK(strstr(a.status, "turned 90") != NULL);
    press(&a, "|");
    CHECK(strstr(a.status, "mirrored") != NULL);
    press(&a, "R");
    CHECK(a.stamp->w == 2 && a.stamp->h == 1);
    press(&a, "\r");
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    CHECK_EQ(a.undo.depth, depth + 1);
    depth = a.undo.depth;
    press(&a, "p\x1b");
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    CHECK_EQ(a.undo.depth, depth);

    CASE("the readout is turned-then-mirrored whatever order the keys came in, as the channel applies it");
    {
        Map *base = stamp_copy(a.stamp, 0, 0, a.stamp->w - 1, a.stamp->h - 1);
        static const char *const seqs[] = { "pr|", "p|r", "p|rr|r", "prr|R" };
        for (size_t i = 0; i < sizeof seqs / sizeof *seqs; i++) {
            map_free(a.stamp);
            a.stamp = stamp_copy(base, 0, 0, base->w - 1, base->h - 1);
            a.stamp_turns = a.stamp_mirrored = 0;
            press(&a, seqs[i]);
            Map *t = stamp_turned(base, a.stamp_turns);
            Map *want = a.stamp_mirrored ? stamp_mirrored(t) : stamp_copy(t, 0, 0, t->w - 1, t->h - 1);
            char *x = stamp_text(a.stamp), *y = stamp_text(want);
            CHECK_EQ(strcmp(x, y), 0);
            free(x); free(y);
            map_free(t); map_free(want);
            press(&a, "\x1b");
        }
        map_free(a.stamp);
        a.stamp = base;
        a.stamp_turns = a.stamp_mirrored = 0;
    }

    CASE(": over a stamp comes back to it: :J6 jumps it there");
    press(&a, "p:J6\r");
    CHECK_EQ(a.ed.mode, ED_STAMP);
    CHECK(a.ed.cx == 9 && a.ed.cy == 5);
    press(&a, ":\x1b");
    CHECK_EQ(a.ed.mode, ED_STAMP);
    press(&a, "\x1b");

    CASE("a stamp that would run off the map is refused where it is, and the preview stays up");
    a.ed.cx = 11; a.ed.cy = 7;
    press(&a, "pp");
    CHECK_EQ(a.ed.mode, ED_STAMP);
    CHECK(strstr(a.status, "runs off the map") != NULL);
    press(&a, "\x1b");

    CASE(":stamp save keeps it, :stamp lists, :stamp NAME picks it up, -f puts it down at once");
    press(&a, ":stamp save Pool\r");
    CHECK(strstr(a.status, "stamp Pool kept") != NULL);
    press(&a, ":stamp save ../x\r");
    CHECK(strstr(a.status, "letters, digits") != NULL);
    press(&a, ":stamp\r");
    CHECK(strstr(a.status, "stamps: Pool") != NULL);
    map_free(a.stamp);
    a.stamp = NULL;
    press(&a, ":stamp Pool\r");
    CHECK_EQ(a.ed.mode, ED_STAMP);
    CHECK(strstr(a.status, "Pool 2x1") != NULL);
    press(&a, "\x1b");
    a.ed.cx = 3; a.ed.cy = 6;
    press(&a, ":stamp Pool -f\r");
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    CHECK_EQ(map_tile(m, 3, 6), TILE_WATER);
    CHECK(strstr(a.status, "stamped Pool 2x1 at D7") != NULL);
    press(&a, ":stamp Nothing\r");
    CHECK(strstr(a.status, "no stamp called Nothing") != NULL);
    press(&a, ":stamp Pool now\r");
    CHECK(strstr(a.status, ":stamp NAME, :stamp NAME -f") != NULL);

    CASE("the agent: stamps lists them, stamp puts one down turned or mirrored, inside the request's one step");
    char *t = ctl_ask(&a, "stamps\n");
    CHECK_EQ(strcmp(t, "ok\nPool  2x1\n"), 0);
    free(t);
    depth = a.undo.depth;
    t = ctl_ask(&a, "stamp Pool J2 rotate 90\nstamp Pool A8 mirror\n");
    CHECK(strncmp(t, "ok\nchanged", 10) == 0);
    free(t);
    CHECK_EQ(a.undo.depth, depth + 1);
    CHECK_EQ(map_tile(m, 9, 1), TILE_WATER);          /* turned: the water on top */
    CHECK_EQ(map_hedge(m, 9, 1), EDGE_WALL);           /* its west wall is now its top */
    CHECK_EQ(map_tile(m, 1, 7), TILE_WATER);           /* mirrored: the water on the right */
    CHECK_EQ(map_vedge(m, 2, 7), EDGE_WALL);
    t = ctl_ask(&a, "stamp Pool L8\n");
    CHECK(strstr(t, "runs off the map") != NULL);
    free(t);
    t = ctl_ask(&a, "stamp Pool B2 rotate 45\n");
    CHECK(strstr(t, "stamp NAME SQUARE [rotate") != NULL);
    free(t);
    t = ctl_ask(&a, "stamp Nope B2\n");
    CHECK(strstr(t, "no stamp called Nope") != NULL);
    free(t);

    CASE("stamps are build mode's");
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    press(&a, ":stamp Pool\r");
    CHECK(strstr(a.status, "stamps are build mode's") != NULL);

    app_free(&a);
    rnd_free(&r);
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", sb.dir);
    sandbox_leave(&sb);
    if (system(cmd) != 0) { }
}
