/* Tests: the control channel, the room language, corridors, --apply. */

#include "harness.h"
#include "app_priv.h"

void test_ctl(void)
{
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */

    CASE("with no map open, status answers and everything else says why not");
    char *t = ctl_ask(&a, "status\n");
    CHECK(strncmp(t, "ok\nmap none open\n", 17) == 0);
    CHECK(strstr(t, "edits not now: no map is open") != NULL);
    free(t);
    t = ctl_ask(&a, "dump\n");
    CHECK_EQ(strcmp(t, "error: line 1: no map is open\n"), 0);
    free(t);

    app_open_map(&a, "tests/fixtures/two-rooms.vtt");
    CHECK(a.map != NULL);
    if (!a.map) { app_free(&a); rnd_free(&r); return; }

    CASE("status: the map, its file, the screen, undo, and how edits are taken");
    t = ctl_ask(&a, "status");
    CHECK(strncmp(t, "ok\nmap Two Rooms  16x9\n", 23) == 0);
    CHECK(strstr(t, "file tests/fixtures/two-rooms.vtt\n") != NULL);
    CHECK(strstr(t, "screen build, normal mode\n") != NULL);
    CHECK(strstr(t, "undo 0 back, 0 forward\n") != NULL);
    CHECK(strstr(t, "edits land at once (:agent accept auto)\n") != NULL);
    free(t);
    a.ctl_auto = 0;
    t = ctl_ask(&a, "status");
    CHECK(strstr(t, "edits proposed, for the GM to review\n") != NULL);
    free(t);
    a.ctl_auto = 1;

    CASE("dump, describe and check are the map tools' own reports");
    {
        char *want = NULL;
        size_t wn = 0;
        FILE *f = open_memstream(&want, &wn);
        maptools_dump(f, a.map, 1, 1, 6, 5);
        fclose(f);
        t = ctl_ask(&a, "dump B2:G6\n");
        CHECK(strncmp(t, "ok\n", 3) == 0 && strcmp(t + 3, want) == 0);
        free(t);
        free(want);

        f = open_memstream(&want, &wn);
        maptools_describe(f, a.map, 1);
        fclose(f);
        t = ctl_ask(&a, "describe json");
        CHECK(strcmp(t + 3, want) == 0);
        CHECK(json_valid(t + 3));
        free(t);
        free(want);

        f = open_memstream(&want, &wn);
        maptools_check_map(f, a.map, 0);
        fclose(f);
        t = ctl_ask(&a, "check");
        CHECK(strcmp(t + 3, want) == 0);
        free(t);
        free(want);
        t = ctl_ask(&a, "check json");
        CHECK(json_valid(t + 3));
        CHECK(strstr(t, "\"file\":\"tests/fixtures/two-rooms.vtt\"") != NULL);
        free(t);
    }

    CASE("lines run in order; blank lines and # lines are skipped");
    t = ctl_ask(&a, "# the map first\n\n   \nstatus\ndump A1\n");
    CHECK(strncmp(t, "ok\nmap Two Rooms", 16) == 0);
    CHECK(strstr(t, "region A1:A1") != NULL);
    free(t);

    CASE("a comment is never split: any length, any quotes");
    t = ctl_ask(&a, "# one two three four five six seven eight nine ten eleven twelve thirteen \"x\n"
                    "  # indented, with a lone \" quote\nstatus\n");
    CHECK(strncmp(t, "ok\nmap", 6) == 0);
    free(t);

    CASE("an error names its line and sends nothing else");
    t = ctl_ask(&a, "status\ndump B2:ZZ99:4\n");
    CHECK(strncmp(t, "error: line 2: ZZ99:4 is not a square", 37) == 0);
    CHECK(strchr(t, '\n') == t + strlen(t) - 1);
    free(t);
    t = ctl_ask(&a, "describe yaml\n");
    CHECK_EQ(strcmp(t, "error: line 1: describe takes nothing but json after it\n"), 0);
    free(t);
    t = ctl_ask(&a, "frobnicate\n");
    CHECK_EQ(strcmp(t, "error: line 1: unknown request frobnicate\n"), 0);
    free(t);
    t = ctl_ask(&a, "status now\n");
    CHECK(strncmp(t, "error: line 1: status takes nothing", 35) == 0);
    free(t);

    CASE("words: quotes, escapes, and the mistakes a quote can make");
    {
        static const struct { const char *req, *err; } bad[] = {
            { "dump \"B2\n",           "a quote is not closed" },
            { "dump \"B2\"x\n",        "a closing quote runs into the next word" },
            { "dump B\"2\n",           "a quote in the middle of a word" },
            { "a b c d e f g h i j k l m\n", "more than 12 words" },
        };
        for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
            t = ctl_ask(&a, bad[i].req);
            CHECK(strstr(t, bad[i].err) != NULL);
            free(t);
        }
        t = ctl_ask(&a, "dump \"B2:C3\"\n");      /* a quoted word is a word */
        CHECK(strncmp(t, "ok\n", 3) == 0);
        free(t);
        char big[1100];
        memset(big, 'x', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        t = ctl_ask(&a, big);
        CHECK(strstr(t, "the line is over 1023 characters") != NULL);
        free(t);
        char word[300] = "dump ";
        memset(word + 5, 'B', 290);
        word[295] = '\0';
        t = ctl_ask(&a, word);
        CHECK(strstr(t, "a word over 255 characters") != NULL);
        free(t);
    }

    CASE("a nul byte inside the request is refused whole");
    {
        const char req[] = "status\0dump\n";
        size_t len = sizeof req - 1;
        t = app_ctl_exec(&a, req, &len);
        CHECK(t && strcmp(t, "error: a nul byte in the request\n") == 0);
        free(t);
    }

    CASE("edits wait, with the reason, whenever the GM is part way through something, and land at the next key");
    CHECK(app_ctl_busy(&a) == NULL);
    press(&a, ":");
    CHECK(app_ctl_busy(&a) != NULL && strstr(app_ctl_busy(&a), ": command"));
    press(&a, "\x1b");
    CHECK(app_ctl_busy(&a) == NULL);
    press(&a, "g");
    CHECK(app_ctl_busy(&a) != NULL && strstr(app_ctl_busy(&a), "part way"));
    press(&a, "\x1b");
    CHECK(app_ctl_busy(&a) == NULL);
    press(&a, "sn");                                        /* a note's prompt */
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK(app_ctl_busy(&a) != NULL && strstr(app_ctl_busy(&a), "prompt"));
    t = ctl_ask(&a, "tile A1 water");
    CHECK(t && !strncmp(t, "ok\nproposal #", 13) && strstr(t, "\nlands when the GM is back: the GM is answering a prompt\n"));
    free(t);
    CHECK(map_tile(a.map, 0, 0) != TILE_WATER);              /* not yet */
    t = ctl_ask(&a, "status");                               /* a read is answered */
    CHECK(t && !strncmp(t, "ok\n", 3));
    CHECK(t && strstr(t, "edits proposed, landing when the GM is back: the GM is answering a prompt\n"));
    free(t);
    press(&a, "\x1b");
    CHECK(app_ctl_busy(&a) == NULL);
    CHECK_EQ(map_tile(a.map, 0, 0), TILE_WATER);             /* the GM is back */
    a.modal = MODAL_PICKER;                                  /* a list to choose from */
    t = ctl_ask(&a, "tile A1 hazard");
    CHECK(t && strstr(t, "\nlands when the GM is back: the GM is choosing from a list\n"));
    free(t);
    CHECK_EQ(map_tile(a.map, 0, 0), TILE_WATER);
    a.modal = MODAL_MESSAGE;                                 /* a message on the screen */
    t = ctl_ask(&a, "tile A2 hazard");
    CHECK(t && strstr(t, "\nlands when the GM is back: a question is open on the GM's screen\n"));
    free(t);
    a.modal = MODAL_NONE;
    press(&a, "\x1b");
    CHECK_EQ(map_tile(a.map, 0, 0), TILE_HAZARD);            /* both, in turn */
    CHECK_EQ(map_tile(a.map, 0, 1), TILE_HAZARD);
    press(&a, "w l");                                        /* the pen down, a wall laid */
    t = ctl_ask(&a, "tile A1 water");
    CHECK(t && strstr(t, "\nlands when the GM is back: the GM is laying wall\n"));
    free(t);
    CHECK_EQ(map_tile(a.map, 0, 0), TILE_HAZARD);            /* never into a stroke */
    press(&a, " \x1b");
    CHECK(app_ctl_busy(&a) == NULL);
    CHECK_EQ(map_tile(a.map, 0, 0), TILE_WATER);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    CHECK_EQ(a.screen, SCREEN_PLAY);
    CHECK(app_ctl_busy(&a) != NULL && strstr(app_ctl_busy(&a), "play mode"));
    t = ctl_ask(&a, "status");
    CHECK(strstr(t, "screen play\n") != NULL);
    CHECK(strstr(t, "edits proposed, landing when the GM is back: the GM is in play mode") != NULL);
    free(t);

    CASE(":agent with the channel off says so; on and off from the command line");
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    CHECK_EQ(a.screen, SCREEN_EDITOR);
    press(&a, ":agent\r");
    CHECK(strstr(a.status, "the agent channel is off") != NULL);
    CHECK_EQ(a.status_gm, 1);
    press(&a, ":agent sideways\r");
    CHECK(strstr(a.status, ":agent on, :agent off") != NULL);
    press(&a, ":agent off\r");
    CHECK(strstr(a.status, "already off") != NULL);

    app_free(&a);
    rnd_free(&r);
}

/* Services the app's channel until `until` says stop or two seconds pass. */
static void ctl_pump(App *a, int (*until)(void *), void *ctx)
{
    for (int spin = 0; spin < 400 && !(until && until(ctx)); spin++) {
        struct pollfd fds[1 + CTL_SLOTS];
        int n = ctl_pollfds(&a->ctl, fds, 1 + CTL_SLOTS);
        poll(fds, (nfds_t)n, 5);
        uint64_t now = prof_now_ns() / 1000000u;
        ctl_service(&a->ctl, fds, n, now);
        app_tick(a, now);
    }
}

/* For ctl_pump: stop once the first job has a proposal. */
static int a_job_ready(void *ctx)
{
    App *a = ctx;
    return a->jobs[0].used && a->jobs[0].state == JOB_READY;
}

/* For ctl_pump: stop once this many waits are held. */
typedef struct { App *a; int n; } WaitersAre;
static int waiters_are(void *ctx)
{
    WaitersAre *w = ctx;
    return ctl_waiters(&w->a->ctl) == w->n;
}

static int ctl_raw_connect(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    str_lcpy(sa.sun_path, path, sizeof sa.sun_path);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { close(fd); return -1; }
    return fd;
}

static int ctl_read_some(void *ctx)
{
    CtlReader *rd = ctx;
    for (;;) {
        struct pollfd p = { rd->fd, POLLIN, 0 };
        if (poll(&p, 1, 0) <= 0) return rd->done;
        ssize_t k = read(rd->fd, rd->buf + rd->n, sizeof rd->buf - 1 - rd->n);
        if (k < 0) rd->reset = 1;          /* the answer must end in a clean EOF */
        if (k <= 0) { rd->done = 1; rd->buf[rd->n] = '\0'; return 1; }
        rd->n += (size_t)k;
    }
}

static int ctl_child_done(void *ctx)
{
    int *st = ctx;
    return st[1] || (st[1] = waitpid((pid_t)st[0], &st[2], WNOHANG) > 0);
}

