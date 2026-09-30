/* Tests: distances, rulesets, the ruler, sight, the range overlay. */

#include "harness.h"

/* ----------------------------------------------------------------- ruler */

void test_dist(void)
{
    CASE("a zero offset is zero under every metric");
    for (int m = 0; m < DIST_COUNT; m++) CHECK_EQ((int)(dist_tiles(m, 0, 0) * 10), 0);

    /* The 3-4-5 triangle makes the metrics tell their differences plainly. */
    CASE("the metrics differ as documented on a 4x3 offset");
    CHECK_EQ((int)(dist_tiles(DIST_CHEBYSHEV, 4, 3) * 10), 40);
    CHECK_EQ((int)(dist_tiles(DIST_EUCLIDEAN, 4, 3) * 10), 50);
    CHECK_EQ((int)(dist_tiles(DIST_ALT_DIAG,  4, 3) * 10), 50);
    CHECK_EQ((int)(dist_tiles(DIST_MANHATTAN, 4, 3) * 10), 70);

    CASE("a pure diagonal shows the diagonal rule");
    CHECK_EQ((int)(dist_tiles(DIST_CHEBYSHEV, 4, 4) * 10), 40);   /* free diagonals */
    CHECK_EQ((int)(dist_tiles(DIST_ALT_DIAG,  4, 4) * 10), 60);   /* 5-10-5 */
    CHECK_EQ((int)(dist_tiles(DIST_MANHATTAN, 4, 4) * 10), 80);

    CASE("orthogonal offsets agree across all metrics");
    for (int m = 0; m < DIST_COUNT; m++) {
        CHECK_EQ((int)(dist_tiles(m, 6, 0) * 10), 60);
        CHECK_EQ((int)(dist_tiles(m, 0, 6) * 10), 60);
    }

    CASE("distance does not depend on direction");
    for (int m = 0; m < DIST_COUNT; m++) {
        double a = dist_tiles(m, 5, 3), b = dist_tiles(m, -5, -3);
        double c = dist_tiles(m, -5, 3), d = dist_tiles(m, 5, -3);
        CHECK_EQ((int)(a * 100), (int)(b * 100));
        CHECK_EQ((int)(a * 100), (int)(c * 100));
        CHECK_EQ((int)(a * 100), (int)(d * 100));
    }

    CASE("metric names round-trip, with aliases");
    for (int m = 0; m < DIST_COUNT; m++)
        CHECK_EQ(dist_metric_from_name(dist_metric_name(m)), m);
    CHECK_EQ(dist_metric_from_name("5e"), DIST_CHEBYSHEV);
    CHECK_EQ(dist_metric_from_name("5-10-5"), DIST_ALT_DIAG);
    CHECK_EQ(dist_metric_from_name("nonsense"), -1);
}

void test_ruleset(void)
{
    CASE("an unset ruleset means no bands, not a crash");
    const Ruleset *none = ruleset_by_name("");
    CHECK(none != NULL);
    CHECK(ruleset_band(none, 25.0) == NULL);
    CHECK(ruleset_band(NULL, 25.0) == NULL);

    CASE("an unknown ruleset is reported, not guessed at");
    CHECK(ruleset_by_name("pathfinder") == NULL);

    const Ruleset *dh = ruleset_by_name("daggerheart");
    CHECK(dh != NULL);
    if (!dh) return;

    CASE("every band resolves and none is skipped");
    for (int i = 0; i < dh->nbands; i++) {
        const char *got = ruleset_band(dh, dh->bands[i].max);
        CHECK(got != NULL);
        CHECK_EQ(strcmp(got, dh->bands[i].name), 0);
    }

    CASE("band thresholds are ordered, so lookup is unambiguous");
    for (int i = 1; i < dh->nbands; i++) CHECK(dh->bands[i].max > dh->bands[i - 1].max);

    CASE("the last band catches everything beyond it");
    const char *far = ruleset_band(dh, 1e9);
    CHECK(far != NULL);
    CHECK_EQ(strcmp(far, dh->bands[dh->nbands - 1].name), 0);

    CASE("zero distance lands in the first band");
    CHECK_EQ(strcmp(ruleset_band(dh, 0.0), dh->bands[0].name), 0);

    CASE("daggerheart thresholds come from the book, so they are verified");
    CHECK_EQ(dh->verified, 1);

    /* The SRD gives each band twice: a fiction distance and an estimate for a
     * physical map. These are the map estimates converted at the book's own
     * "1 inch is roughly 5 feet", so at a five-foot square they must land on
     * the square counts the book's estimates work out to. */
    CASE("the bands land on the book's square counts at five-foot squares");
    const struct { int squares; const char *band; } EXPECT[] = {
        {  0, "Melee"      },   /* the same square */
        {  1, "Melee"      },   /* touching: adjacent */
        {  2, "Very Close" },
        {  3, "Very Close" },   /* game card, 2-3 in */
        {  4, "Close"      },
        {  6, "Close"      },   /* pen or pencil, 5-6 in */
        {  7, "Far"        },
        { 12, "Far"        },   /* sheet of paper long edge, 11-12 in */
        { 13, "Very Far"   },
        { 99, "Very Far"   },
    };
    for (size_t i = 0; i < sizeof EXPECT / sizeof *EXPECT; i++) {
        const char *got = ruleset_band(dh, EXPECT[i].squares * 5.0);
        CHECK(got != NULL);
        if (got) CHECK_EQ(strcmp(got, EXPECT[i].band), 0);
    }

    /* Thresholds are held in feet rather than squares so that changing the
     * scale keeps them describing the same fictional distance. */
    CASE("a ten-foot square puts three squares in Close, not Very Close");
    CHECK_EQ(strcmp(ruleset_band(dh, 3 * 10.0), "Close"), 0);
    CHECK_EQ(strcmp(ruleset_band(dh, 1 * 10.0), "Very Close"), 0);

    /* Out of Range is a call about the scene, not a distance: anything the
     * cursor can reach is by definition on the map. */
    CASE("no band is Out of Range, because nothing on the map can be");
    for (int i = 0; i < dh->nbands; i++)
        CHECK(strcmp(dh->bands[i].name, "Out of Range") != 0);
}

