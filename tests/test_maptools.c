/* Tests: the map tools: --dump-map, --describe, --check, the loader's diagnostics. */

#include "harness.h"

/* A map built by hand: rooms and what joins them. */
static Map *rooms_fixture(void)
{
    /* Three rooms in a row, 3 wide each, walls between: A|B through a door,
     * B|C through a window only. And a sealed D below A, and a void gap. */
    Map *m = map_new(11, 5, "rooms");
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 11; x++) map_set_tile(m, x, y, x == 3 || x == 7 ? TILE_VOID : TILE_FLOOR);
    map_set_tile(m, 3, 1, TILE_FLOOR);                      /* the door squares across the gaps */
    map_set_tile(m, 7, 1, TILE_FLOOR);
    for (int y = 0; y < 5; y++) { map_set_vedge(m, 3, y, EDGE_WALL); map_set_vedge(m, 4, y, EDGE_WALL); }
    map_set_vedge(m, 3, 1, EDGE_DOOR_CLOSED);               /* A <-> corridor square */
    map_set_vedge(m, 4, 1, EDGE_DOOR_OPEN);                 /* corridor square <-> B */
    for (int y = 0; y < 5; y++) { map_set_vedge(m, 7, y, EDGE_WALL); map_set_vedge(m, 8, y, EDGE_WALL); }
    map_set_vedge(m, 7, 1, EDGE_WINDOW);
    map_set_vedge(m, 8, 1, EDGE_WINDOW);
    for (int x = 0; x < 3; x++) map_set_hedge(m, x, 3, EDGE_WALL);   /* D: rows 3-4 of A's column */
    Token t = { 0 };
    t.x = 1; t.y = 1; t.size = 1; t.kind = TOKEN_PLAYER;
    str_lcpy(t.label, "Aria \"the Bold\"", sizeof t.label);         /* a quote to escape */
    tokens_add(&m->tokens, t);
    return m;
}

void test_map_tools_describe(void)
{
    char err[256];

    CASE("rooms: doors of any kind join them for reaching, windows and walls do not, every door splits");
    Map *m = rooms_fixture();
    Rooms r;
    rooms_build(m, &r);
    /* A (rows 0-2 of x 0-2), the corridor square D2... names in reading order. */
    CHECK_EQ(r.n, 6);
    int a = rooms_at(&r, m, 0, 0), cor1 = rooms_at(&r, m, 3, 1), b = rooms_at(&r, m, 5, 0);
    int cor2 = rooms_at(&r, m, 7, 1), c = rooms_at(&r, m, 9, 0), d = rooms_at(&r, m, 0, 4);
    CHECK(a != cor1 && cor1 != b && b != cor2 && cor2 != c && d != a);
    CHECK_EQ(r.start, a);                                   /* Aria's room */
    CHECK(r.reach[a] && r.reach[cor1] && r.reach[b]);       /* a closed door and an open one */
    CHECK(!r.reach[cor2] && !r.reach[c]);                   /* windows only */
    CHECK(!r.reach[d]);                                     /* sealed by a wall */
    CHECK_EQ(r.v[a].fx, 0);
    CHECK_EQ(r.v[a].squares, 9);
    rooms_free(&r);

    CASE("describe names rooms by their first square and says where each door leads; JSON parses");
    size_t n;
    char *t = describe_text(m, 0, &n);
    CHECK(strstr(t, "room 1 (A1)") != NULL);
    CHECK(strstr(t, "not reachable") != NULL);
    CHECK(strstr(t, "NOT REACHABLE") != NULL);
    CHECK(strstr(t, "door         C2|D2    to room") != NULL);
    free(t);
    t = describe_text(m, 1, &n);
    CHECK(json_valid(t));
    CHECK(strstr(t, "\"label\":\"Aria \\\"the Bold\\\"\"") != NULL);   /* escaped */
    CHECK(strstr(t, "\"reachable\":false") != NULL);
    free(t);
    map_free(m);

    CASE("the goldens: the fixture with every boundary kind, as text and as JSON");
    m = mapio_load("tests/fixtures/kinds.vtt", err, sizeof err);
    CHECK(m != NULL);
    if (m) {
        t = describe_text(m, 0, &n);
        golden_bytes("describe-kinds", t, n);
        free(t);
        t = describe_text(m, 1, &n);
        CHECK(json_valid(t));
        golden_bytes("describe-kinds-json", t, n);
        free(t);
        map_free(m);
    }

    CASE("the worst case: 512x512 with a wall on every boundary is 262,144 rooms, linearly");
    m = map_new(512, 512, "cells");
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 512; x++) {
            map_set_tile(m, x, y, TILE_FLOOR);
            map_set_vedge(m, x, y, EDGE_WALL);
            map_set_hedge(m, x, y, EDGE_WALL);
        }
    uint64_t t0 = prof_now_ns();
    rooms_build(m, &r);
    uint64_t took = prof_now_ns() - t0;
    CHECK_EQ(r.n, 512 * 512);
    CHECK(took < 2000000000ull);                            /* generous: ASan, a busy machine */
    rooms_free(&r);
    map_free(m);

    CASE("the account of the worst case is written in full");
    m = map_new(512, 512, "cells");
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 512; x++) {
            map_set_tile(m, x, y, TILE_FLOOR);
            map_set_vedge(m, x, y, EDGE_WALL);
            map_set_hedge(m, x, y, EDGE_WALL);
        }
    t = describe_text(m, 0, &n);
    CHECK(strstr(t, "room 262144 (SR512)") != NULL);
    free(t);
    map_free(m);

    CASE("a door in a stub of wall inside one room is listed once");
    m = map_new(3, 3, "stub");
    for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) map_set_tile(m, x, y, TILE_FLOOR);
    map_set_vedge(m, 1, 1, EDGE_DOOR_CLOSED);             /* a door standing alone in the room */
    t = describe_text(m, 0, &n);
    const char *first = strstr(t, "B2|");
    CHECK(first == NULL || strstr(first + 1, "B2|") == NULL);
    const char *door = strstr(t, "A2|B2");
    CHECK(door != NULL && strstr(door + 1, "A2|B2") == NULL);
    free(t);
    map_free(m);

    CASE("one open 512x512 room is one room, filled without recursion");
    m = map_new(512, 512, "open");
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 512; x++) map_set_tile(m, x, y, TILE_FLOOR);
    rooms_build(m, &r);
    CHECK_EQ(r.n, 1);
    CHECK_EQ(r.v[0].squares, 512 * 512);
    rooms_free(&r);
    map_free(m);
}

