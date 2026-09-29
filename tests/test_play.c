/* Tests: play mode: creatures, the cursor, groups, occupancy, moving, the trail, labels, culling. */

#include "harness.h"

/* ------------------------------------------------------------------ play */

void test_play(void)
{
    Map *m = map_new(12, 10, "play");
    map_fill_tiles(m, 0, 0, 11, 9, TILE_FLOOR);

    Undo u;
    undo_init(&u);
    Play p;
    play_init(&p);

    CASE("play starts with nothing selected and walls enforced");
    CHECK_EQ(p.sel, -1);
    CHECK_EQ(p.enforce_walls, 1);
    CHECK_EQ(p.next_size, 1);

    Token a = { 2, 2, 1, TOKEN_PLAYER, "Aria" };
    int ai = undo_add_token(&u, m, a);

    CASE("a 1x1 token moves freely on open floor");
    play_focus(&p, ai);
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 1);
    CHECK_EQ(m->tokens.v[ai].x, 3);
    CHECK_EQ(p.steps, 1);
    CHECK_EQ(play_step(m, &u, &p, 0, 1), 1);
    CHECK_EQ(p.steps, 2);

    CASE("a wall stops it, and the step is not counted");
    map_set_vedge(m, 4, 3, EDGE_WALL);
    int before = p.steps;
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 0);
    CHECK_EQ(m->tokens.v[ai].x, 3);
    CHECK_EQ(p.steps, before);

    /* Rules-agnostic means the GM can always overrule the map. */
    CASE("blocking can be switched off");
    p.enforce_walls = 0;
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 1);
    CHECK_EQ(m->tokens.v[ai].x, 4);
    p.enforce_walls = 1;
    map_set_vedge(m, 4, 3, EDGE_NONE);

    CASE("the map edge stops a token even with walls off");
    p.enforce_walls = 0;
    m->tokens.v[ai].x = 0;
    m->tokens.v[ai].y = 0;
    CHECK_EQ(play_step(m, &u, &p, -1, 0), 0);
    CHECK_EQ(play_step(m, &u, &p, 0, -1), 0);
    p.enforce_walls = 1;

    /* A big token has to be stopped by a wall anywhere along its leading
     * face, not only the one tile the anchor happens to sit on. */
    CASE("a 2x2 token is blocked by a wall on any part of its face");
    Token big = { 4, 4, 2, TOKEN_ENEMY, "Ogre" };
    int bi = undo_add_token(&u, m, big);
    play_focus(&p, bi);
    CHECK_EQ(token_can_move(m, &m->tokens.v[bi], 1, 0, 1, bi), 1);
    map_set_vedge(m, 6, 5, EDGE_WALL);        /* the token's lower-right face */
    CHECK_EQ(token_can_move(m, &m->tokens.v[bi], 1, 0, 1, bi), 0);
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 0);
    map_set_vedge(m, 6, 5, EDGE_WALL * 0);

    CHECK_EQ(token_can_move(m, &m->tokens.v[bi], 0, 1, 1, bi), 1);
    map_set_hedge(m, 5, 6, EDGE_WALL);        /* below its right-hand column */
    CHECK_EQ(token_can_move(m, &m->tokens.v[bi], 0, 1, 1, bi), 0);
    map_set_hedge(m, 5, 6, EDGE_NONE);

    CASE("a big token needs its whole footprint on the map");
    m->tokens.v[bi].x = 10;
    CHECK_EQ(token_can_move(m, &m->tokens.v[bi], 1, 0, 1, bi), 0);
    m->tokens.v[bi].x = 4;

    CASE("void tiles stop a token like a wall does");
    map_set_tile(m, 6, 4, TILE_VOID);
    CHECK_EQ(token_can_move(m, &m->tokens.v[bi], 1, 0, 1, bi), 0);
    map_set_tile(m, 6, 4, TILE_FLOOR);

    CASE("placement checks the footprint fits");
    CHECK_EQ(play_can_place(m, 11, 9, 1, -1), 1);
    CHECK_EQ(play_can_place(m, 11, 9, 2, -1), 0);
    CHECK_EQ(play_can_place(m, 10, 8, 2, -1), 1);
    CHECK_EQ(play_can_place(m, -1, 0, 1, -1), 0);

    /* Aria is parked on 0,0 from the edge test above, and the ogre's 2x2 sits
     * at 4,4. A stack of tokens is a stack nobody can see into. */
    CASE("placement also checks the square is free");
    CHECK_EQ(play_can_place(m, 0, 0, 1, -1), 0);
    CHECK_EQ(play_can_place(m, 4, 4, 1, -1), 0);
    CHECK_EQ(play_can_place(m, 5, 5, 1, -1), 0);    /* the far corner of the 2x2 */
    CHECK_EQ(play_can_place(m, 3, 3, 2, -1), 0);    /* only its corner overlaps */
    CHECK_EQ(play_can_place(m, 6, 6, 1, -1), 1);

    CASE("a token may grow where it already stands");
    CHECK_EQ(play_can_place(m, 4, 4, 3, -1), 0);
    CHECK_EQ(play_can_place(m, 4, 4, 3, bi), 1);

    CASE("cycling wraps in both directions");
    play_focus(&p, -1);
    play_cycle(&p, m, 1, PLAY_ANY_KIND);
    CHECK_EQ(p.sel, 0);
    play_cycle(&p, m, 1, PLAY_ANY_KIND);
    CHECK_EQ(p.sel, 1);
    play_cycle(&p, m, 1, PLAY_ANY_KIND);
    CHECK_EQ(p.sel, 0);          /* wrapped */
    play_cycle(&p, m, -1, PLAY_ANY_KIND);
    CHECK_EQ(p.sel, 1);

    CASE("selecting by tile finds the token under the cursor");
    m->tokens.v[bi].x = 4;
    m->tokens.v[bi].y = 4;
    play_select_at(&p, m, 5, 5, 1);      /* inside the 2x2 footprint */
    CHECK_EQ(p.sel, bi);
    play_select_at(&p, m, 9, 9, 1);
    CHECK_EQ(p.sel, -1);

    CASE("moves undo one step at a time");
    play_focus(&p, ai);
    m->tokens.v[ai].x = 5;
    m->tokens.v[ai].y = 5;
    undo_clear(&u);
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
    undo_begin(&u); play_step(m, &u, &p, 1, 0); undo_end(&u);
    CHECK_EQ(m->tokens.v[ai].x, 7);
    undo_undo(&u, m);
    CHECK_EQ(m->tokens.v[ai].x, 6);
    undo_undo(&u, m);
    CHECK_EQ(m->tokens.v[ai].x, 5);

    CASE("stepping with nothing selected does nothing");
    play_focus(&p, -1);
    CHECK_EQ(play_step(m, &u, &p, 1, 0), 0);

    undo_free(&u);
    map_free(m);
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

