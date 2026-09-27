/* Tests: stamps, undo nesting, the control channel. */

#include "harness.h"

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

void test_undo_nesting(void)
{
    Map *m = map_new(6, 4, "n");
    Undo u;
    undo_init(&u);

    CASE("a helper's batch inside an operation's is part of it: one step, closed by the outermost end");
    undo_begin(&u);
    undo_set_tile(&u, m, 0, 0, TILE_WATER);
    undo_begin(&u);                                   /* a helper */
    undo_set_tile(&u, m, 1, 0, TILE_WATER);
    undo_end(&u);
    CHECK_EQ(u.open, 1);                              /* still the operation's */
    CHECK_EQ(undo_balanced(&u), 0);
    undo_begin(&u);                                   /* a second helper */
    undo_set_tile(&u, m, 2, 0, TILE_WATER);
    undo_end(&u);
    undo_end(&u);
    CHECK_EQ(u.open, 0);
    CHECK_EQ(undo_balanced(&u), 1);
    CHECK_EQ(u.nmarks, 1);
    undo_undo(&u, m);
    CHECK(map_tile(m, 0, 0) == TILE_VOID && map_tile(m, 2, 0) == TILE_VOID);

    CASE("a stroke stays open across calls, is balanced between keys, and ends when told");
    undo_clear(&u);
    undo_stroke(&u);
    undo_set_hedge(&u, m, 0, 1, EDGE_WALL);
    undo_stroke(&u);                                  /* the next step */
    undo_set_hedge(&u, m, 1, 1, EDGE_WALL);
    CHECK_EQ(undo_balanced(&u), 1);
    undo_stroke_end(&u);
    CHECK_EQ(u.open, 0);
    CHECK_EQ(u.nmarks, 1);

    CASE("another tool mid-stroke ends the stroke: two steps, as before nesting");
    undo_clear(&u);
    undo_stroke(&u);
    undo_set_hedge(&u, m, 2, 1, EDGE_WALL);
    undo_begin(&u);                                   /* a fill */
    undo_set_tile(&u, m, 3, 3, TILE_WATER);
    undo_end(&u);
    CHECK_EQ(u.open, 0);
    CHECK_EQ(u.nmarks, 2);

    CASE("undo and redo close whatever is open, however deep");
    undo_clear(&u);
    undo_begin(&u);
    undo_begin(&u);
    undo_set_tile(&u, m, 4, 3, TILE_WATER);
    undo_undo(&u, m);
    CHECK_EQ(u.open, 0);
    CHECK_EQ(u.nest, 0);
    CHECK_EQ(map_tile(m, 4, 3), TILE_VOID);

    CASE("abort forgets a nested batch whole");
    undo_clear(&u);
    undo_begin(&u);
    undo_set_tile(&u, m, 5, 3, TILE_WATER);
    undo_begin(&u);
    undo_set_tile(&u, m, 5, 2, TILE_WATER);
    undo_end(&u);
    undo_abort(&u, m);
    CHECK(u.open == 0 && u.nest == 0 && u.nmarks == 0 && u.nops == 0);
    CHECK(map_tile(m, 5, 3) == TILE_VOID && map_tile(m, 5, 2) == TILE_VOID);

    undo_free(&u);
    map_free(m);
}

void test_ctl(void)
{
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);

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

    CASE("status: the map, its file, the screen, undo, and edits taken in build mode");
    t = ctl_ask(&a, "status");
    CHECK(strncmp(t, "ok\nmap Two Rooms  16x9\n", 23) == 0);
    CHECK(strstr(t, "file tests/fixtures/two-rooms.vtt\n") != NULL);
    CHECK(strstr(t, "screen build, normal mode\n") != NULL);
    CHECK(strstr(t, "undo 0 back, 0 forward\n") != NULL);
    CHECK(strstr(t, "edits taken\n") != NULL);
    free(t);

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

    CASE("edits are refused, with the reason, whenever the GM is part way through something");
    CHECK(app_ctl_busy(&a) == NULL);
    press(&a, ":");
    CHECK(app_ctl_busy(&a) != NULL && strstr(app_ctl_busy(&a), ": command"));
    press(&a, "\x1b");
    CHECK(app_ctl_busy(&a) == NULL);
    press(&a, "g");
    CHECK(app_ctl_busy(&a) != NULL && strstr(app_ctl_busy(&a), "part way"));
    press(&a, "\x1b");
    CHECK(app_ctl_busy(&a) == NULL);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    CHECK_EQ(a.screen, SCREEN_PLAY);
    CHECK(app_ctl_busy(&a) != NULL && strstr(app_ctl_busy(&a), "play mode"));
    t = ctl_ask(&a, "status");
    CHECK(strstr(t, "screen play\n") != NULL);
    CHECK(strstr(t, "edits not now: the GM is in play mode") != NULL);
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
void ctl_pump(App *a, int (*until)(void *), void *ctx)
{
    for (int spin = 0; spin < 400 && !(until && until(ctx)); spin++) {
        struct pollfd fds[1 + CTL_MAX_CONN];
        int n = ctl_pollfds(&a->ctl, fds, 1 + CTL_MAX_CONN);
        poll(fds, (nfds_t)n, 5);
        uint64_t now = prof_now_ns() / 1000000u;
        ctl_service(&a->ctl, fds, n, now);
        app_tick(a, now);
    }
}

int ctl_raw_connect(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    str_lcpy(sa.sun_path, path, sizeof sa.sun_path);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { close(fd); return -1; }
    return fd;
}

