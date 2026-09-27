/* Tests: golden frames, terminal io, the ruler, the range overlay, doors and terrain, the map browser, status markers, the movement trail. */

#include "harness.h"

/* ---------------------------------------------------------------- golden */

/* Drives a real App through a scripted session, renders one frame, and
 * compares the plain-text dump with a stored file. Segments are fed one at a
 * time with the pending-ESC timeout resolved between them, exactly as the
 * event loop does, so `esc` followed by a command is expressible.
 *
 * Run with VTT_UPDATE_GOLDEN=1 to rewrite the expectations. */
/* Compares `data` with tests/golden/NAME.txt, or writes it there under
 * VTT_UPDATE_GOLDEN=1: the frame goldens and the map tools' reports alike. */
void golden_bytes(const char *name, const char *data, size_t len)
{
    char path[256];
    snprintf(path, sizeof path, "tests/golden/%s.txt", name);

    if (getenv("VTT_UPDATE_GOLDEN")) {
        FILE *f = fopen(path, "w");
        if (f) { fwrite(data, 1, len, f); fclose(f); }
        fprintf(stderr, "  wrote %s\n", path);
    } else {
        FILE *f = fopen(path, "rb");
        g_checks++;
        if (!f) {
            g_fails++;
            fprintf(stderr, "  FAIL [%s] missing golden %s "
                            "(VTT_UPDATE_GOLDEN=1 make test to create)\n", name, path);
        } else {
            char  *want = xmalloc(len + 4096);
            size_t n    = fread(want, 1, len + 4096, f);
            fclose(f);
            if (n != len || memcmp(want, data, n) != 0) {
                g_fails++;
                fprintf(stderr, "  FAIL [%s] differs from %s\n", name, path);
                /* Show the first differing line, which is usually enough to
                 * see what moved. */
                size_t i = 0, line = 1, ls = 0;
                while (i < n && i < len && want[i] == data[i]) {
                    if (want[i] == '\n') { line++; ls = i + 1; }
                    i++;
                }
                size_t le = ls;
                while (le < len && data[le] != '\n') le++;
                fprintf(stderr, "    line %zu\n      want: %.*s\n      got : %.*s\n",
                        line, (int)(le - ls), want + ls, (int)(le - ls), data + ls);
            }
            free(want);
        }
    }

}

void golden(const char *name, int w, int h, const char *map_path,
            const char *const *segments, int nsegments, int ascii)
{
    Renderer r;
    App      a;

    rnd_init(&r);
    rnd_resize(&r, w, h);
    app_init(&a, NULL, &r);
    a.ascii = ascii;
    draw_set_ascii(ascii);

    if (map_path && app_open_map(&a, map_path) != 0) {
        g_fails++;
        fprintf(stderr, "  FAIL [%s] could not open fixture %s\n", name, map_path);
        rnd_free(&r);
        return;
    }

    InputParser p;
    input_init(&p);
    for (int i = 0; i < nsegments; i++) {
        input_feed(&p, segments[i], strlen(segments[i]));
        Key k;
        while (input_next(&p, &k)) app_key(&a, k);
        while (input_pending(&p) && input_timeout(&p, &k)) app_key(&a, k);
    }

    rnd_begin(&r);
    app_draw(&a);

    ByteBuf out;
    bb_init(&out, 16384);
    rnd_dump(&r, &out);
    golden_bytes(name, out.data, out.len);

    bb_free(&out);
    app_free(&a);
    rnd_free(&r);
    draw_set_ascii(0);
}

#define FIXTURE "tests/fixtures/two-rooms.vtt"

void test_golden(void)
{
    CASE("menu");
    {
        static const char *const seg[] = { "" };
        golden("menu", 72, 20, NULL, seg, 1, 0);
    }

    CASE("build mode on a loaded map");
    {
        static const char *const seg[] = { "" };
        golden("build", 72, 20, FIXTURE, seg, 1, 0);
    }

    /* The whole point of the wall tool: walking an outline with the pen down
     * should leave a sealed room. */
    CASE("a room traced with the wall tool");
    {
        static const char *const seg[] = { "gg0jjjjjjllllllllllllw lljjhhkk" };
        golden("traced-room", 72, 20, FIXTURE, seg, 1, 0);
    }

    CASE("visual selection cleared to void");
    {
        static const char *const seg[] = { "gg0vlljj", "x" };
        golden("cleared", 72, 20, FIXTURE, seg, 2, 0);
    }

    CASE("play mode with a token picked up and moved");
    {
        /* F2 into play, tab to the first token, grab it, walk east. */
        static const char *const seg[] = { "\x1b[12~\t\r", "lll" };
        golden("play-moving", 72, 20, FIXTURE, seg, 2, 0);
    }

    CASE("play mode in ascii");
    {
        static const char *const seg[] = { "\x1b[12~" };
        golden("play-ascii", 72, 20, FIXTURE, seg, 1, 1);
    }

    CASE("the new-map prompt");
    {
        static const char *const seg[] = { "j\r", "Ambush" };
        golden("prompt", 72, 20, NULL, seg, 2, 0);
    }

    CASE("the ruler measuring across a room");
    {
        /* Anchor inside the west room, then measure out through its wall so
         * the readout has to report sight as broken. */
        static const char *const seg[] = { "gg0jjll", "m", "llllll" };
        golden("ruler", 72, 20, FIXTURE, seg, 3, 0);
    }

    CASE("the ruler with several legs");
    {
        static const char *const seg[] = { "gg0jjll", "m", "lll", "\r", "jjj", "\r", "ll" };
        golden("ruler-legs", 72, 20, FIXTURE, seg, 7, 0);
    }

    /* The fill itself is a background color, which a text dump cannot show;
     * this pins the readout, which is the part that names names. */
    CASE("the range overlay's readout");
    {
        static const char *const seg[] = { ":ruleset daggerheart\r", "\x1b[12~", "\t", "rrrr" };
        golden("range", 84, 20, FIXTURE, seg, 4, 0);
    }

    /* Doors, windows and terrain all carry their own glyph, so a
     * text dump pins them. */
    CASE("every boundary kind and terrain, in build mode");
    {
        static const char *const seg[] = { "" };
        golden("kinds-build", 72, 16, "tests/fixtures/kinds.vtt", seg, 1, 0);
    }

    /* The same map in play mode: the secret door must be a wall. */
    CASE("the same map in play mode, with the secret door hidden");
    {
        static const char *const seg[] = { "\x1b[12~" };
        golden("kinds-play", 72, 16, "tests/fixtures/kinds.vtt", seg, 1, 0);
    }

    CASE("a narrow terminal still lays out");
    {
        static const char *const seg[] = { "" };
        golden("narrow", 34, 12, FIXTURE, seg, 1, 0);
    }
}

/* ---------------------------------------------------------------- term io */

/* These cover the failure that produced visible artifacts: the terminal falls
 * behind, part of a frame never arrives, and the renderer goes on believing
 * the screen shows what it drew. */