void test_ruler(void)
{
    Ruler r;
    ruler_reset(&r);

    CASE("an inactive ruler measures nothing");
    CHECK_EQ(r.active, 0);
    CHECK_EQ((int)ruler_tiles(&r, DIST_CHEBYSHEV), 0);

    CASE("anchor and cursor give a single segment");
    ruler_start(&r, 2, 2);
    CHECK_EQ(r.active, 1);
    CHECK_EQ(r.n, 1);
    CHECK_EQ((int)ruler_tiles(&r, DIST_CHEBYSHEV), 0);
    ruler_set_cursor(&r, 6, 5);
    CHECK_EQ((int)ruler_tiles(&r, DIST_CHEBYSHEV), 4);
    CHECK_EQ((int)ruler_tiles(&r, DIST_MANHATTAN), 7);

    /* A bent path is what waypoints are for: each leg measured, then summed. */
    CASE("waypoints accumulate leg by leg");
    ruler_start(&r, 0, 0);
    ruler_set_cursor(&r, 3, 0);
    CHECK_EQ(ruler_add_waypoint(&r), 1);
    CHECK_EQ(r.n, 2);
    ruler_set_cursor(&r, 3, 4);
    CHECK_EQ((int)ruler_tiles(&r, DIST_CHEBYSHEV), 7);      /* 3 across, 4 down */
    CHECK_EQ(ruler_add_waypoint(&r), 1);
    ruler_set_cursor(&r, 5, 4);
    CHECK_EQ((int)ruler_tiles(&r, DIST_CHEBYSHEV), 9);

    /* Dropping a leg removes the waypoint but leaves the cursor where it is,
     * so the last leg now runs from the previous waypoint to the cursor. */
    CASE("dropping a leg re-measures from the previous waypoint");
    CHECK_EQ(ruler_drop_waypoint(&r), 1);
    CHECK_EQ(r.n, 2);
    CHECK_EQ((int)ruler_tiles(&r, DIST_CHEBYSHEV), 7);      /* 3 across, then 4 */
    CHECK_EQ(ruler_drop_waypoint(&r), 1);
    CHECK_EQ(r.n, 1);
    CHECK_EQ(ruler_drop_waypoint(&r), 0);                   /* the anchor stays */

    CASE("waypoints stop at the cap without corrupting the ruler");
    ruler_start(&r, 0, 0);
    int added = 0;
    for (int i = 0; i < RULER_MAX_POINTS + 8; i++) {
        ruler_set_cursor(&r, i + 1, 0);
        if (ruler_add_waypoint(&r)) added++;
    }
    CHECK_EQ(r.n, RULER_MAX_POINTS);
    CHECK_EQ(added, RULER_MAX_POINTS - 1);
    CHECK(ruler_tiles(&r, DIST_CHEBYSHEV) > 0);

    CASE("the traced line runs from anchor to cursor without gaps");
    ruler_start(&r, 1, 1);
    ruler_set_cursor(&r, 6, 4);
    RulerPt pts[64];
    int n = ruler_trace(&r, pts, 64);
    CHECK(n >= 6);
    CHECK_EQ(pts[0].x, 1);
    CHECK_EQ(pts[0].y, 1);
    CHECK_EQ(pts[n - 1].x, 6);
    CHECK_EQ(pts[n - 1].y, 4);
    for (int i = 1; i < n; i++) {
        int dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        CHECK(dx <= 1 && dy <= 1 && (dx || dy));    /* contiguous, no repeats */
    }

    CASE("a waypoint join is not traced twice");
    ruler_start(&r, 0, 0);
    ruler_set_cursor(&r, 3, 0);
    ruler_add_waypoint(&r);
    ruler_set_cursor(&r, 3, 3);
    n = ruler_trace(&r, pts, 64);
    for (int i = 1; i < n; i++)
        CHECK(!(pts[i].x == pts[i - 1].x && pts[i].y == pts[i - 1].y));

    CASE("trace reports the full length even when the buffer is small");
    RulerPt few[3];
    ruler_start(&r, 0, 0);
    ruler_set_cursor(&r, 20, 0);
    CHECK_EQ(ruler_trace(&r, few, 3), 3);           /* capped, never overrun */
}