static char *check_text(const char *path, int json, int *rc, size_t *len)
{
    char  *buf = NULL;
    size_t n   = 0;
    FILE  *f   = open_memstream(&buf, &n);
    *rc = maptools_check(f, path, json);
    fclose(f);
    if (len) *len = n;
    return buf;
}

/* A scene's block that does not stand is dropped with W025, and the map's
 * own creatures are not touched by it. */
static void scene_findings(void)
{
    char path[] = "/tmp/vtt-w025-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return;
    const char *text =
        "VTT 11\nname W\nsize 4 3\ntiles\n....\n....\n....\n"
        "token player 0 0 1 \"Aria\"\n"
        "scene \"Good\"\ntoken enemy 1 1 1 \"Imp\"\nround 3\nendscene\n"
        "scene \"Good\"\nendscene\n"
        "scene \"Boxed\" 0 0 9 9\nendscene\n"
        "endscene\n"
        "scene \"\ntoken enemy 1 1 1 \"Lost\"\n"
        "scene \"Open\"\ntoken enemy 2 2 1 \"Rat\"\n";
    if (write(fd, text, strlen(text)) < 0) { close(fd); unlink(path); return; }
    close(fd);
    int rc;
    char *t = check_text(path, 0, &rc, NULL);
    CHECK(strstr(t, "scene Good dropped: a scene of that name came before it") != NULL);
    CHECK(strstr(t, "scene Boxed dropped: its box is not on the map") != NULL);
    CHECK(strstr(t, "endscene with no scene open") != NULL);
    CHECK(strstr(t, "scene Open dropped: the file ends before its endscene") != NULL);
    CHECK(strstr(t, "scene ? dropped: its name does not read") != NULL);
    CHECK(strstr(t, "scene  dropped") == NULL);            /* one finding for the unnamed block, not two */
    free(t);
    char err[200];
    Map *m = mapio_load(path, err, sizeof err);
    CHECK(m != NULL);
    if (m) {
        CHECK_EQ(m->nscenes, 1);
        CHECK(m->tokens.n == 1 && !strcmp(m->tokens.v[0].label, "Aria"));
        CHECK(m->scenes[0].tokens.n == 1 && m->scenes[0].round == 0);   /* no fight: the round is sanitized */
        map_free(m);
    }
    unlink(path);
}