void test_term_io(void)
{
    signal(SIGPIPE, SIG_IGN);

    /* A frame that does not fully arrive must not advance `front`. Otherwise
     * the cells that were dropped are diffed away on every later frame and
     * stay wrong on screen forever. */
    CASE("a failed write forces a full repaint instead of trusting front");
    {
        Renderer r;
        rnd_init(&r);
        rnd_resize(&r, 20, 5);

        Term t;
        memset(&t, 0, sizeof t);

        int devnull = open("/dev/null", O_WRONLY);
        CHECK(devnull >= 0);
        t.out_fd = devnull;

        Style st = style(COL_DEFAULT, COL_DEFAULT, 0);

        /* A clean frame first, so front is in sync and force_full is clear. */
        rnd_begin(&r);
        draw_text(&r, 0, 0, "hello", -1, st);
        rnd_flush(&r, &t);
        CHECK_EQ(r.force_full, 0);
        CHECK_EQ(t.dead, 0);

        /* Now break the destination and change one cell. */
        int fds[2];
        CHECK_EQ(pipe(fds), 0);
        close(fds[0]);                       /* reader gone: writes get EPIPE */
        t.out_fd = fds[1];

        rnd_begin(&r);
        draw_text(&r, 0, 0, "hellp", -1, st);
        rnd_flush(&r, &t);

        CHECK_EQ(t.dead, 1);                 /* a real failure, not backpressure */
        CHECK_EQ(r.bytes_written, 0);        /* reports what arrived, not what we hoped */
        CHECK_EQ(r.force_full, 1);           /* the next frame must repaint everything */

        /* Redrawing the same content must now emit the whole screen, which is
         * only true if front was left alone. */
        close(fds[1]);
        t.out_fd = devnull;
        t.dead   = 0;
        rnd_begin(&r);
        draw_text(&r, 0, 0, "hellp", -1, st);
        rnd_flush(&r, &t);
        CHECK_EQ(r.cells_changed, 100);      /* 20 x 5, every cell */
        CHECK_EQ(r.force_full, 0);

        close(devnull);
        rnd_free(&r);
    }

    /* The original bug: stdout was non-blocking, so a terminal that fell
     * behind made write() return EAGAIN and the rest of the frame was thrown
     * away. Backpressure must be waited out instead. */
    CASE("term_write delivers everything even when the reader is slow");
    {
        int fds[2];
        CHECK_EQ(pipe(fds), 0);

        int fl = fcntl(fds[1], F_GETFL, 0);
        fcntl(fds[1], F_SETFL, fl | O_NONBLOCK);   /* force the EAGAIN path */

        const size_t N = 512 * 1024;              /* far beyond any pipe buffer */
        char *buf = xmalloc(N);
        memset(buf, 'x', N);

        pid_t pid = fork();
        CHECK(pid >= 0);
        if (pid == 0) {
            /* Child: drain slowly, so the writer really does hit EAGAIN. */
            close(fds[1]);
            char   sink[8192];
            size_t total = 0;
            for (;;) {
                ssize_t n = read(fds[0], sink, sizeof sink);
                if (n <= 0) break;
                total += (size_t)n;
                if ((total / sizeof sink) % 4 == 0) {
                    struct timespec ts = { 0, 1000000 };   /* 1ms */
                    nanosleep(&ts, NULL);
                }
            }
            close(fds[0]);
            _exit(total == N ? 0 : 1);
        }

        close(fds[0]);
        Term t;
        memset(&t, 0, sizeof t);
        t.out_fd = fds[1];

        size_t wrote = term_write(&t, buf, N);
        CHECK_EQ(wrote, N);                       /* nothing dropped */
        CHECK_EQ(t.dead, 0);                      /* slow is not dead */

        close(fds[1]);
        int status = 0;
        waitpid(pid, &status, 0);
        CHECK(WIFEXITED(status));
        CHECK_EQ(WEXITSTATUS(status), 0);         /* the child saw every byte */

        free(buf);
    }

    /* The drain loop reads only what the parser can hold, because input_feed
     * discards the rest; without that a long burst loses keystrokes. */
    CASE("input_room bounds what input_feed can accept");
    {
        InputParser p;
        input_init(&p);
        size_t cap = input_room(&p);
        CHECK(cap > 0);

        char *big = xmalloc(cap + 64);
        memset(big, 'j', cap + 64);

        input_feed(&p, big, cap);
        CHECK_EQ(input_room(&p), 0);

        int n = 0;
        Key k;
        while (input_next(&p, &k)) n++;
        CHECK_EQ((size_t)n, cap);                 /* every byte became a key */
        CHECK_EQ(input_room(&p), cap);

        /* Offering more than the room silently drops the excess, which is
         * exactly why the caller must ask first. */
        input_feed(&p, big, cap + 64);
        CHECK_EQ(input_room(&p), 0);

        free(big);
    }
}

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

void test_map_format_v2(void)
{
    char path[] = "/tmp/vtt-v2-XXXXXX";
    int  fd = mkstemp(path);
    if (fd >= 0) close(fd);

    Map *m = map_new(10, 6, "kinds");
    for (int k = TILE_FLOOR, x = 0; x < 10; x++, k++) {
        if (k >= TILE_COUNT) k = TILE_FLOOR;
        for (int y = 0; y < 6; y++) map_set_tile(m, x, y, (uint8_t)k);
    }
    for (int k = EDGE_WALL, y = 0; y < 6; y++, k++) {
        if (k >= EDGE_COUNT) k = EDGE_WALL;
        map_set_vedge(m, 3, y, (uint8_t)k);
        map_set_hedge(m, y, 2, (uint8_t)k);
    }

    char err[MAPIO_ERR_MAX] = { 0 };
    CASE("every terrain and boundary kind survives a save and load");
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);

    Map *l = mapio_load(path, err, sizeof err);
    CHECK(l != NULL);
    if (l) {
        int tiles_ok = 1, v_ok = 1, h_ok = 1;
        for (int y = 0; y < 6; y++)
            for (int x = 0; x < 10; x++)
                if (map_tile(l, x, y) != map_tile(m, x, y)) tiles_ok = 0;
        for (int y = 0; y < 6; y++)
            for (int x = 0; x <= 10; x++)
                if (map_vedge(l, x, y) != map_vedge(m, x, y)) v_ok = 0;
        for (int y = 0; y <= 6; y++)
            for (int x = 0; x < 10; x++)
                if (map_hedge(l, x, y) != map_hedge(m, x, y)) h_ok = 0;
        CHECK(tiles_ok);
        CHECK(v_ok);
        CHECK(h_ok);
        map_free(l);
    }

    /* A v1 map predates doors and terrain, and must still open. */
    CASE("a version 1 map still loads, as walls and plain floor");
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("VTT 1\nname Old\nsize 4 3\nzoom 1\n"
              "tiles\n....\n....\n....\n"
              "vedges\n|   |\n|   |\n|   |\n"
              "hedges\n----\n    \n    \n----\n", f);
        fclose(f);
    }
    Map *old = mapio_load(path, err, sizeof err);
    CHECK(old != NULL);
    if (old) {
        CHECK_EQ(map_tile(old, 0, 0), TILE_FLOOR);
        CHECK_EQ(map_vedge(old, 0, 0), EDGE_WALL);
        CHECK_EQ(map_vedge(old, 4, 0), EDGE_WALL);
        CHECK_EQ(map_hedge(old, 0, 0), EDGE_WALL);      /* written as '-' */
        CHECK_EQ(map_hedge(old, 0, 1), EDGE_NONE);
        map_free(old);
    }

    CASE("an unreadable character reads as empty rather than failing the load");
    f = fopen(path, "w");
    if (f) {
        fputs("VTT 2\nname Odd\nsize 3 2\ntiles\n.@.\n...\n"
              "vedges\n|@ |\n    \nhedges\n-@-\n   \n   \n", f);
        fclose(f);
    }
    Map *odd = mapio_load(path, err, sizeof err);
    CHECK(odd != NULL);
    if (odd) {
        CHECK_EQ(map_tile(odd, 1, 0), TILE_VOID);
        CHECK_EQ(map_vedge(odd, 0, 0), EDGE_WALL);
        CHECK_EQ(map_vedge(odd, 1, 0), EDGE_NONE);
        map_free(odd);
    }

    map_free(m);
    unlink(path);
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

