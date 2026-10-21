/* Tests: the control channel, the room language, corridors, --apply. */

#include "link.h"
#include "card.h"
#include "harness.h"
#include "app_priv.h"

void test_ctl(void)
{
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    a.world.ctl_auto = 1;                       /* edits land at once (decision 6) */

    CASE("with no map open, status answers and everything else says why not");
    char *t = ctl_ask(&a, "status\n");
    CHECK(strncmp(t, "ok\nmap none open\n", 17) == 0);
    CHECK(strstr(t, "edits not now: no map is open") != NULL);
    free(t);
    t = ctl_ask(&a, "dump\n");
    CHECK_EQ(strcmp(t, "error state: line 1: no map is open\n"), 0);
    free(t);

    app_open_map(&a, "tests/fixtures/two-rooms.vtt");
    CHECK(a.world.map != NULL);
    if (!a.world.map) { app_free(&a); rnd_free(&r); return; }

    CASE("status: the map, its file, the screen, undo, and how edits are taken");
    t = ctl_ask(&a, "status");
    CHECK(strncmp(t, "ok\nmap Two Rooms  16x9\n", 23) == 0);
    CHECK(strstr(t, "file tests/fixtures/two-rooms.vtt\n") != NULL);
    CHECK(strstr(t, "screen build, normal mode\n") != NULL);
    CHECK(strstr(t, "undo 0 back, 0 forward\n") != NULL);
    CHECK(strstr(t, "edits land at once (:agent accept auto)\n") != NULL);
    free(t);
    a.world.ctl_auto = 0;
    t = ctl_ask(&a, "status");
    CHECK(strstr(t, "edits proposed, for the GM to review\n") != NULL);
    free(t);
    a.world.ctl_auto = 1;

    CASE("dump, describe and check are the map tools' own reports");
    {
        char *want = NULL;
        size_t wn = 0;
        FILE *f = open_memstream(&want, &wn);
        maptools_dump(f, a.world.map, 1, 1, 6, 5);
        fclose(f);
        t = ctl_ask(&a, "dump B2:G6\n");
        /* The tool's report less its key: an agent reads the key once, with `dump key`. */
        const char *key = strstr(want, "\nkey\n"), *foot = key ? strstr(key, "\nmap  ") : NULL;
        CHECK(key && foot);
        if (key && foot) {
            CHECK(strncmp(t, "ok\n", 3) == 0 && !strncmp(t + 3, want, (size_t)(key - want)) &&
                  !strcmp(t + 3 + (key - want), foot));
            CHECK(strstr(t, "key") == NULL);
            char *kt = ctl_ask(&a, "dump key");
            CHECK(kt && !strncmp(kt, "ok\nkey\n  .  floor", 17) && strlen(kt + 3) == (size_t)(foot - key - 1));
            CHECK(kt && !strncmp(kt + 3, key + 1, (size_t)(foot - key - 1)));
            free(kt);
        }
        free(t);
        free(want);
        t = ctl_ask(&a, "dump all");                       /* the same as none, on a map this small */
        char *t2 = ctl_ask(&a, "dump");
        CHECK(t && t2 && !strcmp(t, t2) && !strncmp(t, "ok\n", 3));
        free(t);
        free(t2);
        t = ctl_ask(&a, "dump B2:C3 D4");
        CHECK(ctl_refused(t, "usage", 1) && strstr(t, "dump takes one region"));
        free(t);

        /* describe REGION: the rooms that meet it, whole; the map's lines always. */
        f = open_memstream(&want, &wn);
        maptools_describe(f, a.world.map, 0);
        fclose(f);
        t = ctl_ask(&a, "describe A1:P9");                 /* every room: the same report but for its region line */
        CHECK(t && strstr(t, "  region A1:P9: ") && strlen(t + 3) > strlen(want));
        free(t);
        free(want);
        t = ctl_ask(&a, "describe ZZ1");
        CHECK(ctl_refused(t, "usage", 1));
        free(t);
        t = ctl_ask(&a, "describe B2 json extra");
        CHECK(ctl_refused(t, "usage", 1) && strstr(t, "describe [REGION] [json]"));
        free(t);
        t = ctl_ask(&a, "describe B2 json");
        CHECK(t && !strncmp(t, "ok\n", 3) && json_valid(t + 3) && strstr(t, "\"region\":\"B2\"") && strstr(t, "\"rooms_told\":"));
        free(t);

        f = open_memstream(&want, &wn);
        maptools_describe(f, a.world.map, 1);
        fclose(f);
        t = ctl_ask(&a, "describe json");
        CHECK(strcmp(t + 3, want) == 0);
        CHECK(json_valid(t + 3));
        free(t);
        free(want);

        f = open_memstream(&want, &wn);
        maptools_check_map(f, a.world.map, 0);
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
    CHECK(strncmp(t, "error usage: line 2: ZZ99:4 is not a square", 43) == 0);
    CHECK(strchr(t, '\n') == t + strlen(t) - 1);
    free(t);
    t = ctl_ask(&a, "describe yaml\n");                    /* a word that is no region */
    CHECK(ctl_refused(t, "usage", 1) && strstr(t, "yaml is not a square"));
    free(t);
    t = ctl_ask(&a, "frobnicate\n");
    CHECK_EQ(strcmp(t, "error usage: line 1: unknown request frobnicate\n"), 0);
    free(t);
    t = ctl_ask(&a, "status now\n");
    CHECK(strncmp(t, "error usage: line 1: status takes nothing", 41) == 0);
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
        t = request_exec(&a.world, req, &len);
        CHECK(t && strcmp(t, "error usage: a nul byte in the request\n") == 0);
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
    CHECK(map_tile(a.world.map, 0, 0) != TILE_WATER);              /* not yet */
    t = ctl_ask(&a, "status");                               /* a read is answered */
    CHECK(t && !strncmp(t, "ok\n", 3));
    CHECK(t && strstr(t, "edits proposed, landing when the GM is back: the GM is answering a prompt\n"));
    free(t);
    press(&a, "\x1b");
    CHECK(app_ctl_busy(&a) == NULL);
    CHECK_EQ(map_tile(a.world.map, 0, 0), TILE_WATER);             /* the GM is back */
    a.modal = MODAL_PICKER;                                  /* a list to choose from */
    t = ctl_ask(&a, "tile A1 hazard");
    CHECK(t && strstr(t, "\nlands when the GM is back: the GM is choosing from a list\n"));
    free(t);
    CHECK_EQ(map_tile(a.world.map, 0, 0), TILE_WATER);
    a.modal = MODAL_MESSAGE;                                 /* a message on the screen */
    t = ctl_ask(&a, "tile A2 hazard");
    CHECK(t && strstr(t, "\nlands when the GM is back: a question is open on the GM's screen\n"));
    free(t);
    a.modal = MODAL_NONE;
    press(&a, "\x1b");
    CHECK_EQ(map_tile(a.world.map, 0, 0), TILE_HAZARD);            /* both, in turn */
    CHECK_EQ(map_tile(a.world.map, 0, 1), TILE_HAZARD);
    press(&a, "w l");                                        /* the pen down, a wall laid */
    t = ctl_ask(&a, "tile A1 water");
    CHECK(t && strstr(t, "\nlands when the GM is back: the GM is laying wall\n"));
    free(t);
    CHECK_EQ(map_tile(a.world.map, 0, 0), TILE_HAZARD);            /* never into a stroke */
    press(&a, " \x1b");
    CHECK(app_ctl_busy(&a) == NULL);
    CHECK_EQ(map_tile(a.world.map, 0, 0), TILE_WATER);
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
    CHECK(strstr(a.status, ":agent on|off, :agent accept auto|review, :agent claude") != NULL);
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
    return a->world.jobs[0].used && a->world.jobs[0].state == JOB_READY;
}

/* For ctl_pump: stop once this many waits are held. */
typedef struct { App *a; int n; } WaitersAre;
static int waiters_are(void *ctx)
{
    WaitersAre *w = ctx;
    return ctl_waiters(&w->a->ctl) == w->n;
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
    a.world.ctl_auto = 1;                       /* edits land at once (decision 6) */
    app_open_map(&a, "tests/fixtures/two-rooms.vtt");
    CHECK(a.world.map != NULL);
    if (!a.world.map) { app_free(&a); rnd_free(&r); return; }
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
    snprintf(sel, sizeof sel, "selected %s ", a.world.map->tokens.v[0].label);
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
    CHECK(a.world.map == NULL);
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
    a.world.ctl_auto = 1;                       /* edits land at once (decision 6) */
    CHECK(ctl_blank_map(&a, sb.dir, 12, 8));
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.world.map;
    char *t, *before, *after;

    CASE("room: floor, walls round it, one undo step; the GM is told and shown where");
    int depth = a.world.undo.depth;
    t = ctl_ask(&a, "room B2:D4\n");
    CHECK(!strncmp(t, "ok\nproposal #1: ", 16));
    CHECK(strstr(t, "\nchanged B2:D4: 1 line, one undo step\n") != NULL);
    free(t);
    CHECK_EQ(a.world.undo.depth, depth + 1);
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
    app_tick(&a, a.world.now_ms + PING_SHOW_MS);
    CHECK_EQ(a.agent_ring.until_ms, 0u);

    CASE("many lines are still one step: u takes all of them back together");
    before = ctl_snapshot(m);
    depth = a.world.undo.depth;
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
    CHECK_EQ(a.world.undo.depth, depth + 1);
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
    depth = a.world.undo.depth;
    int nmarks = a.world.undo.nmarks;
    t = ctl_ask(&a, "tile A1:L8 hazard\ntoken del Ghoul\nnote A1 \"x\"\ntoken add enemy B7 \"Aria\"\n");
    CHECK_EQ(strcmp(t, "error conflict: line 4: there is already a creature called Aria\n"), 0);
    free(t);
    after = ctl_snapshot(m);
    CHECK_EQ(strcmp(before, after), 0);
    CHECK_EQ(a.world.undo.depth, depth);
    CHECK_EQ(a.world.undo.nmarks, nmarks);
    CHECK_EQ(a.world.undo.open, 0);
    CHECK_EQ(m->tokens.n, 2);
    free(after);
    free(before);

    CASE("a room, drawn through the editor's own helper, is rolled back with the rest");
    before = ctl_snapshot(m);
    t = ctl_ask(&a, "room A6:C8\nwall E6:F7 door\ntoken move Nobody A1\n");
    CHECK(ctl_refused(t, "missing", 3));
    free(t);
    after = ctl_snapshot(m);
    CHECK_EQ(strcmp(before, after), 0);
    CHECK_EQ(a.world.undo.open, 0);
    CHECK_EQ(a.world.undo.nest, 0);
    free(after);
    free(before);

    CASE("the mistakes an edit can make, each named");
    {
        static const struct { const char *req, *cls, *err; } bad[] = {
            { "room B2:Z99\n", "usage",              "Z99 is not a square on this map (A1 to L8)" },
            { "room\n", "usage",                     "room REGION" },
            { "tile B2 lava\n", "usage",             "lava: a tile is void, floor" },
            { "wall B2:C3 portcullis\n", "usage",    "portcullis: a boundary is none, wall" },
            { "edge C3|E3 door\n", "usage",          "the squares are not side by side" },
            { "edge C3/D3 door\n", "usage",          "the squares are not side by side" },
            { "edge -|- door\n", "usage",            "a boundary needs a square on one side" },
            { "edge C3 door\n", "usage",             "is not a boundary" },
            { "edge -|B1 door\n", "usage",           "the squares are not side by side" },
            { "token add dragon C3 \"X\"\n", "usage", "a creature is a player or an enemy" },
            { "token add enemy C3 size 4 \"X\"\n", "usage", "size is 1, 2 or 3" },
            { "token add enemy L8 size 2 \"X\"\n", "conflict", "hangs off the map" },
            { "token add enemy J5 \"X\"\n", "conflict", "J5 is taken by Ghoul" },
            { "token add enemy C3 \"\"\n", "usage",  "a creature needs a label" },
            { "token add enemy C3 \"abcdefghijabcdefghijabcdefghij12\"\n", "limit", "the label is over 31 characters" },
            { "token move Nobody C3\n", "missing",     "no creature called Nobody" },
            { "token move A1 C3\n", "missing",         "no creature stands on A1" },
            { "token move Ghoul G5\n", "conflict",      "G5 is taken by Aria" },
            { "token set Ghoul color red\n", "usage", "token set changes a label, a size, a note or hidden" },
            { "token fly Ghoul\n", "usage",          "token fly: add, move, del or set" },
            { "note Z1 \"x\"\n", "usage",            "is not a square" },
            { "fog paint B2:C3 1\n", "missing",        "there is no fog patch 1" },
            { "fog paint B2:C3 16\n", "usage",       "a fog patch is 1 to 15" },
            { "fog B2:C3 1\n", "usage",              "fog paint REGION N" },
        };
        before = ctl_snapshot(m);
        for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
            t = ctl_ask(&a, bad[i].req);
            CHECK(ctl_refused(t, bad[i].cls, 1) && strstr(t, bad[i].err) != NULL);
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
    depth = a.world.undo.depth;
    t = ctl_ask(&a, "tile B2 floor\n");
    CHECK_EQ(strcmp(t, "ok\nno change: the map already looked like that\n"), 0);
    free(t);
    CHECK_EQ(a.world.undo.depth, depth);

    CASE("the agent's undo: its own last change, only while nothing came after");
    t = ctl_ask(&a, "tile B2 water\n");
    free(t);
    t = ctl_ask(&a, "undo\n");
    CHECK_EQ(strcmp(t, "ok\ntook back the last change\n"), 0);
    free(t);
    CHECK_EQ(map_tile(m, 1, 1), TILE_FLOOR);
    CHECK(strstr(a.status, "the agent took back its last change") != NULL);
    t = ctl_ask(&a, "undo\n");                        /* once: an older landing of its own is the last step now, and is named */
    CHECK(ctl_refused(t, "state", 1) && strstr(t, "undo is once - job ") != NULL);
    free(t);
    t = ctl_ask(&a, "tile B2 water\n");
    free(t);
    a.ed.cx = 5; a.ed.cy = 5;
    press(&a, " ");                                  /* the GM paints a square */
    t = ctl_ask(&a, "undo\n");                        /* the GM's step lies over the agent's */
    CHECK(strstr(t, "something has happened since") != NULL && ctl_refused(t, "state", 1));
    free(t);
    CHECK_EQ(map_tile(m, 1, 1), TILE_WATER);
    press(&a, "u");                                  /* and takes it back again: the agent's is the last step once more */
    t = ctl_ask(&a, "undo\n");
    CHECK_EQ(strcmp(t, "ok\ntook back the last change\n"), 0);
    free(t);
    CHECK_EQ(map_tile(m, 1, 1), TILE_FLOOR);
    t = ctl_ask(&a, "tile B2 water\n");
    free(t);
    t = ctl_ask(&a, "tile B3 water\nundo\n");
    CHECK_EQ(strcmp(t, "error usage: undo goes in a request of its own\n"), 0);
    free(t);
    CHECK_EQ(map_tile(m, 1, 2), TILE_FLOOR);           /* nothing ran */
    t = ctl_ask(&a, "tile B3 water\n");
    free(t);
    t = ctl_ask(&a, "# first\nundo\nundo\n");
    CHECK_EQ(strcmp(t, "error usage: undo goes in a request of its own\n"), 0);
    free(t);
    CHECK_EQ(map_tile(m, 1, 2), TILE_WATER);           /* not taken back half way */
    CASE("the agent's undo is the log's step back, as the GM's u is: a change outside the log does not stop it");
    CHECK(map_note_set(m, 7, 7, "the GM's own"));      /* outside the log, as a fog setting is */
    t = ctl_ask(&a, "undo\n");
    CHECK_EQ(strcmp(t, "ok\ntook back the last change\n"), 0);
    free(t);
    CHECK_EQ(map_tile(m, 1, 2), TILE_FLOOR);
    CHECK(map_note_at(m, 7, 7) != NULL);
    t = ctl_ask(&a, "tile B3 water\n");                /* as the cases after found it */
    free(t);

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
    int d0 = a.world.undo.depth;
    press(&a, "w ll");
    CHECK(map_hedge(m, 6, 7) == EDGE_WALL || map_hedge(m, 7, 7) == EDGE_WALL);
    CHECK_EQ(a.world.undo.open, 1);
    Key f2k = { KEY_F2, 0, 0 }, f1k = { KEY_F1, 0, 0 };
    app_key(&a, f2k);
    app_key(&a, f1k);
    CHECK_EQ(a.world.undo.open, 0);
    CHECK_EQ(a.ed.pen, 0);
    CHECK_EQ(a.world.undo.depth, d0 + 1);
    CHECK(app_ctl_busy(&a) == NULL);
    press(&a, "u");
    CHECK_EQ(a.world.undo.depth, d0);

    CASE("under review, edits are proposals: the map, its log (the redo tail too), modified and Map.gen stay");
    t = ctl_ask(&a, "tile H8 water\n");                  /* landed, then undone: a redo tail */
    free(t);
    press(&a, "u");
    a.world.ctl_auto = 0;
    before = ctl_snapshot(m);
    int      nmarks0 = a.world.undo.nmarks, udepth = a.world.undo.depth, modified = m->modified;
    unsigned stamp = a.world.undo.stamp, gen = m->gen;
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
    undo_stroke(&a.world.undo);                            /* ... with a stroke open */
    t = ctl_ask(&a, "tile B4 hazard\ntoken add enemy C3 \"Imp\"\n");
    CHECK(t && strstr(t, "\nwaiting for the GM's review"));
    free(t);
    undo_stroke_end(&a.world.undo);
    a.ed.pen = 0;
    press(&a, "\x1b");
    after = ctl_snapshot(m);
    CHECK_EQ(strcmp(before, after), 0);
    CHECK_EQ(a.world.undo.nmarks, nmarks0);
    CHECK_EQ(a.world.undo.depth, udepth);
    CHECK_EQ(a.world.undo.stamp, stamp);
    CHECK_EQ(m->gen, gen);
    CHECK_EQ(m->modified, modified);
    CHECK_EQ(m->tokens.n, 0);
    int ready = 0;
    for (int i = 0; i < JOB_MAX; i++) ready += a.world.jobs[i].used && a.world.jobs[i].state == JOB_READY;
    CHECK_EQ(ready, 3);
    press(&a, "\x12");                              /* ctrl-r: the tail is still there */
    CHECK_EQ(map_tile(m, 7, 7), TILE_WATER);
    free(after);
    free(before);
    jobs_reset(&a);
    a.world.ctl_auto = 1;

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
    a.world.ctl_auto = 1;                       /* edits land at once (decision 6) */
    CHECK(ctl_blank_map(&a, sb.dir, MAP_MAX_DIM, MAP_MAX_DIM));

    CASE("a request may change twice the largest map's squares and no more");
    char last[MAP_COORD_MAX], req[128];
    map_coord_name(MAP_MAX_DIM - 1, MAP_MAX_DIM - 1, last, sizeof last);
    snprintf(req, sizeof req, "tile A1:%s water\ntile A1:%s floor\n", last, last);
    char *t = ctl_ask(&a, req);
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    snprintf(req, sizeof req, "tile A1:%s water\ntile A1:%s floor\ntile A1 hazard\n", last, last);
    t = ctl_ask(&a, req);                 /* over the socket's road the squares covered are counted first */
    CHECK(strstr(t, "error limit: line 3: the request's edits cover more than 524288 squares") != NULL);
    free(t);
    CHECK_EQ(map_tile(a.world.map, 0, 0), TILE_FLOOR);
    a.world.ctl_direct = 1;                     /* --apply on a file: only what is recorded is bounded, as ever */
    t = ctl_ask(&a, req);
    CHECK(strstr(t, "error limit: line 3: the request changes more than 524288 things at once") != NULL);
    free(t);
    a.world.ctl_direct = 0;
    CHECK_EQ(map_tile(a.world.map, 0, 0), TILE_FLOOR);
    CHECK_EQ(map_tile(a.world.map, 5, 5), TILE_FLOOR);

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
    a.world.ctl_auto = 1;                       /* edits land at once (decision 6) */
    char path[700];
    snprintf(path, sizeof path, "%s/void.vtt", sb.dir);
    {
        Map *v = map_new(30, 20, "void");
        char err[200];
        mapio_write(v, path, err, sizeof err);
        map_free(v);
    }
    app_open_map(&a, path);
    Map *m = a.world.map;
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
    CHECK(strstr(t, "error conflict: line 4: no room in Closet for a 1x1 creature") != NULL);
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
    int d = a.world.undo.depth;
    press(&a, ":area Hall remove\r");
    CHECK(map_area_find(m, "Hall") == -1 && a.world.undo.depth == d + 1);     /* one undo step of its own */
    press(&a, "u");
    CHECK(map_area_find(m, "Hall") >= 0 && a.world.undo.depth == d);

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
    a.world.ctl_auto = 1;                       /* edits land at once (decision 6) */
    char path[700];
    snprintf(path, sizeof path, "%s/void.vtt", sb.dir);
    {
        Map *v = map_new(30, 20, "void");
        char err[200];
        mapio_write(v, path, err, sizeof err);
        map_free(v);
    }
    app_open_map(&a, path);
    Map *m = a.world.map;
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
    int depth = a.world.undo.depth;
    t = ctl_ask(&a, "corridor B E\n");
    CHECK(strncmp(t, "ok\n", 3) == 0);
    free(t);
    CHECK_EQ(a.world.undo.depth, depth + 1);
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
    CHECK(strstr(t, "error conflict: line 4: it would cross ground already at") != NULL);
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
    a.world.ctl_auto = 1;                       /* edits land at once (decision 6) */
    char path[700];
    snprintf(path, sizeof path, "%s/void.vtt", sb.dir);
    {
        Map *v = map_new(30, 30, "void");
        char err[200];
        mapio_write(v, path, err, sizeof err);
        map_free(v);
    }
    app_open_map(&a, path);
    Map *m = a.world.map;
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
    /* The `vtt --apply` this runs looks for a vtt holding its map: in the
     * sandbox, never among the user's own. */
    CHECK(getenv("XDG_RUNTIME_DIR") && !strcmp(getenv("XDG_RUNTIME_DIR"), sb.dir));
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
    /* The sandbox is the runtime directory (sandbox_enter): no socket here
     * is the user's. */
    CHECK(getenv("XDG_RUNTIME_DIR") && !strcmp(getenv("XDG_RUNTIME_DIR"), sb.dir));

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
        /* Seventy dumps of the whole map: far past what one send takes. */
        char req[400] = "";
        for (int i = 0; i < 70; i++) strcat(req, "dump\n");
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
        CHECK_EQ(strcmp(rd.buf, "error limit: the request is over 64 KB\n"), 0);
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
                child_exit(rc);
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
            else        CHECK(strstr(text, "vtt: error usage: line 1: unknown request bogus") != NULL);
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
            child_exit(rc);
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
            child_exit(ctl_client_main("status", 0));
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

    CASE("a waiter that hangs up frees its place; eight wait while four more are served; a ninth is told");
    {
        int w[CTL_MAX_WAIT + 1];
        for (int i = 0; i < CTL_MAX_WAIT; i++) {
            w[i] = ctl_raw_connect(a.ctl.path);
            CHECK(write(w[i], "wait for 600\n", 13) == 13);
            shutdown(w[i], SHUT_WR);
            /* Four are read at a time: the rest come once those are held. */
            if (i % CTL_MAX_CONN == CTL_MAX_CONN - 1) ctl_pump(&a, waiters_are, &(WaitersAre){ &a, i + 1 });
        }
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, CTL_MAX_WAIT });
        CHECK_EQ(ctl_waiters(&a.ctl), CTL_MAX_WAIT);
        CtlReader rd = { ctl_raw_connect(a.ctl.path), "", 0, 0 };   /* a request beside them */
        CHECK(write(rd.fd, "status\n", 7) == 7);
        shutdown(rd.fd, SHUT_WR);
        ctl_pump(&a, ctl_read_some, &rd);
        CHECK(rd.done && !strncmp(rd.buf, "ok\nmap ", 7));
        close(rd.fd);
        CtlReader r5 = { ctl_raw_connect(a.ctl.path), "", 0, 0 };   /* one waiter too many */
        CHECK(write(r5.fd, "wait for 600\n", 13) == 13);
        shutdown(r5.fd, SHUT_WR);
        ctl_pump(&a, ctl_read_some, &r5);
        CHECK(r5.done && !strncmp(r5.buf, "error limit: as many agents", 27));
        close(r5.fd);
        close(w[0]);                                       /* one hangs up */
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, CTL_MAX_WAIT - 1 });
        CHECK_EQ(ctl_waiters(&a.ctl), CTL_MAX_WAIT - 1);
        for (int i = 1; i < CTL_MAX_WAIT; i++) close(w[i]);
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 0 });
        CHECK_EQ(ctl_waiters(&a.ctl), 0);
        CHECK_EQ(a.ctl.nc, 0);
    }
    jobs_reset(&a);

    CASE("one wait a name: a newer one from the same name is held and the older answered superseded; no name is nobody's");
    {
        CtlReader w1 = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
        CHECK(write(w1.fd, "agent scout\nwait for 600\n", 25) == 25);
        shutdown(w1.fd, SHUT_WR);
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 1 });
        CHECK_EQ(ctl_read_some(&w1), 0);
        CtlReader w2 = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
        CHECK(write(w2.fd, "agent scout\nwait for 600\n", 25) == 25);
        shutdown(w2.fd, SHUT_WR);
        ctl_pump(&a, ctl_read_some, &w1);                      /* the older is answered at once */
        char want1[120];
        snprintf(want1, sizeof want1, "ok\nseq %u\nsuperseded: a newer wait from scout took this one's place\n", a.world.event_seq);
        CHECK(w1.done && !strcmp(w1.buf, want1));
        CHECK_EQ(ctl_waiters(&a.ctl), 1);                      /* and the newer is the one held */
        CHECK_EQ(ctl_read_some(&w2), 0);
        close(w1.fd);
        /* Another name's wait, and two with no name, are each their own. */
        CtlReader w3 = { ctl_raw_connect(a.ctl.path), "", 0, 0 }, w4 = { ctl_raw_connect(a.ctl.path), "", 0, 0 },
                  w5 = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
        CHECK(write(w3.fd, "agent other\nwait for 600\n", 25) == 25);
        CHECK(write(w4.fd, "wait for 600\n", 13) == 13);
        CHECK(write(w5.fd, "wait for 600\n", 13) == 13);
        shutdown(w3.fd, SHUT_WR); shutdown(w4.fd, SHUT_WR); shutdown(w5.fd, SHUT_WR);
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 4 });
        CHECK_EQ(ctl_waiters(&a.ctl), 4);
        CHECK(!ctl_read_some(&w2) && !ctl_read_some(&w3) && !ctl_read_some(&w4) && !ctl_read_some(&w5));
        /* With every slot held, a name's own newer wait is still taken: it cannot lock itself out. */
        int fill[CTL_MAX_WAIT];
        for (int k = 4; k < CTL_MAX_WAIT; k++) {
            fill[k] = ctl_raw_connect(a.ctl.path);
            CHECK(write(fill[k], "wait for 600\n", 13) == 13);
            shutdown(fill[k], SHUT_WR);
        }
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, CTL_MAX_WAIT });
        CtlReader w6 = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
        CHECK(write(w6.fd, "agent scout\nwait for 600\n", 25) == 25);
        shutdown(w6.fd, SHUT_WR);
        ctl_pump(&a, ctl_read_some, &w2);
        CHECK(w2.done && strstr(w2.buf, "\nsuperseded: "));
        CHECK_EQ(ctl_read_some(&w6), 0);
        close(w2.fd); close(w3.fd); close(w4.fd); close(w5.fd); close(w6.fd);
        for (int k = 4; k < CTL_MAX_WAIT; k++) close(fill[k]);
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 0 });
        CHECK_EQ(a.ctl.nc, 0);

        /* An event made by a request in the very turn that supersedes: the older wait is answered
         * with it, or nobody would hear it (the newer one waits from after it). */
        CtlReader o1 = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
        CHECK(write(o1.fd, "agent scout\nwait for 600\n", 25) == 25);
        shutdown(o1.fd, SHUT_WR);
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 1 });
        unsigned before = a.world.event_seq;
        a.world.ctl_auto = 1;
        CtlReader e1 = { ctl_raw_connect(a.ctl.path), "", 0, 0 }, o2 = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
        CHECK(write(e1.fd, "agent other\ntile B2 hazard\n", 27) == 27);
        CHECK(write(o2.fd, "agent scout\nwait for 600\n", 25) == 25);
        shutdown(e1.fd, SHUT_WR); shutdown(o2.fd, SHUT_WR);
        poll(NULL, 0, 20);                                     /* both in the socket before a turn reads either */
        ctl_pump(&a, ctl_read_some, &o1);
        a.world.ctl_auto = 0;
        CHECK_EQ(a.world.event_seq, before + 2);                     /* proposed, and accepted at once */
        char ev[64];
        snprintf(ev, sizeof ev, "\n%u job ", before + 2);
        CHECK(o1.done && strstr(o1.buf, ev) && strstr(o1.buf, " accepted: ") && strstr(o1.buf, "\nsuperseded: "));
        close(o1.fd); close(e1.fd); close(o2.fd);
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 0 });
        press(&a, "u");
        jobs_reset(&a);
    }

    CASE("vtt --ctl writes the caller's name from VTT_AGENT, and refuses one that is no name");
    {
        char outf[700];
        snprintf(outf, sizeof outf, "%s/named-out.txt", sb.dir);
        static const struct { const char *name, *req; int rc; const char *says; } N[] = {
            { "tester", "status", 0, "\nyou tester\n" },
            { "tester", "agent other\nstatus", 0, "\nyou other\n" },      /* a request's own line wins */
            { "tester", "# mine\n\n  agent other\nstatus", 0, "\nyou other\n" },   /* wherever the server would find it */
            { "no good", "status", 2, "" },
            { "", "status", 0, "\nyou cli\n" },
        };
        for (size_t k = 0; k < sizeof N / sizeof *N; k++) {
            fflush(stdout);
            pid_t pid = fork();
            if (pid == 0) {
                int o = open(outf, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                dup2(o, 1);
                int e = open("/dev/null", O_WRONLY);
                dup2(e, 2);
                setenv("VTT_AGENT", N[k].name, 1);
                int rc = ctl_client_main(N[k].req, 0);
                fflush(stdout);
                child_exit(rc);
            }
            int stc[3] = { (int)pid, 0, 0 };
            ctl_pump(&a, ctl_child_done, stc);
            CHECK(stc[1] && WIFEXITED(stc[2]) && WEXITSTATUS(stc[2]) == N[k].rc);
            FILE *f = fopen(outf, "r");
            char  text[700] = "";
            size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
            text[n] = '\0';
            if (f) fclose(f);
            CHECK(strstr(text, N[k].says) != NULL);
        }
    }

    CASE(":agent off: the socket stays (an --apply must find the map), a held wait stays, the checkpoint stops");
    CtlReader held = { ctl_raw_connect(a.ctl.path), "", 0, 0 };
    CHECK(write(held.fd, "wait for 600\n", 13) == 13);
    shutdown(held.fd, SHUT_WR);
    ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 1 });
    CHECK(a.world.agent_seen_ms != 0);
    press(&a, ":agent off\r");
    CHECK_EQ(a.world.agent_on, 0);
    CHECK_EQ(ctl_active(&a.ctl), 1);
    CHECK(access(want, F_OK) == 0);
    CHECK(strstr(a.status, "agent channel off - requests are refused") != NULL);
    CHECK_EQ(a.world.agent_seen_ms, 0u);
    app_tick(&a, prof_now_ns() / 1000000u);
    CHECK(a.world.map->cp == NULL);                          /* nothing of the map is told */
    CHECK_EQ(ctl_waiters(&a.ctl), 1);
    press(&a, ":agent off\r");
    CHECK(strstr(a.status, "already off") != NULL);

    /* docs/V2.md, ruling 9: off refuses, and says which vtt to turn on. Until
     * then (docs/CONFLICTS.md, decision 5) proposals, status and wait were taken. */
    CASE("off, over the socket: every request is refused with the map's name and what to type; only holds is answered");
    {
        const char *off = "error state: line 1: the agent channel is off in the vtt with Two Rooms - type :agent on in that vtt, then ask again\n";
        static const char *const REFUSED[] = { "dump\n", "status\n", "help\n", "wait 0\n", "wait for 0\n", "jobs\n", "marked\n",
                                               "describe\n", "undo\n", "job 1 take\n", "scene save Now\n", "scene Nope\n",
                                               "tile A1 water\n", "propose \"a well\"\ntile B2 water\n",
                                               "propose apply \"plan.txt\"\ntile B2 hazard\n" };
        a.world.ctl_auto = 1;                                    /* nor does anything land */
        int was = map_tile(a.world.map, 1, 1);
        for (size_t k = 0; k < sizeof REFUSED / sizeof *REFUSED; k++) {
            char *t = sock_ask(&a, REFUSED[k]);
            if (!t || strcmp(t, off) != 0) printf("    not refused: %s", REFUSED[k]);
            CHECK(t && !strcmp(t, off));
            free(t);
        }
        a.world.ctl_auto = 0;
        CHECK_EQ(map_tile(a.world.map, 1, 1), was);
        CHECK_EQ(a.world.jobs[0].used, 0);                       /* and nothing became a job */
        /* --apply's own question, or it would take an open map for a file nobody holds and write it. */
        struct stat fs;
        char req[96];
        CHECK(stat("tests/fixtures/two-rooms.vtt", &fs) == 0);
        snprintf(req, sizeof req, "holds %llu %llu\n", (unsigned long long)fs.st_dev, (unsigned long long)fs.st_ino);
        char *t = sock_ask(&a, req);
        CHECK(t && !strcmp(t, "ok\nholds\n"));
        free(t);
        t = sock_ask(&a, "holds 1 2\n");
        CHECK(t && !strcmp(t, "ok\nno\n"));
        free(t);
        t = sock_ask(&a, "holds here\n");
        CHECK(t && strstr(t, "holds DEVICE INODE"));
        free(t);
    }

    CASE("--ctl finds no vtt with its channel on, though one listens (exit 2): it says which vtt holds which map");
    {
        char errf[700];
        snprintf(errf, sizeof errf, "%s/ctl-off-err.txt", sb.dir);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            int o = open(errf, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            dup2(o, 2);
            child_exit(ctl_client_main("status", 0));
        }
        int stc[3] = { (int)pid, 0, 0 };
        ctl_pump(&a, ctl_child_done, stc);
        CHECK(stc[1] && WIFEXITED(stc[2]) && WEXITSTATUS(stc[2]) == 2);
        FILE *f = fopen(errf, "r");
        char  text[700] = "", wantoff[200];
        size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
        text[n] = '\0';
        if (f) fclose(f);
        snprintf(wantoff, sizeof wantoff, "vtt: no vtt is taking requests - type :agent on in the one you mean\n"
                                    "  pid %ld: error state: line 1: the agent channel is off in the vtt with Two Rooms - ", (long)getpid());
        CHECK(!strncmp(text, wantoff, strlen(wantoff)));
    }
    close(held.fd);
    ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 0 });

    CASE("--apply to a map open in a vtt whose channel is off: refused, saying what to type, and the file is not written");
    {
        const char *file = "tests/fixtures/two-rooms.vtt";
        struct stat f0, f1;
        stat(file, &f0);
        char errf[700];
        snprintf(errf, sizeof errf, "%s/apply-err.txt", sb.dir);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            int o = open("/dev/null", O_WRONLY);
            dup2(o, 1);
            int e = open(errf, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            dup2(e, 2);
            int rc = ctl_apply_open(file, "plan.txt", "tile B2 hazard\n", 15, -1);
            child_exit(rc < 0 ? 99 : rc);
        }
        int stc[3] = { (int)pid, 0, 0 };
        ctl_pump(&a, ctl_child_done, stc);
        CHECK(stc[1] && WIFEXITED(stc[2]) && WEXITSTATUS(stc[2]) == 1);     /* not 99: it did not take the file for nobody's */
        FILE *f = fopen(errf, "r");
        char  text[512] = "";
        size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
        text[n] = '\0';
        if (f) fclose(f);
        CHECK(strstr(text, "the agent channel is off in the vtt with Two Rooms - type :agent on") != NULL);
        stat(file, &f1);
        CHECK(f0.st_mtime == f1.st_mtime && f0.st_size == f1.st_size && f0.st_ino == f1.st_ino);
        CHECK_EQ(a.world.jobs[0].used, 0);
    }
    press(&a, ":agent on\r");
    CHECK_EQ(a.world.agent_on, 1);

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
            jobs_reset(&a);
            fflush(stdout);
            pid_t pid = fork();
            if (pid == 0) {
                int o = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                dup2(o, 1);
                int e = open("/dev/null", O_WRONLY);
                dup2(e, 2);
                int rc = ctl_apply_open(file, "plan.txt", R[k].plan, strlen(R[k].plan), R[k].wait);
                fflush(stdout);
                child_exit(rc < 0 ? 99 : rc);
            }
            int stc[3] = { (int)pid, 0, 0 };
            if (R[k].keys) {
                ctl_pump(&a, a_job_ready, &a);
                CHECK_EQ(a.world.jobs[0].state, JOB_READY);
                CHECK_EQ(a.world.jobs[0].from, JOB_FROM_APPLY);
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
        jobs_reset(&a);

        CASE("the largest plan --apply reads, under the longest name a file can have, is taken by an open map too (HEALTH 14)");
        {
            unsigned dropped0 = a.ctl.dropped;
            char *plan = malloc(CTL_PLAN_CAP + 1), name[256];
            size_t k = (size_t)snprintf(plan, CTL_PLAN_CAP + 1, "tile B6 hazard\n");
            while (k < CTL_PLAN_CAP) {                     /* comment lines to the last byte */
                size_t line = CTL_PLAN_CAP - k > 1000 ? 1000 : CTL_PLAN_CAP - k;
                memset(plan + k, '#', line - 1);
                plan[k + line - 1] = '\n';
                k += line;
            }
            memset(name, '"', sizeof name - 1);            /* every byte of it escaped */
            name[sizeof name - 1] = '\0';
            fflush(stdout);
            pid_t pid = fork();
            if (pid == 0) {
                int o = open("/dev/null", O_WRONLY);
                dup2(o, 1);
                dup2(o, 2);
                int rc = ctl_apply_open(file, name, plan, CTL_PLAN_CAP, -1);
                free(plan);                          /* the child's copy: held in a local, it reads as a leak at -O2 */
                child_exit(rc < 0 ? 99 : rc);
            }
            int stc[3] = { (int)pid, 0, 0 };
            ctl_pump(&a, ctl_child_done, stc);
            CHECK(stc[1] && WIFEXITED(stc[2]) && WEXITSTATUS(stc[2]) == 0);
            CHECK_EQ(a.world.jobs[0].state, JOB_READY);
            CHECK_EQ(a.ctl.dropped, dropped0);
            free(plan);
            jobs_reset(&a);
        }

        CASE("--apply to a file no vtt has open is the file's, as before: nobody holds it");
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) child_exit(ctl_apply_open("tests/fixtures/kinds.vtt", "plan.txt", "tile B2 hazard\n", 15, -1) < 0 ? 0 : 1);
        int stc[3] = { (int)pid, 0, 0 };
        ctl_pump(&a, ctl_child_done, stc);
        CHECK(stc[1] && WIFEXITED(stc[2]) && WEXITSTATUS(stc[2]) == 0);
    }
    CASE("--apply --wait when the vtt goes before a verdict: exit 2, not a verdict (HEALTH 12)");
    {
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            int o = open("/dev/null", O_WRONLY);
            dup2(o, 1);
            dup2(o, 2);
            int rc = ctl_apply_open("tests/fixtures/two-rooms.vtt", "plan.txt", "tile B7 hazard\n", 15, 20);
            child_exit(rc < 0 ? 99 : rc);
        }
        ctl_pump(&a, waiters_are, &(WaitersAre){ &a, 1 });
        CHECK_EQ(a.world.jobs[0].state, JOB_READY);
        ctl_stop(&a.ctl);                              /* as quitting does: the held wait is closed with nothing said */
        int stc = 0;
        waitpid(pid, &stc, 0);
        CHECK(WIFEXITED(stc) && WEXITSTATUS(stc) == 2);
        jobs_reset(&a);
    }
    /* The cases below want nobody listening, and nobody is. */
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
            child_exit(ctl_apply_open("tests/fixtures/kinds.vtt", "plan.txt", "tile B2 hazard\n", 15, -1) & 0xff);
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
        CHECK_EQ(ctl_verdict_in("ok\nseq 3\n3 job 1 part: B2: ground in B2 - the rest waits\n", 1, &seq, line, sizeof line), 0);
        CHECK_EQ(ctl_verdict_in("ok\nseq 3\n3 job 1 feedback: like job 1 scrapped, but better\n", 1, &seq, line, sizeof line), 5);
        CHECK(!strcmp(line, "like job 1 scrapped, but better"));
        CHECK_EQ(ctl_verdict_in("ok\nseq 3\n3 job 1 scrapped\n", 1, &seq, line, sizeof line), 4);
        CHECK_EQ(ctl_verdict_in("ok\nseq 3\n3 job 1 removed\n", 1, &seq, line, sizeof line), 4);
        CHECK_EQ(ctl_verdict_in("ok\nseq 3\n3 map closed: Blank - its jobs went with it\n", 1, &seq, line, sizeof line), 4);
        CHECK_EQ(ctl_verdict_in("ok\nseq 3\n", 1, &seq, line, sizeof line), -1);
        /* Not events: a kind nobody sends, two words for one, no subject, job 0, a number alone. */
        static const char *const NOT[] = { "3 job 1 pondered", "3 job 1 accepted in part: x", "3 accepted: x", "3 job 0 scrapped",
                                           "3 job scrapped", "3", "seq 3", "lost 1-2: more happened", "3 mapclosed: x", "3 job 1scrapped" };
        for (size_t k = 0; k < sizeof NOT / sizeof *NOT; k++) {
            const char *text;
            size_t tl;
            int num;
            CHECK_EQ(ctl_event_parse(NOT[k], strlen(NOT[k]), &num, &text, &tl), -1);
        }
        const char *text;
        size_t tl;
        int num;
        CHECK_EQ(ctl_event_parse("12 job 34 feedback: a: b", 24, &num, &text, &tl), CTL_EV_FEEDBACK);
        CHECK(num == 34 && tl == 4 && !strncmp(text, "a: b", 4));
        CHECK_EQ(ctl_event_parse("12 map closed: Blank", 20, &num, &text, &tl), CTL_EV_CLOSED);
        CHECK(num == 0 && tl == 5);
        CHECK_EQ(ctl_event_parse("12 job 3 scrapped", 17, &num, &text, &tl), CTL_EV_SCRAPPED);
        CHECK(num == 3 && tl == 0);
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
            child_exit(ctl_client_main("status", 0));
        }
        int stc = 0;
        waitpid(pid, &stc, 0);
        CHECK(WIFEXITED(stc) && WEXITSTATUS(stc) == 2);
        CHECK(access(stale, F_OK) != 0);
    }

    app_free(&a);
    rnd_free(&r);
    rmdir(dir);
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
    a.world.ctl_auto = 1;                       /* edits land at once (decision 6) */
    CHECK(ctl_blank_map(&a, sb.dir, 12, 8));
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.world.map;
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
    int depth = a.world.undo.depth;
    t = ctl_ask(&a, "token add player B2 from wight hidden");
    CHECK(t && strstr(t, "ok\n") == t);
    CHECK(t && strstr(t, "placed \"Wight 2\" at B2\n") != NULL);
    free(t);
    CHECK_EQ(m->tokens.n, 2);
    const Token *p = &m->tokens.v[1];
    CHECK(p->kind == TOKEN_PLAYER && p->size == 2 && p->hidden && p->x == 1 && p->y == 1);
    CHECK(!strcmp(m->rolls[0].name, "drain"));
    CHECK_EQ(a.world.undo.depth, depth + 1);
    t = ctl_ask(&a, "undo");
    free(t);
    CHECK_EQ(m->tokens.n, 1);
    CHECK(!m->rolls[0].name[0]);

    CASE("a request that fails later takes back the creature and its roll");
    t = ctl_ask(&a, "token add enemy B2 from wight\ntile Z99 water");
    CHECK(t && ctl_refused(t, "usage", 2));
    free(t);
    CHECK_EQ(m->tokens.n, 1);
    CHECK(!m->rolls[0].name[0]);

    CASE("refusals: no such character, no label, no room, a bad word");
    t = ctl_ask(&a, "token add enemy B2 from ghast");
    CHECK(t && ctl_refused(t, "missing", 1) && strstr(t, "no character called ghast") != NULL);
    free(t);
    t = ctl_ask(&a, "token add enemy B2 from nameless");
    CHECK(t && ctl_refused(t, "state", 1) && strstr(t, "has no label") != NULL);
    free(t);
    t = ctl_ask(&a, "token add enemy L8 from wight");
    CHECK(t && ctl_refused(t, "conflict", 1) && strstr(t, "L8") != NULL);
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
    a.world.ctl_auto = 1;                       /* edits land at once (decision 6) */
    CHECK(ctl_blank_map(&a, sb.dir, 12, 8));
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.world.map;
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
    CHECK(t && strstr(t, "error usage: scene save goes in a request of its own") != NULL);
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
    CHECK(t && ctl_refused(t, "usage", 2));
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

    CASE("a scene's two refusals have their classes: a bad name is usage, a full table is limit");
    {
        int had = m->nscenes;
        t = ctl_ask(&a, "scene save \"save me\"");
        CHECK(t && ctl_refused(t, "usage", 1));
        free(t);
        for (int k = m->nscenes; k < MAP_SCENES_MAX; k++) {
            char rq[40];
            snprintf(rq, sizeof rq, "scene save Fill%d", k);
            t = ctl_ask(&a, rq);
            CHECK(t && !strncmp(t, "ok\n", 3));
            free(t);
        }
        t = ctl_ask(&a, "scene save OneMore");
        CHECK(t && ctl_refused(t, "limit", 1) && strstr(t, "a map holds"));
        press(&a, ":scene save OneMore\r");                 /* the GM's: the same words (world_ops.c) */
        CHECK(t && a.status_gm && strstr(t, "no room: a map holds 16 scenes\n") && m->nscenes == MAP_SCENES_MAX &&
              !strcmp(a.status, "no room: a map holds 16 scenes - :scene NAME remove makes room"));
        free(t);
        while (m->nscenes > had) {
            char rq[40];
            snprintf(rq, sizeof rq, "scene %s remove", m->scenes[m->nscenes - 1].name);
            free(ctl_ask(&a, rq));
        }
        CHECK_EQ(m->nscenes, had);
    }

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