void test_sight(void)
{
    Map *m = map_new(12, 12, "sight");
    map_fill_tiles(m, 0, 0, 11, 11, TILE_FLOOR);

    Ruler r;
    ruler_start(&r, 1, 1);

    CASE("open floor never blocks sight");
    ruler_set_cursor(&r, 9, 7);
    CHECK_EQ(ruler_sight_blocked(&r, m), 0);

    CASE("a wall across the line blocks it");
    for (int y = 0; y < 12; y++) map_set_vedge(m, 5, y, EDGE_WALL);
    CHECK_EQ(ruler_sight_blocked(&r, m), 1);

    CASE("a gap in that wall lets sight through");
    ruler_set_cursor(&r, 9, 1);
    map_set_vedge(m, 5, 1, EDGE_NONE);
    CHECK_EQ(ruler_sight_blocked(&r, m), 0);
    map_set_vedge(m, 5, 1, EDGE_WALL);
    CHECK_EQ(ruler_sight_blocked(&r, m), 1);

    CASE("sight is symmetric");
    Ruler back;
    ruler_start(&back, 9, 1);
    ruler_set_cursor(&back, 1, 1);
    CHECK_EQ(ruler_sight_blocked(&back, m), 1);

    for (int y = 0; y < 12; y++) map_set_vedge(m, 5, y, EDGE_NONE);

    /* Sight is not movement: you can see over a pit you cannot walk across. */
    CASE("void tiles do not block sight the way they block movement");
    map_set_tile(m, 5, 4, TILE_VOID);
    ruler_start(&r, 4, 4);
    ruler_set_cursor(&r, 7, 4);
    CHECK_EQ(ruler_sight_blocked(&r, m), 0);
    CHECK_EQ(map_blocked(m, 4, 4, 1, 0), 1);        /* but you cannot step there */
    map_set_tile(m, 5, 4, TILE_FLOOR);

    /* Looking diagonally past a corner: one wall still leaves a way round,
     * two walls meeting at the corner do not. */
    CASE("a diagonal past a single wall is still visible");
    ruler_start(&r, 2, 2);
    ruler_set_cursor(&r, 3, 3);
    CHECK_EQ(ruler_sight_blocked(&r, m), 0);
    map_set_vedge(m, 3, 2, EDGE_WALL);
    CHECK_EQ(ruler_sight_blocked(&r, m), 0);

    CASE("a diagonal into a sealed corner is not");
    map_set_hedge(m, 2, 3, EDGE_WALL);
    CHECK_EQ(ruler_sight_blocked(&r, m), 1);
    map_set_vedge(m, 3, 2, EDGE_NONE);
    map_set_hedge(m, 2, 3, EDGE_NONE);

    CASE("measuring to where you stand is always clear");
    ruler_start(&r, 5, 5);
    CHECK_EQ(ruler_sight_blocked(&r, m), 0);

    map_free(m);
}

void test_measure_settings(void)
{
    char path[] = "/tmp/vtt-scale-XXXXXX";
    int  fd = mkstemp(path);
    if (fd >= 0) close(fd);

    Map *m = map_new(8, 8, "scaled");
    map_fill_tiles(m, 0, 0, 7, 7, TILE_FLOOR);

    CASE("a new map defaults to five-foot squares, 5-10-5, and no ruleset");
    CHECK_EQ((int)m->scale_ft, 5);
    CHECK_EQ(m->metric, DIST_ALT_DIAG);
    CHECK_EQ(m->metric, MAP_METRIC_DEFAULT);
    CHECK_EQ(m->ruleset[0], '\0');

    m->scale_ft = 10.0;
    m->metric   = DIST_EUCLIDEAN;
    str_lcpy(m->ruleset, "daggerheart", sizeof m->ruleset);

    char err[MAPIO_ERR_MAX] = { 0 };
    CASE("measurement settings travel with the map");
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);

    Map *l = mapio_load(path, err, sizeof err);
    CHECK(l != NULL);
    if (l) {
        CHECK_EQ((int)l->scale_ft, 10);
        CHECK_EQ(l->metric, DIST_EUCLIDEAN);
        CHECK_EQ(strcmp(l->ruleset, "daggerheart"), 0);
        map_free(l);
    }

    /* An older map has none of these lines, and must still load. */
    CASE("a map without measurement settings gets the defaults");
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("VTT 1\nname Old\nsize 4 4\nzoom 1\ntiles\n....\n....\n....\n....\n", f);
        fclose(f);
    }
    Map *old = mapio_load(path, err, sizeof err);
    CHECK(old != NULL);
    if (old) {
        CHECK_EQ((int)old->scale_ft, (int)MAP_SCALE_DEFAULT);
        CHECK_EQ(old->metric, MAP_METRIC_DEFAULT);
        CHECK_EQ(old->ruleset[0], '\0');
        map_free(old);
    }

    CASE("a nonsense scale falls back rather than poisoning every measurement");
    f = fopen(path, "w");
    if (f) {
        fputs("VTT 1\nname Bad\nsize 4 4\nscale -3\nmetric wat\nruleset nope\n"
              "tiles\n....\n....\n....\n....\n", f);
        fclose(f);
    }
    Map *bad = mapio_load(path, err, sizeof err);
    CHECK(bad != NULL);
    if (bad) {
        CHECK_EQ((int)bad->scale_ft, (int)MAP_SCALE_DEFAULT);
        CHECK_EQ(bad->metric, MAP_METRIC_DEFAULT);
        CHECK_EQ(bad->ruleset[0], '\0');
        map_free(bad);
    }

    map_free(m);
    unlink(path);
}

/* --------------------------------------------------------- range overlay */