/* -------------------------------------------------------- deleting maps */

void write_map_file(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs("VTT 2\nname x\nsize 2 2\nzoom 1\ntiles\n..\n..\n"
          "vedges\n   \n   \nhedges\n  \n  \n  \n", f);
    fclose(f);
}

int file_exists(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return access(path, F_OK) == 0;
}

/* Drives the app through the real input parser rather than a hand-rolled
 * translation, so a test types what a terminal would send: Ctrl-U arrives as
 * 0x15 and becomes MOD_CTRL 'u', and a trailing ESC resolves on the timeout
 * exactly as the event loop resolves it. */
/* Between keys nothing may be left open in the undo log but a wall stroke
 * (undo_balanced): every test that types keys checks it, a failure only --
 * as a CHECK it would add one to the count for every key in the suite. */
static void press_balanced(const App *a, Key k)
{
    static int told;
    if (undo_balanced(&a->undo)) return;
    g_fails++;
    if (told++ < 3)
        fprintf(stderr, "  FAIL [%s] the undo log is left open (nest %d) after key %d/%u\n",
                g_case, a->undo.nest, (int)k.kind, (unsigned)k.ch);
}

void press(App *a, const char *keys)
{
    InputParser p;
    input_init(&p);
    input_feed(&p, keys, strlen(keys));

    Key k;
    while (input_next(&p, &k)) { app_key(a, k); press_balanced(a, k); }
    while (input_pending(&p) && input_timeout(&p, &k)) { app_key(a, k); press_balanced(a, k); }
}

/* The browser reads the working directory AND the user's map directory, so a
 * test that deletes or renames has to pin both. Without this it would list --
 * and then act on -- somebody's real map. */

Sandbox sandbox_enter(const char *tag)
{
    Sandbox s;
    memset(&s, 0, sizeof s);

    snprintf(s.dir, sizeof s.dir, "/tmp/vtt-%s-XXXXXX", tag);
    if (!mkdtemp(s.dir)) return s;
    if (!getcwd(s.cwd, sizeof s.cwd)) return s;

    snprintf(s.datadir, sizeof s.datadir, "%s/xdg", s.dir);
    mkdir(s.datadir, 0755);

    const char *old = getenv("XDG_DATA_HOME");
    if (old) str_lcpy(s.saved_xdg, old, sizeof s.saved_xdg);
    setenv("XDG_DATA_HOME", s.datadir, 1);

    s.ok = 1;
    return s;
}

void sandbox_leave(Sandbox *s)
{
    if (!s->ok) return;
    if (chdir(s->cwd) != 0) { }
    if (s->saved_xdg[0]) setenv("XDG_DATA_HOME", s->saved_xdg, 1);
    else                 unsetenv("XDG_DATA_HOME");
    rmdir(s->datadir);
}

void test_delete_map(void)
{
    Sandbox sb = sandbox_enter("del");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    const char *dir = sb.dir;

    write_map_file(dir, "alpha.vtt");
    write_map_file(dir, "bravo.vtt");
    write_map_file(dir, "charlie.vtt");
    if (chdir(dir) != 0) { CHECK(0); return; }

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    press(&a, "\r");                       /* menu -> Open Map */
    CASE("the browser finds the maps");
    CHECK_EQ(a.screen, SCREEN_BROWSER);
    CHECK_EQ(a.nentries, 3);

    CASE("d asks before it deletes anything");
    press(&a, "jd");                       /* select bravo, then delete */
    CHECK_EQ(a.modal, MODAL_CONFIRM_DELETE);
    CHECK(strstr(a.modal_body, "bravo.vtt") != NULL);
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 1);   /* nothing gone yet */

    /* While the question is up, nothing else may act -- least of all the
     * keys that would move the selection out from under it. */
    CASE("the confirmation swallows every other key");
    int sel_before = a.browser.sel;
    press(&a, "jkgGr");
    CHECK_EQ(a.modal, MODAL_CONFIRM_DELETE);
    CHECK_EQ(a.browser.sel, sel_before);
    CHECK_EQ(a.nentries, 3);

    CASE("n keeps the file");
    press(&a, "n");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 1);
    CHECK_EQ(a.nentries, 3);

    CASE("esc keeps it too");
    press(&a, "d\x1b");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 1);

    CASE("y deletes it, and only it, and its recovery copy with it");
    write_map_file(dir, "bravo.vtt.autosave");
    press(&a, "dy");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 0);
    CHECK_EQ(file_exists(dir, "bravo.vtt.autosave"), 0);
    CHECK_EQ(file_exists(dir, "alpha.vtt"), 1);
    CHECK_EQ(file_exists(dir, "charlie.vtt"), 1);

    CASE("the list refreshes without being asked");
    CHECK_EQ(a.nentries, 2);

    /* Deleting several in a row should not send you back to the top. */
    CASE("the caret keeps its place");
    CHECK_EQ(a.browser.sel, 1);
    CHECK(strstr(a.entries[a.browser.sel].name, "charlie") != NULL);

    CASE("deleting the last entry clamps the caret rather than running off");
    press(&a, "dy");
    CHECK_EQ(a.nentries, 1);
    CHECK_EQ(a.browser.sel, 0);
    CHECK(strstr(a.entries[0].name, "alpha") != NULL);

    CASE("an empty list has nothing to delete and says so");
    press(&a, "dy");
    CHECK_EQ(a.nentries, 0);
    press(&a, "d");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "nothing") != NULL);

    /* A file that will not unlink must report, not pretend. */
    CASE("a delete that fails reports instead of lying");
    write_map_file(dir, "guard.vtt");
    char sub[1200];
    snprintf(sub, sizeof sub, "%.1000s/locked", dir);
    if (mkdir(sub, 0755) == 0) {
        write_map_file(sub, "inner.vtt");
        chmod(sub, 0500);                  /* readable, not writable */
    }
    press(&a, "r");
    CHECK(a.nentries >= 1);

    app_free(&a);
    rnd_free(&r);

    sandbox_leave(&sb);

    /* Tidy up whatever survived. */
    chmod(sub, 0700);
    char p2[1400];
    snprintf(p2, sizeof p2, "%.1200s/inner.vtt", sub); unlink(p2);
    rmdir(sub);
    snprintf(p2, sizeof p2, "%.1200s/alpha.vtt", dir); unlink(p2);
    snprintf(p2, sizeof p2, "%.1200s/guard.vtt", dir); unlink(p2);
    rmdir(dir);
}

/* Reads the map's title straight out of the file, to check the rename reached
 * inside and not only the directory entry. */
static void read_title(const char *dir, const char *name, char *out, size_t n)
{
    out[0] = '\0';
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "name ", 5)) {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
            str_lcpy(out, line + 5, n);
            break;
        }
    fclose(f);
}