int ctl_read_some(void *ctx)
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

int ctl_child_done(void *ctx)
{
    int *st = ctx;
    return st[1] || (st[1] = waitpid((pid_t)st[0], &st[2], WNOHANG) > 0);
}

void test_ctl_marked(void)
{
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
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

/* A w x h map of floor, no walls, in dir; opened in build mode. */
static int ctl_blank_map(App *a, const char *dir, int w, int h)
{
    char path[700];
    snprintf(path, sizeof path, "%s/blank.vtt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "VTT 6\nname Blank\nsize %d %d\ntiles\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) fputc('.', f);
        fputc('\n', f);
    }
    fclose(f);
    return app_open_map(a, path) == 0 && a->map != NULL;
}

/* The map's squares, boundaries, creatures and notes, to compare before and
 * after a request that must have changed nothing. */
char *ctl_snapshot(const Map *m)
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
    CHECK(ctl_blank_map(&a, sb.dir, 12, 8));
    if (!a.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.map;
    char *t, *before, *after;

    CASE("room: floor, walls round it, one undo step; the GM is told and shown where");
    int depth = a.undo.depth;
    t = ctl_ask(&a, "room B2:D4\n");
    CHECK_EQ(strcmp(t, "ok\nchanged B2:D4: 1 line, one undo step\n"), 0);
    free(t);
    CHECK_EQ(a.undo.depth, depth + 1);
    CHECK_EQ(map_vedge(m, 1, 1), EDGE_WALL);
    CHECK_EQ(map_vedge(m, 4, 3), EDGE_WALL);
    CHECK_EQ(map_vedge(m, 2, 2), EDGE_NONE);         /* inside is left alone */
    CHECK_EQ(map_hedge(m, 1, 1), EDGE_WALL);
    CHECK_EQ(map_hedge(m, 3, 4), EDGE_WALL);
    CHECK_EQ(strcmp(a.status, "agent: room B2:D4 - u takes it back"), 0);
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
    CHECK(strncmp(t, "ok\nchanged E2:K7: 7 lines, one undo step\n", 42) == 0);
    free(t);
    CHECK_EQ(a.undo.depth, depth + 1);
    CHECK_EQ(map_tile(m, 7, 3), TILE_WATER);
    CHECK_EQ(map_vedge(m, 5, 3), EDGE_DOOR_CLOSED);
    CHECK_EQ(map_hedge(m, 6, 6), EDGE_WINDOW);
    CHECK_EQ(m->tokens.n, 2);
    CHECK(map_note_at(m, 10, 1) && !strcmp(map_note_at(m, 10, 1), "secret door here?"));
    CHECK(strstr(a.status, "agent: room F2:K6 and 6 more - u takes them back") != NULL);
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
            { "token set Ghoul color red\n", "token set changes a label, a size or a note" },
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
    CHECK(strstr(t, "edits taken\n") != NULL);
    CHECK(strstr(t, "and this request's changes one more") != NULL);
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

    CASE("busy: edits wait while the GM is part way through something; reads do not");
    before = ctl_snapshot(m);
    press(&a, ":");
    t = ctl_ask(&a, "status\ntile B2 hazard\n");
    CHECK_EQ(strcmp(t, "busy: the GM is typing a : command\n"), 0);
    free(t);
    press(&a, "\x1b");
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    t = ctl_ask(&a, "tile B2 hazard\n");
    CHECK_EQ(strcmp(t, "busy: the GM is in play mode - edits are build mode's\n"), 0);
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
    t = ctl_ask(&a, "tile B2 hazard\n");
    CHECK_EQ(strcmp(t, "busy: the GM is laying wall\n"), 0);
    free(t);
    undo_stroke_end(&a.undo);
    a.ed.pen = 0;
    press(&a, "\x1b");
    after = ctl_snapshot(m);
    CHECK_EQ(strcmp(before, after), 0);
    free(after);
    free(before);

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

void test_gray_marker(void)
{
    CASE("a marker saved as grey loads as gray and is written as gray");
    CHECK(status_color_from_name("grey") >= 0);
    CHECK_EQ(status_color_from_name("grey"), status_color_from_name("gray"));
    CHECK_EQ(strcmp(status_color_name((uint8_t)status_color_from_name("grey")), "gray"), 0);

    char path[] = "/tmp/vtt-test-XXXXXX";
    int  fd = mkstemp(path);
    if (fd >= 0) close(fd);
    write_file(path, "VTT 3\nsize 2 1\ntiles\n..\nvedges\n\nhedges\n\n\n"
                     "token player 0 0 1 \"Aria\"\ntokenstatus grey \"Hidden\"\n");
    char err[MAPIO_ERR_MAX] = { 0 };
    Map *m = mapio_load(path, err, sizeof err);
    CHECK(m != NULL);
    if (m) {
        CHECK_EQ(m->tokens.n, 1);
        CHECK_EQ(m->tokens.v[0].nstatus, 1);
        CHECK_EQ(m->tokens.v[0].status[0].color, status_color_from_name("gray"));
        CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
        map_free(m);
        FILE *f = fopen(path, "r");
        char buf[512] = { 0 };
        if (f) { if (fread(buf, 1, sizeof buf - 1, f) == 0) buf[0] = 0; fclose(f); }
        CHECK(strstr(buf, "tokenstatus gray \"Hidden\"") != NULL);
        CHECK(strstr(buf, "grey") == NULL);
    }
    unlink(path);
}