void test_map_tools_check(void)
{
    int    rc;
    size_t n;

    CASE("the shipped fixtures are clean: exit 0");
    static const char *const clean[] = { "tests/fixtures/two-rooms.vtt", "tests/fixtures/kinds.vtt",
                                         "tests/fixtures/crowd.vtt" };
    for (int i = 0; i < 3; i++) {
        char *t = check_text(clean[i], 0, &rc, NULL);
        CHECK_EQ(rc, 0);
        CHECK(strstr(t, "no findings") != NULL);
        free(t);
    }

    CASE("scenes that do not stand are dropped with W025");
    scene_findings();

    CASE("the broken fixture: one of every mistake, each found, in a stable order; exit 1");
    char *t = check_text("tests/fixtures/broken.vtt", 0, &rc, &n);
    CHECK_EQ(rc, 1);
    golden_bytes("check-broken", t, n);
    static const char *const codes[] = {
        "E010", "E011", "E013", "E014", "E101", "E110", "E111", "E112", "W015", "W016", "W018", "W019",
        "W020", "W102", "W103", "W104", "W113", "W120", "W121", "W130", "W140", "N021", "N131",
    };
    for (size_t i = 0; i < sizeof codes / sizeof *codes; i++) {
        char want[16];
        snprintf(want, sizeof want, "\n%s ", codes[i]);
        if (!strstr(t, want) && strncmp(t, want + 1, 5) != 0) {
            CHECK(!"a code the broken fixture should raise");
            fprintf(stderr, "    missing %s\n", codes[i]);
        }
    }
    CHECK(strstr(t, "8 errors, 15 warnings, 2 notes") != NULL);
    free(t);

    CASE("--json: the same findings, valid, with coordinates beside the names");
    t = check_text("tests/fixtures/broken.vtt", 1, &rc, &n);
    CHECK_EQ(rc, 1);
    CHECK(json_valid(t));
    CHECK(strstr(t, "\"code\":\"E101\"") != NULL);
    CHECK(strstr(t, "\"edge\":\"v\"") != NULL);
    golden_bytes("check-broken-json", t, n);
    free(t);

    CASE("the dump and the account read the broken map without fault (ASan is the check)");
    {
        char err[256];
        Map *m = mapio_load("tests/fixtures/broken.vtt", err, sizeof err);
        CHECK(m != NULL);
        if (m) {
            FILE *sink = fopen("/dev/null", "w");
            maptools_dump(sink, m, 0, 0, m->w - 1, m->h - 1);
            maptools_describe(sink, m, 0);
            maptools_describe(sink, m, 1);
            fclose(sink);
            map_free(m);
        }
    }

    CASE("rules that must not fire: an outer wall on the map's edge, a door with a wall at one end");
    {
        Sandbox sb = sandbox_enter("checkok");
        char path[600];
        snprintf(path, sizeof path, "%s/ok.vtt", sb.dir);
        FILE *f = fopen(path, "w");
        /* A room walled on the map's edge, an inner wall with a door at its
         * open end (one wall end), a door off the map (a way out), and a
         * disabled fog patch: only notes. */
        fputs("VTT 6\nsize 5 3\ntiles\n.....\n.....\n.....\n"
              "vedges\n|  | |\n|  + +\n|    |\nhedges\n-----\n     \n     \n-----\n"
              "fog on\nfogpatch 1 Mist disabled\nfog\nA....\n.....\n.....\n", f);
        fclose(f);
        t = check_text(path, 0, &rc, NULL);
        CHECK_EQ(rc, 0);                                   /* notes only: clean */
        CHECK(strstr(t, "W103") == NULL && strstr(t, "W102") == NULL && strstr(t, "W104") == NULL);
        CHECK(strstr(t, "N105 door-off-map") != NULL);
        CHECK(strstr(t, "N131") != NULL);
        free(t);
        sandbox_leave(&sb);
    }

    CASE("--region: either order, one square, a row alone, and what is not a square");
    {
        Map *m = map_new(10, 8, "r");
        int x0, y0, x1, y1;
        CHECK_EQ(maptools_region(m, "C4:B2", &x0, &y0, &x1, &y1), 1);
        CHECK(x0 == 1 && y0 == 1 && x1 == 2 && y1 == 3);
        CHECK_EQ(maptools_region(m, "d5", &x0, &y0, &x1, &y1), 1);
        CHECK(x0 == 3 && x1 == 3 && y0 == 4 && y1 == 4);
        CHECK_EQ(maptools_region(m, "5:6", &x0, &y0, &x1, &y1), 1);
        CHECK(x0 == 0 && x1 == 9 && y0 == 4 && y1 == 5);
        CHECK_EQ(maptools_region(m, "B2:", &x0, &y0, &x1, &y1), 0);
        CHECK_EQ(maptools_region(m, "hello", &x0, &y0, &x1, &y1), 0);
        CHECK_EQ(maptools_region(m, ":B2", &x0, &y0, &x1, &y1), 0);
        map_free(m);
    }

    CASE("a file that is not a map: E001 and exit 2");
    t = check_text("/nonexistent/map.vtt", 0, &rc, NULL);
    CHECK_EQ(rc, 2);
    CHECK(strncmp(t, "E001 unreadable", 15) == 0);
    free(t);
    t = check_text("/nonexistent/map.vtt", 1, &rc, NULL);
    CHECK_EQ(rc, 2);
    CHECK(json_valid(t));
    free(t);
}