void test_rename_map(void)
{
    Sandbox sb = sandbox_enter("ren");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    const char *dir = sb.dir;

    write_map_file(dir, "alpha.vtt");
    write_map_file(dir, "bravo.vtt");
    if (chdir(dir) != 0) { CHECK(0); sandbox_leave(&sb); return; }

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    press(&a, "\r");
    CHECK_EQ(a.nentries, 2);

    CASE("R offers the current name, without its extension");
    press(&a, "jR");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK_EQ(strcmp(a.prompt.buf, "bravo"), 0);

    CASE("esc leaves the file alone");
    press(&a, "\x1b");
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 1);

    /* The name in the browser and the title in the editor should not drift
     * apart, so a rename reaches inside the file too. */
    CASE("renaming moves the file and retitles the map, and its recovery copy follows");
    write_map_file(dir, "bravo.vtt.autosave");
    press(&a, "R\025goblin\r");
    CHECK_EQ(file_exists(dir, "bravo.vtt"), 0);
    CHECK_EQ(file_exists(dir, "goblin.vtt"), 1);
    CHECK_EQ(file_exists(dir, "bravo.vtt.autosave"), 0);
    CHECK_EQ(file_exists(dir, "goblin.vtt.autosave"), 1);
    unlink("goblin.vtt.autosave");
    char title[128];
    read_title(dir, "goblin.vtt", title, sizeof title);
    CHECK_EQ(strcmp(title, "goblin"), 0);

    CASE("the caret follows the file to wherever it now sorts");
    CHECK_EQ(a.nentries, 2);
    CHECK(strstr(a.entries[a.browser.sel].name, "goblin") != NULL);

    /* rename(2) would silently destroy the other map; it must refuse. */
    CASE("renaming onto an existing map refuses instead of clobbering it");
    press(&a, "R\025alpha\r");
    CHECK_EQ(a.modal, MODAL_MESSAGE);
    CHECK(strstr(a.modal_body, "already exists") != NULL);
    CHECK_EQ(file_exists(dir, "goblin.vtt"), 1);
    CHECK_EQ(file_exists(dir, "alpha.vtt"), 1);
    read_title(dir, "alpha.vtt", title, sizeof title);
    CHECK_EQ(strcmp(title, "x"), 0);        /* the other map is untouched */
    press(&a, " ");                         /* dismiss */

    CASE("a name with a slash is refused: this renames, it does not move");
    press(&a, "R\025../escaped\r");
    CHECK_EQ(file_exists(dir, "goblin.vtt"), 1);
    CHECK(strstr(a.status, "cannot contain") != NULL);

    CASE("an empty name is refused");
    press(&a, "R\025\r");
    CHECK_EQ(file_exists(dir, "goblin.vtt"), 1);
    CHECK_EQ(a.nentries, 2);

    CASE("a typed extension is not doubled up");
    press(&a, "R\025ogre.vtt\r");
    CHECK_EQ(file_exists(dir, "ogre.vtt"), 1);
    CHECK_EQ(file_exists(dir, "ogre.vtt.vtt"), 0);

    CASE("renaming to the same name is a no-op, not a self-destruct");
    press(&a, "R\r");
    CHECK_EQ(file_exists(dir, "ogre.vtt"), 1);

    /* A map too damaged to parse is exactly when you want to move it out of
     * the way, so the file rename must not depend on the load. */
    CASE("a map that will not load still renames, keeping its old title");
    char broken[1200];
    snprintf(broken, sizeof broken, "%.1000s/broken.vtt", dir);
    FILE *bf = fopen(broken, "w");
    if (bf) { fputs("VTT 2\nname keep\nsize 0 0\ngarbage\n", bf); fclose(bf); }
    press(&a, "r");
    int found = -1;
    for (int i = 0; i < a.nentries; i++)
        if (strstr(a.entries[i].name, "broken")) found = i;
    CHECK(found >= 0);
    if (found >= 0) {
        a.browser.sel = found;
        press(&a, "R\025salvaged\r");
        CHECK_EQ(file_exists(dir, "salvaged.vtt"), 1);
        CHECK_EQ(file_exists(dir, "broken.vtt"), 0);
        read_title(dir, "salvaged.vtt", title, sizeof title);
        CHECK_EQ(strcmp(title, "keep"), 0);          /* contents preserved */
        CHECK(strstr(a.status, "title unchanged") != NULL);
    }

    /* The browser used to swallow its own confirmations: the message was set
     * but never drawn, so a delete reported nothing at all. */
    CASE("the browser actually draws its status message");
    app_set_status(&a, "a distinctive message");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf frame;
    bb_init(&frame, 8192);
    rnd_dump(&r, &frame);
    bb_putc(&frame, '\0');
    CHECK(strstr(frame.data, "a distinctive message") != NULL);
    bb_free(&frame);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);

    char p2[1400];
    const char *leftovers[] = { "alpha.vtt", "ogre.vtt", "salvaged.vtt", "goblin.vtt" };
    for (size_t i = 0; i < sizeof leftovers / sizeof *leftovers; i++) {
        snprintf(p2, sizeof p2, "%.1200s/%.40s", dir, leftovers[i]);
        unlink(p2);
    }
    rmdir(dir);
}

/* ----------------------------------------------------- duplicating maps */

static int files_identical(const char *dir, const char *a, const char *b)
{
    char pa[1200], pb[1200];
    snprintf(pa, sizeof pa, "%.1000s/%.60s", dir, a);
    snprintf(pb, sizeof pb, "%.1000s/%.60s", dir, b);

    FILE *fa = fopen(pa, "rb"), *fb = fopen(pb, "rb");
    if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return 0; }

    int same = 1, ca, cb;
    do { ca = fgetc(fa); cb = fgetc(fb); if (ca != cb) same = 0; }
    while (same && ca != EOF && cb != EOF);

    fclose(fa);
    fclose(fb);
    return same;
}