/* One request over the socket, the whole answer (free it). */
static char *sock_ask(App *a, const char *req)
{
    CtlReader rd = { ctl_raw_connect(a->ctl.path), "", 0, 0 };
    if (rd.fd < 0) return NULL;
    if (write(rd.fd, req, strlen(req)) < 0) { }
    shutdown(rd.fd, SHUT_WR);
    ctl_pump(a, ctl_read_some, &rd);
    close(rd.fd);
    return strdup(rd.buf);
}

void test_ctl_marked(void)
{
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */
    app_open_map(&a, "tests/fixtures/two-rooms.vtt");
    CHECK(a.map != NULL);
    if (!a.map) { app_free(&a); rnd_free(&r); return; }
    a.ed.cx = 2;
    a.ed.cy = 2;
    char *t;

    CASE("marked: the cursor, and a brush's whole footprint");
    t = ctl_ask(&a, "marked");
    CHECK_EQ(strcmp(t, "ok\nscreen build, normal mode\ncursor C3\n"), 0);
    free(t);
    press(&a, "2b");
    t = ctl_ask(&a, "marked");
    CHECK(strstr(t, "cursor C3:D4\n") != NULL);
    free(t);
    press(&a, "1b");

    CASE("marked: a v box and a V circle");
    press(&a, "vllj");
    t = ctl_ask(&a, "marked");
    CHECK(strstr(t, "screen build, visual mode\n") != NULL);
    CHECK(strstr(t, "cursor E4\n") != NULL);
    CHECK(strstr(t, "box C3:E4, 3x2\n") != NULL);
    free(t);
    press(&a, "\x1b");
    a.ed.cx = 2; a.ed.cy = 2;
    press(&a, "Vll");
    t = ctl_ask(&a, "marked");
    CHECK(strstr(t, "box circle round C3, radius 2, over A1:E5\n") != NULL);
    free(t);
    t = ctl_ask(&a, "marked json");
    CHECK(json_valid(t + 3));
    CHECK(strstr(t, "\"box\":{\"shape\":\"circle\",\"between\":\"squares\"") != NULL);
    CHECK(strstr(t, "\"center\":\"C3\"") != NULL);
    free(t);
    press(&a, "\x1b");

    CASE("marked: wall mode's corner, and a box anchored between corners");
    a.ed.cx = 2; a.ed.cy = 2;
    press(&a, "w");
    t = ctl_ask(&a, "marked");
    CHECK(strstr(t, "screen build, wall mode\n") != NULL);
    CHECK(strstr(t, "corner at the top left of ") != NULL);
    free(t);
    int wx = a.ed.wx, wy = a.ed.wy;
    press(&a, "vlljj");
    t = ctl_ask(&a, "marked");
    char want[64], a0[MAP_COORD_MAX], a1[MAP_COORD_MAX];
    map_coord_name(wx, wy, a0, sizeof a0);
    map_coord_name(wx + 1, wy + 1, a1, sizeof a1);
    snprintf(want, sizeof want, "box %s:%s, 2x2\n", a0, a1);
    CHECK(strstr(t, want) != NULL);
    free(t);
    press(&a, "\x1b\x1b");
    CHECK_EQ(a.ed.mode, ED_NORMAL);

    CASE("marked: a wall-mode line of corners holds no squares, and a corner on the east edge is named from inside");
    a.ed.cx = 15; a.ed.cy = 2;
    press(&a, "wl");
    t = ctl_ask(&a, "marked");
    CHECK(strstr(t, "corner at the top right of P") != NULL);
    free(t);
    press(&a, "vjj");
    t = ctl_ask(&a, "marked");
    CHECK(strstr(t, "box a line of corners, no squares inside it\n") != NULL);
    free(t);
    t = ctl_ask(&a, "marked json");
    CHECK(json_valid(t + 3));
    CHECK(strstr(t, "\"squares\":null") != NULL);
    free(t);
    press(&a, "\x1b\x1b");

    CASE("marked: the ruler's points and its length");
    a.ed.cx = 2; a.ed.cy = 2;
    press(&a, "mlll");
    t = ctl_ask(&a, "marked");
    CHECK(strstr(t, "ruler C3 to F3, 15 ft\n") != NULL);
    free(t);
    press(&a, "\x1b");

    CASE("marked: the creatures selected in play, and a play box");
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    CHECK_EQ(a.screen, SCREEN_PLAY);
    play_focus(&a.play, 0);
    t = ctl_ask(&a, "marked");
    CHECK(strstr(t, "screen play\n") != NULL);
    char sel[64];
    snprintf(sel, sizeof sel, "selected %s ", a.map->tokens.v[0].label);
    CHECK(strstr(t, sel) != NULL);
    free(t);

    CASE("marked: pings -- the GM's and a phone's -- kept after their rings come down");
    app_tick(&a, 1000);
    a.ed.cx = 2; a.ed.cy = 2;
    press(&a, "gp");
    CHECK_EQ(a.npings, 1);
    app_ping(&a, 3, 5, 4, 5, 4);
    app_tick(&a, 13500);
    CHECK_EQ(a.npings, 0);                         /* the rings are down */
    t = ctl_ask(&a, "marked");
    CHECK(strstr(t, "pinged by the GM at C3, 12 s ago\n") != NULL);
    CHECK(strstr(t, "pinged by phone 3 at F5, 12 s ago\n") != NULL);
    free(t);
    app_tick(&a, 20000);
    press(&a, "gp");                              /* the GM's record moves, not doubles */
    CHECK_EQ(a.npinged, 2);
    t = ctl_ask(&a, "marked json");
    CHECK(json_valid(t + 3));
    CHECK(strstr(t, "{\"by\":\"gm\",\"phone\":null,\"at\":{\"region\":\"C3\"") != NULL);
    CHECK(strstr(t, "\"seconds_ago\":0}") != NULL);
    CHECK(strstr(t, "{\"by\":\"phone\",\"phone\":3,") != NULL);
    free(t);

    CASE("the record goes with the map");
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    press(&a, ":q!\r");
    CHECK(a.map == NULL);
    CHECK_EQ(a.npinged, 0);

    app_free(&a);
    rnd_free(&r);
}

/* The map's squares, boundaries, creatures and notes, to compare before and
 * after a request that must have changed nothing. */
static char *ctl_snapshot(const Map *m)
{
    char  *buf = NULL;
    size_t n   = 0;
    FILE  *f   = open_memstream(&buf, &n);
    maptools_dump(f, m, 0, 0, m->w - 1, m->h - 1);
    for (int i = 0; i < m->nnotes; i++) fprintf(f, "note %d %d %s\n", m->notes[i].x, m->notes[i].y, m->notes[i].text);
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) fputc('a' + (fog_at(m, x, y) & FOG_ID), f);
    fclose(f);
    return buf;
}