/* -------------------------------------------------- key tables and the ? page */

/* Creatures off the window are skipped before any drawing: the frame must
 * be the one it would be without them, and one just inside must still draw.
 * Every square in a band round the view, a 1x1 and a 3x3 creature each, a
 * marker and a label on both, at two zooms. */
void test_cull(void)
{
    Map *m = map_new(60, 40, "cull");
    map_fill_tiles(m, 0, 0, 59, 39, TILE_FLOOR);
    Play p;
    play_init(&p);
    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    Editor e;
    ed_init(&e, m);
    e.labels = 0;
    ed_layout(&e, m, 80, 24);
    e.cx = 30; e.cy = 20;
    grid_center_on(&e.view, m, e.cx, e.cy);

    CASE("a creature off the window draws nothing, one on its edge still draws");
    for (int zoom = 0; zoom < ZOOM_COUNT; zoom++) {
        e.view.zoom = zoom;
        grid_center_on(&e.view, m, e.cx, e.cy);
        /* On a square's edge, so the boundary line beyond the last square
         * shown is on screen: what the one square to spare is for. */
        e.view.cam_x -= e.view.cam_x % zoom_pw(zoom);
        e.view.cam_y -= e.view.cam_y % zoom_ph(zoom);
        ByteBuf none;
        bb_init(&none, 32768);
        rnd_begin(&r);
        play_draw(&r, m, &e, &p, &THEME_DARK, 0, 0);
        rnd_dump(&r, &none);
        bb_putc(&none, '\0');

        int vx0, vy0, vx1, vy1, culled = 0, drawn = 0, wrong = 0, skipped_not = 0;
        grid_visible_tiles(&e.view, m, &vx0, &vy0, &vx1, &vy1);
        vx0--; vy0--; vx1++; vy1++;                        /* as play_draw does */
        for (int size = 1; size <= 3; size += 2)
            for (int y = vy0 - 3; y <= vy1 + 3; y++)
                for (int x = vx0 - 3; x <= vx1 + 3; x++) {
                    int edge = x <= vx0 + 1 || x >= vx1 - 1 || y <= vy0 + 1 || y >= vy1 - 1;
                    if (!edge || x < 0 || y < 0 || x + size > 60 || y + size > 40) continue;
                    /* Everything that reaches past a creature's squares: its
                     * turn bars, and a fifth marker on the row below. */
                    Token t;
                    memset(&t, 0, sizeof t);
                    t.x = (int16_t)x; t.y = (int16_t)y; t.size = (uint8_t)size; t.kind = TOKEN_ENEMY;
                    t.turn = (uint8_t)(TURN_IN | TURN_ACTING);
                    str_lcpy(t.label, "Wide Goblin", sizeof t.label);
                    for (int k = 0; k < TOKEN_STATUS_MAX; k++) token_add_status(&t, (uint8_t)k, "Marked");
                    int off = x > vx1 || y > vy1 || x + size - 1 < vx0 || y + size - 1 < vy0;
                    /* The frame with it on the map, and the frame without it
                     * with it drawn anyway, uncut, under play_draw's clip:
                     * the cull must never change the picture. */
                    ByteBuf with, drawn_anyway;
                    bb_init(&with, 32768);
                    bb_init(&drawn_anyway, 32768);
                    tokens_add(&m->tokens, t);
                    rnd_begin(&r);
                    unsigned long drew = grid_tokens_drawn;
                    play_draw(&r, m, &e, &p, &THEME_DARK, 0, 0);
                    drew = grid_tokens_drawn - drew;
                    if (off && drew) skipped_not++;  /* off the window, and drawn */
                    if (!off && !drew) wrong++;      /* on it, and not */
                    rnd_dump(&r, &with);
                    bb_putc(&with, '\0');
                    m->tokens.n = 0;
                    rnd_begin(&r);
                    play_draw(&r, m, &e, &p, &THEME_DARK, 0, 0);
                    ClipRect saved = grid_clip_push(&r, &e.view, m);
                    grid_draw_token(&r, &e.view, &t, &THEME_DARK, 0, 0);
                    grid_draw_token_status(&r, &e.view, &t, &THEME_DARK, 0);
                    rnd_clip_restore(&r, saved);
                    rnd_dump(&r, &drawn_anyway);
                    bb_putc(&drawn_anyway, '\0');
                    if (strcmp(with.data, drawn_anyway.data)) wrong++;
                    culled += off;
                    drawn  += strcmp(with.data, none.data) != 0;
                    bb_free(&with);
                    bb_free(&drawn_anyway);
                }
        CHECK_EQ(wrong, 0);
        CHECK_EQ(skipped_not, 0);                        /* the cull happens */
        CHECK(culled > 0);
        CHECK(drawn > 0);
        bb_free(&none);
    }

    rnd_free(&r);
    map_free(m);
}