void test_duplicate_map(void)
{
    Sandbox sb = sandbox_enter("dup");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    const char *dir = sb.dir;

    write_map_file(dir, "goblin.vtt");
    if (chdir(dir) != 0) { CHECK(0); sandbox_leave(&sb); return; }

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

    press(&a, "\r");
    CHECK_EQ(a.nentries, 1);

    CASE("c offers a name that is already free");
    press(&a, "c");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK_EQ(strcmp(a.prompt.buf, "goblin copy"), 0);

    CASE("esc leaves nothing behind");
    press(&a, "\x1b");
    CHECK_EQ(a.nentries, 1);
    CHECK_EQ(file_exists(dir, "goblin copy.vtt"), 0);

    CASE("accepting it copies the file and titles the copy");
    press(&a, "c\r");
    CHECK_EQ(file_exists(dir, "goblin.vtt"), 1);       /* original untouched */
    CHECK_EQ(file_exists(dir, "goblin copy.vtt"), 1);
    char title[128];
    read_title(dir, "goblin.vtt", title, sizeof title);
    CHECK_EQ(strcmp(title, "x"), 0);
    read_title(dir, "goblin copy.vtt", title, sizeof title);
    CHECK_EQ(strcmp(title, "goblin copy"), 0);

    CASE("the caret moves to the copy");
    CHECK_EQ(a.nentries, 2);
    CHECK(strstr(a.entries[a.browser.sel].name, "goblin copy") != NULL);

    /* Duplicating a duplicate should count up from the original rather than
     * stacking the word. */
    CASE("a copy of a copy is offered the next number");
    press(&a, "c");
    CHECK_EQ(strcmp(a.prompt.buf, "goblin copy 2"), 0);
    press(&a, "\r");
    CHECK_EQ(file_exists(dir, "goblin copy 2.vtt"), 1);
    CHECK_EQ(file_exists(dir, "goblin copy copy.vtt"), 0);

    press(&a, "c");
    CHECK_EQ(strcmp(a.prompt.buf, "goblin copy 3"), 0);
    press(&a, "\x1b");

    CASE("duplicating onto an existing map refuses, leaving it alone");
    press(&a, "c\025goblin\r");
    CHECK_EQ(a.modal, MODAL_MESSAGE);
    CHECK(strstr(a.modal_body, "already exists") != NULL);
    read_title(dir, "goblin.vtt", title, sizeof title);
    CHECK_EQ(strcmp(title, "x"), 0);                   /* not overwritten */
    press(&a, " ");

    CASE("a copy needs a name of its own");
    press(&a, "g");                                    /* first entry */
    press(&a, "c\025goblin\r");
    CHECK(a.modal == MODAL_MESSAGE || strstr(a.status, "name of its own") != NULL);
    if (a.modal == MODAL_MESSAGE) press(&a, " ");

    CASE("a name with a slash is refused");
    press(&a, "c\025../escaped\r");
    CHECK_EQ(file_exists(dir, "escaped.vtt"), 0);
    CHECK(strstr(a.status, "cannot contain") != NULL);

    CASE("an empty name is refused");
    int before = a.nentries;
    press(&a, "c\025\r");
    CHECK_EQ(a.nentries, before);

    /* The copy is the bytes, not a re-serialization, so a map the loader
     * would choke on still duplicates exactly. */
    CASE("a map that will not load copies byte for byte, title untouched");
    char broken[1200];
    snprintf(broken, sizeof broken, "%.1000s/broken.vtt", dir);
    FILE *bf = fopen(broken, "w");
    if (bf) { fputs("VTT 2\nname keep\nsize 0 0\ngarbage here\n", bf); fclose(bf); }
    press(&a, "r");

    int found = -1;
    for (int i = 0; i < a.nentries; i++)
        if (strstr(a.entries[i].name, "broken")) found = i;
    CHECK(found >= 0);
    if (found >= 0) {
        a.browser.sel = found;
        press(&a, "c\025salvage\r");
        CHECK_EQ(file_exists(dir, "salvage.vtt"), 1);
        CHECK_EQ(files_identical(dir, "broken.vtt", "salvage.vtt"), 1);
        read_title(dir, "salvage.vtt", title, sizeof title);
        CHECK_EQ(strcmp(title, "keep"), 0);
        CHECK(strstr(a.status, "title unchanged") != NULL);
    }

    CASE("an empty list has nothing to duplicate");
    while (a.nentries > 0) press(&a, "dy");
    press(&a, "c");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "nothing") != NULL);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
    rmdir(dir);
}

/* ------------------------------------------------------- status markers */

void test_status(void)
{
    Token t;
    memset(&t, 0, sizeof t);
    t.size = 1;
    str_lcpy(t.label, "Goblin", sizeof t.label);

    CASE("a token starts unmarked");
    CHECK_EQ(t.nstatus, 0);

    CASE("markers accumulate up to the cap, then refuse");
    for (int i = 0; i < TOKEN_STATUS_MAX; i++)
        CHECK_EQ(token_add_status(&t, (uint8_t)i, "Poisoned"), 1);
    CHECK_EQ(t.nstatus, TOKEN_STATUS_MAX);
    CHECK_EQ(token_add_status(&t, 0, "Marked"), 0);
    CHECK_EQ(t.nstatus, TOKEN_STATUS_MAX);

    CASE("clearing removes all of them");
    token_clear_status(&t);
    CHECK_EQ(t.nstatus, 0);
    CHECK_EQ(token_add_status(&t, 0, "Poisoned"), 1);

    /* The map shows an initial rather than a dot, so a glance says which
     * condition it is and not merely that there is one. */
    CASE("a marker draws as the first letter of its word");
    CHECK_EQ(status_glyph(&t.status[0]), 'P');
    token_clear_status(&t);
    token_add_status(&t, 0, "burning");
    CHECK_EQ(status_glyph(&t.status[0]), 'B');       /* upper-cased */
    token_clear_status(&t);
    token_add_status(&t, 0, "");
    CHECK_EQ(status_glyph(&t.status[0]), 0x25CFu);   /* a dot, with no word */

    CASE("a color out of range wraps rather than reading past the palette");
    token_clear_status(&t);
    token_add_status(&t, 200, "X");
    CHECK(t.status[0].color < STATUS_COLOR_COUNT);

    CASE("color names round-trip");
    for (int i = 0; i < STATUS_COLOR_COUNT; i++)
        CHECK_EQ(status_color_from_name(status_color_name((uint8_t)i)), i);
    CHECK_EQ(status_color_from_name("chartreuse"), -1);

    CASE("a long word is truncated, not overrun");
    token_clear_status(&t);
    token_add_status(&t, 0, "an extremely long condition name indeed");
    CHECK(strlen(t.status[0].label) < STATUS_LABEL_MAX);

    /* A condition ends on its own schedule, so the one that ended has to be
     * the one that goes -- and the rest have to keep the order they are drawn
     * and numbered in, or the next question would answer about the wrong one. */
    CASE("one marker can be taken off, leaving the rest in order");
    token_clear_status(&t);
    token_add_status(&t, 0, "Poisoned");
    token_add_status(&t, 1, "Marked");
    token_add_status(&t, 2, "Burning");
    token_remove_status(&t, 1);
    CHECK_EQ(t.nstatus, 2);
    CHECK_EQ(strcmp(t.status[0].label, "Poisoned"), 0);
    CHECK_EQ(strcmp(t.status[1].label, "Burning"), 0);
    CHECK_EQ(t.status[1].color, 2);

    CASE("the vacated slot is wiped, not left holding the old word");
    CHECK_EQ(t.status[2].label[0], '\0');

    CASE("removing the first and the last both work");
    token_remove_status(&t, 1);
    CHECK_EQ(t.nstatus, 1);
    CHECK_EQ(strcmp(t.status[0].label, "Poisoned"), 0);
    token_remove_status(&t, 0);
    CHECK_EQ(t.nstatus, 0);

    CASE("an index nobody holds is a no-op, not a corruption");
    token_add_status(&t, 0, "Poisoned");
    token_remove_status(&t, -1);
    token_remove_status(&t, 1);
    token_remove_status(&t, TOKEN_STATUS_MAX + 5);
    CHECK_EQ(t.nstatus, 1);
    CHECK_EQ(strcmp(t.status[0].label, "Poisoned"), 0);
}

void test_unique_label(void)
{
    TokenList l;
    memset(&l, 0, sizeof l);
    char out[TOKEN_LABEL_MAX];

    CASE("an unused label is left alone");
    tokens_unique_label(&l, "Goblin", out, sizeof out);
    CHECK_EQ(strcmp(out, "Goblin"), 0);

    Token g;
    memset(&g, 0, sizeof g);
    g.size = 1;
    str_lcpy(g.label, "Goblin", sizeof g.label);
    tokens_add(&l, g);

    CASE("a taken one gets the next number");
    tokens_unique_label(&l, "Goblin", out, sizeof out);
    CHECK_EQ(strcmp(out, "Goblin 2"), 0);

    str_lcpy(g.label, "Goblin 2", sizeof g.label);
    tokens_add(&l, g);
    tokens_unique_label(&l, "Goblin", out, sizeof out);
    CHECK_EQ(strcmp(out, "Goblin 3"), 0);

    /* Copying a copy should continue the run rather than stack numbers. */
    CASE("a numbered label continues the run");
    tokens_unique_label(&l, "Goblin 2", out, sizeof out);
    CHECK_EQ(strcmp(out, "Goblin 3"), 0);
    CHECK(strstr(out, "2 2") == NULL);

    CASE("an unlabeled token stays unlabeled");
    tokens_unique_label(&l, "", out, sizeof out);
    CHECK_EQ(out[0], '\0');

    tokens_free(&l);
}