void test_map_tools_dump(void)
{
    char err[256];

    CASE("the dump: whole fixtures and a region, as the goldens have them");
    static const struct { const char *file, *golden; int x0, y0, x1, y1; } cases[] = {
        { "tests/fixtures/two-rooms.vtt", "dump-two-rooms", 0, 0, 99, 99 },
        { "tests/fixtures/kinds.vtt",     "dump-kinds",     0, 0, 99, 99 },
        { "tests/fixtures/two-rooms.vtt", "dump-region",    1, 1, 6, 5 },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        Map *m = mapio_load(cases[i].file, err, sizeof err);
        CHECK(m != NULL);
        if (!m) continue;
        size_t n;
        char *t = tool_text(m, cases[i].x0, cases[i].y0, cases[i].x1, cases[i].y1, &n);
        golden_bytes(cases[i].golden, t, n);
        CHECK(strstr(t, " \n") == NULL);                    /* no trailing blanks */
        free(t);
        map_free(m);
    }

    CASE("fog, notes and a creature's note get their own sections");
    {
        Sandbox sb = sandbox_enter("dumpfog");
        CHECK_EQ(sb.ok, 1);
        char path[600];
        snprintf(path, sizeof path, "%s/f.vtt", sb.dir);
        FILE *f = fopen(path, "w");
        fputs("VTT 6\nname Cellar\nsize 5 3\nscale 5\nmetric chebyshev\ntiles\n.....\n.~~..\n.....\n"
              "vedges\n|  S |\n|  | |\n|  + |\nhedges\n-----\n     \n     \n-----\n"
              "token player 0 0 1 \"Aria\"\ntokennote \"wants the amulet\"\n"
              "token enemy 3 1 2 \"Ogre\"\nnote 1 2 \"pressure plate\"\n"
              "fog on\nfogpatch 1 Cellar reveal 2 memory on\nfogpatch 2 Pit reveal manual memory off\n"
              "fog\n..AAA\n.aAAB\n..1BB\n", f);
        fclose(f);
        Map *m = mapio_load(path, err, sizeof err);
        CHECK(m != NULL);
        if (m) {
            size_t n;
            char *t = tool_text(m, 0, 0, 99, 99, &n);
            golden_bytes("dump-fog", t, n);
            CHECK(strstr(t, "wants the amulet") != NULL);
            CHECK(strstr(t, "pressure plate") != NULL);
            CHECK(strstr(t, "reveal manual") != NULL);
            free(t);
            map_free(m);
        }
        sandbox_leave(&sb);
    }

    CASE("past Z the columns take two header rows, and every one is labeled");
    {
        Map *m = map_new(30, 2, "wide");
        char *t = tool_text(m, 0, 0, 99, 99, NULL);
        const char *nl = strchr(t, '\n');
        CHECK(nl != NULL);
        if (nl) {
            char first[256];
            size_t fl = (size_t)(nl - t) < sizeof first - 1 ? (size_t)(nl - t) : sizeof first - 1;
            memcpy(first, t, fl);
            first[fl] = '\0';
            CHECK(strstr(first, "A A A A") != NULL);                      /* AA..AD's first letters */
            CHECK(strstr(nl, "Y Z A B C D") != NULL);                     /* ...and their last */
        }
        free(t);
        map_free(m);
    }
}