/* The channel's table of requests (request.c) is the one list of verbs: help
 * prints it, and docs/AGENTS.md may neither miss a verb nor name one that is
 * not there. */
void test_ctl_table(void)
{
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    app_open_map(&a, "tests/fixtures/two-rooms.vtt");

    size_t dl = 0;
    int    big = 0;
    char  *doc = file_read("docs/AGENTS.md", (size_t)1 << 20, &dl, &big);
    CHECK(doc != NULL);
    if (!doc) return;

    CASE("every request in the table is in docs/AGENTS.md, written as code");
    int nverbs = 0;
    unsigned fl;
    for (const char *v; (v = request_verb(nverbs, &fl)) != NULL; nverbs++) {
        if (!strcmp(v, "holds")) continue;          /* --apply's own question, not an agent's */
        char a1[48], a2[48];                        /* a table's row, not a mention in passing */
        snprintf(a1, sizeof a1, "\n| `%s ", v);
        snprintf(a2, sizeof a2, "\n| `%s`", v);
        if (!strstr(doc, a1) && !strstr(doc, a2)) { printf("    no row in AGENTS.md: %s\n", v); CHECK(0); }
    }
    CHECK(nverbs > 25);

    CASE("every row of AGENTS.md's request tables starts with a request in the table");
    const char *from = strstr(doc, "## The request language"), *to = from ? strstr(from, "## From a description") : NULL;
    CHECK(from && to);
    int rows = 0;
    for (const char *p = from; p && p < to; p = strchr(p + 1, '\n')) {
        if (strncmp(p, "\n| `", 4) != 0) continue;
        char word[32];
        size_t k = strcspn(p + 4, " `");
        if (k >= sizeof word) { CHECK(0); continue; }
        memcpy(word, p + 4, k);
        word[k] = '\0';
        int known = 0;
        for (int i = 0; request_verb(i, NULL); i++) known |= !strcmp(request_verb(i, NULL), word);
        if (!known) { printf("    AGENTS.md has a row for `%s`, which is no request\n", word); CHECK(0); }
        rows++;
    }
    CHECK(rows > 30);

    CASE("help lists every request; help VERB says how one is written; an unknown one is refused");
    char *h = ctl_ask(&a, "help");
    CHECK(!strncmp(h, "ok\n", 3));
    for (int i = 0; request_verb(i, NULL); i++) {
        char at[48];
        snprintf(at, sizeof at, "\n%s ", request_verb(i, NULL));
        CHECK(strstr(h, at) != NULL);
    }
    free(h);
    h = ctl_ask(&a, "help token");
    CHECK(strstr(h, "token move WHO SQUARE") != NULL);
    free(h);
    h = ctl_ask(&a, "help undo");
    CHECK(strstr(h, "alone in its request") && strstr(h, "of a running vtt only"));
    free(h);
    h = ctl_ask(&a, "help frobnicate");
    CHECK(!strncmp(h, "error missing: line 1: no request called frobnicate", 51));
    free(h);

    CASE("help json is one array with a row a request, and reads back");
    h = ctl_ask(&a, "help json");
    CHECK(!strncmp(h, "ok\n[", 4));
    int objs = 0;
    for (const char *p = h; (p = strstr(p, "\"name\":")) != NULL; p++) objs++;
    CHECK_EQ(objs, nverbs);
    CHECK(strstr(h, "\"name\":\"wait\",\"kind\":\"read\"") != NULL);
    free(h);

    CASE("the table's kinds are the ones the channel has always had");
    {
        char  got[512] = "";
        FILE *f = fmemopen(got, sizeof got - 1, "w");
        request_edit_verbs(f);
        fclose(f);
        CHECK(!strcmp(got, "room, area, door, corridor, tile, wall, edge, stamp, note, fog, floor, env, link, token, scene"));
        char alone[64] = "", nomap[64] = "", off[64] = "";
        const char *v;
        for (int i = 0; (v = request_verb(i, &fl)) != NULL; i++) {
            if (fl & CTL_RQ_ALONE) { strcat(alone, v); strcat(alone, " "); }
            if (fl & CTL_RQ_NOMAP) { strcat(nomap, v); strcat(nomap, " "); }
            if ((fl & CTL_RQ_OFF) && !(fl & CTL_RQ_EDIT)) { strcat(off, v); strcat(off, " "); }
        }
        CHECK(!strcmp(alone, "wait undo "));
        CHECK(!strcmp(nomap, "agent status wait holds help "));
        CHECK(!strcmp(off, "holds "));         /* --apply's own question, and nothing else */
    }

    CASE("with no map open, help still answers and a read still does not");
    {
        Renderer r2;
        App      b;
        rnd_init(&r2);
        rnd_resize(&r2, 80, 24);
        app_init(&b, NULL, &r2);
        h = ctl_ask(&b, "help wait");
        CHECK(!strncmp(h, "ok\nwait [SEQ]", 13));
        free(h);
        h = ctl_ask(&b, "dump");
        CHECK(!strcmp(h, "error state: line 1: no map is open\n"));
        free(h);
        app_free(&b);
        rnd_free(&r2);
    }

    CASE("help job names the forms job takes, and each is one the channel knows");
    h = ctl_ask(&a, "help job");
    CHECK(strstr(h, "job N area REGION") && strstr(h, "job N say ") && !strstr(h, "job N\n"));
    free(h);
    CASE("a link to another map: each refusal has the class its reason asks for");
    {
        char *t = ctl_ask(&a, "link B2 to ../x Hall");
        CHECK(t && ctl_refused(t, "usage", 1));
        free(t);
        t = ctl_ask(&a, "link B2 to nowhere Hall");
        CHECK(t && ctl_refused(t, "missing", 1) && strstr(t, "no map nowhere beside this one"));
        free(t);
        t = ctl_ask(&a, "link B2 to kinds Nowhere");
        CHECK(t && ctl_refused(t, "missing", 1) && strstr(t, "has no area or square called"));
        free(t);
        char why[200], kept[sizeof a.world.map->path];
        int  kind = -1;
        strcpy(kept, a.world.map->path);
        a.world.map->path[0] = '\0';                             /* never saved: nothing to be beside */
        t = ctl_ask(&a, "link B2 to kinds A1");
        CHECK(t && ctl_refused(t, "state", 1) && strstr(t, "save this map first"));
        free(t);
        strcpy(a.world.map->path, kept);
        /* A void square there is in the way, not missing: the first void square of the map beside. */
        Map *k = mapio_load("tests/fixtures/links.vtt", why, sizeof why);
        CHECK(k != NULL);
        int vx = -1, vy = -1;
        for (int y = 0; k && y < k->h && vx < 0; y++)
            for (int x = 0; x < k->w && vx < 0; x++)
                if (map_tile(k, x, y) == TILE_VOID) { vx = x; vy = y; }
        if (k) map_free(k);
        if (vx >= 0) {
            char at[MAP_COORD_MAX], rq[64];
            map_coord_name(vx, vy, at, sizeof at);
            CHECK(link_map_check(a.world.map, "links", at, why, sizeof why, &kind) != NULL && kind == LINK_MAP_VOID);
            snprintf(rq, sizeof rq, "link B2 to links %s", at);
            t = ctl_ask(&a, rq);
            CHECK(t && ctl_refused(t, "conflict", 1) && strstr(t, "is void"));
            free(t);
        }
        CHECK(vx >= 0);                                    /* or the void class went unchecked */
    }

    {
        /* The words are the wire: an agent's instructions name them. */
        static const char *const CLS[CTL_E_COUNT] = { "usage", "missing", "conflict", "state", "limit", "internal" };
        static const char *const EV[CTL_EV_COUNT] = { "asked", "accepted", "part", "feedback", "scrapped", "removed", "back", "changed", "closed",
                                                         "taken", "proposed", "undone" };
        char row[48];
        for (int k = 0; k < CTL_E_COUNT; k++) {
            snprintf(row, sizeof row, "\n| `%s` |", CLS[k]);
            CHECK(!strcmp(ctl_class_name(k), CLS[k]) && strstr(doc, row));
        }
        for (int k = 0; k < CTL_EV_COUNT; k++) {
            snprintf(row, sizeof row, " %s` |", EV[k]);         /* `job N asked` | ... */
            CHECK(!strcmp(ctl_event_name(k), EV[k]) && strstr(doc, row));
        }
    }
    h = ctl_ask(&a, "job 1");
    CHECK(ctl_refused(h, "usage", 1));                  /* bare `job N` is no request, and help does not offer it */
    free(h);

    CASE("status ends with seq, the number a first wait is given");
    h = ctl_ask(&a, "status");
    CHECK(strstr(h, "\nagent off\nseq 0\n") != NULL);
    free(h);

    CASE("help changes nothing and makes no proposal");
    CHECK_EQ(a.world.undo.depth, 0);
    CHECK_EQ(world_job_find(&a.world, 1), -1);

    free(doc);
    app_free(&a);
    rnd_free(&r);
}