void test_status_io(void)
{
    char path[] = "/tmp/vtt-status-XXXXXX";
    int  fd = mkstemp(path);
    if (fd >= 0) close(fd);

    Map *m = map_new(8, 8, "marked");
    map_fill_tiles(m, 0, 0, 7, 7, TILE_FLOOR);

    Token a = { 1, 1, 1, TOKEN_PLAYER, "Aria", { { 0, "" } }, 0 };
    Token b = { 4, 4, 2, TOKEN_ENEMY, "Ogre Chief", { { 0, "" } }, 0 };
    token_add_status(&a, 0, "Poisoned");
    token_add_status(&a, 3, "Blessed by Fate");
    token_add_status(&b, 6, "Marked");
    tokens_add(&m->tokens, a);
    tokens_add(&m->tokens, b);

    char err[MAPIO_ERR_MAX] = { 0 };
    CASE("markers travel with the map");
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);

    Map *l = mapio_load(path, err, sizeof err);
    CHECK(l != NULL);
    if (l) {
        CHECK_EQ(l->tokens.n, 2);
        CHECK_EQ(l->tokens.v[0].nstatus, 2);
        CHECK_EQ(l->tokens.v[1].nstatus, 1);
        CHECK_EQ(strcmp(l->tokens.v[0].status[0].label, "Poisoned"), 0);
        CHECK_EQ(l->tokens.v[0].status[0].color, 0);
        CHECK_EQ(strcmp(l->tokens.v[0].status[1].label, "Blessed by Fate"), 0);
        CHECK_EQ(l->tokens.v[0].status[1].color, 3);
        CHECK_EQ(strcmp(l->tokens.v[1].status[0].label, "Marked"), 0);
        CHECK_EQ(l->tokens.v[1].status[0].color, 6);
        map_free(l);
    }

    /* A marker line must attach to the token above it and nothing else. */
    CASE("a stray marker line with no token before it is ignored");
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("VTT 3\nname Stray\nsize 3 3\ntiles\n...\n...\n...\n"
              "tokenstatus red \"Orphan\"\n"
              "token enemy 1 1 1 \"Real\"\n"
              "tokenstatus blue \"Mine\"\n", f);
        fclose(f);
    }
    Map *stray = mapio_load(path, err, sizeof err);
    CHECK(stray != NULL);
    if (stray) {
        CHECK_EQ(stray->tokens.n, 1);
        CHECK_EQ(stray->tokens.v[0].nstatus, 1);
        CHECK_EQ(strcmp(stray->tokens.v[0].status[0].label, "Mine"), 0);
        map_free(stray);
    }

    CASE("an unknown color name drops the marker rather than the map");
    f = fopen(path, "w");
    if (f) {
        fputs("VTT 3\nname Odd\nsize 3 3\ntiles\n...\n...\n...\n"
              "token enemy 1 1 1 \"Real\"\ntokenstatus chartreuse \"Nope\"\n", f);
        fclose(f);
    }
    Map *odd = mapio_load(path, err, sizeof err);
    CHECK(odd != NULL);
    if (odd) {
        CHECK_EQ(odd->tokens.n, 1);
        CHECK_EQ(odd->tokens.v[0].nstatus, 0);
        map_free(odd);
    }

    map_free(m);
    unlink(path);
}

void test_token_edit_undo(void)
{
    Map *m = map_new(8, 8, "edit");
    map_fill_tiles(m, 0, 0, 7, 7, TILE_FLOOR);

    Undo u;
    undo_init(&u);

    Token g = { 2, 2, 1, TOKEN_ENEMY, "Goblin", { { 0, "" } }, 0 };
    undo_begin(&u);
    int idx = undo_add_token(&u, m, g);
    undo_end(&u);

    /* Marking, relabeling and resizing all edit a token in place, and all
     * three should be one u away. */
    CASE("adding a marker undoes");
    Token t = m->tokens.v[idx];
    token_add_status(&t, 0, "Poisoned");
    undo_begin(&u);
    undo_edit_token(&u, m, idx, t);
    undo_end(&u);
    CHECK_EQ(m->tokens.v[idx].nstatus, 1);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(m->tokens.v[idx].nstatus, 0);
    CHECK_EQ(undo_redo(&u, m), 1);
    CHECK_EQ(m->tokens.v[idx].nstatus, 1);

    CASE("clearing markers undoes, restoring every one");
    t = m->tokens.v[idx];
    token_add_status(&t, 2, "Marked");
    undo_begin(&u); undo_edit_token(&u, m, idx, t); undo_end(&u);
    CHECK_EQ(m->tokens.v[idx].nstatus, 2);

    t = m->tokens.v[idx];
    token_clear_status(&t);
    undo_begin(&u); undo_edit_token(&u, m, idx, t); undo_end(&u);
    CHECK_EQ(m->tokens.v[idx].nstatus, 0);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(m->tokens.v[idx].nstatus, 2);
    CHECK_EQ(strcmp(m->tokens.v[idx].status[1].label, "Marked"), 0);

    CASE("relabeling undoes");
    t = m->tokens.v[idx];
    str_lcpy(t.label, "Hobgoblin", sizeof t.label);
    undo_begin(&u); undo_edit_token(&u, m, idx, t); undo_end(&u);
    CHECK_EQ(strcmp(m->tokens.v[idx].label, "Hobgoblin"), 0);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(strcmp(m->tokens.v[idx].label, "Goblin"), 0);

    CASE("resizing undoes");
    t = m->tokens.v[idx];
    t.size = 3;
    undo_begin(&u); undo_edit_token(&u, m, idx, t); undo_end(&u);
    CHECK_EQ(m->tokens.v[idx].size, 3);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(m->tokens.v[idx].size, 1);

    /* A keystroke that turns out to change nothing should cost neither an
     * undo step nor the redo tail waiting behind it. */
    CASE("an edit that changes nothing costs no undo step, and keeps redo");
    CHECK_EQ(undo_can_redo(&u), 1);
    int before = u.nmarks;
    undo_begin(&u);
    undo_edit_token(&u, m, idx, m->tokens.v[idx]);
    undo_end(&u);
    CHECK_EQ(u.nmarks, before);
    CHECK_EQ(undo_can_redo(&u), 1);
    CHECK_EQ(undo_redo(&u, m), 1);
    CHECK_EQ(m->tokens.v[idx].size, 3);

    undo_free(&u);
    map_free(m);
}

/* Driving the whole app rather than the model: the point of the chooser is
 * the keystrokes, and a test that called clear_token_status directly would
 * not notice if `c` never reached it. */