/* ------------------------------------------------------------ map diagnostics */

typedef struct { char codes[64][8]; int lines[64]; int n; } DiagLog;

static void diag_collect(void *ctx, int line, int col, const char *code, const char *slug, const char *msg)
{
    (void)col; (void)slug; (void)msg;
    DiagLog *d = ctx;
    if (d->n < 64) { str_lcpy(d->codes[d->n], code, sizeof d->codes[0]); d->lines[d->n] = line; d->n++; }
}

static int diag_has(const DiagLog *d, const char *code)
{
    for (int i = 0; i < d->n; i++) if (!strcmp(d->codes[i], code)) return 1;
    return 0;
}

static int diag_line(const DiagLog *d, const char *code)
{
    for (int i = 0; i < d->n; i++) if (!strcmp(d->codes[i], code)) return d->lines[i];
    return -1;
}

/* Loads `text` through the diagnostic loader; returns the map (freed by
 * the caller) and what it said. */
static Map *diag_load(const char *dir, const char *text, DiagLog *d)
{
    char path[600], err[256];
    snprintf(path, sizeof path, "%s/d.vtt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return NULL;
    fputs(text, f);
    fclose(f);
    memset(d, 0, sizeof *d);
    return mapio_load_diag(path, err, sizeof err, diag_collect, d);
}

/* The loader forgives a damaged map and, asked, says what it forgave: with
 * the line, and without changing what loads. */
void test_map_diag(void)
{
    Sandbox sb = sandbox_enter("mapdiag");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    DiagLog d;
    char err[256];

    CASE("the shipped fixtures load without a word");
    static const char *const clean[] = { "tests/fixtures/two-rooms.vtt", "tests/fixtures/kinds.vtt",
                                         "tests/fixtures/crowd.vtt" };
    char here[1024];
    for (size_t i = 0; i < 3; i++) {
        snprintf(here, sizeof here, "%s/%s", sb.cwd, clean[i]);
        memset(&d, 0, sizeof d);
        Map *m = mapio_load_diag(here, err, sizeof err, diag_collect, &d);
        CHECK(m != NULL);
        int loud = 0;
        for (int k = 0; k < d.n; k++) loud += d.codes[k][0] != 'N';
        CHECK_EQ(loud, 0);
        map_free(m);
    }

    CASE("a vedges section one row short swallows the hedges header, and says so at that line");
    Map *m = diag_load(sb.dir,
        "VTT 2\nsize 3 2\ntiles\n...\n...\n"
        "vedges\n|  |\n"                                   /* one row, not two */
        "hedges\n---\n   \n---\n", &d);
    CHECK(m != NULL);
    CHECK(diag_has(&d, "E011"));
    CHECK_EQ(diag_line(&d, "E011"), 8);                    /* 'hedges' is line 8 */
    CHECK(diag_has(&d, "W019"));                           /* the stranded hedges rows */
    int strays = 0;
    for (int k = 0; k < d.n; k++) strays += !strcmp(d.codes[k], "W019");
    CHECK_EQ(strays, 1);                                   /* one finding for the run */
    map_free(m);

    CASE("a section one row long leaves a stray row; a row too long is cut, and says where");
    m = diag_load(sb.dir,
        "VTT 2\nsize 3 2\ntiles\n...\n....\n...\n"         /* second row long, then one too many */
        "vedges\n|  |\n|  |\nhedges\n---\n   \n---\n", &d);
    CHECK(diag_has(&d, "E010"));
    CHECK_EQ(diag_line(&d, "E010"), 5);
    CHECK(diag_has(&d, "W019"));
    CHECK_EQ(diag_line(&d, "W019"), 6);
    map_free(m);

    CASE("a character no row knows reads as empty and is named with its column");
    m = diag_load(sb.dir, "VTT 2\nsize 3 1\ntiles\n.X.\nvedges\n| Q|\nhedges\n---\n---\n", &d);
    CHECK(diag_has(&d, "E013"));
    CHECK_EQ(map_tile(m, 1, 0), TILE_VOID);                /* loaded exactly as before */
    int bad = 0;
    for (int k = 0; k < d.n; k++) bad += !strcmp(d.codes[k], "E013");
    CHECK_EQ(bad, 2);
    map_free(m);

    CASE("records that do not parse are named, dropped as ever");
    m = diag_load(sb.dir,
        "VTT 6\nsize 3 2\ntiles\n...\n...\n"
        "tokenstatus red \"Early\"\n"                      /* no token yet */
        "token player 9 9 1 \"Off\"\n"                     /* off the map */
        "token enemy 0 0 7 \"Big\"\n"                      /* size clamped */
        "note 5 5 \"nowhere\"\n"
        "clock 3 4\n"
        "fogpatch 99 Bad reveal 1\n"
        "mystery line\n", &d);
    CHECK(m != NULL);
    int records = 0;
    for (int k = 0; k < d.n; k++) records += !strcmp(d.codes[k], "E014");
    CHECK_EQ(records, 5);
    CHECK_EQ(diag_line(&d, "E014"), 6);
    CHECK(diag_has(&d, "W020"));
    CHECK(diag_has(&d, "W015"));
    CHECK_EQ(m->tokens.n, 1);
    map_free(m);

    CASE("header settings it does not know or cannot use");
    m = diag_load(sb.dir, "VTT 5\nsize 2 1\nzoom 9\nscale -3\nruleset nosuch\nmetric bent\ntiles\n..\n", &d);
    CHECK(diag_has(&d, "W016"));
    CHECK(diag_has(&d, "W017"));
    CHECK_EQ(diag_line(&d, "W017"), 6);
    int clamps = 0;
    for (int k = 0; k < d.n; k++) clamps += !strcmp(d.codes[k], "W020");
    CHECK_EQ(clamps, 2);
    CHECK_EQ(m->zoom, 3);
    map_free(m);

    CASE("short rows are the format's leniency: one note a section, not a failure");
    m = diag_load(sb.dir, "VTT 2\nsize 4 3\ntiles\n..\n.\n....\n", &d);
    CHECK(diag_has(&d, "N021"));
    int notes = 0;
    for (int k = 0; k < d.n; k++) notes += !strcmp(d.codes[k], "N021");
    CHECK_EQ(notes, 1);
    CHECK_EQ(diag_line(&d, "N021"), 4);
    map_free(m);

    CASE("a fog row naming a patch no line creates, told once for that patch");
    m = diag_load(sb.dir, "VTT 6\nsize 3 2\ntiles\n...\n...\nfog on\nfogpatch 1 Crypt\nfog\nAC.\n.CC\n", &d);
    int unknown = 0;
    for (int k = 0; k < d.n; k++) unknown += !strcmp(d.codes[k], "W018");
    CHECK_EQ(unknown, 1);
    CHECK_EQ(diag_line(&d, "W018"), 9);
    CHECK_EQ(fog_at(m, 1, 0) & FOG_ID, 0);
    map_free(m);

    CASE("a file that ends inside a section says so");
    m = diag_load(sb.dir, "VTT 2\nsize 2 3\ntiles\n..\n", &d);
    CHECK(diag_has(&d, "E011"));
    map_free(m);

    CASE("a fog row spelling a word is a fog row, not a swallowed record");
    m = diag_load(sb.dir, "VTT 6\nsize 4 2\ntiles\n....\n....\nfog on\n"
                          "fogpatch 6 Six\nfogpatch 7 Seven\nfogpatch 15 Last\nfogpatch 14 Four\n"
                          "fog\nfog.\nname\n", &d);
    CHECK(!diag_has(&d, "E011"));
    map_free(m);

    CASE("short wall and fog rows are noted too, and a vedges row as wide as the map is a warning");
    m = diag_load(sb.dir, "VTT 2\nsize 4 2\ntiles\n....\n....\nvedges\n|  |\n|   |\nhedges\n---\n    \n----\n", &d);
    CHECK(diag_has(&d, "W022"));
    CHECK_EQ(diag_line(&d, "W022"), 7);
    int shorts = 0;
    for (int k = 0; k < d.n; k++) shorts += !strcmp(d.codes[k], "N021");
    CHECK_EQ(shorts, 2);                                   /* vedges row 1, hedges row 1 */
    map_free(m);

    CASE("a line of spaces between sections is blank, not a stray row");
    m = diag_load(sb.dir, "VTT 2\nsize 2 1\ntiles\n..\n   \nvedges\n| |\n", &d);
    CHECK(!diag_has(&d, "W019"));
    map_free(m);

    CASE("a header line after the sections says why it is ignored; no size at all says so");
    m = diag_load(sb.dir, "VTT 2\nsize 2 1\ntiles\n..\nname Late\n", &d);
    CHECK(diag_has(&d, "W015"));
    map_free(m);
    m = diag_load(sb.dir, "VTT 2\ntiles\n..\nsize 2 1\n", &d);
    CHECK(m == NULL);

    CASE("the broken fixture loads byte-for-byte the same through either loader");
    {
        char here2[1024], o1[700], o2[700];
        snprintf(here2, sizeof here2, "%s/tests/fixtures/broken.vtt", sb.cwd);
        Map *b1 = mapio_load(here2, err, sizeof err);
        Map *b2 = mapio_load_diag(here2, err, sizeof err, diag_collect, &d);
        CHECK(b1 && b2);
        snprintf(o1, sizeof o1, "%s/o1.vtt", sb.dir);
        snprintf(o2, sizeof o2, "%s/o2.vtt", sb.dir);
        if (b1 && b2) {
            CHECK_EQ(mapio_write(b1, o1, err, sizeof err), 0);
            CHECK_EQ(mapio_write(b2, o2, err, sizeof err), 0);
            FILE *f1 = fopen(o1, "rb"), *f2 = fopen(o2, "rb");
            char  x1[8192], x2[8192];
            size_t n1 = f1 ? fread(x1, 1, sizeof x1, f1) : 0, n2 = f2 ? fread(x2, 1, sizeof x2, f2) : 1;
            if (f1) fclose(f1);
            if (f2) fclose(f2);
            CHECK(n1 > 0);
            CHECK(n1 == n2 && !memcmp(x1, x2, n1));
        }
        map_free(b1);
        map_free(b2);
    }

    CASE("without a sink the loader is silent and loads the same map");
    map_free(diag_load(sb.dir, "VTT 2\nsize 3 2\ntiles\n.X.\n...\nvedges\n|  |\n", &d));
    char path[600];
    snprintf(path, sizeof path, "%s/d.vtt", sb.dir);
    Map *a1 = mapio_load(path, err, sizeof err);
    Map *a2 = mapio_load_diag(path, err, sizeof err, diag_collect, &d);
    CHECK(a1 && a2);
    if (a1 && a2) CHECK_EQ(memcmp(a1->tiles, a2->tiles, (size_t)a1->w * (size_t)a1->h), 0);
    map_free(a1);
    map_free(a2);

    sandbox_leave(&sb);
}

/* A version 12 map with one damaged line: what the loader says, and what it
 * keeps. The base is a 6x4 room; `tail` goes after the tiles. */
typedef struct { const char *code; int n; } Found;

static void found_code(void *ctx, int line, int col, const char *code,
                       const char *slug, const char *msg)
{
    Found *f = ctx;
    (void)line; (void)col; (void)slug; (void)msg;
    if (!strcmp(code, f->code)) f->n++;
}

static int damaged(const char *dir, const char *tail, const char *code, int *nlinks, int *nscenes, int *round)
{
    char path[600], err[160];
    snprintf(path, sizeof path, "%s/damaged.vtt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "VTT 12\nname D\nsize 6 4\ntiles\n......\n......\n......\n......\n%s", tail);
    fclose(f);
    Found fd = { code, 0 };
    Map *m = mapio_load_diag(path, err, sizeof err, found_code, &fd);
    unlink(path);
    if (!m) return -1;
    if (nlinks) *nlinks = m->nlinks;
    if (nscenes) *nscenes = m->nscenes;
    if (round) *round = m->nscenes ? m->scenes[0].round : -1;
    map_free(m);
    return fd.n;
}

void test_loader_damage(void)
{
    Sandbox sb = sandbox_enter("damage");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    int nl = -1, ns = -1, rd = -1;

    CASE("a link to another map that does not read is dropped, and said");
    static const char *const bad_links[] = {
        "link 1 stairs 1 0 0 to town \"Gate\"\n",              /* the map unquoted */
        "link 1 stairs 1 0 0 to \"town\" \"Gate\" oneway\n",  /* a word it does not take */
        "link 1 stairs 1 0 0 to \"town\" \"\"\n",             /* no place */
        "link 1 stairs 1 0 0 to \"to/wn\" \"Gate\"\n",        /* a path, not a name */
        "link 1 stairs 1 0 0 to \"town\n",                      /* the quote never closes */
        "link 1 stairs 9 0 0 to \"town\" \"Gate\"\n",         /* too big */
        "link 0 stairs 1 0 0 to \"town\" \"Gate\"\n",         /* no number 0 */
        "link 1 elevator 1 0 0 to \"town\" \"Gate\"\n",       /* no such kind */
    };
    for (size_t i = 0; i < sizeof bad_links / sizeof bad_links[0]; i++) {
        CHECK_EQ(damaged(sb.dir, bad_links[i], "E014", &nl, NULL, NULL), 1);
        CHECK_EQ(nl, 0);
    }
    CHECK_EQ(damaged(sb.dir, "link 1 stairs 1 0 0 to \"town\" \"Gate\" secret\n", "E014", &nl, NULL, NULL), 0);
    CHECK_EQ(nl, 1);                                         /* the good one, for contrast */
    CHECK_EQ(damaged(sb.dir, "link 1 stairs 1 0 0 to \"town\" \"Gate\"\n"
                             "link 1 ladder 1 2 2 to \"crypt\" \"Hall\"\n", "E014", &nl, NULL, NULL), 1);
    CHECK_EQ(nl, 1);                                         /* a number used twice: the first stays */

    CASE("a scene cut short, boxed off the map or upside down, or a seventeenth, is dropped");
    CHECK_EQ(damaged(sb.dir, "scene \"A\"\ntoken enemy 1 1 1 \"G\"\nscene \"B\"\nendscene\n", "W025", NULL, &ns, NULL), 1);
    CHECK_EQ(ns, 1);                                         /* A went; B stayed */
    CHECK_EQ(damaged(sb.dir, "scene \"A\" 4 3 1 1\nendscene\n", "W025", NULL, &ns, NULL), 1);
    CHECK_EQ(ns, 0);
    CHECK_EQ(damaged(sb.dir, "scene \"A\" 0 0 9 9\nendscene\n", "W025", NULL, &ns, NULL), 1);
    CHECK_EQ(ns, 0);
    CHECK_EQ(damaged(sb.dir, "scene \"A\" 0 0\nendscene\n", "W025", NULL, &ns, NULL), 1);
    CHECK_EQ(ns, 0);
    CHECK_EQ(damaged(sb.dir, "scene \"A\"\ntoken enemy 1 1 1 \"G\"\n", "W025", NULL, &ns, NULL), 1);
    CHECK_EQ(ns, 0);                                         /* the file ends inside it */
    {
        char many[17 * 32] = "";
        for (int i = 0; i < 17; i++) {
            size_t l = strlen(many);
            snprintf(many + l, sizeof many - l, "scene \"S%d\"\nendscene\n", i);
        }
        CHECK_EQ(damaged(sb.dir, many, "W025", NULL, &ns, NULL), 1);
        CHECK_EQ(ns, MAP_SCENES_MAX);
    }

    CASE("a scene's round out of range is clamped, and said; with nobody in the order it is none");
    #define FIGHTER "token enemy 1 1 1 \"G\"\ntokenturn 5\n"
    CHECK_EQ(damaged(sb.dir, "scene \"A\"\n" FIGHTER "round 99999\nendscene\n", "W020", NULL, &ns, &rd), 1);
    CHECK_EQ(ns, 1);
    CHECK_EQ(rd, INT16_MAX);
    CHECK_EQ(damaged(sb.dir, "scene \"A\"\n" FIGHTER "round -3\nendscene\n", "W020", NULL, &ns, &rd), 1);
    CHECK_EQ(rd, 0);
    CHECK_EQ(damaged(sb.dir, "scene \"A\"\n" FIGHTER "round 3\nendscene\n", "W020", NULL, &ns, &rd), 0);
    CHECK_EQ(rd, 3);
    CHECK_EQ(damaged(sb.dir, "scene \"A\"\ntoken enemy 1 1 1 \"G\"\nround 3\nendscene\n", "W020", NULL, &ns, &rd), 0);
    CHECK_EQ(rd, 0);
    #undef FIGHTER

    sandbox_leave(&sb);
}