void test_range(void)
{
    Map *m = map_new(21, 15, "range");
    map_fill_tiles(m, 0, 0, 20, 14, TILE_FLOOR);
    str_lcpy(m->ruleset, "daggerheart", sizeof m->ruleset);

    RangeOverlay ro;
    range_clear(&ro);

    CASE("the overlay starts off and highlights nothing");
    CHECK_EQ(ro.active, 0);
    CHECK_EQ(range_contains(&ro, m, 5, 5), 0);

    CASE("cycling walks the bands then switches off");
    const Ruleset *rs = ruleset_by_name("daggerheart");
    CHECK(rs != NULL);
    for (int i = 0; i < rs->nbands; i++) {
        CHECK_EQ(range_cycle(&ro, m, -1, 10, 7, 0), i);
        CHECK_EQ(ro.active, 1);
        CHECK_EQ(ro.band, i);
    }
    CHECK_EQ(range_cycle(&ro, m, -1, 10, 7, 0), -1);
    CHECK_EQ(ro.active, 0);

    /* The anchor is taken once, on the way in, so walking the cursor away
     * while flipping through bands does not drag the highlight with it. */
    CASE("cycling does not move the anchor");
    range_clear(&ro);
    range_cycle(&ro, m, -1, 10, 7, 0);
    range_cycle(&ro, m, -1, 2, 2, 0);
    range_cycle(&ro, m, -1, 18, 13, 0);
    int ax, ay, as;
    range_anchor(&ro, m, &ax, &ay, &as);
    CHECK_EQ(ax, 10);
    CHECK_EQ(ay, 7);
    CHECK_EQ(as, 1);

    /* Most games say "creatures within 50 ft" rather than naming bands, so
     * a map with no ruleset still gets an overlay: a plain radius, one
     * square's worth of reach per press. */
    CASE("without a ruleset, r grows a radius a square at a time");
    Map *plain = map_new(30, 30, "plain");
    map_fill_tiles(plain, 0, 0, 29, 29, TILE_FLOOR);
    RangeOverlay bare;
    range_clear(&bare);
    CHECK_EQ(range_cycle(&bare, plain, -1, 5, 5, 0), 1);
    CHECK_EQ(bare.active, 1);
    CHECK_EQ(range_cycle(&bare, plain, -1, 5, 5, 0), 2);
    CHECK_EQ(bare.radius, 2);

    CASE("the radius is squares of the map's scale, and contains agrees");
    CHECK_EQ(range_units_to(&bare, plain, 7, 5), 2 * plain->scale_ft);
    CHECK_EQ(range_contains(&bare, plain, 7, 5), 1);   /* 2 sq: the edge */
    CHECK_EQ(range_contains(&bare, plain, 8, 5), 0);   /* 3 sq: outside */

    CASE("a count names the radius outright, so 20r is 100 ft");
    CHECK_EQ(range_cycle(&bare, plain, -1, 5, 5, 20), 20);
    CHECK_EQ(bare.radius, 20);
    CHECK_EQ(range_contains(&bare, plain, 25, 5), 1);
    CHECK_EQ(range_contains(&bare, plain, 26, 5), 0);

    CASE("a negative count is no count, and the radius is capped");
    CHECK_EQ(range_cycle(&bare, plain, -1, 5, 5, -7), 21);
    CHECK_EQ(range_cycle(&bare, plain, -1, 5, 5, 1000000), RANGE_RADIUS_MAX);
    CHECK_EQ(range_cycle(&bare, plain, -1, 5, 5, 0), RANGE_RADIUS_MAX);
    CHECK_EQ(range_cycle(&bare, plain, -1, 5, 5, 20), 20);   /* back to 20 for what follows */

    /* With bands, a count names one -- 2r is the second -- from off or
     * while active, and a count past the last names the last rather than
     * switching the overlay off. */
    CASE("with bands, a count names a band, and too big a count the last");
    range_clear(&ro);
    CHECK_EQ(range_cycle(&ro, m, -1, 10, 7, 2), 1);
    CHECK_EQ(ro.active, 1);
    CHECK_EQ(range_cycle(&ro, m, -1, 10, 7, 1), 0);
    CHECK_EQ(range_cycle(&ro, m, -1, 10, 7, 99), rs->nbands - 1);
    CHECK_EQ(ro.active, 1);
    CHECK_EQ(range_cycle(&ro, m, -1, 10, 7, 0), -1);   /* the bare press past it: off */
    CHECK_EQ(ro.active, 0);

    /* ------------------------------------------------------ templates */

    /* The shapes point at the cursor. Anchor at k11 (10,10), reach six
     * squares, default 5-10-5 metric. */
    CASE("a cone is as wide as it is far, and points at the cursor");
    RangeOverlay tp;
    range_clear(&tp);
    range_cycle(&tp, plain, -1, 10, 10, 6);
    CHECK_EQ(range_cycle_shape(&tp, 2), RANGE_CONE);
    range_set_aim(&tp, 10, 10);
    CHECK_EQ(range_aimed(&tp, plain), 0);              /* on the origin: nowhere to point */
    CHECK_EQ(range_contains(&tp, plain, 11, 10), 0);
    char tbuf[256];
    range_status(&tp, plain, tbuf, sizeof tbuf);
    CHECK(strstr(tbuf, "Cone (30 ft, 6 sq)") != NULL);
    CHECK(strstr(tbuf, "aim") != NULL);

    range_set_aim(&tp, 20, 10);                         /* east */
    CHECK_EQ(range_aimed(&tp, plain), 1);
    CHECK_EQ(range_contains(&tp, plain, 11, 10), 1);
    CHECK_EQ(range_contains(&tp, plain, 11, 11), 0);   /* one out, one off: too wide */
    CHECK_EQ(range_contains(&tp, plain, 13, 11), 1);
    CHECK_EQ(range_contains(&tp, plain, 13, 9), 1);
    CHECK_EQ(range_contains(&tp, plain, 15, 12), 1);
    CHECK_EQ(range_contains(&tp, plain, 16, 10), 1);   /* six out: the edge */
    CHECK_EQ(range_contains(&tp, plain, 17, 10), 0);   /* seven: past the reach */
    CHECK_EQ(range_contains(&tp, plain, 9, 10), 0);    /* behind */
    CHECK_EQ(range_contains(&tp, plain, 10, 10), 0);   /* the origin itself */
    range_set_aim(&tp, 10, 2);                          /* swing it north */
    CHECK_EQ(range_contains(&tp, plain, 10, 7), 1);
    CHECK_EQ(range_contains(&tp, plain, 13, 10), 0);

    CASE("a line is one square wide, straight or diagonal");
    range_cycle_shape(&tp, 3);
    range_set_aim(&tp, 20, 10);
    for (int x = 11; x <= 16; x++) CHECK_EQ(range_contains(&tp, plain, x, 10), 1);
    CHECK_EQ(range_contains(&tp, plain, 17, 10), 0);
    CHECK_EQ(range_contains(&tp, plain, 12, 11), 0);
    CHECK_EQ(range_contains(&tp, plain, 9, 10), 0);
    range_set_aim(&tp, 20, 20);
    CHECK_EQ(range_contains(&tp, plain, 11, 11), 1);
    CHECK_EQ(range_contains(&tp, plain, 12, 12), 1);
    CHECK_EQ(range_contains(&tp, plain, 12, 11), 0);

    CASE("a square has a side the length of the reach, against the origin");
    range_cycle_shape(&tp, 4);
    range_cycle(&tp, plain, -1, 10, 10, 3);
    range_set_aim(&tp, 20, 10);                         /* east: x 11..13, y 9..11 */
    CHECK_EQ(range_contains(&tp, plain, 11, 9), 1);
    CHECK_EQ(range_contains(&tp, plain, 13, 11), 1);   /* the far corner counts */
    CHECK_EQ(range_contains(&tp, plain, 14, 10), 0);
    CHECK_EQ(range_contains(&tp, plain, 11, 8), 0);
    CHECK_EQ(range_contains(&tp, plain, 10, 10), 0);
    range_set_aim(&tp, 11, 2);                          /* mostly north: x 9..11, y 7..9 */
    CHECK_EQ(range_contains(&tp, plain, 9, 7), 1);
    CHECK_EQ(range_contains(&tp, plain, 11, 9), 1);
    CHECK_EQ(range_contains(&tp, plain, 11, 10), 0);
    /* An even side cannot center on one square: it leans with the cursor. */
    range_cycle(&tp, plain, -1, 10, 10, 2);
    range_set_aim(&tp, 20, 12);                         /* east, leaning south */
    CHECK_EQ(range_contains(&tp, plain, 11, 10), 1);
    CHECK_EQ(range_contains(&tp, plain, 12, 11), 1);
    CHECK_EQ(range_contains(&tp, plain, 11, 9), 0);
    range_set_aim(&tp, 20, 8);                          /* east, leaning north */
    CHECK_EQ(range_contains(&tp, plain, 11, 9), 1);
    CHECK_EQ(range_contains(&tp, plain, 11, 11), 0);

    CASE("a creature is caught when any of its squares is, and the status names the shape");
    Token ogre2 = { 12, 10, 2, TOKEN_ENEMY, "Ogre" };  /* 12..13 x 10..11 */
    Token bat = { 12, 14, 1, TOKEN_ENEMY, "Bat" };
    tokens_add(&plain->tokens, ogre2);
    tokens_add(&plain->tokens, bat);
    range_cycle_shape(&tp, 3);
    range_cycle(&tp, plain, -1, 10, 10, 6);
    range_set_aim(&tp, 20, 10);
    range_status(&tp, plain, tbuf, sizeof tbuf);
    CHECK(strstr(tbuf, "Line (30 ft, 6 sq)") != NULL);
    CHECK(strstr(tbuf, "1 in range: Ogre") != NULL);
    plain->tokens.n = 0;

    CASE("with bands the shape rides on the band, and a 2x2 origin centers a square");
    RangeOverlay bs;
    range_clear(&bs);
    Token giant = { 4, 4, 2, TOKEN_PLAYER, "Giant" };
    int gi = tokens_add(&m->tokens, giant);
    range_cycle(&bs, m, gi, 4, 4, 2);                   /* Very Close: 15 ft, 3 sq */
    range_cycle_shape(&bs, 2);
    range_set_aim(&bs, 12, 4);
    range_status(&bs, m, tbuf, sizeof tbuf);
    CHECK(strstr(tbuf, "Very Close cone (15 ft, 3 sq) from Giant") != NULL);
    range_cycle_shape(&bs, 4);
    range_cycle(&bs, m, gi, 4, 4, 1);                   /* Melee: a side of one... */
    range_cycle(&bs, m, gi, 4, 4, 0);                   /* ...then Very Close: three */
    range_set_aim(&bs, 12, 5);
    CHECK_EQ(range_contains(&bs, m, 6, 3), 0);
    CHECK_EQ(range_contains(&bs, m, 6, 4), 1);          /* rows 4..6: the lean is south */
    CHECK_EQ(range_contains(&bs, m, 8, 6), 1);
    CHECK_EQ(range_contains(&bs, m, 9, 5), 0);
    m->tokens.n = gi;

    CASE("switching off keeps the shape; clearing resets it");
    range_off(&tp);
    CHECK_EQ(tp.active, 0);
    CHECK_EQ(tp.shape, RANGE_LINE);
    range_clear(&tp);
    CHECK_EQ(tp.shape, RANGE_CIRCLE);
    CHECK_EQ(range_cycle_shape(&tp, 99), RANGE_SQUARE);  /* past the last names the last */
    CHECK_EQ(range_cycle_shape(&tp, 0), RANGE_CIRCLE);

    CASE("the radius overlay never cycles itself off -- esc is how it goes");
    for (int i = 0; i < 40; i++) range_cycle(&bare, plain, -1, 5, 5, 0);
    CHECK_EQ(bare.active, 1);
    CHECK_EQ(bare.radius, 60);

    CASE("its status leads with the reach, having no band name to lead with");
    char rbuf[192];
    range_status(&bare, plain, rbuf, sizeof rbuf);
    CHECK(strstr(rbuf, "Range (300 ft, 60 sq)") != NULL);

    range_clear(&bare);
    map_free(plain);

    /* Melee is one square, so exactly the eight neighbors and the anchor. */
    CASE("Melee covers the anchor and its neighbors, and nothing else");
    range_clear(&ro);
    range_cycle(&ro, m, -1, 10, 7, 0);          /* band 0: Melee */
    int inside = 0;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (range_contains(&ro, m, x, y)) inside++;
    CHECK_EQ(inside, 9);
    CHECK_EQ(range_contains(&ro, m, 10, 7), 1);
    CHECK_EQ(range_contains(&ro, m, 11, 8), 1);
    CHECK_EQ(range_contains(&ro, m, 12, 7), 0);

    CASE("the highlight matches the band's reach exactly");
    ro.band = 2;                              /* Close: 30 ft, 6 squares */
    CHECK_EQ(range_contains(&ro, m, 16, 7), 1);   /* 6 squares east */
    CHECK_EQ(range_contains(&ro, m, 17, 7), 0);   /* 7 squares east */
    CHECK_EQ(range_contains(&ro, m, 10, 13), 1);  /* 6 squares south */

    /* Under 5-10-5 a diagonal costs more, so the region is an octagon rather
     * than the square Chebyshev would give. */
    CASE("the shape follows the metric");
    CHECK_EQ(m->metric, DIST_ALT_DIAG);
    CHECK_EQ(range_contains(&ro, m, 14, 11), 1);  /* 4 diagonal: 6 tiles */
    CHECK_EQ(range_contains(&ro, m, 15, 12), 0);  /* 5 diagonal: 7 tiles */
    int alt_count = 0;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (range_contains(&ro, m, x, y)) alt_count++;

    m->metric = DIST_CHEBYSHEV;
    CHECK_EQ(range_contains(&ro, m, 15, 12), 1);  /* free diagonals reach further */
    int cheb_count = 0;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (range_contains(&ro, m, x, y)) cheb_count++;
    CHECK(cheb_count > alt_count);
    m->metric = DIST_ALT_DIAG;

    CASE("the highlight is symmetric about its anchor");
    for (int d = 1; d <= 6; d++) {
        CHECK_EQ(range_contains(&ro, m, 10 + d, 7), range_contains(&ro, m, 10 - d, 7));
        CHECK_EQ(range_contains(&ro, m, 10, 7 + d), range_contains(&ro, m, 10, 7 - d));
    }

    CASE("scale changes what a band reaches");
    m->scale_ft = 10.0;
    CHECK_EQ(range_contains(&ro, m, 13, 7), 1);   /* 3 squares = 30 ft */
    CHECK_EQ(range_contains(&ro, m, 14, 7), 0);   /* 4 squares = 40 ft */
    m->scale_ft = 5.0;

    /* A creature bigger than one square reaches from its nearest square, not
     * from a corner. */
    CASE("a large anchor measures from its nearest square");
    Token big = { 10, 7, 3, TOKEN_ENEMY, "Troll" };
    int bi = tokens_add(&m->tokens, big);
    range_clear(&ro);
    range_cycle(&ro, m, bi, 0, 0, 0);            /* Melee, anchored to the troll */
    range_anchor(&ro, m, &ax, &ay, &as);
    CHECK_EQ(ax, 10);
    CHECK_EQ(as, 3);
    CHECK_EQ(range_contains(&ro, m, 13, 9), 1);   /* touching its east face */
    CHECK_EQ(range_contains(&ro, m, 14, 9), 0);
    CHECK_EQ(range_contains(&ro, m, 9, 7), 1);    /* and its west face */

    CASE("the highlight follows the creature it is anchored to");
    m->tokens.v[bi].x = 4;
    range_anchor(&ro, m, &ax, &ay, &as);
    CHECK_EQ(ax, 4);
    CHECK_EQ(range_contains(&ro, m, 3, 7), 1);
    CHECK_EQ(range_contains(&ro, m, 13, 9), 0);

    /* Indices shift when a token is removed, so an anchor holding one must
     * not silently start following whoever inherits it. */
    CASE("removing the anchor's creature leaves the highlight where it stood");
    range_token_removed(&ro, bi, 4, 7);
    CHECK_EQ(ro.token, -1);
    range_anchor(&ro, m, &ax, &ay, &as);
    CHECK_EQ(ax, 4);
    CHECK_EQ(ay, 7);

    CASE("removing an earlier token keeps the anchor on the same creature");
    range_clear(&ro);
    range_cycle(&ro, m, 3, 0, 0, 0);
    range_token_removed(&ro, 1, 0, 0);
    CHECK_EQ(ro.token, 2);
    range_token_removed(&ro, 5, 0, 0);        /* a later one changes nothing */
    CHECK_EQ(ro.token, 2);

    map_free(m);
}