void test_clear_status_keys(void)
{
    Sandbox sb = sandbox_enter("clr");
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
    CHECK_EQ(a.screen, SCREEN_PLAY);

    Token g = { 0, 0, 1, TOKEN_ENEMY, "Goblin", { { 0, "" } }, 0 };
    token_add_status(&g, 0, "Poisoned");
    token_add_status(&g, 3, "Marked");
    token_add_status(&g, 5, "Burning");
    int idx = tokens_add(&a.map->tokens, g);
    a.ed.cx = 0; a.ed.cy = 0;

    CASE("s d on a token wearing several markers asks which one");
    press(&a, "sd");
    CHECK_EQ(a.modal, MODAL_CLEAR_STATUS);
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 3);

    /* The map only ever shows initials, and two conditions can share one, so
     * the question has to spell the words out. */
    CASE("the chooser names every marker in full");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf frame;
    bb_init(&frame, 16384);
    rnd_dump(&r, &frame);
    bb_putc(&frame, '\0');
    CHECK(strstr(frame.data, "Poisoned") != NULL);
    CHECK(strstr(frame.data, "Marked") != NULL);
    CHECK(strstr(frame.data, "Burning") != NULL);
    CHECK(strstr(frame.data, "Goblin") != NULL);
    CHECK(strstr(frame.data, "1-3") != NULL);
    bb_free(&frame);

    CASE("esc leaves every marker where it was");
    press(&a, "\x1b");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 3);

    CASE("a number takes off that marker and only that one");
    press(&a, "sd2");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 2);
    CHECK_EQ(strcmp(a.map->tokens.v[idx].status[0].label, "Poisoned"), 0);
    CHECK_EQ(strcmp(a.map->tokens.v[idx].status[1].label, "Burning"), 0);
    CHECK(strstr(a.status, "Marked") != NULL);

    CASE("clearing one marker undoes");
    press(&a, "u");
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 3);
    CHECK_EQ(strcmp(a.map->tokens.v[idx].status[1].label, "Marked"), 0);

    CASE("a number past the last row is ignored, and the question stays up");
    press(&a, "sd4");
    CHECK_EQ(a.modal, MODAL_CLEAR_STATUS);
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 3);

    CASE("a clears them all at once");
    press(&a, "a");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 0);
    CHECK(strstr(a.status, "3 markers") != NULL);
    press(&a, "u");
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 3);

    /* A chooser with one row is a keystroke that asks nothing. */
    CASE("a single marker clears without a question");
    press(&a, "sda");
    press(&a, "sa");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    press(&a, "Stunned\r");
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 1);
    press(&a, "sd");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(a.map->tokens.v[idx].nstatus, 0);
    CHECK(strstr(a.status, "Stunned") != NULL);

    CASE("s d on a bare token says so rather than opening an empty question");
    press(&a, "sd");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "no markers") != NULL);

    CASE("s d away from any token says so");
    play_focus(&a.play, -1);
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "sd");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "no token here") != NULL);

    app_free(&a);
    rnd_free(&r);
    unlink(path);
    sandbox_leave(&sb);
}

/* --------------------------------------------------------- movement trail */