void test_ctl_edits(void)
{
    Sandbox sb = sandbox_enter("ctledit");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */
    CHECK(ctl_blank_map(&a, sb.dir, 12, 8));
    if (!a.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.map;
    char *t, *before, *after;

    CASE("room: floor, walls round it, one undo step; the GM is told and shown where");
    int depth = a.undo.depth;
    t = ctl_ask(&a, "room B2:D4\n");
    CHECK(!strncmp(t, "ok\nproposal #1: ", 16));
    CHECK(strstr(t, "\nchanged B2:D4: 1 line, one undo step\n") != NULL);
    free(t);
    CHECK_EQ(a.undo.depth, depth + 1);
    CHECK_EQ(map_vedge(m, 1, 1), EDGE_WALL);
    CHECK_EQ(map_vedge(m, 4, 3), EDGE_WALL);
    CHECK_EQ(map_vedge(m, 2, 2), EDGE_NONE);         /* inside is left alone */
    CHECK_EQ(map_hedge(m, 1, 1), EDGE_WALL);
    CHECK_EQ(map_hedge(m, 3, 4), EDGE_WALL);
    CHECK(!strncmp(a.status, "#1 accepted: ", 13) && strstr(a.status, " - u takes it back"));
    CHECK_EQ(a.status_gm, 1);
    CHECK(a.agent_ring.until_ms != 0);
    CHECK(a.agent_ring.x0 == 1 && a.agent_ring.y0 == 1 && a.agent_ring.x1 == 3 && a.agent_ring.y1 == 3);
    CHECK_EQ(app_view_differs(&a), 1);                /* the ring is the GM's */
    app_tick(&a, a.now_ms + PING_SHOW_MS);
    CHECK_EQ(a.agent_ring.until_ms, 0u);

    CASE("many lines are still one step: u takes all of them back together");
    before = ctl_snapshot(m);
    depth = a.undo.depth;
    t = ctl_ask(&a, "# a crypt\n"
                    "room F2:K6\n"
                    "tile G3:H4 water\n"
                    "edge E4|F4 door\n"
                    "edge G6/G7 window\n"
                    "token add enemy J5 \"Ghoul\"\n"
                    "token add player G5 size 2 \"Aria\"\n"
                    "note K2 \"secret door here?\"\n");
    CHECK(strstr(t, "\nchanged E2:K7: 7 lines, one undo step\n") != NULL);
    free(t);
    CHECK_EQ(a.undo.depth, depth + 1);
    CHECK_EQ(map_tile(m, 7, 3), TILE_WATER);
    CHECK_EQ(map_vedge(m, 5, 3), EDGE_DOOR_CLOSED);
    CHECK_EQ(map_hedge(m, 6, 6), EDGE_WINDOW);
    CHECK_EQ(m->tokens.n, 2);
    CHECK(map_note_at(m, 10, 1) && !strcmp(map_note_at(m, 10, 1), "secret door here?"));
    CHECK(strstr(a.status, "accepted: ") && strstr(a.status, "2 creatures") && strstr(a.status, "u takes it back"));
    press(&a, "u");
    after = ctl_snapshot(m);
    CHECK_EQ(strcmp(before, after), 0);
    CHECK_EQ(m->tokens.n, 0);
    CHECK(map_note_at(m, 10, 1) == NULL);
    free(after);
    press(&a, "\x12");                               /* ctrl-r: all of it again */
    CHECK_EQ(m->tokens.n, 2);
    CHECK(map_note_at(m, 10, 1) != NULL);
    free(before);

    CASE("all or nothing: a bad line undoes the lines before it and says which");
    before = ctl_snapshot(m);
    depth = a.undo.depth;
    int nmarks = a.undo.nmarks;
    t = ctl_ask(&a, "tile A1:L8 hazard\ntoken del Ghoul\nnote A1 \"x\"\ntoken add enemy B7 \"Aria\"\n");
    CHECK_EQ(strcmp(t, "error: line 4: there is already a creature called Aria\n"), 0);
    free(t);
    after = ctl_snapshot(m);
    CHECK_EQ(strcmp(before, after), 0);
    CHECK_EQ(a.undo.depth, depth);
    CHECK_EQ(a.undo.nmarks, nmarks);
    CHECK_EQ(a.undo.open, 0);
    CHECK_EQ(m->tokens.n, 2);
    free(after);
    free(before);

    CASE("a room, drawn through the editor's own helper, is rolled back with the rest");
    before = ctl_snapshot(m);
    t = ctl_ask(&a, "room A6:C8\nwall E6:F7 door\ntoken move Nobody A1\n");
    CHECK(strncmp(t, "error: line 3:", 14) == 0);
    free(t);
    after = ctl_snapshot(m);
    CHECK_EQ(strcmp(before, after), 0);
    CHECK_EQ(a.undo.open, 0);
    CHECK_EQ(a.undo.nest, 0);
    free(after);
    free(before);

    CASE("the mistakes an edit can make, each named");
    {
        static const struct { const char *req, *err; } bad[] = {
            { "room B2:Z99\n",              "Z99 is not a square on this map (A1 to L8)" },
            { "room\n",                     "room REGION" },
            { "tile B2 lava\n",             "lava: a tile is void, floor" },
            { "wall B2:C3 portcullis\n",    "portcullis: a boundary is none, wall" },
            { "edge C3|E3 door\n",          "the squares are not side by side" },
            { "edge C3/D3 door\n",          "the squares are not side by side" },
            { "edge -|- door\n",            "a boundary needs a square on one side" },
            { "edge C3 door\n",             "is not a boundary" },
            { "edge -|B1 door\n",           "the squares are not side by side" },
            { "token add dragon C3 \"X\"\n", "a creature is a player or an enemy" },
            { "token add enemy C3 size 4 \"X\"\n", "size is 1, 2 or 3" },
            { "token add enemy L8 size 2 \"X\"\n", "hangs off the map" },
            { "token add enemy J5 \"X\"\n", "J5 is taken by Ghoul" },
            { "token add enemy C3 \"\"\n",  "a creature needs a label" },
            { "token add enemy C3 \"abcdefghijabcdefghijabcdefghij12\"\n", "the label is over 31 characters" },
            { "token move Nobody C3\n",     "no creature called Nobody" },
            { "token move A1 C3\n",         "no creature stands on A1" },
            { "token move Ghoul G5\n",      "G5 is taken by Aria" },
            { "token set Ghoul color red\n", "token set changes a label, a size, a note or hidden" },
            { "token fly Ghoul\n",          "token fly: add, move, del or set" },
            { "note Z1 \"x\"\n",            "is not a square" },
            { "fog paint B2:C3 1\n",        "there is no fog patch 1" },
            { "fog paint B2:C3 16\n",       "a fog patch is 1 to 15" },
            { "fog B2:C3 1\n",              "fog paint REGION N" },
        };
        before = ctl_snapshot(m);
        for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
            t = ctl_ask(&a, bad[i].req);
            CHECK(strncmp(t, "error: line 1: ", 15) == 0 && strstr(t, bad[i].err) != NULL);
            if (!strstr(t, bad[i].err)) fprintf(stderr, "    got: %s", t);
            free(t);
        }
        after = ctl_snapshot(m);
        CHECK_EQ(strcmp(before, after), 0);
        free(after);
        free(before);
    }

    CASE("void is not ground: a creature cannot be put there");
    t = ctl_ask(&a, "tile A8 void\ntoken add enemy A8 \"X\"\n");
    CHECK(strstr(t, "A8 is void - a creature needs ground") != NULL);
    free(t);

    CASE("edges off the map's side, named with -");
    t = ctl_ask(&a, "edge -|A1 door\nedge L8|- window\nedge -/C1 wall\nedge C8/- wall\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    CHECK_EQ(map_vedge(m, 0, 0), EDGE_DOOR_CLOSED);
    CHECK_EQ(map_vedge(m, 12, 7), EDGE_WINDOW);
    CHECK_EQ(map_hedge(m, 2, 0), EDGE_WALL);
    CHECK_EQ(map_hedge(m, 2, 8), EDGE_WALL);

    CASE("creatures: by label (any case), by a square they stand on; moved, changed, taken off");
    t = ctl_ask(&a, "token move ghoul I5\ntoken set H6 label \"Aria the Bold\"\ntoken set Ghoul size 2\n"
                    "token set Ghoul note \"hates fire\"\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    {
        int g = -1, p = -1;
        for (int i = 0; i < m->tokens.n; i++) {
            if (!strcmp(m->tokens.v[i].label, "Ghoul")) g = i;
            if (!strcmp(m->tokens.v[i].label, "Aria the Bold")) p = i;
        }
        CHECK(g >= 0 && p >= 0);
        if (g >= 0) {
            CHECK(m->tokens.v[g].x == 8 && m->tokens.v[g].y == 4 && m->tokens.v[g].size == 2);
            CHECK_EQ(strcmp(m->tokens.v[g].note, "hates fire"), 0);
        }
    }
    t = ctl_ask(&a, "token move Ghoul H5\n");            /* onto Aria */
    CHECK(strstr(t, "H5 is taken by Aria the Bold") != NULL);
    free(t);

    CASE("the creature holding the turn is the GM's to pass on before it goes");
    m->tokens.v[0].turn = TURN_IN | TURN_ACTING;
    m->round = 1;
    char req[64];
    snprintf(req, sizeof req, "token del \"%s\"\n", m->tokens.v[0].label);
    t = ctl_ask(&a, req);
    CHECK(strstr(t, "holds the turn - the GM passes it on first") != NULL);
    free(t);
    m->tokens.v[0].turn = 0;
    m->round = 0;
    t = ctl_ask(&a, "token del Ghoul\ntoken del \"Aria the Bold\"\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    CHECK_EQ(m->tokens.n, 0);

    CASE("notes on squares: one set, one taken off, both undone by u");
    t = ctl_ask(&a, "note C3 \"trap?\"\nnote K2\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    CHECK(map_note_at(m, 2, 2) && map_note_at(m, 10, 1) == NULL);
    press(&a, "u");
    CHECK(map_note_at(m, 2, 2) == NULL);
    CHECK(map_note_at(m, 10, 1) && !strcmp(map_note_at(m, 10, 1), "secret door here?"));
    press(&a, "\x12");

    CASE("fog paint: into a patch, and 0 scrubs; the patch must be there");
    str_lcpy(m->fog_patches[0].name, "Mist", sizeof m->fog_patches[0].name);
    t = ctl_ask(&a, "fog paint B2:C3 1\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    CHECK_EQ(fog_at(m, 1, 1) & FOG_ID, 1);
    CHECK_EQ(fog_at(m, 2, 2) & FOG_ID, 1);
    t = ctl_ask(&a, "fog paint C3 0\n");
    free(t);
    CHECK_EQ(fog_at(m, 2, 2) & FOG_ID, 0);

    CASE("a read after an edit in the same request sees it, and does not take the request's batch for the GM's");
    t = ctl_ask(&a, "tile E7 water\nstatus\ndump E7\n");
    CHECK(strstr(t, "edits land at once") != NULL);
    CHECK(strstr(t, "and this request's changes proposed") != NULL);
    CHECK(strstr(t, "7  ~") != NULL);
    free(t);

    CASE("dump refuses a region off the map; a half region and a size after the label say what is wrong");
    t = ctl_ask(&a, "dump B2:ZZ99\n");
    CHECK(strstr(t, "ZZ99 is not a square on this map") != NULL);
    free(t);
    t = ctl_ask(&a, "room B2:\n");
    CHECK(strstr(t, "B2:: a region is two squares") != NULL);
    free(t);
    t = ctl_ask(&a, "token add enemy C3 \"X\" size 2\n");
    CHECK(strstr(t, "the size goes before the label") != NULL);
    free(t);

    CASE("a request that changes nothing is no undo step");
    depth = a.undo.depth;
    t = ctl_ask(&a, "tile B2 floor\n");
    CHECK_EQ(strcmp(t, "ok\nno change: the map already looked like that\n"), 0);
    free(t);
    CHECK_EQ(a.undo.depth, depth);

    CASE("the agent's undo: its own last change, only while nothing came after");
    t = ctl_ask(&a, "tile B2 water\n");
    free(t);
    t = ctl_ask(&a, "undo\n");
    CHECK_EQ(strcmp(t, "ok\ntook back the last change\n"), 0);
    free(t);
    CHECK_EQ(map_tile(m, 1, 1), TILE_FLOOR);
    CHECK(strstr(a.status, "the agent took back its last change") != NULL);
    t = ctl_ask(&a, "undo\n");
    CHECK(strstr(t, "there is no change of the agent's to take back") != NULL);
    free(t);
    t = ctl_ask(&a, "tile B2 water\n");
    free(t);
    a.ed.cx = 5; a.ed.cy = 5;
    press(&a, " ");                                  /* the GM paints a square */
    press(&a, "u");                                  /* and takes it back again */
    t = ctl_ask(&a, "undo\n");
    CHECK(strstr(t, "something has happened since") != NULL);
    free(t);
    CHECK_EQ(map_tile(m, 1, 1), TILE_WATER);
    t = ctl_ask(&a, "tile B3 water\nundo\n");
    CHECK_EQ(strcmp(t, "error: undo goes in a request of its own\n"), 0);
    free(t);
    CHECK_EQ(map_tile(m, 1, 2), TILE_FLOOR);           /* nothing ran */
    t = ctl_ask(&a, "tile B3 water\n");
    free(t);
    t = ctl_ask(&a, "# first\nundo\nundo\n");
    CHECK_EQ(strcmp(t, "error: undo goes in a request of its own\n"), 0);
    free(t);
    CHECK_EQ(map_tile(m, 1, 2), TILE_WATER);           /* not taken back half way */
    CASE("the agent's undo also stops at a change the GM made outside the log");
    CHECK(map_note_set(m, 7, 7, "the GM's own"));      /* outside the log, as a fog setting is */
    t = ctl_ask(&a, "undo\n");
    CHECK(strstr(t, "something has happened since") != NULL);
    free(t);
    CHECK_EQ(map_tile(m, 1, 2), TILE_WATER);

    CASE("the GM's own note on a square is one u away, and ctrl-r puts it back");
    a.ed.cx = 3; a.ed.cy = 6;
    press(&a, "sntrap?\r");
    CHECK(map_note_at(m, 3, 6) && !strcmp(map_note_at(m, 3, 6), "trap?"));
    press(&a, "u");
    CHECK(map_note_at(m, 3, 6) == NULL);
    press(&a, "\x12");
    CHECK(map_note_at(m, 3, 6) != NULL);
    press(&a, "sn\025\r");                            /* cleared, and back with u */
    CHECK(map_note_at(m, 3, 6) == NULL);
    press(&a, "u");
    CHECK(map_note_at(m, 3, 6) != NULL);

    CASE("F1 or F2 mid-stroke lifts the pen: the run is one step and the channel is not kept waiting");
    a.ed.cx = 6; a.ed.cy = 7;                        /* ground no earlier case walled */
    int d0 = a.undo.depth;
    press(&a, "w ll");
    CHECK(map_hedge(m, 6, 7) == EDGE_WALL || map_hedge(m, 7, 7) == EDGE_WALL);
    CHECK_EQ(a.undo.open, 1);
    Key f2k = { KEY_F2, 0, 0 }, f1k = { KEY_F1, 0, 0 };
    app_key(&a, f2k);
    app_key(&a, f1k);
    CHECK_EQ(a.undo.open, 0);
    CHECK_EQ(a.ed.pen, 0);
    CHECK_EQ(a.undo.depth, d0 + 1);
    CHECK(app_ctl_busy(&a) == NULL);
    press(&a, "u");
    CHECK_EQ(a.undo.depth, d0);

    CASE("under review, edits are proposals: the map, its log (the redo tail too), modified and Map.gen stay");
    t = ctl_ask(&a, "tile H8 water\n");                  /* landed, then undone: a redo tail */
    free(t);
    press(&a, "u");
    a.ctl_auto = 0;
    before = ctl_snapshot(m);
    int      nmarks0 = a.undo.nmarks, udepth = a.undo.depth, modified = m->modified;
    unsigned stamp = a.undo.stamp, gen = m->gen;
    press(&a, ":");                                  /* typing a : command */
    t = ctl_ask(&a, "status\ntile B2 hazard\n");
    CHECK(t && !strncmp(t, "ok\n", 3) && strstr(t, "\nwaiting for the GM's review - :review "));
    free(t);
    press(&a, "\x1b");
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    t = ctl_ask(&a, "tile B3 hazard\n");             /* play mode */
    CHECK(t && strstr(t, "\nwaiting for the GM's review"));
    free(t);
    t = ctl_ask(&a, "dump B2\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    a.ed.cx = 5; a.ed.cy = 5;
    press(&a, "w");                                  /* wall mode ... */
    a.ed.pen = 1;
    undo_stroke(&a.undo);                            /* ... with a stroke open */
    t = ctl_ask(&a, "tile B4 hazard\ntoken add enemy C3 \"Imp\"\n");
    CHECK(t && strstr(t, "\nwaiting for the GM's review"));
    free(t);
    undo_stroke_end(&a.undo);
    a.ed.pen = 0;
    press(&a, "\x1b");
    after = ctl_snapshot(m);
    CHECK_EQ(strcmp(before, after), 0);
    CHECK_EQ(a.undo.nmarks, nmarks0);
    CHECK_EQ(a.undo.depth, udepth);
    CHECK_EQ(a.undo.stamp, stamp);
    CHECK_EQ(m->gen, gen);
    CHECK_EQ(m->modified, modified);
    CHECK_EQ(m->tokens.n, 0);
    int ready = 0;
    for (int i = 0; i < JOB_MAX; i++) ready += a.jobs[i].used && a.jobs[i].state == JOB_READY;
    CHECK_EQ(ready, 3);
    press(&a, "\x12");                              /* ctrl-r: the tail is still there */
    CHECK_EQ(map_tile(m, 7, 7), TILE_WATER);
    free(after);
    free(before);
    app_jobs_clear(&a);
    a.ctl_auto = 1;

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

void test_ctl_cap(void)
{
    Sandbox sb = sandbox_enter("ctlcap");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */
    CHECK(ctl_blank_map(&a, sb.dir, MAP_MAX_DIM, MAP_MAX_DIM));

    CASE("a request may change twice the largest map's squares and no more");
    char last[MAP_COORD_MAX], req[128];
    map_coord_name(MAP_MAX_DIM - 1, MAP_MAX_DIM - 1, last, sizeof last);
    snprintf(req, sizeof req, "tile A1:%s water\ntile A1:%s floor\n", last, last);
    char *t = ctl_ask(&a, req);
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    snprintf(req, sizeof req, "tile A1:%s water\ntile A1:%s floor\ntile A1 hazard\n", last, last);
    t = ctl_ask(&a, req);
    CHECK(strstr(t, "error: line 3: the request changes more than 524288 things at once") != NULL);
    free(t);
    CHECK_EQ(map_tile(a.map, 0, 0), TILE_FLOOR);
    CHECK_EQ(map_tile(a.map, 5, 5), TILE_FLOOR);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

void test_room_language(void)
{
    Sandbox sb = sandbox_enter("roomlang");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */
    char path[700];
    snprintf(path, sizeof path, "%s/void.vtt", sb.dir);
    {
        Map *v = map_new(30, 20, "void");
        char err[200];
        mapio_write(v, path, err, sizeof err);
        map_free(v);
    }
    app_open_map(&a, path);
    Map *m = a.map;
    CHECK(m != NULL);
    if (!m) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    char *t;

    CASE("room NAME REGION and room NAME SQUARE WxH draw and name");
    t = ctl_ask(&a, "room Crypt B2 8x6\nroom Hall B12:E14\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    int ci = map_area_find(m, "Crypt");
    CHECK(ci >= 0 && m->areas[ci].x0 == 1 && m->areas[ci].x1 == 8 && m->areas[ci].y1 == 6);
    CHECK_EQ(map_vedge(m, 9, 3), EDGE_WALL);
    CHECK(map_area_find(m, "Hall") >= 0);

    CASE("relative rooms: each side, gap, and the three alignments");
    {
        static const struct { const char *req; int x0, y0; } rel[] = {
            { "room A 4x2 east of Crypt\n",               9, 3 },   /* middle of rows 1-6 */
            { "room B 4x2 east of Crypt gap 2 top\n",    11, 1 },
            { "room C 4x2 east of Crypt gap 7 bottom\n", 16, 5 },
            { "room D 2x2 south of Crypt gap 1 right\n",  7, 8 },
            { "room E 2x2 south of Crypt gap 1 left\n",   1, 8 },
            { "room F 3x1 south of Crypt gap 1\n",        3, 8 },
        };
        for (size_t i = 0; i < sizeof rel / sizeof *rel; i++) {
            t = ctl_ask(&a, rel[i].req);
            CHECK(strncmp(t, "ok\n", 3) == 0);
            if (strncmp(t, "ok\n", 3)) fprintf(stderr, "    %s -> %s", rel[i].req, t);
            free(t);
            char nm[2] = { (char)('A' + i), 0 };
            int k = map_area_find(m, nm);
            CHECK(k >= 0 && m->areas[k].x0 == rel[i].x0 && m->areas[k].y0 == rel[i].y0);
            if (k >= 0 && (m->areas[k].x0 != rel[i].x0 || m->areas[k].y0 != rel[i].y0))
                fprintf(stderr, "    %s at %d,%d\n", nm, m->areas[k].x0, m->areas[k].y0);
        }
        press(&a, "u");                                /* one at a time, back */
    }
    t = ctl_ask(&a, "room G 4x2 north of Crypt\n");
    CHECK(strstr(t, "would run off the north side of the map") != NULL);
    free(t);
    t = ctl_ask(&a, "room G 4x2 east of Crypt top\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    t = ctl_ask(&a, "room H 4x2 north of Crypt gap 0 top\n");
    CHECK(strstr(t, "lines up left, middle or right") != NULL);
    free(t);
    t = ctl_ask(&a, "room I 4x2 east of Nowhere\n");
    CHECK(strstr(t, "no room called Nowhere") != NULL);
    free(t);
    t = ctl_ask(&a, "room C3 4x2 east of Crypt\n");
    CHECK(strstr(t, "not a square") != NULL);
    free(t);

    CASE("door by side: the middle by default, a numbered square, a kind");
    t = ctl_ask(&a, "door Crypt west\ndoor Crypt north 1 window\ndoor Crypt south 8 secret\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    CHECK_EQ(map_vedge(m, 1, 3), EDGE_DOOR_CLOSED);        /* 6 rows: the third */
    CHECK_EQ(map_hedge(m, 1, 1), EDGE_WINDOW);
    CHECK_EQ(map_hedge(m, 8, 7), EDGE_SECRET_CLOSED);
    t = ctl_ask(&a, "door Crypt south 9\n");
    CHECK(strstr(t, "1-8, from the left") != NULL);
    free(t);

    CASE("a name where a region goes is the area's box; where a creature goes, the free square nearest its middle");
    t = ctl_ask(&a, "tile Hall water\ntoken add enemy Crypt \"Ghoul\"\ntoken add player Crypt size 2 \"Ogre\"\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    CHECK(map_tile(m, 1, 11) == TILE_WATER && map_tile(m, 4, 13) == TILE_WATER);
    CHECK(m->tokens.v[0].x == 4 && m->tokens.v[0].y == 3);  /* 8x6 at B2: E4 */
    CHECK(m->tokens.v[1].x >= 1 && m->tokens.v[1].x <= 7 && m->tokens.v[1].y >= 1 && m->tokens.v[1].y <= 5);
    t = ctl_ask(&a, "tile J18 floor\narea Closet J18:J18\ntoken add enemy Closet \"A\"\ntoken add enemy Closet \"B\"\n");
    CHECK(strstr(t, "error: line 4: no room in Closet for a 1x1 creature") != NULL);
    free(t);
    CHECK_EQ(map_area_find(m, "Closet"), -1);               /* rolled back with it */

    CASE("a bare row is not a square; a mistyped room says it is neither");
    t = ctl_ask(&a, "note 5 \"x\"\n");
    CHECK(strstr(t, "5 is not a square on this map") != NULL);
    free(t);
    t = ctl_ask(&a, "token add enemy Cryptt \"X\"\n");
    CHECK(strstr(t, "nor a room's name") != NULL);
    free(t);

    CASE("a note on a room goes on its middle square");
    t = ctl_ask(&a, "note Crypt \"the lid is loose\"\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    CHECK(map_note_at(m, 4, 3) && !strcmp(map_note_at(m, 4, 3), "the lid is loose"));   /* B2:I7: E4 */

    CASE("area NAME REGION names without drawing; area NAME remove takes it off");
    t = ctl_ask(&a, "area Nook J18:K19\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    CHECK_EQ(map_tile(m, 9, 17), TILE_VOID);
    t = ctl_ask(&a, "area Nook off\n");
    CHECK(t && strstr(t, "area Nook remove") != NULL);
    CHECK(map_area_find(m, "Nook") >= 0);
    free(t);
    t = ctl_ask(&a, "area Nook remove\n");
    free(t);
    CHECK_EQ(map_area_find(m, "Nook"), -1);
    t = ctl_ask(&a, "area Nook remove\n");
    CHECK(strstr(t, "no area called Nook") != NULL);
    free(t);

    CASE("describe names rooms by their areas; marked says which area the cursor is in");
    t = ctl_ask(&a, "describe\n");
    CHECK(strstr(t, "room 1 Crypt (B2)") != NULL);
    free(t);
    a.ed.cx = 3; a.ed.cy = 3;
    t = ctl_ask(&a, "marked\n");
    CHECK(strstr(t, "cursor D4, in Crypt\n") != NULL);
    free(t);
    t = ctl_ask(&a, "dump\n");
    CHECK(strstr(t, "\nareas\n  Crypt            B2:I7\n") != NULL);
    free(t);

    CASE(":area names the v box, jumps to one, lists them, takes one off; undoable");
    a.ed.cx = 20; a.ed.cy = 15;
    press(&a, "vll:area Ledge\r");
    int li = map_area_find(m, "Ledge");
    CHECK(li >= 0 && m->areas[li].x0 == 20 && m->areas[li].x1 == 22);
    CHECK_EQ(a.status_gm, 1);
    press(&a, "u");
    CHECK_EQ(map_area_find(m, "Ledge"), -1);
    press(&a, ":area Crypt\r");
    CHECK(a.ed.cx == 1 && a.ed.cy == 1);
    CHECK(strstr(a.status, "jumped to Crypt") != NULL);
    press(&a, ":areas\r");
    CHECK(strstr(a.status, "areas: Crypt B2:I7") != NULL);
    press(&a, ":area Nowhere\r");
    CHECK(strstr(a.status, "no area called Nowhere") != NULL);
    press(&a, ":area Hall off\r");                       /* not an area called "Hall off" */
    CHECK(map_area_find(m, "Hall") >= 0 && map_area_find(m, "Hall off") < 0);
    CHECK(strstr(a.status, ":area Hall remove") != NULL);
    press(&a, ":area Hall remove\r");
    CHECK_EQ(map_area_find(m, "Hall"), -1);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

void test_corridors(void)
{
    Sandbox sb = sandbox_enter("corridor");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */
    char path[700];
    snprintf(path, sizeof path, "%s/void.vtt", sb.dir);
    {
        Map *v = map_new(30, 20, "void");
        char err[200];
        mapio_write(v, path, err, sizeof err);
        map_free(v);
    }
    app_open_map(&a, path);
    Map *m = a.map;
    if (!m) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    char *t;
    t = ctl_ask(&a, "room A B2 4x4\nroom B 4x4 east of A gap 3\nroom C 4x4 south of A gap 4 left\n"
                    "room D 4x4 east of C gap 5\nroom E 2x2 east of B gap 0\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);

    CASE("straight across: floor between, walls along, a door at each end");
    t = ctl_ask(&a, "corridor A B\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    /* A is B2:E5 (x 1-4, y 1-4), B x 8-11: the corridor x 5-7 on the middle row y 2. */
    for (int x = 5; x <= 7; x++) {
        CHECK_EQ(map_tile(m, x, 2), TILE_FLOOR);
        CHECK_EQ(map_hedge(m, x, 2), EDGE_WALL);
        CHECK_EQ(map_hedge(m, x, 3), EDGE_WALL);
    }
    CHECK_EQ(map_vedge(m, 5, 2), EDGE_DOOR_CLOSED);
    CHECK_EQ(map_vedge(m, 8, 2), EDGE_DOOR_CLOSED);

    CASE("straight down, two wide: open ends");
    t = ctl_ask(&a, "corridor A C width 2\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    for (int y = 5; y <= 8; y++) {
        CHECK(map_tile(m, 2, y) == TILE_FLOOR && map_tile(m, 3, y) == TILE_FLOOR);
        CHECK(map_vedge(m, 2, y) == EDGE_WALL && map_vedge(m, 4, y) == EDGE_WALL);
    }
    CHECK(map_hedge(m, 2, 5) == EDGE_NONE && map_hedge(m, 3, 5) == EDGE_NONE);
    CHECK(map_hedge(m, 2, 9) == EDGE_NONE && map_hedge(m, 3, 9) == EDGE_NONE);

    CASE("rooms already sharing a wall: only the doorway");
    int depth = a.undo.depth;
    t = ctl_ask(&a, "corridor B E\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    CHECK_EQ(a.undo.depth, depth + 1);
    int bi = map_area_find(m, "B");
    CHECK_EQ(map_vedge(m, m->areas[bi].x1 + 1, m->areas[map_area_find(m, "E")].y0), EDGE_DOOR_CLOSED);

    CASE("one bend for rooms apart both ways: out of the side, along, and in");
    t = ctl_ask(&a, "room P Q8 2x2\nroom Q X14 2x2\ncorridor P Q\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    if (strncmp(t, "ok\n", 3)) fprintf(stderr, "    %s", t);
    free(t);
    /* P x 16-17 y 7-8, Q x 23-24 y 13-14: along row 7 from x 18 to 23,
     * then down column 23 to row 12. */
    CHECK(map_tile(m, 18, 7) == TILE_FLOOR && map_tile(m, 23, 7) == TILE_FLOOR && map_tile(m, 23, 12) == TILE_FLOOR);
    CHECK_EQ(map_tile(m, 22, 8), TILE_VOID);
    CHECK_EQ(map_vedge(m, 18, 7), EDGE_DOOR_CLOSED);
    CHECK_EQ(map_hedge(m, 23, 13), EDGE_DOOR_CLOSED);
    CHECK(map_hedge(m, 20, 7) == EDGE_WALL && map_hedge(m, 20, 8) == EDGE_WALL);
    CHECK(map_vedge(m, 24, 7) == EDGE_WALL && map_hedge(m, 23, 7) == EDGE_WALL);
    CHECK(map_vedge(m, 23, 10) == EDGE_WALL && map_vedge(m, 24, 10) == EDGE_WALL);
    CHECK_EQ(map_hedge(m, 23, 8), EDGE_NONE);                /* the bend is open inside */

    CASE("when the first way round is blocked, the bend goes the other way");
    t = ctl_ask(&a, "room R B18 2x2\nroom S H14 2x2\ntile C14:G15 floor\ncorridor R S\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    if (strncmp(t, "ok\n", 3)) fprintf(stderr, "    %s", t);
    free(t);

    CASE("refused whole: through ground, through a named room, too little shared, overlapping");
    char *before = ctl_snapshot(m);
    t = ctl_ask(&a, "room G S2 2x2\nroom K 2x2 south of G gap 6\ntile S5:T6 floor\ncorridor G K\n");
    CHECK(strstr(t, "error: line 4: it would cross ground already at") != NULL);
    free(t);
    t = ctl_ask(&a, "room G S2 2x2\nroom K 2x2 south of G gap 6\narea Pit S5:T6\ncorridor G K\n");
    CHECK(strstr(t, "it would cut through Pit at") != NULL);
    free(t);
    t = ctl_ask(&a, "room G S2 2x2\nroom K S9 2x2\ncorridor G K width 3\n");
    CHECK(strstr(t, "share fewer than 3 columns") != NULL);
    free(t);
    t = ctl_ask(&a, "area Big A1:Z5\ncorridor A Big\n");
    CHECK(strstr(t, "overlap") != NULL);
    free(t);
    t = ctl_ask(&a, "corridor A A\n");
    CHECK(strstr(t, "a corridor joins two rooms") != NULL);
    free(t);
    char *after = ctl_snapshot(m);
    free(before);
    free(after);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

void test_corridor_edges(void)
{
    Sandbox sb = sandbox_enter("corridor2");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */
    char path[700];
    snprintf(path, sizeof path, "%s/void.vtt", sb.dir);
    {
        Map *v = map_new(30, 30, "void");
        char err[200];
        mapio_write(v, path, err, sizeof err);
        map_free(v);
    }
    app_open_map(&a, path);
    Map *m = a.map;
    if (!m) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    char *t;

    CASE("a corridor running past a room keeps that room's doors and windows");
    t = ctl_ask(&a, "room Ka B2 3x3\nroom Kb 3x3 east of Ka gap 3\nroom Kc E4 3x1\ndoor Kc north 2\ndoor Kc north 3 window\n"
                    "corridor Ka Kb\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    if (strncmp(t, "ok\n", 3)) fprintf(stderr, "    %s", t);
    free(t);
    {
        int kc = map_area_find(m, "Kc");
        if (kc >= 0) {
            CHECK_EQ(map_hedge(m, m->areas[kc].x0 + 1, m->areas[kc].y0), EDGE_DOOR_CLOSED);
            CHECK_EQ(map_hedge(m, m->areas[kc].x0 + 2, m->areas[kc].y0), EDGE_WINDOW);
        }
    }

    CASE("a bend is never wider than the sides it leaves and enters; the changed area is the corridor's");
    t = ctl_ask(&a, "room Na B10 4x4\nroom Nb L17 1x2\n");     /* Nb is one wide and two tall */
    free(t);
    char *snap = ctl_snapshot(m);
    t = ctl_ask(&a, "corridor Na Nb width 3\n");
    CHECK(strstr(t, "too narrow for a bend 3 wide") != NULL);
    free(t);
    t = ctl_ask(&a, "corridor Na Nb width 2\n");            /* down first: Na's four, Nb's two */
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    press(&a, "u");
    char *snap2 = ctl_snapshot(m);
    CHECK_EQ(strcmp(snap, snap2), 0);
    free(snap); free(snap2);
    t = ctl_ask(&a, "corridor Na Nb\n");
    CHECK(strstr(t, "\nchanged ") != NULL);
    free(t);

    CASE("an area holding both rooms whole is no obstacle, and the rooms keep their own names");
    t = ctl_ask(&a, "room Ha B24 2x2\nroom Hb 2x2 east of Ha gap 3\narea Floor A23:Z27\ncorridor Ha Hb\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    if (strncmp(t, "ok\n", 3)) fprintf(stderr, "    %s", t);
    free(t);
    {
        int ha = map_area_find(m, "Ha");
        CHECK(ha >= 0 && map_area_at(m, m->areas[ha].x0, m->areas[ha].y0) == ha);
    }

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

void test_apply(void)
{
    Sandbox sb = sandbox_enter("apply");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char vtt[] = "./vtt";
    CHECK(access(vtt, X_OK) == 0);
    char plan[700], map[700], cmd[2400];
    snprintf(plan, sizeof plan, "%s/plan.txt", sb.dir);
    snprintf(map, sizeof map, "%s/crypt.vtt", sb.dir);
    FILE *f = fopen(plan, "w");
    fputs("# a crypt\nroom Crypt B2 8x6\nroom Vault 6x4 east of Crypt gap 3\n"
          "room Well 4x3 south of Crypt gap 2 left\ncorridor Crypt Vault\ncorridor Crypt Well width 2\n"
          "door Crypt north middle window\ntoken add enemy Vault \"Ghoul\"\n"
          "token add player Crypt size 2 \"Aria\"\ntile Well water\n", f);
    fclose(f);

    CASE("--apply: a new map from a plan, saved; the result as the golden has it");
    snprintf(cmd, sizeof cmd, "%s '%s' --apply '%s' > /dev/null 2>&1", vtt, map, plan);
    CHECK_EQ(WEXITSTATUS(system(cmd)), 2);                 /* not there, and no --new */
    snprintf(cmd, sizeof cmd, "%s '%s' --apply '%s' --new 30x18 > /dev/null", vtt, map, plan);
    CHECK_EQ(WEXITSTATUS(system(cmd)), 0);
    char err[256];
    Map *m = mapio_load(map, err, sizeof err);
    CHECK(m != NULL);
    if (m) {
        size_t n;
        char *txt = tool_text(m, 0, 0, m->w - 1, m->h - 1, &n);
        golden_bytes("apply-crypt", txt, n);
        free(txt);
        CHECK_EQ(m->nareas, 3);
        map_free(m);
    }

    CASE("docs/AGENTS.md's example plan applies, and checks clean");
    {
        FILE *doc = fopen("docs/AGENTS.md", "r");
        CHECK(doc != NULL);
        char line[512], ex[700];
        snprintf(ex, sizeof ex, "%s/guide.txt", sb.dir);
        FILE *out = fopen(ex, "w");
        int in = 0, lines = 0;
        while (doc && out && fgets(line, sizeof line, doc)) {
            if (!in && !strncmp(line, "   # The drowned crypt", 22)) in = 1;
            else if (in && !strncmp(line, "   ```", 6)) break;
            if (in) { fputs(line + 3, out); lines++; }
        }
        if (doc) fclose(doc);
        if (out) fclose(out);
        CHECK(lines > 5);
        char gm[700];
        snprintf(gm, sizeof gm, "%s/guide.vtt", sb.dir);
        snprintf(cmd, sizeof cmd, "%s '%s' --apply '%s' --new 40x24 > /dev/null", vtt, gm, ex);
        CHECK_EQ(WEXITSTATUS(system(cmd)), 0);
        snprintf(cmd, sizeof cmd, "%s '%s' --check > /dev/null", vtt, gm);
        CHECK_EQ(WEXITSTATUS(system(cmd)), 0);
    }

    CASE("--apply over a newer autosave works on the file as saved, and says so");
    {
        char as[800], ep[700];
        snprintf(as, sizeof as, "%s.autosave", map);
        snprintf(cmd, sizeof cmd, "cp '%s' '%s' && touch -d '+1 minute' '%s'", map, as, as);
        CHECK_EQ(system(cmd), 0);
        snprintf(ep, sizeof ep, "%s/one.txt", sb.dir);
        f = fopen(ep, "w");
        fputs("note A1 \"x\"\n", f);
        fclose(f);
        snprintf(cmd, sizeof cmd, "%s '%s' --apply '%s' > /dev/null 2>&1", vtt, map, ep);
        CHECK_EQ(WEXITSTATUS(system(cmd)), 0);
        unlink(as);
    }

    CASE("--new with a failing plan leaves no file behind");
    {
        char nm[700], bp[700];
        snprintf(nm, sizeof nm, "%s/never.vtt", sb.dir);
        snprintf(bp, sizeof bp, "%s/bad.txt", sb.dir);
        f = fopen(bp, "w");
        fputs("room A Z99 2x2\n", f);
        fclose(f);
        snprintf(cmd, sizeof cmd, "%s '%s' --apply '%s' --new 10x10 > /dev/null 2>&1", vtt, nm, bp);
        CHECK_EQ(WEXITSTATUS(system(cmd)), 1);
        CHECK(access(nm, F_OK) != 0);
    }

    CASE("--apply: a failing plan changes nothing and saves nothing (exit 1)");
    struct stat st0, st1;
    stat(map, &st0);
    f = fopen(plan, "w");
    fputs("tile Crypt hazard\ntoken add enemy Z99 \"X\"\n", f);
    fclose(f);
    snprintf(cmd, sizeof cmd, "%s '%s' --apply '%s' > /dev/null 2>&1", vtt, map, plan);
    CHECK_EQ(WEXITSTATUS(system(cmd)), 1);
    stat(map, &st1);
    CHECK(st0.st_mtime == st1.st_mtime && st0.st_size == st1.st_size);
    m = mapio_load(map, err, sizeof err);
    CHECK(m && map_tile(m, 1, 1) == TILE_FLOOR);
    map_free(m);

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", sb.dir);
    sandbox_leave(&sb);
    if (system(cmd) != 0) { }
}

void test_ctl_live(void)
{
    Sandbox sb = sandbox_enter("ctl");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char saved_rt[1024] = "";
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt) str_lcpy(saved_rt, rt, sizeof saved_rt);
    setenv("XDG_RUNTIME_DIR", sb.dir, 1);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    app_open_map(&a, "tests/fixtures/two-rooms.vtt");

    CASE("a directory open to others is refused, and nothing listens");
    char dir[600];
    snprintf(dir, sizeof dir, "%s/vtt", sb.dir);
    mkdir(dir, 0755);
    chmod(dir, 0755);
    press(&a, ":agent on\r");
    CHECK(strstr(a.status, "not this user's alone") != NULL);
    CHECK_EQ(ctl_active(&a.ctl), 0);
    chmod(dir, 0700);

    CASE(":agent on listens at <dir>/<pid>.sock, only for this user");
    press(&a, ":agent on\r");
    CHECK_EQ(ctl_active(&a.ctl), 1);
    char want[700];
    snprintf(want, sizeof want, "%s/%ld.sock", dir, (long)getpid());
    CHECK_EQ(strcmp(a.ctl.path, want), 0);
    struct stat st;
    CHECK(stat(want, &st) == 0 && S_ISSOCK(st.st_mode) && (st.st_mode & 077) == 0);
    CHECK(strstr(a.status, "agent channel on") != NULL);

    CASE("a request over the socket: written, shut, answered, closed");
    {
        CtlReader rd = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
        CHECK(rd.fd >= 0);
        CHECK(write(rd.fd, "status\n", 7) == 7);
        shutdown(rd.fd, SHUT_WR);
        ctl_pump(&a, ctl_read_some, &rd);
        CHECK_EQ(rd.done, 1);
        CHECK(strncmp(rd.buf, "ok\nmap Two Rooms", 16) == 0);
        close(rd.fd);
        CHECK_EQ(a.ctl.requests, 1u);
        CHECK_EQ(a.ctl.nc, 0);
    }

    CASE("an answer too big for the socket's buffer goes out in pieces");
    {
        CtlReader rd = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
        /* Forty dumps of the whole map: far past what one send takes. */
        char req[400] = "";
        for (int i = 0; i < 40; i++) strcat(req, "dump\n");
        CHECK(write(rd.fd, req, strlen(req)) == (ssize_t)strlen(req));
        shutdown(rd.fd, SHUT_WR);
        size_t total = 0;
        int    got_ok = 0, spins = 0;
        while (spins++ < 400) {
            struct pollfd fds[1 + CTL_SLOTS];
            int n = ctl_pollfds(&a.ctl, fds, 1 + CTL_SLOTS);
            poll(fds, (nfds_t)n, 5);
            uint64_t now = prof_now_ns() / 1000000u;
            ctl_service(&a.ctl, fds, n, now);
            app_tick(&a, now);
            char chunk[65536];
            ssize_t k;
            while ((k = recv(rd.fd, chunk, sizeof chunk, MSG_DONTWAIT)) > 0) {
                if (!total) got_ok = !strncmp(chunk, "ok\n", 3);
                total += (size_t)k;
            }
            if (k == 0) break;
        }
        CHECK_EQ(got_ok, 1);
        CHECK(total > 40000);
        close(rd.fd);
        CHECK_EQ(a.ctl.nc, 0);
    }

    CASE("a request over 64 KB is answered with why, not run");
    {
        CtlReader rd = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
        fcntl(rd.fd, F_SETFL, fcntl(rd.fd, F_GETFL) | O_NONBLOCK);
        char *big = malloc(CTL_REQ_CAP + 100);
        memset(big, '#', CTL_REQ_CAP + 100);
        /* Written as the server takes it: the socket buffer is smaller
         * than the request. */
        size_t off = 0;
        for (int spin = 0; spin < 400 && off < CTL_REQ_CAP + 100; spin++) {
            ssize_t k = write(rd.fd, big + off, CTL_REQ_CAP + 100 - off);
            if (k > 0) off += (size_t)k;
            struct pollfd fds[1 + CTL_SLOTS];
            int n = ctl_pollfds(&a.ctl, fds, 1 + CTL_SLOTS);
            poll(fds, (nfds_t)n, 5);
            ctl_service(&a.ctl, fds, n, prof_now_ns() / 1000000u);
        }
        free(big);
        CHECK_EQ(off, (size_t)CTL_REQ_CAP + 100);    /* the server kept reading */
        shutdown(rd.fd, SHUT_WR);
        fcntl(rd.fd, F_SETFL, fcntl(rd.fd, F_GETFL) & ~O_NONBLOCK);
        ctl_pump(&a, ctl_read_some, &rd);
        CHECK_EQ(strcmp(rd.buf, "error: the request is over 64 KB\n"), 0);
        CHECK_EQ(rd.reset, 0);
        close(rd.fd);
    }

    CASE("past four connections, the next is closed at once; a silent one is dropped at the deadline");
    {
        int fd[CTL_MAX_CONN + 1];
        for (int i = 0; i <= CTL_MAX_CONN; i++) fd[i] = ctl_raw_connect(a.ctl.path);
        uint32_t dropped = a.ctl.dropped;
        struct pollfd fds[1 + CTL_SLOTS];
        int n = ctl_pollfds(&a.ctl, fds, 1 + CTL_SLOTS);
        poll(fds, (nfds_t)n, 5);
        uint64_t now = prof_now_ns() / 1000000u;
        ctl_service(&a.ctl, fds, n, now);
        CHECK_EQ(a.ctl.nc, CTL_MAX_CONN);
        CHECK_EQ(a.ctl.dropped, dropped + 1);
        CHECK(ctl_due(&a.ctl, now) > CTL_TIMEOUT_MS - 1000);
        /* Nothing ready; only the clock has moved. */
        memset(fds, 0, sizeof fds);
        fds[0].fd = a.ctl.listen_fd;
        ctl_service(&a.ctl, fds, 1, now + CTL_TIMEOUT_MS);
        CHECK_EQ(a.ctl.nc, 0);
        CHECK_EQ(ctl_due(&a.ctl, now), -1);
        for (int i = 0; i <= CTL_MAX_CONN; i++) close(fd[i]);
    }

    CASE("vtt --ctl: finds the one vtt, prints the report, exits 0; 1 on an error");
    {
        char out[700];
        snprintf(out, sizeof out, "%s/ctl-out.txt", sb.dir);
        for (int round = 0; round < 2; round++) {
            fflush(stdout);
            pid_t pid = fork();
            if (pid == 0) {
                int o = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                dup2(o, 1);
                dup2(o, 2);
                int rc = ctl_client_main(round ? "bogus" : "status", 0);
                fflush(stdout);
                _exit(rc);
            }
            int stc[3] = { (int)pid, 0, 0 };
            ctl_pump(&a, ctl_child_done, stc);
            CHECK_EQ(stc[1], 1);
            CHECK(WIFEXITED(stc[2]) && WEXITSTATUS(stc[2]) == round);
            FILE *f = fopen(out, "r");
            char  text[512] = "";
            size_t k = f ? fread(text, 1, sizeof text - 1, f) : 0;
            text[k] = '\0';
            if (f) fclose(f);
            if (!round) CHECK(strncmp(text, "map Two Rooms", 13) == 0);
            else        CHECK(strstr(text, "vtt: error: line 1: unknown request bogus") != NULL);
        }
        unlink(out);
    }

    CASE("vtt --ctl with a request over 64 KB, or far past the socket's buffer: the answer says why, exit 1");
    for (int round = 0; round < 2; round++) {
        size_t big = round ? 300 * 1024 : CTL_REQ_CAP + 5000;
        char  *req = malloc(big + 1);
        memset(req, '#', big);
        req[big] = '\0';
        char out[700];
        snprintf(out, sizeof out, "%s/ctl-big.txt", sb.dir);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            int o = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            dup2(o, 2);
            int rc = ctl_client_main(req, 0);
            _exit(rc);
        }
        int stc[3] = { (int)pid, 0, 0 };
        ctl_pump(&a, ctl_child_done, stc);
        CHECK(WIFEXITED(stc[2]) && WEXITSTATUS(stc[2]) == 1);
        FILE *f = fopen(out, "r");
        char  text[256] = "";
        size_t k = f ? fread(text, 1, sizeof text - 1, f) : 0;
        text[k] = '\0';
        if (f) fclose(f);
        CHECK(strstr(text, "the request is over 64 KB") != NULL);
        unlink(out);
        free(req);
    }

    CASE("--ctl will not talk through a directory that is not this user's alone");
    {
        chmod(dir, 0755);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            int o = open("/dev/null", O_WRONLY);
            dup2(o, 2);
            _exit(ctl_client_main("status", 0));
        }
        int stc = 0;
        waitpid(pid, &stc, 0);
        CHECK(WIFEXITED(stc) && WEXITSTATUS(stc) == 2);
        CHECK(access(want, F_OK) == 0);            /* and removed nothing */
        chmod(dir, 0700);
    }

    CASE("wait: held with nothing to hear, outside the ten-second deadline, and the loop does not spin on it");
    {
        CtlReader rd = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
        CHECK(write(rd.fd, "wait for 600\n", 13) == 13);
        shutdown(rd.fd, SHUT_WR);
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 1 });
        for (int k = 0; k < 20; k++) {                     /* twenty turns of the loop */
            struct pollfd pf[1 + CTL_SLOTS];
            int pn = ctl_pollfds(&a.ctl, pf, 1 + CTL_SLOTS);
            poll(pf, (nfds_t)pn, 2);
            ctl_service(&a.ctl, pf, pn, prof_now_ns() / 1000000u);
            app_tick(&a, prof_now_ns() / 1000000u);
        }
        CHECK_EQ(ctl_read_some(&rd), 0);                   /* held: no answer */
        CHECK_EQ(ctl_waiters(&a.ctl), 1);
        uint64_t now = prof_now_ns() / 1000000u;
        CHECK(ctl_due(&a.ctl, now) > 500000);              /* its own deadline, not 0 */
        struct pollfd fds[1 + CTL_SLOTS];
        memset(fds, 0, sizeof fds);
        fds[0].fd = a.ctl.listen_fd;
        ctl_service(&a.ctl, fds, 1, now + 2 * CTL_TIMEOUT_MS);   /* the sweep leaves it */
        CHECK_EQ(ctl_waiters(&a.ctl), 1);

        CASE("a verdict reaches the waiter after the key, with no further key or wake");
        app_key(&a, (Key){ KEY_F1, 0, 0 });
        press(&a, ":ask a well\r");
        app_events_flush(&a, prof_now_ns() / 1000000u);    /* what main does after keys */
        for (int k = 0; k < 50 && !ctl_read_some(&rd); k++) poll(NULL, 0, 2);
        CHECK_EQ(rd.done, 1);
        CHECK(strncmp(rd.buf, "ok\nseq 1\n1 job 1 asked: a well\n", 31) == 0);
        close(rd.fd);
        CHECK_EQ(ctl_waiters(&a.ctl), 0);
    }

    CASE("wait SEQ answers at once when something has happened since; its time runs out with only seq");
    {
        CtlReader rd = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
        CHECK(write(rd.fd, "wait 0\n", 7) == 7);
        shutdown(rd.fd, SHUT_WR);
        ctl_pump(&a, ctl_read_some, &rd);
        CHECK(rd.done && strstr(rd.buf, "1 job 1 asked: a well\n"));
        close(rd.fd);
        CtlReader r2 = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
        CHECK(write(r2.fd, "wait 1 for 1\n", 13) == 13);
        shutdown(r2.fd, SHUT_WR);
        ctl_pump(&a, ctl_read_some, &r2);                  /* two seconds: its one is up */
        CHECK(r2.done && !strcmp(r2.buf, "ok\nseq 1\n"));
        close(r2.fd);
    }

    CASE("a waiter that hangs up frees its place; four wait while four more are served; a fifth is told");
    {
        int w[CTL_MAX_WAIT + 1];
        for (int i = 0; i < CTL_MAX_WAIT; i++) {
            w[i] = ctl_raw_connect(a.ctl.path);
            CHECK(write(w[i], "wait for 600\n", 13) == 13);
            shutdown(w[i], SHUT_WR);
        }
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, CTL_MAX_WAIT });
        CHECK_EQ(ctl_waiters(&a.ctl), CTL_MAX_WAIT);
        CtlReader rd = { ctl_raw_connect(a.ctl.path), "", 0, 0 };   /* a request beside them */
        CHECK(write(rd.fd, "status\n", 7) == 7);
        shutdown(rd.fd, SHUT_WR);
        ctl_pump(&a, ctl_read_some, &rd);
        CHECK(rd.done && !strncmp(rd.buf, "ok\nmap ", 7));
        close(rd.fd);
        CtlReader r5 = { ctl_raw_connect(a.ctl.path), "", 0, 0 };   /* a fifth waiter */
        CHECK(write(r5.fd, "wait for 600\n", 13) == 13);
        shutdown(r5.fd, SHUT_WR);
        ctl_pump(&a, ctl_read_some, &r5);
        CHECK(r5.done && !strncmp(r5.buf, "error: as many agents", 21));
        close(r5.fd);
        close(w[0]);                                       /* one hangs up */
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, CTL_MAX_WAIT - 1 });
        CHECK_EQ(ctl_waiters(&a.ctl), CTL_MAX_WAIT - 1);
        for (int i = 1; i < CTL_MAX_WAIT; i++) close(w[i]);
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 0 });
        CHECK_EQ(ctl_waiters(&a.ctl), 0);
        CHECK_EQ(a.ctl.nc, 0);
    }
    app_jobs_clear(&a);

    CASE(":agent off: the socket stays (an --apply must find the map), a held wait stays, the checkpoint stops");
    CtlReader held = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
    CHECK(write(held.fd, "wait for 600\n", 13) == 13);
    shutdown(held.fd, SHUT_WR);
    ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 1 });
    CHECK(a.agent_seen_ms != 0);
    press(&a, ":agent off\r");
    CHECK_EQ(a.agent_on, 0);
    CHECK_EQ(ctl_active(&a.ctl), 1);
    CHECK(access(want, F_OK) == 0);
    CHECK(strstr(a.status, "agent channel off - a proposal") != NULL);
    CHECK_EQ(a.agent_seen_ms, 0u);
    app_tick(&a, prof_now_ns() / 1000000u);
    CHECK(a.map->cp == NULL);                          /* nothing of the map is told */
    CHECK_EQ(ctl_waiters(&a.ctl), 1);
    press(&a, ":agent off\r");
    CHECK(strstr(a.status, "already off") != NULL);

    CASE("off, over the socket: status, holds, wait and proposals; no read of the map, nothing at once");
    {
        char *t = sock_ask(&a, "dump\n");
        CHECK(t && !strcmp(t, "error: line 1: the agent channel is off - only proposals, status and wait are taken (:agent on)\n"));
        free(t);
        static const char *const REFUSED[] = { "jobs\n", "marked\n", "describe\n", "undo\n", "job 1 take\n",
                                               "scene save Now\n", "scene Start remove\n", "scene diff Start\n",
                                               "tile A1 water\ndump A1\n" };
        for (size_t k = 0; k < sizeof REFUSED / sizeof *REFUSED; k++) {
            t = sock_ask(&a, REFUSED[k]);
            CHECK(t && strstr(t, ": the agent channel is off"));
            free(t);
        }
        t = sock_ask(&a, "scene Nope\n");                  /* putting one back is a proposal */
        CHECK(t && strstr(t, "no scene called Nope"));
        free(t);
        CHECK_EQ(a.jobs[0].used, 0);
        t = sock_ask(&a, "status\n");
        CHECK(t && !strncmp(t, "ok\n", 3) && strstr(t, "\nagent off - proposals, status and wait only\n"));
        free(t);
        struct stat fs;
        char req[96];
        CHECK(stat("tests/fixtures/two-rooms.vtt", &fs) == 0);
        snprintf(req, sizeof req, "holds %llu %llu\n", (unsigned long long)fs.st_dev, (unsigned long long)fs.st_ino);
        t = sock_ask(&a, req);
        CHECK(t && !strcmp(t, "ok\nholds\n"));
        free(t);
        t = sock_ask(&a, "holds 1 2\n");
        CHECK(t && !strcmp(t, "ok\nno\n"));
        free(t);
        t = sock_ask(&a, "holds here\n");
        CHECK(t && strstr(t, "holds DEVICE INODE"));
        free(t);
        a.ctl_auto = 1;                                    /* at once is :agent on's */
        int was = map_tile(a.map, 1, 1);
        t = sock_ask(&a, "propose apply \"plan.txt\"\ntile B2 hazard\n");
        CHECK(t && !strncmp(t, "ok\nproposal #", 13) && strstr(t, "\nwaiting for the GM's review"));
        free(t);
        a.ctl_auto = 0;
        CHECK_EQ(map_tile(a.map, 1, 1), was);
        int found = 0;
        for (int i = 0; i < JOB_MAX; i++)
            found += a.jobs[i].used && a.jobs[i].from == JOB_FROM_APPLY && !strcmp(a.jobs[i].text, "--apply plan.txt");
        CHECK_EQ(found, 1);
        app_jobs_clear(&a);
    }

    CASE("--ctl finds no vtt with its channel on, though one listens (exit 2)");
    {
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            int o = open("/dev/null", O_WRONLY);
            dup2(o, 2);
            _exit(ctl_client_main("status", 0));
        }
        int stc[3] = { (int)pid, 0, 0 };
        ctl_pump(&a, ctl_child_done, stc);
        CHECK(stc[1] && WIFEXITED(stc[2]) && WEXITSTATUS(stc[2]) == 2);
    }
    close(held.fd);
    ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 0 });

    CASE("--apply to a map open in a vtt: a proposal there, the file untouched; --wait gives the GM's verdict");
    {
        const char *file = "tests/fixtures/two-rooms.vtt";
        struct stat f0, f1;
        stat(file, &f0);
        char out[700];
        snprintf(out, sizeof out, "%s/apply-out.txt", sb.dir);
        /* What the GM does with it, and what --apply --wait 20 then says. */
        static const struct { const char *plan, *keys, *more; int wait, rc; const char *says; } R[] = {
            { "tile B2 hazard\n", "\r",  NULL,          20, 0, "job 1 accepted: ground in B2, not saved\n" },
            { "tile B3 hazard\n", "d",   NULL,          20, 4, "job 1 scrapped\n" },
            { "tile B4 hazard\n", "c",   "not there\r", 20, 5, "not there\n" },
            { "tile B5 hazard\n", NULL,  NULL,           0, 3, "no verdict in 0 seconds - proposal #1 still waits for the GM\n" },
            { "tile B6 hazard\n", NULL,  NULL,          -1, 0, "waiting for the GM's review - :review 1\n" },
            { "tile B2 bogus\n",  NULL,  NULL,          20, 1, "" },
        };
        for (size_t k = 0; k < sizeof R / sizeof *R; k++) {
            app_jobs_clear(&a);
            fflush(stdout);
            pid_t pid = fork();
            if (pid == 0) {
                int o = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                dup2(o, 1);
                int e = open("/dev/null", O_WRONLY);
                dup2(e, 2);
                int rc = ctl_apply_open(file, "plan.txt", R[k].plan, strlen(R[k].plan), R[k].wait);
                fflush(stdout);
                _exit(rc < 0 ? 99 : rc);
            }
            int stc[3] = { (int)pid, 0, 0 };
            if (R[k].keys) {
                ctl_pump(&a, a_job_ready, &a);
                CHECK_EQ(a.jobs[0].state, JOB_READY);
                CHECK_EQ(a.jobs[0].from, JOB_FROM_APPLY);
                press(&a, ":review\r");
                press(&a, R[k].keys);
                if (R[k].more) press(&a, R[k].more);
                app_events_flush(&a, prof_now_ns() / 1000000u);   /* what main does after keys */
            }
            ctl_pump(&a, ctl_child_done, stc);
            CHECK_EQ(stc[1], 1);
            CHECK(WIFEXITED(stc[2]) && WEXITSTATUS(stc[2]) == R[k].rc);
            FILE *f = fopen(out, "r");
            char  text[512] = "";
            size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
            text[n] = '\0';
            if (f) fclose(f);
            size_t sl = strlen(R[k].says);
            CHECK(n >= sl && !strcmp(text + n - sl, R[k].says));
            if (R[k].rc == 0 && R[k].keys) press(&a, "u");        /* the map as it was */
        }
        stat(file, &f1);
        CHECK(f0.st_mtime == f1.st_mtime && f0.st_size == f1.st_size && f0.st_ino == f1.st_ino);
        app_jobs_clear(&a);

        CASE("--apply to a file no vtt has open is the file's, as before: nobody holds it");
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) _exit(ctl_apply_open("tests/fixtures/kinds.vtt", "plan.txt", "tile B2 hazard\n", 15, -1) < 0 ? 0 : 1);
        int stc[3] = { (int)pid, 0, 0 };
        ctl_pump(&a, ctl_child_done, stc);
        CHECK(stc[1] && WIFEXITED(stc[2]) && WEXITSTATUS(stc[2]) == 0);
    }
    ctl_stop(&a.ctl);                                  /* the cases below want nobody listening */
    CHECK(access(want, F_OK) != 0);

    CASE("a vtt that is there and does not answer may have the map open: --apply does nothing (exit 2)");
    {
        /* A stopped vtt, or one whose GM is in $EDITOR: the connect is
         * taken by the backlog and nobody ever answers. */
        char stub[700];
        snprintf(stub, sizeof stub, "%s/4242.sock", dir);
        int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un ssa;
        memset(&ssa, 0, sizeof ssa);
        ssa.sun_family = AF_UNIX;
        str_lcpy(ssa.sun_path, stub, sizeof ssa.sun_path);
        CHECK(bind(lfd, (struct sockaddr *)&ssa, sizeof ssa) == 0 && listen(lfd, 4) == 0);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            int e = open("/dev/null", O_WRONLY);
            dup2(e, 2);
            _exit(ctl_apply_open("tests/fixtures/kinds.vtt", "plan.txt", "tile B2 hazard\n", 15, -1) & 0xff);
        }
        int stc = 0;
        waitpid(pid, &stc, 0);
        CHECK(WIFEXITED(stc) && WEXITSTATUS(stc) == 2);    /* not 255: "nobody holds it, write the file" */
        close(lfd);
        unlink(stub);
    }

    CASE("--wait reads a verdict for its own job only: job 1 is not job 12, feedback is not a scrap");
    {
        unsigned seq = 0;
        char line[200];
        CHECK_EQ(ctl_verdict_in("ok\nseq 9\n8 job 12 accepted: ground in B2\n9 map changed: ground in C3\n", 1, &seq, line, sizeof line), -1);
        CHECK_EQ(seq, 9u);
        CHECK_EQ(ctl_verdict_in("ok\nseq 9\n8 job 12 accepted: ground in B2\n", 12, &seq, line, sizeof line), 0);
        CHECK(!strcmp(line, "job 12 accepted: ground in B2"));
        CHECK_EQ(ctl_verdict_in("ok\nseq 3\n3 job 1 accepted in part, B2: ground in B2 - the rest waits\n", 1, &seq, line, sizeof line), 0);
        CHECK_EQ(ctl_verdict_in("ok\nseq 3\n3 job 1 feedback: like job 1 scrapped, but better\n", 1, &seq, line, sizeof line), 5);
        CHECK(!strcmp(line, "like job 1 scrapped, but better"));
        CHECK_EQ(ctl_verdict_in("ok\nseq 3\n3 job 1 scrapped\n", 1, &seq, line, sizeof line), 4);
        CHECK_EQ(ctl_verdict_in("ok\nseq 3\n3 job 1 removed by the GM\n", 1, &seq, line, sizeof line), 4);
        CHECK_EQ(ctl_verdict_in("ok\nseq 3\n3 map closed: Blank - its jobs went with it\n", 1, &seq, line, sizeof line), 4);
        CHECK_EQ(ctl_verdict_in("ok\nseq 3\n", 1, &seq, line, sizeof line), -1);
    }

    CASE("a socket file nobody answers on is a crashed vtt's, and --ctl removes it");
    {
        char stale[700];
        snprintf(stale, sizeof stale, "%s/99999999.sock", dir);
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un sa;
        memset(&sa, 0, sizeof sa);
        sa.sun_family = AF_UNIX;
        str_lcpy(sa.sun_path, stale, sizeof sa.sun_path);
        CHECK(bind(fd, (struct sockaddr *)&sa, sizeof sa) == 0);
        close(fd);                             /* bound, never listened: refused */
        CHECK(access(stale, F_OK) == 0);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            int o = open("/dev/null", O_WRONLY);
            dup2(o, 2);
            _exit(ctl_client_main("status", 0));
        }
        int stc = 0;
        waitpid(pid, &stc, 0);
        CHECK(WIFEXITED(stc) && WEXITSTATUS(stc) == 2);
        CHECK(access(stale, F_OK) != 0);
    }

    app_free(&a);
    rnd_free(&r);
    rmdir(dir);
    if (saved_rt[0]) setenv("XDG_RUNTIME_DIR", saved_rt, 1);
    else             unsetenv("XDG_RUNTIME_DIR");
    sandbox_leave(&sb);
    rmdir(sb.dir);
}

/* ------------------------------------------------ characters on the channel */

void test_ctl_characters(void)
{
    Sandbox sb = sandbox_enter("ctlchars");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */
    CHECK(ctl_blank_map(&a, sb.dir, 12, 8));
    if (!a.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.map;
    char err[160];

    CASE("characters with none saved");
    char *t = ctl_ask(&a, "characters");
    CHECK(t && strstr(t, "ok\nno characters\n") == t);
    free(t);

    /* A template to place: a 2x2 Wight with HP and a roll. */
    Token w;
    memset(&w, 0, sizeof w);
    w.kind = TOKEN_ENEMY; w.size = 2;
    str_lcpy(w.label, "Wight", sizeof w.label);
    w.ncounters = 1;
    str_lcpy(w.counters[0].name, "HP", sizeof w.counters[0].name);
    w.counters[0].value = w.counters[0].max = 9;
    tokens_add(&m->tokens, w);
    str_lcpy(m->rolls[0].name, "drain", sizeof m->rolls[0].name);
    str_lcpy(m->rolls[0].expr, "1d10", sizeof m->rolls[0].expr);
    const char *rolls[] = { "drain" };
    CHECK(character_save(m, 0, "wight", rolls, 1, err, sizeof err) == 0);
    w.label[0] = '\0';
    tokens_add(&m->tokens, w);
    CHECK(character_save(m, 1, "nameless", NULL, 0, err, sizeof err) == 0);
    tokens_free(&m->tokens);
    memset(m->rolls, 0, sizeof m->rolls);

    CASE("characters lists name, label, side, size, counters and rolls");
    t = ctl_ask(&a, "characters");
    CHECK(t && strstr(t, "wight  \"Wight\" enemy 2x2  HP 9  rolls drain = 1d10\n") != NULL);
    CHECK(t && strstr(t, "nameless  \"\" enemy 2x2") != NULL);
    free(t);

    CASE("token add ... from NAME places it, numbered, says the label, adds the roll; one undo step");
    tokens_add(&m->tokens, (Token){ .x = 8, .y = 0, .size = 1, .kind = TOKEN_PLAYER, .label = "Wight" });
    int depth = a.undo.depth;
    t = ctl_ask(&a, "token add player B2 from wight hidden");
    CHECK(t && strstr(t, "ok\n") == t);
    CHECK(t && strstr(t, "placed \"Wight 2\" at B2\n") != NULL);
    free(t);
    CHECK_EQ(m->tokens.n, 2);
    const Token *p = &m->tokens.v[1];
    CHECK(p->kind == TOKEN_PLAYER && p->size == 2 && p->hidden && p->x == 1 && p->y == 1);
    CHECK(!strcmp(m->rolls[0].name, "drain"));
    CHECK_EQ(a.undo.depth, depth + 1);
    t = ctl_ask(&a, "undo");
    free(t);
    CHECK_EQ(m->tokens.n, 1);
    CHECK(!m->rolls[0].name[0]);

    CASE("a request that fails later takes back the creature and its roll");
    t = ctl_ask(&a, "token add enemy B2 from wight\ntile Z99 water");
    CHECK(t && strncmp(t, "error: line 2", 13) == 0);
    free(t);
    CHECK_EQ(m->tokens.n, 1);
    CHECK(!m->rolls[0].name[0]);

    CASE("refusals: no such character, no label, no room, a bad word");
    t = ctl_ask(&a, "token add enemy B2 from ghast");
    CHECK(t && strstr(t, "no character called ghast") != NULL);
    free(t);
    t = ctl_ask(&a, "token add enemy B2 from nameless");
    CHECK(t && strstr(t, "has no label") != NULL);
    free(t);
    t = ctl_ask(&a, "token add enemy L8 from wight");
    CHECK(t && strncmp(t, "error:", 6) == 0 && strstr(t, "L8") != NULL);
    free(t);
    t = ctl_ask(&a, "token add enemy B2 from wight loud");
    CHECK(t && strstr(t, "from NAME [hidden]") != NULL);
    free(t);
    CHECK_EQ(m->tokens.n, 1);

    CASE("into a named area: the free square nearest its middle");
    t = ctl_ask(&a, "area Hall C3:H6\ntoken add enemy Hall from wight");
    CHECK(t && strstr(t, "ok\n") == t);
    CHECK(t && strstr(t, "placed \"Wight 2\" at E4") != NULL);
    free(t);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* ---------------------------------------------------- scenes on the channel */

void test_ctl_scenes(void)
{
    Sandbox sb = sandbox_enter("ctlscenes");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */
    CHECK(ctl_blank_map(&a, sb.dir, 12, 8));
    if (!a.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.map;
    char *t = ctl_ask(&a, "token add player B2 \"Aria\"\ntoken add enemy F6 size 2 \"Ogre\"");
    free(t);

    CASE("scenes with none; scene save alone saves, and with a region only those in it");
    t = ctl_ask(&a, "scenes");
    CHECK(t && strstr(t, "no scenes") != NULL);
    free(t);
    t = ctl_ask(&a, "scene save \"Start\"");
    CHECK(t && strstr(t, "ok\nsaved scene \"Start\": 2 creatures\n") == t);
    free(t);
    t = ctl_ask(&a, "area Den E5:H8\nscene save Den Den");
    CHECK(t && strstr(t, "error: scene save goes in a request of its own") != NULL);
    free(t);
    t = ctl_ask(&a, "area Den E5:H8");
    free(t);
    t = ctl_ask(&a, "scene save Den Den");
    CHECK(t && strstr(t, "saved scene \"Den\": 1 creature, E5:H8") != NULL);
    free(t);
    t = ctl_ask(&a, "scenes");
    CHECK(t && strstr(t, "\"Start\"  2 creatures\n\"Den\"  1 creature, E5:H8\n") != NULL);
    free(t);

    CASE("scene diff reads what changed, anywhere; no changes when nothing did");
    t = ctl_ask(&a, "scene diff Start");
    CHECK(t && strstr(t, "ok\nno changes\n") == t);
    free(t);
    t = ctl_ask(&a, "token move Aria D4\ntoken set Ogre hidden on");
    free(t);
    t = ctl_ask(&a, "scene diff start");
    CHECK(t && strstr(t, "moved \"Aria\" B2 -> D4\n") != NULL);
    CHECK(t && strstr(t, "changed \"Ogre\": hidden yes (was no)\n") != NULL);
    free(t);

    CASE("scene NAME puts it back as an edit, all or nothing with the request");
    t = ctl_ask(&a, "scene Start\ntile Z99 water");
    CHECK(t && strncmp(t, "error: line 2", 13) == 0);
    free(t);
    CHECK_EQ(m->tokens.v[0].x, 3);                         /* rolled back: Aria still at D4 */
    t = ctl_ask(&a, "scene Start");
    CHECK(t && strstr(t, "ok\n") == t);
    free(t);
    CHECK_EQ(m->tokens.v[0].x, 1);
    CHECK(!m->tokens.v[1].hidden);
    t = ctl_ask(&a, "undo");
    free(t);
    CHECK_EQ(m->tokens.v[0].x, 3);

    CASE("remove goes alone too; off says what to type; bad forms say the forms");
    t = ctl_ask(&a, "scene Den off");
    CHECK(t && strstr(t, "scene Den remove") != NULL && m->nscenes == 2);
    free(t);
    t = ctl_ask(&a, "scene Den remove");
    CHECK(t && strstr(t, "removed scene \"Den\"") != NULL && m->nscenes == 1);
    free(t);
    t = ctl_ask(&a, "scene diff Den");
    CHECK(t && strstr(t, "no scene called Den") != NULL);
    free(t);
    t = ctl_ask(&a, "scene save");
    CHECK(t && strstr(t, "scene save NAME [REGION]") != NULL);
    free(t);
    t = ctl_ask(&a, "scene");
    CHECK(t && strstr(t, "scene save NAME [REGION]") != NULL);
    free(t);
    t = ctl_ask(&a, "scene diff");
    CHECK(t && strstr(t, "scene diff NAME") != NULL);
    free(t);

    CASE("in play mode: reads work, saving is refused, putting back waits as any edit does");
    app_key(&a, (Key){ KEY_F2, 0, 0 });
    t = ctl_ask(&a, "scene diff Start");
    CHECK(t && strncmp(t, "ok", 2) == 0);
    free(t);
    t = ctl_ask(&a, "scene save Late");
    CHECK(t && strncmp(t, "busy:", 5) == 0);
    free(t);
    t = ctl_ask(&a, "scene Start");
    CHECK(t && strncmp(t, "ok\n", 3) == 0 && (strstr(t, "\nlands when the GM is back: the GM is in play mode") || strstr(t, "no change")));
    free(t);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}