/* ------------------------------------------- reads that cost what was asked */

/* docs/V2.md, D7: a whole-map dump of a big map is asked for by name,
 * describe takes a region, and `changes` reads back where events were. */
void test_ctl_reads(void)
{
    Sandbox sb = sandbox_enter("ctlreads");
    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 80, 60));               /* 4800 squares: past CTL_DUMP_SQUARES */
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    char *t;

    CASE("dump with no region on a big map is refused as a limit, naming the map's corners; all and a region are taken");
    CHECK((long)a.world.map->w * a.world.map->h > CTL_DUMP_SQUARES);
    t = ctl_ask(&a, "dump");
    CHECK(ctl_refused(t, "limit", 1) && strstr(t, "the map is 80x60, A1 to CB60") && strstr(t, "dump all for its 4800 squares"));
    free(t);
    t = ctl_ask(&a, "dump all");
    CHECK(t && !strncmp(t, "ok\n", 3) && strstr(t, "\n60 ") && !strstr(t, "\nregion "));
    free(t);
    t = ctl_ask(&a, "dump A1:D4");
    CHECK(t && !strncmp(t, "ok\n", 3) && strstr(t, "\nregion A1:D4\n") && !strstr(t, "\n5 |"));
    free(t);

    CASE("describe REGION tells the rooms that meet it, whole, and says how many of how many");
    a.world.ctl_auto = 1;
    t = ctl_ask(&a, "tile A1:CB60 void\nroom West B2:F6\nroom East BT50:BX54\ntoken add enemy BU51 \"Ghoul\"\ntoken add player C3 \"Aria\"");
    CHECK(t && !strncmp(t, "ok\n", 3));
    free(t);
    t = ctl_ask(&a, "describe");
    CHECK(t && strstr(t, "B2:F6") && strstr(t, "BT50:BX54") && !strstr(t, "  region "));
    free(t);
    t = ctl_ask(&a, "describe A1:H8");
    CHECK(t && strstr(t, "  region A1:H8: 1 of the 2 rooms\n") && strstr(t, "B2:F6") && strstr(t, "Aria"));
    CHECK(t && !strstr(t, "BT50:BX54") && !strstr(t, "Ghoul"));
    CHECK(t && strstr(t, "2 creatures, 2 rooms"));          /* the map's own line is the whole map's */
    free(t);
    t = ctl_ask(&a, "describe West");                      /* an area's name is a region */
    CHECK(t && strstr(t, ": 1 of the 2 rooms\n") && !strstr(t, "Ghoul"));
    free(t);
    t = ctl_ask(&a, "describe D4");                        /* one square inside it: the room, whole */
    CHECK(t && strstr(t, "B2:F6") && strstr(t, "Aria"));
    free(t);
    t = ctl_ask(&a, "describe AA30:AB31");
    CHECK(t && strstr(t, ": 0 of the 2 rooms\n") && !strstr(t, "\nroom "));
    free(t);
    t = ctl_ask(&a, "describe A1:H8 json");
    CHECK(t && json_valid(t + 3) && strstr(t, "\"rooms\":2") && strstr(t, "\"rooms_told\":1") && !strstr(t, "Ghoul"));
    free(t);

    CASE("a job's dump is held to the same: none refused on a big map, all and a region taken");
    a.world.ctl_auto = 0;
    t = ctl_ask(&a, "propose \"a pool\"\ntile C3:D4 water");
    const char *hash = t ? strstr(t, "proposal #") : NULL;
    int num = hash ? atoi(hash + 10) : 0;
    CHECK(num > 0);
    free(t);
    char jq[64];
    snprintf(jq, sizeof jq, "job %d dump", num);
    t = ctl_ask(&a, jq);
    CHECK(ctl_refused(t, "limit", 1));
    free(t);
    snprintf(jq, sizeof jq, "job %d dump all", num);
    t = ctl_ask(&a, jq);
    CHECK(t && !strncmp(t, "ok\n", 3) && !strstr(t, "\nkey\n"));
    free(t);
    snprintf(jq, sizeof jq, "job %d dump B2:F6", num);
    t = ctl_ask(&a, jq);
    CHECK(t && !strncmp(t, "ok\n", 3) && strstr(t, "~ ~"));
    free(t);
    snprintf(jq, sizeof jq, "job %d dump B2:F6 more", num);
    t = ctl_ask(&a, jq);
    CHECK(ctl_refused(t, "usage", 1));
    free(t);

    snprintf(jq, sizeof jq, "job %d dump key", num);
    t = ctl_ask(&a, jq);
    CHECK(t && !strncmp(t, "ok\nkey\n", 7));
    free(t);

    CASE("an area the GM called all or key is still an area: dump reads its squares");
    a.world.ctl_auto = 1;
    t = ctl_ask(&a, "area all H2:I3\narea key K2:L3");
    CHECK(t && !strncmp(t, "ok\n", 3));
    free(t);
    t = ctl_ask(&a, "dump all");
    CHECK(t && strstr(t, "\nregion H2:I3\n"));
    free(t);
    t = ctl_ask(&a, "dump key");
    CHECK(t && strstr(t, "\nregion K2:L3\n"));
    free(t);
    t = ctl_ask(&a, "area all remove\narea key remove");
    free(t);
    a.world.ctl_auto = 0;
    t = ctl_ask(&a, "dump key");
    CHECK(t && !strncmp(t, "ok\nkey\n", 7));
    free(t);

    CASE("the refusal's example region is on the map, however short the map");
    {
        Map *thin = map_new(512, 9, "thin");
        char *body = NULL, err[200] = "", w[CTL_WORDS][CTL_WORD_MAX];
        size_t bl = 0;
        FILE *f = open_memstream(&body, &bl);
        strcpy(w[0], "dump");
        CHECK_EQ(request_dump(&a.world, thin, w, 1, 1, 1, f, err, sizeof err), 0);
        CHECK(strstr(err, "A1 to SR9") && strstr(err, "like A1:AF9,"));
        fclose(f);
        free(body);
        map_free(thin);
    }

    CASE("a request's reads are bounded by the squares of map they walk: one whole read of the largest, counted, not timed");
    {
        Sandbox sb2 = sandbox_enter("ctlreads2");
        Renderer r2;
        App b;
        rnd_init(&r2);
        rnd_resize(&r2, 100, 30);
        app_init(&b, NULL, &r2);
        CHECK(ctl_blank_map(&b, sb2.dir, 512, 400));        /* 204,800 squares: one whole read fits, two do not */
        if (b.world.map) {
            b.world.ctl_auto = 1;
            char *u = ctl_ask(&b, "describe");
            CHECK(u && !strncmp(u, "ok\n", 3));
            free(u);
            u = ctl_ask(&b, "check");                        /* each request counts from nothing */
            CHECK(u && !strncmp(u, "ok\n", 3));
            free(u);
            u = ctl_ask(&b, "describe\ncheck");
            CHECK(ctl_refused(u, "limit", 2) && strstr(u, "reads more than 300000 squares of map"));
            free(u);
            /* Floor and walls on every square, saved: the file is as big as
             * it gets, and check, which reads it too, still fits once. */
            Map *fm = b.world.map;
            map_fill_tiles(fm, 0, 0, fm->w - 1, fm->h - 1, TILE_FLOOR);
            for (int y = 0; y < fm->h; y++)
                for (int x = 0; x <= fm->w; x++) map_set_vedge(fm, x, y, EDGE_WALL);
            map_touch(fm);
            CHECK_EQ(app_save_map(&b, fm->path), 0);
            CHECK(fm->base_len > 400000);
            u = ctl_ask(&b, "check");
            CHECK(u && !strncmp(u, "ok\n", 3));
            free(u);
            u = ctl_ask(&b, "dump all\ndump A1:D4\nscene diff None");
            CHECK(ctl_refused(u, "missing", 3));              /* a region's squares are its own: 16 more is nothing */
            free(u);
            u = ctl_ask(&b, "dump A1:SR200\ndump A201:SR400");   /* two halves: the whole map, once */
            CHECK(u && !strncmp(u, "ok\n", 3));
            free(u);
            u = ctl_ask(&b, "dump A1:SR200\ndump A201:SR400\ndump A1:SR200");
            CHECK(ctl_refused(u, "limit", 3));
            free(u);
            /* All or nothing as ever: the edit before the read that is one too many is not kept. */
            uint8_t was = map_tile(b.world.map, 0, 0);
            u = ctl_ask(&b, "tile A1 water\ncheck\ndescribe");
            CHECK(ctl_refused(u, "limit", 3));
            CHECK_EQ(map_tile(b.world.map, 0, 0), was);
            free(u);
            /* A job read back is the map copied: counted once, whatever is read of it. */
            b.world.ctl_auto = 0;
            u = ctl_ask(&b, "propose\ntile B2 water");
            const char *hash2 = u ? strstr(u, "proposal #") : NULL;
            int n2 = hash2 ? atoi(hash2 + 10) : 0;
            CHECK(n2 > 0);
            free(u);
            char q[96];
            snprintf(q, sizeof q, "job %d dump all", n2);
            u = ctl_ask(&b, q);
            CHECK(u && !strncmp(u, "ok\n", 3));
            free(u);
            snprintf(q, sizeof q, "job %d dump B2\njob %d check", n2, n2);
            u = ctl_ask(&b, q);
            CHECK(ctl_refused(u, "limit", 2));
            free(u);
            /* Edits are bounded by the squares they cover, changed or not: a fill of what is there
             * already records nothing, and was three seconds of one turn (the budget's review). */
            b.world.ctl_auto = 1;
            u = ctl_ask(&b, "tile A1:SR400 floor\ntile A1:SR400 floor");      /* 409,600: under twice the largest map */
            CHECK(u && !strncmp(u, "ok\n", 3));
            free(u);
            u = ctl_ask(&b, "tile A1:SR400 floor\ntile A1:SR400 floor\ntile A1:SR400 floor");
            CHECK(ctl_refused(u, "limit", 3) && strstr(u, "edits cover more than 524288 squares"));
            free(u);
            u = ctl_ask(&b, "fog paint A1:SR400 0\nfog paint A1:SR400 0\nfog paint A1:SR400 0");
            CHECK(ctl_refused(u, "limit", 3));
            free(u);
            {
                char many[4096] = "";                              /* a wall is its outline: 1,824 squares, hundreds fit */
                for (int k = 0; k < 200; k++) strcat(many, "wall A1:SR400\n");
                u = ctl_ask(&b, many);
                CHECK(u && !strncmp(u, "ok\n", 3));
                free(u);
                press(&b, "u");
            }
            b.world.ctl_auto = 0;
            /* A listing opens every saved file: three a request, not nine thousand. */
            u = ctl_ask(&b, "stamps\ncharacters\nstamps");
            CHECK(u && !strncmp(u, "ok\n", 3));
            free(u);
            u = ctl_ask(&b, "stamps\ncharacters\nstamps\ncharacters");
            CHECK(ctl_refused(u, "limit", 4));
            free(u);
            /* scene diff walks creatures, not squares: it is not counted. */
            u = ctl_ask(&b, "describe\nscene diff None");
            CHECK(ctl_refused(u, "missing", 2));
            free(u);
            /* A read that walks nothing is bounded by what is piled up: help 12,800 times is not 43 ms. */
            {
                char *many = malloc(60001);
                size_t ml = 0;
                while (ml + 5 <= 60000) { memcpy(many + ml, "help\n", 5); ml += 5; }
                many[ml] = '\0';
                u = ctl_ask(&b, many);
                CHECK(u && !strncmp(u, "error limit: line ", 18) && strstr(u, "the answer was over 2 MB before this line"));
                int at = u ? atoi(u + 18) : 0;
                char *one = ctl_ask(&b, "help");
                size_t page = one ? strlen(one) - 3 : 1;
                CHECK_EQ(at, (int)(CTL_ANSWER_CAP / page) + 2);   /* the first line run with the cap already passed */
                CHECK(u && strlen(u) < 200);                   /* and the megabytes are not sent */
                free(one);
                free(u);
                ml = 0;
                while (ml + 7 <= 60000) { memcpy(many + ml, "floors\n", 7); ml += 7; }
                many[ml] = '\0';
                u = ctl_ask(&b, many);                         /* small answers: 64 KB of them fit */
                CHECK(u && !strncmp(u, "ok\n", 3) && strlen(u) < CTL_ANSWER_CAP / 4);
                free(u);
                b.world.ctl_direct = 1;
                ml = 0;
                while (ml + 5 <= 60000) { memcpy(many + ml, "help\n", 5); ml += 5; }
                many[ml] = '\0';
                u = ctl_ask(&b, many);                         /* --apply on a file: its own to take */
                CHECK(u && !strncmp(u, "ok\n", 3));
                free(u);
                b.world.ctl_direct = 0;
                free(many);
            }
            /* One line's answer is never refused for its size: a crowd dumped whole is what was asked.
             * Only a line after it is. */
            {
                Sandbox sb3 = sandbox_enter("ctlreads3");
                Renderer r3;
                App c;
                rnd_init(&r3);
                rnd_resize(&r3, 100, 30);
                app_init(&c, NULL, &r3);
                CHECK(ctl_blank_map(&c, sb3.dir, 512, 512));
                if (c.world.map) {
                    for (int y = 0; y < 512; y += 4)             /* 16,384 creatures, each with a note */
                        for (int x = 0; x < 512; x += 4) {
                            Token tk;
                            memset(&tk, 0, sizeof tk);
                            tk.kind = TOKEN_ENEMY; tk.size = 1; tk.x = (int16_t)x; tk.y = (int16_t)y;
                            snprintf(tk.label, sizeof tk.label, "G%d", y * 512 + x);
                            snprintf(tk.note, sizeof tk.note, "%060d", 7);
                            tokens_add(&c.world.map->tokens, tk);
                        }
                    char *v = ctl_ask(&c, "dump all");
                    CHECK(v && !strncmp(v, "ok\n", 3) && strlen(v) > CTL_ANSWER_CAP);
                    free(v);
                    v = ctl_ask(&c, "dump all\nstatus");
                    CHECK(ctl_refused(v, "limit", 2) && strstr(v, "send the rest in another request"));
                    free(v);
                }
                app_free(&c);
                rnd_free(&r3);
                sandbox_leave(&sb3);
            }
            /* Work that is no square of map is counted as one: a scene's creatures against the map's,
             * a line for the session log, a stat; and changes is asked three times at most. */
            {
                char *many = malloc(60001);
                size_t ml = 0;
                while (ml + 10 <= 60000) { memcpy(many + ml, "holds 1 2\n", 10); ml += 10; }
                many[ml] = '\0';
                u = ctl_ask(&b, many);
                CHECK(u && !strncmp(u, "error limit: line ", 18) && atoi(u + 18) == CTL_READ_SQUARES / CTL_FILE_CHARGE + 1);
                free(u);
                free(many);
                many = malloc(60001);
                ml = 0;
                for (int k = 0; k < CTL_READ_SQUARES / CTL_FILE_CHARGE + 5; k++) ml += (size_t)snprintf(many + ml, 60000 - ml, "job %d say \"x\"\n", n2);
                u = ctl_ask(&b, many);                           /* a line to the GM and the log each */
                CHECK(u && !strncmp(u, "error limit: line ", 18) && atoi(u + 18) == CTL_READ_SQUARES / CTL_FILE_CHARGE + 1);
                free(u);
                free(many);
                char q4[96];
                snprintf(q4, sizeof q4, "changes %u\nchanges %u\nchanges %u", b.world.event_seq, b.world.event_seq, b.world.event_seq);
                u = ctl_ask(&b, q4);
                CHECK(u && !strncmp(u, "ok\n", 3));
                free(u);
                snprintf(q4, sizeof q4, "changes %u\nchanges %u\nchanges %u\nchanges %u", b.world.event_seq, b.world.event_seq, b.world.event_seq, b.world.event_seq);
                u = ctl_ask(&b, q4);
                CHECK(ctl_refused(u, "limit", 4) && strstr(u, "at most three times"));
                free(u);
            }
            /* changes never refuses for it: what it may not read it lists. */
            CsWhere all;
            memset(&all, 0xff, sizeof all);
            unsigned s9 = b.world.event_seq;
            world_event(&b.world, CTL_EV_CHANGED, 0, "everything");
            world_event_where(&b.world, &all, 0);
            snprintf(q, sizeof q, "describe\nchanges %u", s9);   /* 204,800 read, then 95,200 to spare: more than its answer holds */
            u = ctl_ask(&b, q);
            CHECK(u && !strncmp(u, "ok\n", 3) && strstr(u, "\nchanged A1:") && strstr(u, "\nmore: "));
            free(u);
            /* Within one region of the bound: the squares, not the answer's bytes, turn the first away. */
            snprintf(q, sizeof q, "describe\ndump A1:SR185\nchanges %u", s9);   /* 204,800 + 94,720 = 299,520 */
            u = ctl_ask(&b, q);
            const char *cm = u ? strstr(u, "\nseq ") : NULL;
            CHECK(u && !strncmp(u, "ok\n", 3) && cm && !strstr(cm, "\nchanged ") && strstr(cm, "\nmore: A1:"));
            free(u);
            /* A scene's creatures against the map's is work that is no square, and counted
             * (HEALTH 72): as many diffs as the bound holds, and the next refused. */
            {
                for (int k = 0; k < 500; k++) {
                    Token c = { (int16_t)(k % 100), (int16_t)(300 + k / 100), 1, TOKEN_ENEMY, "" };
                    snprintf(c.label, sizeof c.label, "c%d", k);
                    tokens_add(&b.world.map->tokens, c);
                }
                char serr[160];
                int  si = scene_save(b.world.map, "crowd", NULL, serr, sizeof serr);
                CHECK(si >= 0);
                long one = si >= 0 ? (long)b.world.map->scenes[si].tokens.n * b.world.map->tokens.n / 8 + 1 : 1;
                int  fit = (int)(CTL_READ_SQUARES / one);
                CHECK(b.world.map->tokens.n >= 500 && fit >= 2 && fit < 40);
                char many[1024] = "";
                for (int k = 0; k < fit; k++) strcat(many, "scene diff crowd\n");
                u = ctl_ask(&b, many);
                CHECK(u && !strncmp(u, "ok\n", 3));
                free(u);
                strcat(many, "scene diff crowd\n");
                u = ctl_ask(&b, many);
                CHECK(ctl_refused(u, "limit", fit + 1));
                free(u);
            }
            /* --apply on a file has no GM waiting on a frame: no bound. */
            b.world.ctl_direct = 1;
            u = ctl_ask(&b, "describe\ncheck\ndump all");
            CHECK(u && !strncmp(u, "ok\n", 3));
            free(u);
            b.world.ctl_direct = 0;
        }
        app_free(&b);
        rnd_free(&r2);
        sandbox_leave(&sb2);
    }

    CASE("changes: its words");
    t = ctl_ask(&a, "changes");
    CHECK(ctl_refused(t, "usage", 1) && strstr(t, "changes SEQ [REGION]"));
    free(t);
    t = ctl_ask(&a, "changes soon");
    CHECK(ctl_refused(t, "usage", 1));
    free(t);
    t = ctl_ask(&a, "changes 0 ZZ99");
    CHECK(ctl_refused(t, "usage", 1));
    free(t);
    t = ctl_ask(&a, "changes 0 A1 more");
    CHECK(ctl_refused(t, "usage", 1));
    free(t);

    CASE("changes: nothing since SEQ is seq alone; an event that changed no square adds nothing");
    char rq[64];
    snprintf(rq, sizeof rq, "changes %u", a.world.event_seq);
    t = ctl_ask(&a, rq);
    snprintf(rq, sizeof rq, "ok\nseq %u\n", a.world.event_seq);
    CHECK(t && !strcmp(t, rq));
    free(t);
    CHECK(a.world.agent_seen_ms != 0);                           /* a reader of changes is listening: the checkpoint stays */

    CASE("changes: more than one answer holds is listed, not sent; every piece is in one or the other; a region asks for less");
    ctl_blank_map(&a, sb.dir, 512, 400);                   /* pieces of 32x25 squares */
    if (a.world.map) {
        unsigned s0 = a.world.event_seq;
        CsWhere w = { { 0 } };
        static const int AT[][2] = { { 0, 0 }, { 2, 0 }, { 4, 2 }, { 6, 4 }, { 8, 6 }, { 10, 8 }, { 12, 10 }, { 15, 15 },
                                     { 14, 0 }, { 0, 3 }, { 2, 5 }, { 4, 7 }, { 6, 9 }, { 8, 11 }, { 10, 13 }, { 12, 15 }, { 14, 2 }, { 0, 6 } };
        int npieces = (int)(sizeof AT / sizeof *AT);
        for (int k = 0; k < npieces; k++) cs_where_put(&w, AT[k][0], AT[k][1], 1);
        world_event(&a.world, CTL_EV_CHANGED, 0, "walls in A1");
        world_event_where(&a.world, &w, 0);
        snprintf(rq, sizeof rq, "changes %u", s0);
        t = ctl_ask(&a, rq);
        const char *more = t ? strstr(t, "\nmore: ") : NULL;
        int sent = 0, listed = 0;
        for (int k = 0; k < npieces && t; k++) {
            CsBox b;
            char name[2 * MAP_COORD_MAX + 2], line[48];
            cs_piece_box(512, 400, AT[k][0], AT[k][1], 1, 1, &b);
            cs_box_name(&b, name, sizeof name);
            snprintf(line, sizeof line, "\nchanged %s\n", name);
            int s = strstr(t, line) != NULL;
            snprintf(line, sizeof line, " %s ", name);
            int l = more && strstr(more, line) != NULL;
            CHECK(s != l);                                 /* sent or listed, never both, never neither */
            sent += s; listed += l;
        }
        CHECK(sent >= 3 && listed >= 1);
        CHECK(t && strstr(t, "\nchanged A1:AF25\n"));     /* the first is always sent */
        CHECK(more && strstr(more, " - too much for one answer; dump each\n"));
        CHECK(t && strlen(t) <= CTL_CHANGES_BYTES + 300);
        CHECK(t && !strstr(t, "\nmap  ") && !strstr(t, "\nkey\n"));   /* no foot and no key a region */
        free(t);
        snprintf(rq, sizeof rq, "changes %u B2:C3", s0);    /* clipped to what was asked, inside a piece */
        t = ctl_ask(&a, rq);
        CHECK(t && strstr(t, "\nchanged B2:C3\n   B C\n") && !strstr(t, "more:"));
        free(t);
        snprintf(rq, sizeof rq, "changes %u AG1:AH2", s0);  /* a region nothing changed in */
        t = ctl_ask(&a, rq);
        CHECK(t && !strstr(t, "\nchanged "));
        free(t);

        CASE("changes: a change right across the largest map is cut into regions that each fit an answer");
        s0 = a.world.event_seq;
        memset(&w, 0xff, sizeof w);
        world_event(&a.world, CTL_EV_CHANGED, 0, "everything");
        world_event_where(&a.world, &w, 0);
        snprintf(rq, sizeof rq, "changes %u", s0);
        t = ctl_ask(&a, rq);
        CHECK(t && strstr(t, "\nchanged A1:") && strstr(t, "\nmore: ") && strlen(t) <= CTL_CHANGES_BYTES + 300);
        const char *m2 = t ? strstr(t, "\nmore: ") : NULL;
        if (m2) {
            char first[32];
            sscanf(m2 + 7, "%31s", first);
            snprintf(rq, sizeof rq, "changes %u %s", s0, first);
            char *t3 = ctl_ask(&a, rq);
            char line[48];
            snprintf(line, sizeof line, "\nchanged %s\n", first);
            CHECK(t3 && strstr(t3, line) && !strstr(t3, "more:"));
            free(t3);
        }
        free(t);

        CASE("changes: pieces side by side and one below are one region; an L is two; the budget is this line's own");
        s0 = a.world.event_seq;
        memset(&w, 0, sizeof w);
        ctl_blank_map(&a, sb.dir, 64, 64);                 /* pieces of 4x4 */
        s0 = a.world.event_seq;
        cs_where_put(&w, 0, 0, 1); cs_where_put(&w, 1, 0, 1); cs_where_put(&w, 0, 1, 1); cs_where_put(&w, 1, 1, 1);   /* A1:H8 */
        cs_where_put(&w, 4, 4, 1); cs_where_put(&w, 5, 4, 1); cs_where_put(&w, 4, 5, 1);                               /* Q17:X20, Q21:T24 */
        world_event(&a.world, CTL_EV_CHANGED, 0, "walls");
        world_event_where(&a.world, &w, 0);
        snprintf(rq, sizeof rq, "changes %u", s0);
        t = ctl_ask(&a, rq);
        CHECK(t && strstr(t, "\nchanged A1:H8\n") && strstr(t, "\nchanged Q17:X20\n") && strstr(t, "\nchanged Q21:T24\n"));
        int nreg = 0;
        for (const char *p = t; p && (p = strstr(p, "\nchanged ")); p++) nreg++;
        CHECK_EQ(nreg, 3);
        free(t);

        CASE("changes: a region's real bytes decide, not its squares: one full of creatures is listed, not sent past the answer");
        a.world.ctl_auto = 1;
        char *plan = malloc(60000);
        size_t pl = 0;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                pl += (size_t)snprintf(plan + pl, 60000 - pl, "token add enemy %c%d \"Ghoul %d\"\ntoken set \"Ghoul %d\" note \"%0*d\"\n",
                                       'A' + x, y + 1, y * 16 + x, y * 16 + x, 40, 7);
        t = ctl_ask(&a, plan);
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        free(plan);
        a.world.ctl_auto = 0;
        s0 = a.world.event_seq;
        memset(&w, 0, sizeof w);
        for (int pr = 0; pr < 4; pr++) for (int pc = 0; pc < 4; pc++) cs_where_put(&w, pc, pr, 1);    /* A1:P16, 256 creatures with notes */
        cs_where_put(&w, 10, 10, 1);
        world_event(&a.world, CTL_EV_CHANGED, 0, "a crowd");
        world_event_where(&a.world, &w, 0);
        snprintf(rq, sizeof rq, "changes %u", s0);
        t = ctl_ask(&a, rq);
        CHECK(t && strlen(t) <= CTL_CHANGES_BYTES + 300);
        CHECK(t && strstr(t, "\nmore: A1:P16 ") && !strstr(t, "\nchanged A1:P16\n"));
        free(t);
        snprintf(rq, sizeof rq, "dump A1:D4\nchanges %u AO41", s0);   /* a dump before it does not eat its budget */
        t = ctl_ask(&a, rq);
        CHECK(t && strstr(t, "\nchanged AO41\n") && !strstr(t, "more:"));
        free(t);

        CASE("changes: after the map was closed and another opened, the old map's pieces are nobody's");
        s0 = a.world.event_seq - 1;                              /* from before the crowd's event */
        ctl_blank_map(&a, sb.dir, 80, 60);
        snprintf(rq, sizeof rq, "changes %u", s0);
        t = ctl_ask(&a, rq);
        CHECK(t && strstr(t, " map closed: ") && !strstr(t, "\nchanged ") && !strstr(t, "more:"));
        free(t);
        s0 = a.world.event_seq;
        memset(&w, 0, sizeof w);
        cs_where_put(&w, 0, 0, 1);
        world_event(&a.world, CTL_EV_CHANGED, 0, "walls in A1");   /* on the new map: said as ever */
        world_event_where(&a.world, &w, 0);
        snprintf(rq, sizeof rq, "changes %u", s0);
        t = ctl_ask(&a, rq);
        CHECK(t && strstr(t, "\nchanged A1:E4\n"));
        free(t);
    }

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* ------------------------------------------------------------ agents' names */