void test_trail(void)
{
    Map *m = map_new(12, 10, "trail");
    map_fill_tiles(m, 0, 0, 11, 9, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Play p;
    play_init(&p);

    Token g = { 2, 2, 1, TOKEN_ENEMY, "Goblin", { { 0, "" } }, 0 };
    int idx = undo_add_token(&u, m, g);
    play_focus(&p, idx);
    CASE("picking a token up marks the tile it stood on");
    play_grab(&p, m, 0);
    CHECK_EQ(p.grabbed, 1);
    CHECK_EQ(p.ntrail, 1);
    CHECK_EQ(p.trail[0].x, 2);
    CHECK_EQ(p.trail[0].y, 2);
    CHECK_EQ(p.origin_x, 2);
    CHECK_EQ(p.origin_y, 2);
    CHECK_EQ(p.steps, 0);

    /* Walk east then back west: the route from where it set out is one square,
     * however much the cursor wandered getting there. */
    CASE("the ribbon is the route from the origin, not the squares walked");
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
    undo_begin(&u); play_step(m, &u, &p, -1, 0); undo_end(&u);
    CHECK_EQ(m->tokens.v[idx].x, 3);
    CHECK_EQ(p.ntrail, 2);
    CHECK_EQ(p.trail[0].x, 2);
    CHECK_EQ(p.trail[1].x, 3);

    CASE("the step count is what the route costs, not the keys pressed");
    CHECK_EQ(p.steps, 1);

    /* Across open floor a great many routes are equally short. The one drawn
     * should hug the straight line rather than turning a single corner. */
    CASE("an open diagonal comes out as a staircase, not an L");
    play_focus(&p, idx);
    m->tokens.v[idx].x = 2;
    m->tokens.v[idx].y = 2;
    play_grab(&p, m, 0);
    for (int i = 0; i < 3; i++) {
        undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
        undo_begin(&u); play_step(m, &u, &p, 0, 1); undo_end(&u);
    }
    CHECK_EQ(p.ntrail, 7);
    CHECK_EQ(p.steps, 6);
    int corners = 0;
    for (int i = 1; i + 1 < p.ntrail; i++) {
        int ax = p.trail[i].x - p.trail[i - 1].x, ay = p.trail[i].y - p.trail[i - 1].y;
        int bx = p.trail[i + 1].x - p.trail[i].x, by = p.trail[i + 1].y - p.trail[i].y;
        if (ax != bx || ay != by) corners++;
    }
    CHECK(corners > 1);                     /* an L would turn exactly once */

    CASE("every tile on the route is a step from the one before it");
    for (int i = 1; i < p.ntrail; i++) {
        int d = abs(p.trail[i].x - p.trail[i - 1].x) +
                abs(p.trail[i].y - p.trail[i - 1].y);
        CHECK_EQ(d, 1);
    }

    /* A route has to be one the creature could actually walk, so a wall in
     * the way lengthens it rather than being cut through. */
    CASE("a wall in the way makes the route go round it");
    Map *w = map_new(9, 9, "wall");
    map_fill_tiles(w, 0, 0, 8, 8, TILE_FLOOR);
    for (int y = 0; y <= 3; y++) map_set_vedge(w, 4, y, EDGE_WALL);

    Undo wu;
    undo_init(&wu);
    Play wp;
    play_init(&wp);
    Token t2 = { 3, 0, 1, TOKEN_ENEMY, "W", { { 0, "" } }, 0 };
    int wi = undo_add_token(&wu, w, t2);
    play_focus(&wp, wi);
    play_grab(&wp, w, 0);

    /* Down the near side, round the end of the wall, back up the far side. */
    for (int i = 0; i < 4; i++) { undo_begin(&wu); play_step(w, &wu, &wp, 0, 1); undo_end(&wu); }
    undo_begin(&wu); play_step(w, &wu, &wp, 1, 0); undo_end(&wu);
    CHECK_EQ(w->tokens.v[wi].x, 4);
    CHECK_EQ(w->tokens.v[wi].y, 4);

    CHECK_EQ(wp.ntrail, 6);                 /* five steps: straight is only two */
    CHECK_EQ(wp.steps, 5);
    for (int i = 0; i < wp.ntrail; i++)
        CHECK(!(wp.trail[i].x == 4 && wp.trail[i].y <= 3));   /* never through it */

    CASE("no route at all leaves no ribbon and the keystrokes standing");
    map_fill_tiles(w, 0, 0, 8, 8, TILE_VOID);
    map_set_tile(w, 0, 0, TILE_FLOOR);
    map_set_tile(w, 8, 8, TILE_FLOOR);
    wp.origin_x = 0; wp.origin_y = 0;
    w->tokens.v[wi].x = 8; w->tokens.v[wi].y = 8;
    wp.steps = 7;
    play_trail_sync(&wp, w);
    CHECK_EQ(wp.ntrail, 0);
    CHECK_EQ(wp.steps, 7);

    undo_free(&wu);
    map_free(w);

    /* Undo walks the token back the way it came, so the route shortens with
     * it and the cost comes down: a step that has been undone was not spent. */
    CASE("undo shortens the route and gives the cost back");
    CHECK_EQ(undo_undo(&u, m), 1);
    play_trail_sync(&p, m);
    CHECK_EQ(p.ntrail, 6);
    CHECK_EQ(p.steps, 5);

    CASE("redo lengthens it again");
    CHECK_EQ(undo_redo(&u, m), 1);
    play_trail_sync(&p, m);
    CHECK_EQ(p.ntrail, 7);
    CHECK_EQ(p.steps, 6);

    CASE("undoing back to the start leaves just the origin");
    for (int i = 0; i < 6; i++) { CHECK_EQ(undo_undo(&u, m), 1); play_trail_sync(&p, m); }
    CHECK_EQ(p.ntrail, 1);
    CHECK_EQ(p.steps, 0);
    CHECK_EQ(p.trail[0].x, 2);
    CHECK_EQ(p.trail[0].y, 2);

    CASE("sync does nothing at all when no token is held");
    p.grabbed = 0;
    play_trail_sync(&p, m);
    CHECK_EQ(p.ntrail, 0);

    undo_free(&u);
    map_free(m);

    /* The biggest map the format allows has more tiles than 16 bits can
     * count, so a route across it has to be measured in something wider. */
    CASE("a route across the largest allowed map is measured, not wrapped");
    Map *big = map_new(MAP_MAX_DIM, 4, "big");
    map_fill_tiles(big, 0, 0, MAP_MAX_DIM - 1, 3, TILE_FLOOR);

    Undo bu;
    undo_init(&bu);
    Play bp;
    play_init(&bp);
    Token bt = { 0, 0, 1, TOKEN_ENEMY, "B", { { 0, "" } }, 0 };
    play_focus(&bp, undo_add_token(&bu, big, bt));
    play_grab(&bp, big, 0);
    big->tokens.v[bp.sel].x = (int16_t)(MAP_MAX_DIM - 1);
    play_trail_sync(&bp, big);
    CHECK_EQ(bp.steps, MAP_MAX_DIM - 1);
    CHECK_EQ(bp.ntrail, PLAY_TRAIL_MAX);       /* the ribbon stops at its cap */

    undo_free(&bu);
    map_free(big);
}

void test_trail_draw(void)
{
    Map *m = map_new(12, 10, "trail");
    map_fill_tiles(m, 0, 0, 11, 9, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Play p;
    play_init(&p);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);

    GridView g;
    memset(&g, 0, sizeof g);
    g.zoom = 1;
    g.view = rect(0, 0, 80, 24);

    Token t = { 2, 2, 1, TOKEN_ENEMY, "G", { { 0, "" } }, 0 };
    int idx = undo_add_token(&u, m, t);
    play_focus(&p, idx);
    CASE("nothing is drawn while no token is held");
    rnd_begin(&r);
    play_trail_draw(&r, m, &g, &p, &THEME_DARK, 0);
    int sx, sy;
    grid_tile_interior(&g, 2, 2, &sx, &sy);
    CHECK(rnd_at(&r, sx, sy)->bg != THEME_DARK.trail_bg);

    play_grab(&p, m, 0);
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
    undo_begin(&u); play_step(m, &u, &p, 0, 1); undo_end(&u);

    CASE("every tile walked over is tinted");
    rnd_begin(&r);
    play_trail_draw(&r, m, &g, &p, &THEME_DARK, 0);
    const int walked[3][2] = { { 2, 2 }, { 3, 2 }, { 3, 3 } };
    for (int i = 0; i < 3; i++) {
        grid_tile_interior(&g, walked[i][0], walked[i][1], &sx, &sy);
        CHECK_EQ(rnd_at(&r, sx, sy)->bg, THEME_DARK.trail_bg);
    }

    /* The corner it did not cut: the ribbon follows the route, so the tile
     * on the diagonal stays untouched. */
    CASE("a tile beside the route is left alone");
    grid_tile_interior(&g, 2, 3, &sx, &sy);
    CHECK(rnd_at(&r, sx, sy)->bg != THEME_DARK.trail_bg);

    /* The whole point of drawing the route rather than the walk: fumbling the
     * cursor out and back should leave nothing behind. */
    CASE("squares only wandered over are not tinted");
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);   /* out to x=4 */
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);   /* and x=5 */
    undo_begin(&u); play_step(m, &u, &p, -1, 0); undo_end(&u);  /* back to x=4 */
    undo_begin(&u); play_step(m, &u, &p, -1, 0); undo_end(&u);  /* back to x=3 */
    CHECK_EQ(m->tokens.v[idx].x, 3);
    rnd_begin(&r);
    play_trail_draw(&r, m, &g, &p, &THEME_DARK, 0);
    grid_tile_interior(&g, 5, 3, &sx, &sy);
    CHECK(rnd_at(&r, sx, sy)->bg != THEME_DARK.trail_bg);
    grid_tile_interior(&g, 4, 3, &sx, &sy);
    CHECK(rnd_at(&r, sx, sy)->bg != THEME_DARK.trail_bg);
    grid_tile_interior(&g, 3, 3, &sx, &sy);
    CHECK_EQ(rnd_at(&r, sx, sy)->bg, THEME_DARK.trail_bg);

    CASE("the tile it set out from carries a mark of its own");
    Rect a;
    grid_token_area(&g, 2, 2, 1, &a);
    CHECK_EQ(rnd_at(&r, a.x, a.y)->ch, 0x25C6u);
    CHECK_EQ(rnd_at(&r, a.x, a.y)->fg, THEME_DARK.trail);

    CASE("ascii mode marks it with a letter instead");
    rnd_begin(&r);
    play_trail_draw(&r, m, &g, &p, &THEME_DARK, 1);
    CHECK_EQ(rnd_at(&r, a.x, a.y)->ch, (uint32_t)'X');

    /* A big creature covers ground, not a thread along its top-left corner. */
    CASE("a 2x2 token tints its whole footprint at every step");
    m->tokens.v[idx].size = 2;
    m->tokens.v[idx].x = 5;
    m->tokens.v[idx].y = 5;
    play_grab(&p, m, 0);
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
    rnd_begin(&r);
    play_trail_draw(&r, m, &g, &p, &THEME_DARK, 0);
    const int covered[6][2] = {
        { 5, 5 }, { 6, 5 }, { 5, 6 }, { 6, 6 }, { 7, 5 }, { 7, 6 },
    };
    for (int i = 0; i < 6; i++) {
        grid_tile_interior(&g, covered[i][0], covered[i][1], &sx, &sy);
        CHECK_EQ(rnd_at(&r, sx, sy)->bg, THEME_DARK.trail_bg);
    }

    /* A long walk should cost the size of the window, not the size of the
     * walk: tiles off screen are never drawn. */
    CASE("tiles scrolled out of view are skipped");
    g.view = rect(0, 0, 20, 10);
    rnd_begin(&r);
    play_trail_draw(&r, m, &g, &p, &THEME_DARK, 0);
    CHECK(1);

    rnd_free(&r);
    undo_free(&u);
    map_free(m);
}