void test_range_sight(void)
{
    Map *m = map_new(21, 11, "rsight");
    map_fill_tiles(m, 0, 0, 20, 10, TILE_FLOOR);
    str_lcpy(m->ruleset, "daggerheart", sizeof m->ruleset);

    RangeOverlay ro;
    range_clear(&ro);
    range_cycle(&ro, m, -1, 5, 5, 0);
    ro.band = 2;                               /* Close, 6 squares */

    CASE("open ground is all in range and all visible");
    CHECK_EQ(range_contains(&ro, m, 11, 5), 1);
    CHECK_EQ(sight_blocked(m, 5, 5, 11, 5), 0);

    /* A wall does not shrink the band -- distance is distance -- it only
     * changes which of those squares can actually be targeted. */
    CASE("a wall leaves squares in range but out of sight");
    for (int y = 0; y < 11; y++) map_set_vedge(m, 8, y, EDGE_WALL);
    CHECK_EQ(range_contains(&ro, m, 11, 5), 1);
    CHECK_EQ(sight_blocked(m, 5, 5, 11, 5), 1);

    CASE("a gap in the wall restores sight along that line");
    map_set_vedge(m, 8, 5, EDGE_NONE);
    CHECK_EQ(sight_blocked(m, 5, 5, 11, 5), 0);
    CHECK_EQ(sight_blocked(m, 5, 5, 11, 1), 1);

    /* The overlay and the ruler must never disagree about the same line. */
    CASE("the overlay and the ruler agree about sight");
    for (int y = 0; y < 11; y++) {
        for (int x = 0; x < 21; x++) {
            Ruler r;
            ruler_start(&r, 5, 5);
            ruler_set_cursor(&r, x, y);
            CHECK_EQ(ruler_sight_blocked(&r, m), sight_blocked(m, 5, 5, x, y));
        }
    }

    map_free(m);
}