/* docs/V2.md, D8: a request says who is asking; a job is its taker's. */
void test_ctl_names(void)
{
    Sandbox sb = sandbox_enter("ctlnames");
    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 20, 12));
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    char *t;

    CASE("status says who vtt took the caller for: the name on the request's first line, or cli");
    t = ctl_ask(&a, "status");
    CHECK(t && strstr(t, "\nyou cli\n"));
    free(t);
    t = ctl_ask(&a, "agent mapper\nstatus");
    CHECK(t && !strncmp(t, "ok\n", 3) && strstr(t, "\nyou mapper\n"));
    free(t);
    t = ctl_ask(&a, "# a comment first\n\nagent mapper-2\nstatus");   /* comments and blanks are not lines */
    CHECK(t && strstr(t, "\nyou mapper-2\n"));
    free(t);
    t = ctl_ask(&a, "status");                               /* a name is its request's, never the next one's */
    CHECK(t && strstr(t, "\nyou cli\n"));
    free(t);

    CASE("a name written wrong, twice, or below another line is refused: nothing is credited to a guess");
    static const char *const BAD[] = { "agent\nstatus", "agent two words\nstatus", "agent b@d\nstatus",
                                       "agent abcdefghijabcdefghijabcdefghij12\nstatus" };
    for (size_t k = 0; k < sizeof BAD / sizeof *BAD; k++) {
        t = ctl_ask(&a, BAD[k]);
        CHECK(ctl_refused(t, "usage", 1) && strstr(t, "agent NAME: a name is 1-31 letters"));
        free(t);
    }
    t = ctl_ask(&a, "status\nagent mapper");
    CHECK(ctl_refused(t, "usage", 2) && strstr(t, "goes first in a request"));
    free(t);
    t = ctl_ask(&a, "agent mapper\nagent other\nstatus");   /* lines are counted from under the name */
    CHECK(ctl_refused(t, "usage", 1));
    free(t);
    t = ctl_ask(&a, "agent abcdefghijabcdefghijabcdefghij1\nstatus");   /* 31: the longest */
    CHECK(t && strstr(t, "\nyou abcdefghijabcdefghijabcdefghij1\n"));
    free(t);

    CASE("the name's line is not one of the request's: a wait or an undo under it still goes alone");
    t = ctl_ask(&a, "agent mapper\nwait 0");
    CHECK(t && !strncmp(t, "ok\nseq ", 7));
    free(t);
    t = ctl_ask(&a, "agent mapper\nwait 0\nstatus");
    CHECK(t && strstr(t, "wait goes in a request of its own"));
    free(t);
    t = ctl_ask(&a, "agent\nundo");                          /* a name written wrong is what is wrong, not the undo under it */
    CHECK(ctl_refused(t, "usage", 1) && strstr(t, "agent NAME: a name is"));
    free(t);
    t = ctl_ask(&a, "agent two words\nwait");
    CHECK(ctl_refused(t, "usage", 1) && strstr(t, "agent NAME: a name is"));
    free(t);
    CHECK_EQ(ctl_held_ms("# c\n\n  agent mapper\nwait 5 for 30\n"), 30000);
    CHECK(ctl_name_line("# c\n\n agent x\nstatus") && !ctl_name_line("status\nagent x") && !ctl_name_line("agents\n") &&
          ctl_name_line("agent") && !ctl_name_line(""));
    CHECK_EQ(ctl_held_ms("agent mapper\nwait 5 for 30\n"), 30000);
    CHECK_EQ(ctl_held_ms("agent mapper\nstatus\n"), 0);
    CHECK(ctl_name_ok("job-12") && ctl_name_ok("A_b-9") && !ctl_name_ok("") && !ctl_name_ok("a b") && !ctl_name_ok("a/b"));

    CASE("a job is its taker's: another name may read it and nothing else, and jobs says whose it is");
    press(&a, ":ask a well\r");
    int num = 0;
    for (int i = 0; i < JOB_MAX; i++) if (a.world.jobs[i].used) num = a.world.jobs[i].num;
    char q[160];
    snprintf(q, sizeof q, "agent mapper\njob %d take", num);
    t = ctl_ask(&a, q);
    CHECK(t && !strncmp(t, "ok\n", 3) && strstr(t, ", taken by mapper"));
    free(t);
    CHECK(strstr(a.status, "taken by mapper") != NULL);
    t = ctl_ask(&a, "jobs json");
    CHECK(t && json_valid(t + 3) && strstr(t, "\"taker\":\"mapper\""));
    free(t);
    static const char *const ACT[] = { "take", "area B2:C3", "say \"mine now\"", "drop", "propose\ntile B2 water" };
    for (size_t k = 0; k < sizeof ACT / sizeof *ACT; k++) {
        snprintf(q, sizeof q, "agent other\njob %d %s", num, ACT[k]);
        t = ctl_ask(&a, q);
        CHECK(ctl_refused(t, "state", 1) && strstr(t, "is mapper's, and you are other"));
        free(t);
        snprintf(q, sizeof q, "job %d %s", num, ACT[k]);      /* nor the caller with no name */
        t = ctl_ask(&a, q);
        CHECK(ctl_refused(t, "state", 1) && strstr(t, "and you are cli"));
        free(t);
    }
    snprintf(q, sizeof q, "agent mapper\njob %d say \"digging\"", num);
    t = ctl_ask(&a, q);
    CHECK(t && !strncmp(t, "ok\n", 3));
    free(t);

    CASE("saying where it will work is having the job: area claims it as take does");
    {
        press(&a, ":ask a moat\r");
        int n2 = 0;
        for (int i = 0; i < JOB_MAX; i++) if (a.world.jobs[i].used && a.world.jobs[i].num > n2) n2 = a.world.jobs[i].num;
        snprintf(q, sizeof q, "agent mapper\njob %d area B2:C3", n2);
        t = ctl_ask(&a, q);
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        snprintf(q, sizeof q, "agent other\njob %d propose\ntile B2 water", n2);
        t = ctl_ask(&a, q);
        CHECK(ctl_refused(t, "state", 1) && strstr(t, "is mapper's"));
        free(t);
        snprintf(q, sizeof q, "agent other\njob %d take", n2);
        t = ctl_ask(&a, q);
        CHECK(ctl_refused(t, "state", 1));
        free(t);
        snprintf(q, sizeof q, ":ask %d remove\r", n2);
        press(&a, q);
    }

    CASE("given back, a job is anyone's again; a proposal of one's own is one's own");
    snprintf(q, sizeof q, "agent mapper\njob %d drop", num);
    t = ctl_ask(&a, q);
    CHECK(t && !strncmp(t, "ok\n", 3));
    free(t);
    snprintf(q, sizeof q, "agent other\njob %d propose\ntile B2 water", num);   /* propose takes it */
    t = ctl_ask(&a, q);
    CHECK(t && !strncmp(t, "ok\n", 3));
    free(t);
    snprintf(q, sizeof q, "agent mapper\njob %d drop", num);
    t = ctl_ask(&a, q);
    CHECK(ctl_refused(t, "state", 1) && strstr(t, "is other's"));
    free(t);
    t = ctl_ask(&a, "agent mapper\npropose \"a pool\"\ntile D4 water");
    const char *hash = t ? strstr(t, "proposal #") : NULL;
    int mine = hash ? atoi(hash + 10) : 0;
    CHECK(mine > 0 && mine != num);
    free(t);
    snprintf(q, sizeof q, "agent other\njob %d drop", mine);
    t = ctl_ask(&a, q);
    CHECK(ctl_refused(t, "state", 1) && strstr(t, "is mapper's"));
    free(t);
    snprintf(q, sizeof q, "agent other\njob %d dump D4", mine);   /* reading it is anyone's */
    t = ctl_ask(&a, q);
    CHECK(t && !strncmp(t, "ok\n", 3));
    free(t);

    CASE("feedback on a job another name took goes to that name: no second agent is started to be refused");
    {
        char mark[700], cmdline[900];
        snprintf(mark, sizeof mark, "%s/started", sb.dir);
        snprintf(cmdline, sizeof cmdline, ":agent command cat > /dev/null; touch %s\r", mark);
        press(&a, cmdline);
        press(&a, ":log on\r");
        snprintf(q, sizeof q, ":review %d\r", num);         /* other's proposal on the GM's job */
        press(&a, q);
        press(&a, "c");
        press(&a, "deeper\r");
        /* The GM's line said whose it went back to; the review moved on to the next ready job
         * and wrote over it, so the session's log is where it is read. */
        char *logged = slurp(a.slog.path), back[64];
        snprintf(back, sizeof back, "#%d back to other: deeper", num);
        CHECK(logged && strstr(logged, back));
        free(logged);
        press(&a, "\x1b:log off\r");
        int slot = -1;
        for (int i = 0; i < JOB_MAX; i++) if (a.world.jobs[i].used && a.world.jobs[i].num == num) slot = i;
        CHECK(slot >= 0 && a.world.jobs[slot].state == JOB_WORKING && !strcmp(a.world.jobs[slot].taker, "other"));
        poll(NULL, 0, 150);                                  /* a started command would have touched it by now */
        CHECK(access(mark, F_OK) != 0);
        snprintf(q, sizeof q, "agent other\njob %d propose\ntile B3 water", num);   /* and other answers it */
        t = ctl_ask(&a, q);
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        press(&a, ":agent command off\r");
    }

    CASE("undo is the caller's own: the last change to land is its maker's to take back");
    a.world.ctl_auto = 1;
    t = ctl_ask(&a, "agent mapper\ntile F6 water");
    CHECK(t && !strncmp(t, "ok\n", 3));
    free(t);
    CHECK_EQ(map_tile(a.world.map, 5, 5), TILE_WATER);
    t = ctl_ask(&a, "agent other\nundo");
    CHECK(ctl_refused(t, "state", 1) && strstr(t, "is mapper's, and you are other"));
    free(t);
    t = ctl_ask(&a, "undo");
    CHECK(ctl_refused(t, "state", 1) && strstr(t, "and you are cli"));
    free(t);
    CHECK_EQ(map_tile(a.world.map, 5, 5), TILE_WATER);
    t = ctl_ask(&a, "agent mapper\nundo");
    CHECK(t && !strncmp(t, "ok\n", 3));
    free(t);
    CHECK(map_tile(a.world.map, 5, 5) != TILE_WATER);
    a.world.ctl_auto = 0;

    CASE("what an agent does is an event for every other name, and none for its own");
    {
        unsigned s0 = a.world.event_seq;
        char     ev[120];
        press(&a, ":ask a tower\r");
        int n3 = a.world.job_last;
        snprintf(q, sizeof q, "agent mapper\njob %d take", n3);
        t = ctl_ask(&a, q);
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        CHECK_EQ(a.world.event_seq, s0 + 2);                       /* asked, taken */
        snprintf(ev, sizeof ev, "\n%u job %d taken: by mapper\n", s0 + 2, n3);
        snprintf(q, sizeof q, "agent other\nwait %u", s0);
        t = ctl_ask(&a, q);
        CHECK(t && strstr(t, ev) && strstr(t, " asked: "));
        free(t);
        snprintf(q, sizeof q, "wait %u", s0);                /* and the caller with no name */
        t = ctl_ask(&a, q);
        CHECK(t && strstr(t, ev));
        free(t);
        snprintf(q, sizeof q, "agent mapper\nwait %u", s0); /* its own doing is no news to it */
        t = ctl_ask(&a, q);
        CHECK(t && !strstr(t, " taken") && strstr(t, " asked: "));
        free(t);
        snprintf(q, sizeof q, "agent mapper\njob %d take", n3);   /* taken again: nothing new */
        t = ctl_ask(&a, q);
        free(t);
        CHECK_EQ(a.world.event_seq, s0 + 2);

        snprintf(q, sizeof q, "agent mapper\njob %d propose\ntile H8 water", n3);
        t = ctl_ask(&a, q);
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        CHECK_EQ(a.world.event_seq, s0 + 3);
        CHECK_EQ(world_events_after(&a.world, s0 + 2, "mapper"), 0);  /* a wait of its own stays held */
        CHECK_EQ(world_events_after(&a.world, s0 + 2, "other"), 1);
        CHECK_EQ(world_events_after(&a.world, s0 + 2, NULL), 1);
        snprintf(ev, sizeof ev, "\n%u job %d proposed: ground in H8\n", s0 + 3, n3);
        snprintf(q, sizeof q, "agent other\nwait %u", s0 + 2);
        t = ctl_ask(&a, q);
        CHECK(t && strstr(t, ev));
        free(t);
        snprintf(q, sizeof q, "agent mapper\nwait %u", s0 + 2);
        t = ctl_ask(&a, q);
        snprintf(ev, sizeof ev, "ok\nseq %u\n", s0 + 3);
        CHECK(t && !strcmp(t, ev));
        free(t);

        /* Given back, a GM's job is asked again; an agent's own, withdrawn, is scrapped. */
        press(&a, ":ask a ditch\r");
        int n4 = a.world.job_last;
        snprintf(q, sizeof q, "agent mapper\njob %d take", n4);
        t = ctl_ask(&a, q);
        free(t);
        unsigned s1 = a.world.event_seq;
        snprintf(q, sizeof q, "agent mapper\njob %d drop", n4);
        t = ctl_ask(&a, q);
        free(t);
        snprintf(ev, sizeof ev, "\n%u job %d asked: given back by mapper: a ditch\n", s1 + 1, n4);
        snprintf(q, sizeof q, "agent other\nwait %u", s1);
        t = ctl_ask(&a, q);
        CHECK(t && strstr(t, ev));
        free(t);
        snprintf(q, sizeof q, "agent mapper\nwait %u", s1);
        t = ctl_ask(&a, q);
        CHECK(t && !strstr(t, "given back"));
        free(t);
        s1 = a.world.event_seq;                                    /* saying where it works takes a job too, and is told */
        snprintf(q, sizeof q, "agent other\njob %d area B2:C3", n4);
        t = ctl_ask(&a, q);
        free(t);
        snprintf(ev, sizeof ev, "\n%u job %d taken: by other\n", s1 + 1, n4);
        snprintf(q, sizeof q, "agent mapper\nwait %u", s1);
        t = ctl_ask(&a, q);
        CHECK(t && strstr(t, ev));
        free(t);
        t = ctl_ask(&a, "agent mapper\npropose \"a well\"\ntile A12 water");
        int own = a.world.job_last;
        free(t);
        s1 = a.world.event_seq;
        snprintf(q, sizeof q, "agent mapper\njob %d drop", own);
        t = ctl_ask(&a, q);
        free(t);
        snprintf(ev, sizeof ev, "\n%u job %d scrapped: withdrawn by mapper\n", s1 + 1, own);
        snprintf(q, sizeof q, "agent other\nwait %u", s1);
        t = ctl_ask(&a, q);
        CHECK(t && strstr(t, ev));
        free(t);
        snprintf(q, sizeof q, "agent mapper\nwait %u", s1);
        t = ctl_ask(&a, q);
        CHECK(t && !strstr(t, "withdrawn"));
        free(t);
        snprintf(q, sizeof q, ":ask %d remove\r", n4);
        press(&a, q);

    CASE("job N undo takes back a job's landing by number: its taker's, alone, and only while it is the last thing done");
        snprintf(q, sizeof q, ":review %d\r", n3);
        press(&a, q);
        press(&a, "\r");
        press(&a, "\x1b");                                   /* out of whatever review came next */
        CHECK_EQ(map_tile(a.world.map, 7, 7), TILE_WATER);
        snprintf(q, sizeof q, "agent other\njob %d undo", n3);
        t = ctl_ask(&a, q);
        CHECK(ctl_refused(t, "state", 1) && strstr(t, "is mapper's, and you are other"));
        free(t);
        snprintf(q, sizeof q, "agent mapper\njob %d undo\nstatus", n3);
        t = ctl_ask(&a, q);
        CHECK(t && !strcmp(t, "error usage: job N undo goes in a request of its own\n"));
        free(t);
        snprintf(q, sizeof q, "agent mapper\njob %d undo now", n3);
        t = ctl_ask(&a, q);
        CHECK(ctl_refused(t, "usage", 1));
        free(t);
        t = ctl_ask(&a, "agent mapper\njob 98 undo");
        CHECK(ctl_refused(t, "missing", 1));
        free(t);
        undo_begin(&a.world.undo);                                 /* the GM's step, over it */
        undo_set_tile(&a.world.undo, a.world.map, 0, 0, TILE_ROUGH);
        undo_end(&a.world.undo);
        snprintf(q, sizeof q, "agent mapper\njob %d undo", n3);
        t = ctl_ask(&a, q);
        CHECK(ctl_refused(t, "state", 1) && strstr(t, "something has changed the map since"));
        free(t);
        CHECK_EQ(map_tile(a.world.map, 7, 7), TILE_WATER);
        undo_undo(&a.world.undo, a.world.map);                           /* the GM's u */
        s1 = a.world.event_seq;
        t = ctl_ask(&a, q);
        snprintf(ev, sizeof ev, "ok\ntook back #%d\n", n3);
        CHECK(t && !strcmp(t, ev));
        free(t);
        CHECK(map_tile(a.world.map, 7, 7) != TILE_WATER);
        CHECK(strstr(a.status, "taken back by mapper") != NULL);
        /* An event with where, for the others: `changes` reads back the squares, and not for its maker. */
        snprintf(ev, sizeof ev, "job %d undone: taken back by mapper\n", n3);
        snprintf(q, sizeof q, "agent other\nchanges %u", s1);
        t = ctl_ask(&a, q);
        CHECK(t && strstr(t, ev) && strstr(t, "\nchanged "));
        free(t);
        snprintf(q, sizeof q, "agent mapper\nchanges %u", s1);
        t = ctl_ask(&a, q);
        CHECK(t && !strstr(t, "undone") && !strstr(t, "\nchanged "));
        free(t);
        snprintf(q, sizeof q, "agent mapper\njob %d undo", n3);     /* once is all */
        t = ctl_ask(&a, q);
        CHECK(ctl_refused(t, "state", 1) && strstr(t, "has nothing landed to take back"));
        free(t);
        undo_redo(&a.world.undo, a.world.map);                           /* the GM's ctrl-r: plain undo is the same step, the caller's own */
        CHECK_EQ(map_tile(a.world.map, 7, 7), TILE_WATER);
        s1 = a.world.event_seq;
        t = ctl_ask(&a, "agent mapper\nundo");
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        snprintf(ev, sizeof ev, "job %d undone: taken back by mapper", n3);
        /* (The GM's redo before it is told first, as a change of the map's.) */
        CHECK(a.world.event_seq > s1 && !strcmp(a.world.events[a.world.event_seq % EVENT_MAX].text, ev));
        t = ctl_ask(&a, "agent mapper\nundo");                /* and once is all for it too */
        CHECK(ctl_refused(t, "state", 1) && (strstr(t, "there is no change of the agent's to take back") || strstr(t, "undo is once")));
        free(t);

        /* Accepted in two parts, a job is two steps: both go, and nothing under them. */
        press(&a, ":ask two pools\r");
        int n5 = a.world.job_last;
        snprintf(q, sizeof q, "agent mapper\njob %d propose\ntile B10 water\ntile D10 water", n5);
        t = ctl_ask(&a, q);
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        int depth0 = a.world.undo.depth;
        a.ed.cx = 1; a.ed.cy = 9;
        snprintf(q, sizeof q, ":review %d\rv\r", n5);        /* the box is B10 alone */
        press(&a, q);
        CHECK(map_tile(a.world.map, 1, 9) == TILE_WATER && map_tile(a.world.map, 3, 9) != TILE_WATER);
        press(&a, "\r");                                     /* and the rest */
        press(&a, "\x1b");
        CHECK_EQ(map_tile(a.world.map, 3, 9), TILE_WATER);
        CHECK_EQ(a.world.undo.depth, depth0 + 2);
        snprintf(q, sizeof q, "agent mapper\njob %d undo", n5);
        t = ctl_ask(&a, q);
        snprintf(ev, sizeof ev, "ok\ntook back #%d, every part of it\n", n5);
        CHECK(t && !strcmp(t, ev));
        free(t);
        CHECK_EQ(a.world.undo.depth, depth0);
        CHECK(map_tile(a.world.map, 1, 9) != TILE_WATER && map_tile(a.world.map, 3, 9) != TILE_WATER);

        /* A job removed leaves its steps their author and not its number: a new job #N is not the old one. */
        undo_redo(&a.world.undo, a.world.map);
        snprintf(q, sizeof q, ":ask %d remove\r", n5);
        press(&a, q);
        int still = 0;
        for (int i = 0; i < a.world.undo.ntags; i++) still += a.world.undo.tags[i].job == n5;
        CHECK_EQ(still, 0);
        CHECK(undo_tag_top(&a.world.undo) && !strcmp(undo_tag_top(&a.world.undo)->who, "mapper"));
        undo_undo(&a.world.undo, a.world.map);
    }

    CASE("a landing is its maker's whoever has the job since; an --apply's is the GM's; the GM busy, both undos wait");
    {
        char ev[120];
        /* A part lands for mapper, who then gives the job back: the step is still mapper's. */
        press(&a, ":ask a pond\r");
        int n = a.world.job_last;
        snprintf(q, sizeof q, "agent mapper\njob %d propose\ntile B11 water\ntile D11 water", n);
        t = ctl_ask(&a, q);
        free(t);
        a.ed.cx = 1; a.ed.cy = 10;
        snprintf(q, sizeof q, ":review %d\rv\r", n);          /* B11 alone; the review stays on the rest */
        press(&a, q);
        CHECK(map_tile(a.world.map, 1, 10) == TILE_WATER && a.ed.mode == ED_REVIEW);
        /* While the GM reviews the rest of it, its landed part is not pulled from under the review. */
        snprintf(q, sizeof q, "agent mapper\njob %d undo", n);
        t = ctl_ask(&a, q);
        CHECK(t && !strncmp(t, "busy: ", 6) && strstr(t, "reviewing"));
        free(t);
        t = ctl_ask(&a, "agent mapper\nundo");
        CHECK(t && !strncmp(t, "busy: ", 6));
        free(t);
        CHECK_EQ(map_tile(a.world.map, 1, 10), TILE_WATER);
        press(&a, "\x1b");
        snprintf(q, sizeof q, "agent mapper\njob %d drop", n);
        t = ctl_ask(&a, q);
        free(t);
        snprintf(q, sizeof q, "agent other\njob %d undo", n);   /* nobody's job now, and still not other's step */
        t = ctl_ask(&a, q);
        CHECK(ctl_refused(t, "state", 1) && strstr(t, "mapper's"));
        free(t);
        CHECK_EQ(map_tile(a.world.map, 1, 10), TILE_WATER);
        snprintf(q, sizeof q, "agent other\njob %d propose\ntile F11 water", n);
        t = ctl_ask(&a, q);
        free(t);
        snprintf(q, sizeof q, ":review %d\r", n);
        press(&a, q);
        press(&a, "\r");
        press(&a, "\x1b");
        CHECK_EQ(map_tile(a.world.map, 5, 10), TILE_WATER);
        press(&a, ":");                                      /* the GM typing a command: nothing lands, nothing is taken back */
        snprintf(q, sizeof q, "agent other\njob %d undo", n);
        t = ctl_ask(&a, q);
        CHECK(t && !strncmp(t, "busy: ", 6));
        free(t);
        t = ctl_ask(&a, "agent other\nundo");
        CHECK(t && !strncmp(t, "busy: ", 6));
        free(t);
        CHECK_EQ(map_tile(a.world.map, 5, 10), TILE_WATER);
        press(&a, "\x1b");
        t = ctl_ask(&a, q);                                  /* other's own step, and not mapper's under it */
        snprintf(ev, sizeof ev, "ok\ntook back #%d\n", n);
        CHECK(t && !strcmp(t, ev));
        free(t);
        CHECK(map_tile(a.world.map, 5, 10) != TILE_WATER && map_tile(a.world.map, 1, 10) == TILE_WATER);
        undo_undo(&a.world.undo, a.world.map);                           /* mapper's B11 */
        snprintf(q, sizeof q, ":ask %d remove\r", n);
        press(&a, q);

        /* An --apply's change is the GM's alone: neither undo takes it back. */
        t = ctl_ask(&a, "propose apply \"plan.txt\"\ntile C12 water");
        int ap = a.world.job_last;
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        snprintf(q, sizeof q, ":review %d\r", ap);
        press(&a, q);
        press(&a, "\r");
        press(&a, "\x1b");
        CHECK_EQ(map_tile(a.world.map, 2, 11), TILE_WATER);
        CHECK(undo_tag_top(&a.world.undo) == NULL);
        t = ctl_ask(&a, "undo");
        CHECK(ctl_refused(t, "state", 1));
        free(t);
        snprintf(q, sizeof q, "job %d undo", ap);
        t = ctl_ask(&a, q);
        CHECK(ctl_refused(t, "state", 1) && strstr(t, "the GM's to take back"));
        free(t);
        CHECK_EQ(map_tile(a.world.map, 2, 11), TILE_WATER);
        undo_undo(&a.world.undo, a.world.map);

        /* The newest of two taken back by the GM: undo is once, and says what does reach the older. */
        a.world.ctl_auto = 1;
        t = ctl_ask(&a, "agent mapper\ntile H2 water");
        int first = a.world.job_last;
        free(t);
        t = ctl_ask(&a, "agent mapper\ntile H3 water");
        free(t);
        a.world.ctl_auto = 0;
        undo_undo(&a.world.undo, a.world.map);                           /* the GM's u */
        t = ctl_ask(&a, "agent mapper\nundo");
        snprintf(ev, sizeof ev, "job %d undo", first);
        CHECK(ctl_refused(t, "state", 1) && strstr(t, ev));
        free(t);
        snprintf(q, sizeof q, "agent mapper\njob %d undo", first);
        t = ctl_ask(&a, q);
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        CHECK(map_tile(a.world.map, 7, 1) != TILE_WATER);

        /* A landing that writes nothing to the log (a card alone) tags nothing: the GM's step under it stays the GM's. */
        undo_begin(&a.world.undo);
        undo_set_tile(&a.world.undo, a.world.map, 0, 0, TILE_ROUGH);
        undo_end(&a.world.undo);
        Map      *sc = world_scratch_copy(&a.world);
        ChangeSet cs;
        cs_init(&cs);
        CHECK(card_set(sc, "Bog", "a bog") >= 0);
        CHECK(cs_diff(&cs, a.world.map, sc, NULL) > 0);
        int cslot = world_job_new(&a.world, JOB_FROM_AGENT, "a card", NULL, 0);
        CHECK(cslot >= 0);
        if (cslot >= 0) {
            str_lcpy(a.world.jobs[cslot].taker, "mapper", sizeof a.world.jobs[cslot].taker);
            world_job_set_proposal(&a.world, cslot, &cs, NULL);
            snprintf(q, sizeof q, ":review %d\r", a.world.jobs[cslot].num);
            press(&a, q);
            press(&a, "\r");
            press(&a, "\x1b");
            CHECK(card_find(a.world.map, "Bog") >= 0);
            CHECK(undo_tag_top(&a.world.undo) == NULL);
            t = ctl_ask(&a, "agent mapper\nundo");
            CHECK(ctl_refused(t, "state", 1));
            free(t);
            CHECK_EQ(map_tile(a.world.map, 0, 0), TILE_ROUGH);
        }
        cs_free(&cs);
        undo_undo(&a.world.undo, a.world.map);
    }

    CASE("job N conflicts lists what changed under a waiting proposal: what was, what is now; 64 lines and a count");
    {
        t = ctl_ask(&a, "agent mapper\npropose \"a lake\"\ntile A1:T12 water");
        int lake = a.world.job_last;
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        int slot = -1;
        for (int i = 0; i < JOB_MAX; i++) if (a.world.jobs[i].used && a.world.jobs[i].num == lake) slot = i;
        int cells = slot >= 0 ? a.world.jobs[slot].cs.ncells : 0;
        CHECK(cells > 100);
        char want[160];
        snprintf(q, sizeof q, "agent other\njob %d conflicts", lake);    /* a read: anyone's */
        t = ctl_ask(&a, q);
        snprintf(want, sizeof want, "ok\n#%d has no conflicts: nothing it touches has changed since\n", lake);
        CHECK(t && !strcmp(t, want));
        free(t);
        /* One square the lake would change (some are water already), changed under it. */
        int     cx = slot >= 0 && cells ? a.world.jobs[slot].cs.cells[0].x : 0, cy = slot >= 0 && cells ? a.world.jobs[slot].cs.cells[0].y : 0;
        uint8_t was = map_tile(a.world.map, cx, cy);
        char    sq[MAP_COORD_MAX];
        map_coord_name(cx, cy, sq, sizeof sq);
        undo_begin(&a.world.undo);
        undo_set_tile(&a.world.undo, a.world.map, cx, cy, TILE_ROUGH);
        undo_end(&a.world.undo);
        t = ctl_ask(&a, q);
        snprintf(want, sizeof want, "ok\ntile %s: %s when proposed, %s now\n", sq, tile_name(was), tile_name(TILE_ROUGH));
        CHECK(t && !strcmp(t, want));
        free(t);
        undo_begin(&a.world.undo);
        for (int y = 0; y < a.world.map->h; y++)
            for (int x = 0; x < a.world.map->w; x++) undo_set_tile(&a.world.undo, a.world.map, x, y, TILE_HAZARD);
        undo_end(&a.world.undo);
        t = ctl_ask(&a, q);
        int lines = 0;
        for (const char *p = t ? t : ""; *p; p++) lines += *p == '\n';
        snprintf(want, sizeof want, "\nand %d more\n", cells - CTL_CONFLICT_LINES);
        CHECK_EQ(lines, 1 + CTL_CONFLICT_LINES + 1);         /* ok, the 64, the count */
        CHECK(t && strstr(t, want));
        free(t);
        undo_undo(&a.world.undo, a.world.map);
        undo_undo(&a.world.undo, a.world.map);
        {
            /* Counted as a read: the set against the map each time, till the request's bound. */
            int   fit = CTL_READ_SQUARES / (cells + CTL_FILE_CHARGE);
            char *many = malloc((size_t)(fit + 2) * 24);
            size_t ml = 0;
            for (int k = 0; k <= fit; k++) ml += (size_t)sprintf(many + ml, "job %d conflicts\n", lake);
            t = ctl_ask(&a, many);
            CHECK(ctl_refused(t, "limit", fit + 1));
            free(t);
            free(many);
        }
        {
            /* An edge is named by its square and side; a note put where the proposal puts one is said so. */
            t = ctl_ask(&a, "agent mapper\npropose \"a door\"\nedge A1|B1 wall\nnote E5 \"hi\"");
            int door = a.world.job_last;
            free(t);
            undo_begin(&a.world.undo);
            undo_set_vedge(&a.world.undo, a.world.map, 1, 0, EDGE_WINDOW);
            undo_set_note(&a.world.undo, a.world.map, 4, 4, "there");
            undo_end(&a.world.undo);
            snprintf(q, sizeof q, "job %d conflicts", door);
            t = ctl_ask(&a, q);
            snprintf(want, sizeof want, "ok\nedge west of B1: %s when proposed, %s now\nnote E5: one is there now\n",
                     edge_name(EDGE_NONE), edge_name(EDGE_WINDOW));
            CHECK(t && !strcmp(t, want));
            if (t && strcmp(t, want)) printf("    got:\n%s    want:\n%s", t, want);
            free(t);
            undo_undo(&a.world.undo, a.world.map);
            snprintf(q, sizeof q, "agent mapper\njob %d drop", door);
            t = ctl_ask(&a, q);
            free(t);
        }
        snprintf(q, sizeof q, "job %d conflicts now", lake);
        t = ctl_ask(&a, q);
        CHECK(ctl_refused(t, "usage", 1));
        free(t);
        t = ctl_ask(&a, "job 98 conflicts");
        CHECK(ctl_refused(t, "missing", 1));
        free(t);
        snprintf(q, sizeof q, "agent mapper\njob %d drop", lake);
        t = ctl_ask(&a, q);
        free(t);
        snprintf(q, sizeof q, "job %d conflicts", lake);     /* withdrawn: nothing waits */
        t = ctl_ask(&a, q);
        CHECK(ctl_refused(t, "state", 1) && strstr(t, "has no proposal waiting"));
        free(t);
    }

    CASE("an event's text is cut at its room and never past it");
    {
        char longt[400];
        memset(longt, 'x', sizeof longt - 1);
        longt[sizeof longt - 1] = '\0';
        world_event(&a.world, CTL_EV_FEEDBACK, 7, "%s", longt);
        const AgentEvent *e = &a.world.events[a.world.event_seq % EVENT_MAX];
        CHECK_EQ((long)strlen(e->text), EVENT_TEXT_MAX - 1);
        CHECK(!strncmp(e->text, "job 7 feedback: xxx", 19));
        const char *text;
        size_t      tl;
        int         en;
        char        line[EVENT_TEXT_MAX + 16];
        int         ll = snprintf(line, sizeof line, "%u %s", e->seq, e->text);
        CHECK(ctl_event_parse(line, (size_t)ll, &en, &text, &tl) == CTL_EV_FEEDBACK && en == 7);
        /* More of its own than the ring keeps: nothing for it to hear, but `lost` is something to say. */
        for (int k = 0; k < EVENT_MAX + 4; k++) { world_event(&a.world, CTL_EV_TAKEN, 1, "x"); world_event_by(&a.world, "mapper"); }
        CHECK_EQ(world_events_after(&a.world, a.world.event_seq - 3, "mapper"), 0);
        CHECK(world_events_after(&a.world, a.world.event_seq - EVENT_MAX - 2, "mapper") > 0);
        CHECK_EQ(world_events_after(&a.world, a.world.event_seq - 3, "other"), 3);
    }

    CASE("with the channel off the name is taken and the refusal is the channel's");
    a.world.ctl_can_hold = 1;
    a.world.agent_on = 0;
    t = ctl_ask(&a, "agent mapper\nstatus");
    CHECK(ctl_refused(t, "state", 1) && strstr(t, "the agent channel is off"));
    free(t);
    a.world.ctl_can_hold = 0;

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* docs/AGENTS.md is in the binary (tools/embed.sh) so an agent reads the guide
 * of the vtt installed; the skill (tools/skill/SKILL.md) is a dozen lines that
 * point at it, and may name only flags and requests that exist. */