/* Play mode's box is a tint, which no text dump shows: the cells under it
 * take the selection background, and only while it is open. */
void test_play_box(void)
{
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 72, 20);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, "tests/fixtures/two-rooms.vtt"), 0);
    press(&a, "\x1b[12~");
    a.ed.cx = 2; a.ed.cy = 2;

    CASE("v's box tints what it covers, and nothing once closed");
    press(&a, "vjjlll");
    CHECK(a.play.visual);
    rnd_begin(&r);
    app_draw(&a);
    int inside = 0, outside = 0;
    for (int y = 0; y < r.h; y++)
        for (int x = 0; x < r.w; x++) {
            if (r.back[y * r.w + x].bg != a.th->sel_bg) continue;
            int tx, ty;
            if (grid_screen_to_tile(&a.ed.view, a.map, x, y, &tx, &ty) &&
                tx >= 2 && tx <= 5 && ty >= 2 && ty <= 4) inside++;
            else outside++;
        }
    CHECK(inside > 0);
    CHECK_EQ(outside, 0);
    press(&a, "\x1b");
    CHECK(!a.play.visual);
    rnd_begin(&r);
    app_draw(&a);
    int left = 0;
    for (int i = 0; i < r.w * r.h; i++) left += r.back[i].bg == a.th->sel_bg;
    CHECK_EQ(left, 0);

    app_free(&a);
    rnd_free(&r);
}