/* The MOVING line under a ruleset says what moving that far takes, by side,
 * from the band of the straight distance -- on the players' line too. */
static char *moving_line(App *a, int gm)
{
    static char buf[256];
    play_status(&a->play, a->map, &a->ed, gm, buf, sizeof buf);
    return buf;
}

void test_move_rules(void)
{
    Sandbox sb = sandbox_enter("moverules");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[600];
    snprintf(path, sizeof path, "%s/mv.vtt", sb.dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("VTT 2\nname mv\nsize 30 10\nzoom 1\nruleset daggerheart\ntiles\n", f);
        for (int y = 0; y < 10; y++) fputs("..............................\n", f);
        fputs("token player 1 1 1 \"Aria\"\ntoken enemy 1 5 1 \"Ogre\"\n", f);
        fclose(f);
    }
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    press(&a, ":play\r");

    CASE("a player carried within Close: part of an action; past it, an Agility Roll");
    press(&a, ":B2\r\rll");
    CHECK(strstr(moving_line(&a, 1), "Very Close: part of an action") != NULL);
    press(&a, "llllll");
    CHECK(strstr(moving_line(&a, 1), "Far: Agility Roll to move") != NULL);
    CHECK(strstr(moving_line(&a, 0), "Far: Agility Roll to move") != NULL);   /* the players' line */
    CHECK_EQ(a.status[0], '\0');                             /* the pick-up hint has gone */
    press(&a, "\x1b");

    CASE("an adversary: free with an action within Close, a separate action past it");
    press(&a, ":B6\r\rlll");
    CHECK(strstr(moving_line(&a, 1), "Very Close: free with an action") != NULL);
    press(&a, "lllll");
    CHECK(strstr(moving_line(&a, 1), "Far: a separate action") != NULL);

    CASE("back where it started there is nothing to say");
    press(&a, "hhhhhhhh");
    CHECK(strstr(moving_line(&a, 1), "action") == NULL);
    press(&a, "\x1b");

    CASE("without a ruleset the line is as it was");
    press(&a, ":ruleset none\r:B2\r\rllllllll");
    CHECK(strstr(moving_line(&a, 1), "Agility") == NULL);
    CHECK(strstr(moving_line(&a, 1), "Far") == NULL);
    press(&a, "\x1b");

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* g e: a group effect's burst round the cursor, following it; Very Close
 * under Daggerheart unless a count names another band; who is caught; how
 * far it lands from the selected creature. */
static const char *burst_line(App *a)
{
    static char buf[256];
    range_status(&a->play.range, a->map, buf, sizeof buf);
    return buf;
}

void test_group_effect(void)
{
    Sandbox sb = sandbox_enter("burst");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[600];
    snprintf(path, sizeof path, "%s/ge.vtt", sb.dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("VTT 2\nname ge\nsize 30 12\nzoom 1\nruleset daggerheart\ntiles\n", f);
        for (int y = 0; y < 12; y++) fputs("..............................\n", f);
        fputs("token player 1 1 1 \"Aria\"\ntoken enemy 8 5 1 \"Ogre\"\n"
              "token enemy 9 6 1 \"Goblin\"\ntoken enemy 20 5 1 \"Distant\"\n", f);
        fclose(f);
    }
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 110, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    press(&a, ":play\r");

    CASE("g e is a Very Close burst at the cursor, and says who it catches");
    a.ed.cx = 8; a.ed.cy = 5;
    press(&a, "ge");
    CHECK(a.play.range.active && a.play.range.burst);
    CHECK(strstr(burst_line(&a), "Very Close burst at I6 (15 ft, 3 sq) - 2 caught: Ogre, Goblin") != NULL);
    CHECK(range_contains(&a.play.range, a.map, 11, 5));
    CHECK(!range_contains(&a.play.range, a.map, 12, 5));

    CASE("it follows the cursor");
    press(&a, "lll");
    CHECK_EQ(a.play.range.ax, 11);
    CHECK(strstr(burst_line(&a), "burst at L6") != NULL);
    CHECK(range_contains(&a.play.range, a.map, 14, 5) && !range_contains(&a.play.range, a.map, 7, 5));

    CASE("g e again takes it off; a count names another band");
    press(&a, "ge");
    CHECK(!a.play.range.active);
    CHECK(strstr(a.status, "group effect off") != NULL);
    a.ed.cx = 8; a.ed.cy = 5;
    press(&a, "3ge");
    CHECK(strstr(burst_line(&a), "Close burst at I6 (30 ft, 6 sq)") != NULL);
    press(&a, "\x1b");
    CHECK(!a.play.range.active);

    CASE("with a creature selected: the band from it to where the burst lands; it can catch its own");
    press(&a, ":B2\r\r\r");                     /* pick Aria up and put her down: selected */
    CHECK_EQ(a.play.sel, 0);
    a.ed.cx = 8; a.ed.cy = 5;
    press(&a, "ge");
    CHECK(strstr(burst_line(&a), "burst at I6, Far from Aria (15 ft, 3 sq)") != NULL);
    a.ed.cx = 2; a.ed.cy = 2;
    press(&a, "ge3ge");
    CHECK(strstr(burst_line(&a), "caught: Aria") != NULL);
    CHECK_EQ(a.play.range.from, 0);

    CASE("the creature it is from, removed, is forgotten; one before it shifts the index");
    {
        RangeOverlay ro = a.play.range;
        ro.from = 2;
        range_token_removed(&ro, 1, 0, 0);
        CHECK_EQ(ro.from, 1);
        range_token_removed(&ro, 1, 0, 0);
        CHECK_EQ(ro.from, -1);
    }

    CASE("an undo forgets whose the burst was, rather than naming the wrong creature");
    press(&a, "\x1b");
    press(&a, ":B2\r\rl\r");                     /* Aria one step east: something to undo */
    CHECK_EQ(a.map->tokens.v[0].x, 2);
    press(&a, "ge");
    CHECK_EQ(a.play.range.from, 0);
    press(&a, "u");
    CHECK_EQ(a.map->tokens.v[0].x, 1);
    CHECK_EQ(a.play.range.from, -1);
    CHECK(a.play.range.active && a.play.range.burst);

    CASE("esc on a burst says the group effect is off");
    press(&a, "\x1b");
    CHECK(strstr(a.status, "group effect off") != NULL);

    CASE("the burst replaces r's highlight, and r's replaces the burst");
    press(&a, "\x1b");
    press(&a, "r");
    CHECK(a.play.range.active && !a.play.range.burst);
    press(&a, "ge");
    CHECK(a.play.range.burst);
    press(&a, "\x1b");

    CASE("without a ruleset it is one square round, and a count is squares");
    press(&a, ":ruleset none\r:B2\r\r\r");            /* Aria selected again */
    a.ed.cx = 8; a.ed.cy = 5;
    press(&a, "ge");
    CHECK(strstr(burst_line(&a), "Range burst at I6, 45 ft from Aria (5 ft, 1 sq)") != NULL);
    press(&a, "\x1b");
    press(&a, "4ge");
    CHECK(strstr(burst_line(&a), "(20 ft, 4 sq)") != NULL);
    press(&a, "\x1b");

    CASE("a burst follows the cursor when [ and ] move it to another floor");
    CHECK_EQ(app_open_map(&a, "tests/fixtures/floors.vtt"), 0);
    press(&a, ":play\r]:D3\rge");
    CHECK(a.play.range.burst);
    press(&a, "]");
    CHECK(a.ed.cx != 3);                                     /* ] moved the cursor */
    CHECK_EQ(a.play.range.ax, a.ed.cx);
    CHECK_EQ(a.play.range.ay, a.ed.cy);
    press(&a, "\x1b");

    CASE("build mode says where group effects are");
    press(&a, "\x1b[11~ge");
    CHECK(strstr(a.status, "play mode") != NULL);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