void test_agent_guide(void)
{
    extern const char   AGENT_GUIDE[];
    extern const size_t AGENT_GUIDE_LEN;
    size_t dl = 0, sl = 0;
    char  *doc   = file_read("docs/AGENTS.md", (size_t)1 << 20, &dl, NULL);
    char  *skill = file_read("tools/skill/SKILL.md", (size_t)1 << 16, &sl, NULL);
    CHECK(doc != NULL && skill != NULL);
    if (!doc || !skill) { free(doc); free(skill); return; }

    CASE("the embedded guide is docs/AGENTS.md byte for byte (run tools/embed.sh)");
    CHECK_EQ(AGENT_GUIDE_LEN, dl);
    CHECK(AGENT_GUIDE_LEN == dl && !memcmp(AGENT_GUIDE, doc, dl));
    CHECK(strstr(doc, "`vtt --agent-guide`") != NULL);

    CASE("the skill has the frontmatter a skill is found by");
    CHECK(!strncmp(skill, "---\nname: vtt\ndescription: ", 27));
    CHECK(strstr(skill, "`vtt --agent-guide`") != NULL);

    CASE("vtt --agent-guide prints it and exits 0");
    Sandbox sb = sandbox_enter("guide");
    CHECK_EQ(sb.ok, 1);
    char *help = NULL;
    if (sb.ok) {
        FILE *p = popen("./vtt --agent-guide", "r");
        CHECK(p != NULL);
        char  *got = malloc(dl + 2);
        size_t n   = p && got ? fread(got, 1, dl + 1, p) : 0;
        CHECK_EQ(p ? WEXITSTATUS(pclose(p)) : -1, 0);
        CHECK(n == dl && got && !memcmp(got, doc, dl));
        free(got);
        p = popen("./vtt --help", "r");
        help = calloc(1, 16384);
        if (p && help) fread(help, 1, 16383, p);
        if (p) pclose(p);
    }

    CASE("every flag the skill names is in --help; every request in it a verb, a job's a sub-verb");
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    char *hjob = ctl_ask(&a, "help job");
    /* A sub-verb's own arguments (`dump [REGION|all|key]`) are not sub-verbs. */
    for (char *b = hjob ? strchr(hjob, '[') : NULL; b; b = strchr(b, '['))
        for (; *b && *b != ']'; b++) *b = ' ';
    int flags = 0, verbs = 0, subs = 0;
    for (const char *s = strchr(skill, '`'); help && hjob && s; ) {
        const char *e = strchr(s + 1, '`');
        if (!e) break;
        const char *rq = !strncmp(s + 1, "job ", 4) ? s + 1 : NULL;   /* a request header alone */
        for (const char *t = s + 1; t < e; ) {
            while (t < e && *t == ' ') t++;
            const char *w = t;
            while (t < e && *t != ' ') t++;
            if (t - w < 3 || w[0] != '-' || w[1] != '-') continue;
            char want[64];
            snprintf(want, sizeof want, "  %.*s ", (int)(t - w), w);
            if (!strstr(help, want)) { printf("    not in --help: %s\n", want); CHECK(0); }
            flags++;
            if (!strncmp(w, "--ctl ", 6) && t < e) rq = t + 1;
        }
        if (rq && (*rq == '\'' || *rq == '"')) rq++;
        if (rq && strncmp(rq, "<<", 2)) {                           /* a heredoc's request is not on this line */
            size_t vl = strcspn(rq, " '\"`");
            int    ok = 0;
            for (int i = 0; request_verb(i, NULL); i++)
                if (strlen(request_verb(i, NULL)) == vl && !strncmp(request_verb(i, NULL), rq, vl)) ok = 1;
            if (!ok) { printf("    not a request: %.*s\n", (int)vl, rq); CHECK(0); }
            verbs++;
            if (vl == 3 && !strncmp(rq, "job", 3)) {                /* job N SUB: SUB as `help job` writes it */
                const char *sub = rq + 4;
                sub += strcspn(sub, " '\"`");
                sub += *sub == ' ';
                size_t sl2 = strcspn(sub, " '\"`");
                char   a1[48], a2[48];
                snprintf(a1, sizeof a1, "job N %.*s", (int)sl2, sub);
                snprintf(a2, sizeof a2, "|%.*s", (int)sl2, sub);
                int found = 0;
                for (const char *h = hjob; !found && (h = strstr(h, a1)); h++)
                    found = strchr(" |\n", h[strlen(a1)]) != NULL;
                for (const char *h = hjob; !found && (h = strstr(h, a2)); h++)
                    found = strchr(" |\n", h[strlen(a2)]) != NULL;
                if (!found || !sl2) { printf("    not a job request: %.*s\n", (int)sl2, sub); CHECK(0); }
                subs++;
            }
        }
        s = strchr(e + 1, '`');
    }
    CHECK(flags > 8);
    CHECK(verbs > 5);
    CHECK(subs >= 3);
    free(hjob);
    app_free(&a);
    rnd_free(&r);

    free(help);
    free(doc);
    free(skill);
    sandbox_leave(&sb);
}
