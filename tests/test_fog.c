/* Tests: fog of war and sight. */

#include "harness.h"

/* A 10x4 map with a wall down the middle, x=5, for the fog tests. */
static void write_split_map(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs("VTT 2\nname x\nsize 10 4\nzoom 1\ntiles\n"
          "..........\n..........\n..........\n..........\n"
          "vedges\n     |     \n     |     \n     |     \n     |     \n"
          "hedges\n          \n          \n          \n          \n          \n", f);
    fclose(f);
}

/* The first cell of a tile's interior, as drawn into r. */
static const Cell *tile_cell(const Renderer *r, const App *a, int tx, int ty)
{
    int sx, sy;
    grid_tile_interior(&a->ed.view, tx, ty, &sx, &sy);
    return &r->back[(size_t)sy * (size_t)r->w + (size_t)sx];
}

/* Fog, part one: patches painted in build mode, lit and darkened by hand in
 * play, blank in the players' frame, dimmed in the GM's, tinted in build
 * mode, and saved. */
void test_fog(void)
{
    Sandbox sb = sandbox_enter("fog");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    write_split_map(sb.dir, "crypt.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/crypt.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 64, 14);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Map *m = a.map;

    CASE("no fog to begin with: nothing hides, and the frames cannot differ for it");
    CHECK_EQ(fog_any(m), 0);
    press(&a, ":fog\r");
    CHECK(strstr(a.status, "no fog") != NULL);
    press(&a, "gf");
    CHECK(strstr(a.status, "no fog patch to paint") != NULL);

    CASE(":fog NAME makes a patch with the defaults, makes it the brush's, and turns fog on");
    press(&a, ":fog Crypt\r");
    int id = fog_find(m, "Crypt");
    CHECK_EQ(id, 1);
    CHECK_EQ(a.ed.fog_patch, 1);
    CHECK_EQ(m->fog_on, 1);
    CHECK_EQ(m->fog_patches[0].reveal, 2);
    CHECK_EQ(m->fog_patches[0].memory, 1);
    CHECK_EQ(m->fog_patches[0].soft_edge, -1);
    CHECK(strstr(a.status, "fog patch Crypt made") != NULL);
    CHECK(strstr(a.status, "fog on") != NULL);
    CHECK_EQ(fog_any(m), 0);                               /* nothing painted yet */

    CASE("g f paints the box, and it is one undo step; g c scrubs the brush's square");
    a.ed.cx = 5; a.ed.cy = 0;
    press(&a, "v4l3jgf");
    CHECK_EQ(fog_count(m, 1, NULL), 20);
    CHECK_EQ(fog_at(m, 4, 0) & FOG_ID, 0);
    CHECK_EQ(fog_at(m, 5, 0) & FOG_ID, 1);
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    CHECK(strstr(a.status, "fog Crypt over 20 squares") != NULL);
    CHECK_EQ(fog_any(m), 1);
    press(&a, "u");
    CHECK_EQ(fog_count(m, 1, NULL), 0);
    press(&a, "\x12");
    CHECK_EQ(fog_count(m, 1, NULL), 20);
    a.ed.cx = 9; a.ed.cy = 3;
    press(&a, "gc");
    CHECK_EQ(fog_at(m, 9, 3), 0);
    CHECK(strstr(a.status, "scrubbed from 1 square") != NULL);
    press(&a, "u");
    CHECK_EQ(fog_at(m, 9, 3) & FOG_ID, 1);
    char line[192];
    a.ed.cx = 6; a.ed.cy = 1;
    ed_status(&a.ed, m, line, sizeof line);
    CHECK(strstr(line, "fog Crypt") != NULL);

    CASE("build mode tints painted ground in the patch's color");
    rnd_begin(&r); app_draw(&a);
    CHECK_EQ(tile_cell(&r, &a, 7, 2)->bg, a.th->fog_tint[fog_tint(1)]);
    CHECK(tile_cell(&r, &a, 2, 2)->bg != a.th->fog_tint[fog_tint(1)]);

    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    a.ed.cx = 7; a.ed.cy = 1;
    press(&a, "ieOgre\r");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "ipAria\r");
    CHECK_EQ(m->tokens.n, 2);

    CASE("the GM sees hidden ground dimmed and everything on it");
    press(&a, "\x1b");
    rnd_begin(&r); app_draw_view(&a, VIEW_GM);
    CHECK_EQ(tile_cell(&r, &a, 8, 2)->bg, a.th->fog_gm_bg);
    CHECK(tile_cell(&r, &a, 2, 2)->bg != a.th->fog_gm_bg);
    ByteBuf fr;
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "[O]") != NULL);
    bb_free(&fr);

    CASE("the players see nothing of it: no ground, no walls inside it, no Ogre; the dividing wall stays");
    CHECK_EQ(fog_ground_hidden(m, 7, 1), 1);
    CHECK_EQ(fog_token_hidden(m, &m->tokens.v[0]), 1);
    CHECK_EQ(app_view_differs(&a), 1);
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "[O]") == NULL);
    CHECK(strstr(fr.data, "(A)") != NULL);
    CHECK(strstr(fr.data, "┃") != NULL);              /* the heavy dividing wall */
    bb_free(&fr);
    int sx, sy;
    grid_tile_screen(&a.ed.view, 8, 2, &sx, &sy);          /* a corner inside the fog */
    CHECK_EQ(r.back[(size_t)sy * (size_t)r.w + (size_t)sx].ch, ' ');
    CHECK_EQ(tile_cell(&r, &a, 8, 2)->ch, ' ');

    CASE("in the dark the players' frame has no cursor, no name and no count");
    a.ed.cx = 7; a.ed.cy = 1;                              /* on the Ogre */
    play_focus(&a.play, 0);                                /* even selected */
    play_status(&a.play, m, &a.ed, 0, line, sizeof line);
    CHECK(strstr(line, "Ogre") == NULL);
    CHECK(strstr(line, "dark") != NULL);
    CHECK(strstr(line, "token") == NULL);
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    CHECK(tile_cell(&r, &a, 7, 1)->bg != a.th->cursor_bg);
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "Ogre") == NULL);
    bb_free(&fr);
    play_status(&a.play, m, &a.ed, 1, line, sizeof line);  /* the GM's line still says */
    CHECK(strstr(line, "Ogre") != NULL);

    CASE("the players' frame does not light the cursor's row and column in the dark, nor name its square");
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    int accent_labels = 0;
    for (int x = 0; x < r.w; x++) {
        const Cell *c = &r.back[(size_t)1 * (size_t)r.w + (size_t)x];
        if (c->fg == a.th->accent && c->ch >= 'A' && c->ch <= 'J') accent_labels++;
    }
    CHECK_EQ(accent_labels, 0);
    play_focus(&a.play, -1);
    play_status(&a.play, m, &a.ed, 0, line, sizeof line);
    CHECK(strstr(line, "H2") == NULL);
    CHECK(strstr(line, "dark") != NULL);
    play_focus(&a.play, 0);

    CASE("over fog the players' frame carries no status message at all");
    press(&a, ":roll 2d6\r");
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "2d6") == NULL);
    bb_free(&fr);

    CASE("a creature carried out of the dark does not say where it came from");
    a.ed.cx = 7; a.ed.cy = 1;
    press(&a, "\r");                                        /* pick up the Ogre, in the dark */
    press(&a, "\x17hhh\x17");                              /* through the wall, into the light */
    CHECK_EQ(fog_token_hidden(m, &m->tokens.v[0]), 0);
    play_status(&a.play, m, &a.ed, 0, line, sizeof line);
    CHECK(strstr(line, "MOVING") != NULL);
    CHECK(strstr(line, "from") == NULL);
    CHECK(strstr(line, "step") == NULL);
    play_status(&a.play, m, &a.ed, 1, line, sizeof line);
    CHECK(strstr(line, "from H2") != NULL);                 /* the GM's still does */
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, " sq") == NULL);                 /* no distance label */
    bb_free(&fr);
    press(&a, "\x1b");                                      /* cancel: back where it was */
    CHECK_EQ(m->tokens.v[0].x, 7);

    CASE("the title bar and the panel name a creature in the dark '?'");
    press(&a, "si12\r");
    press(&a, "a");                                        /* the Ogre acts */
    char title[128];
    turn_status_view(m, 1, title, sizeof title);
    CHECK(strstr(title, "?'s turn") != NULL);
    CHECK(strstr(title, "Ogre") == NULL);
    turn_status_view(m, 0, title, sizeof title);
    CHECK(strstr(title, "Ogre's turn") != NULL);
    rnd_resize(&r, 90, 14);
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "Ogre") == NULL);
    CHECK(strstr(fr.data, "12  ?") != NULL);
    CHECK(strstr(fr.data, "1 not in the fight") != NULL);  /* Aria, who is in the light */
    bb_free(&fr);
    rnd_begin(&r); app_draw_view(&a, VIEW_GM);
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "12  Ogre") != NULL);
    bb_free(&fr);
    press(&a, ":turns end\r");

    CASE("a range anchored on a creature in the dark is not drawn for the players");
    play_focus(&a.play, 0);
    press(&a, "6r");
    CHECK_EQ(a.play.range.active, 1);
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    CHECK(tile_cell(&r, &a, 3, 1)->bg != a.th->range_bg);  /* in range, on lit ground */
    press(&a, "\x1b\x1b");

    CASE("a range from a creature in the light tints lit ground only");
    play_focus(&a.play, 1);                                /* Aria */
    press(&a, "6r");
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    CHECK(tile_cell(&r, &a, 6, 1)->bg != a.th->range_bg);  /* hidden: blanked */
    CHECK_EQ(tile_cell(&r, &a, 6, 1)->ch, ' ');
    press(&a, "\x1b\x1b");

    CASE("g r lights the cursor's square, and the Ogre on it; g h puts it back; both undo");
    a.ed.cx = 7; a.ed.cy = 1;
    press(&a, "gr");
    CHECK(fog_at(m, 7, 1) & FOG_HELD);
    CHECK(fog_at(m, 7, 1) & FOG_SEEN);
    CHECK_EQ(fog_token_hidden(m, &m->tokens.v[0]), 0);
    CHECK(strstr(a.status, "lit 1 square") != NULL);
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "[O]") != NULL);
    bb_free(&fr);
    press(&a, "u");
    CHECK_EQ(fog_token_hidden(m, &m->tokens.v[0]), 1);
    press(&a, "\x12");
    press(&a, "gh");
    CHECK_EQ(fog_at(m, 7, 1), 1);
    press(&a, "gh");
    CHECK(strstr(a.status, "already dark") != NULL);
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "gr");
    CHECK(strstr(a.status, "no fog here") != NULL);

    CASE("g R lights the whole patch under the cursor, g H darkens it");
    a.ed.cx = 6; a.ed.cy = 2;
    press(&a, "gR");
    CHECK(strstr(a.status, "lit Crypt - 20 squares") != NULL);
    CHECK_EQ(fog_ground_hidden(m, 9, 3), 0);
    press(&a, "gH");
    CHECK_EQ(fog_ground_hidden(m, 9, 3), 1);
    press(&a, "gx");
    CHECK(strstr(a.status, "g wants r") != NULL);

    CASE("a typo in a setting makes no patch; :fog all never paints into a patch that merely starts with All");
    press(&a, ":fog Cellar bogus\r");
    CHECK(strstr(a.status, ":fog NAME [") != NULL);
    CHECK_EQ(fog_find(m, "Cellar"), 0);

    CASE(":fog NAME clear and hide do the same by name; settings change and list");
    press(&a, ":fog cr clear\r");
    CHECK_EQ(fog_ground_hidden(m, 9, 3), 0);
    press(&a, ":fog Crypt hide\r");
    CHECK_EQ(fog_ground_hidden(m, 9, 3), 1);
    press(&a, ":fog Crypt 1\r");
    CHECK_EQ(m->fog_patches[0].reveal, 1);
    press(&a, ":fog Crypt manual\r");
    CHECK_EQ(m->fog_patches[0].reveal, FOG_REVEAL_MANUAL);
    press(&a, ":fog Crypt memory off\r");
    CHECK_EQ(m->fog_patches[0].memory, 0);
    press(&a, ":fog Crypt --soft-edge\r");
    CHECK_EQ(m->fog_patches[0].soft_edge, 1);
    press(&a, ":fog --soft-edge\r");
    CHECK_EQ(m->fog_soft_edge, 1);
    press(&a, ":fog\r");
    CHECK(strstr(a.status, "Crypt manual 0/20 lantern soft *") != NULL);
    press(&a, ":fog Crypt memory maybe\r");
    CHECK(strstr(a.status, "memory on, or off") != NULL);
    press(&a, ":fog Crypt 500\r");
    CHECK(strstr(a.status, ":fog NAME [") != NULL);
    press(&a, ":fog 7up\r");
    CHECK(strstr(a.status, "starting with a letter") != NULL);
    press(&a, ":fog Crypt manual\r");

    CASE("disable keeps the painting and hides nothing; enable puts it back");
    press(&a, ":fog Crypt disable\r");
    CHECK_EQ(fog_any(m), 0);
    CHECK_EQ(fog_ground_hidden(m, 9, 3), 0);
    CHECK_EQ(fog_count(m, 1, NULL), 20);
    CHECK(strstr(a.status, "disabled") != NULL);
    press(&a, ":fog Crypt enable\r");
    CHECK_EQ(fog_ground_hidden(m, 9, 3), 1);

    CASE(":fog off is the master switch; the painting stays");
    press(&a, ":fog off\r");
    CHECK_EQ(fog_any(m), 0);
    CHECK_EQ(fog_count(m, 1, NULL), 20);
    press(&a, ":fog on\r");
    CHECK_EQ(fog_any(m), 1);

    CASE("patches, by prefix, and a second one painted over the first takes the tiles afresh");
    press(&a, ":fog Crate\r");
    CHECK_EQ(a.ed.fog_patch, 2);
    press(&a, ":fog cr\r");
    CHECK(strstr(a.status, "more than one patch") != NULL);
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    a.ed.cx = 9; a.ed.cy = 0;
    press(&a, "gf");
    CHECK_EQ(fog_at(m, 9, 0), 2);
    CHECK_EQ(fog_count(m, 1, NULL), 19);

    CASE("the file keeps the switches, the patches and the ground, held and seen included");
    app_key(&a, f2);
    a.ed.cx = 6; a.ed.cy = 0;
    press(&a, "gr");                                       /* one held tile of Crypt */
    char err[128];
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    char *text = slurp(path);
    CHECK(text != NULL);
    if (text) {
        CHECK_EQ(strncmp(text, "VTT 6\n", 6), 0);
        CHECK(strstr(text, "fog on\n") != NULL);
        CHECK(strstr(text, "fog soft-edge\n") != NULL);
        CHECK(strstr(text, "fogpatch 1 Crypt reveal manual memory off soft-edge on\n") != NULL);
        CHECK(strstr(text, "fogpatch 2 Crate reveal 2 memory on\n") != NULL);
        CHECK(strstr(text, "\nfog\n.....A1AAB\n") != NULL);
        free(text);
    }
    Map *back = mapio_load(path, err, sizeof err);
    CHECK(back != NULL);
    if (back) {
        CHECK_EQ(memcmp(back->fog, m->fog, 40), 0);
        CHECK_EQ(back->fog_on, 1);
        CHECK_EQ(back->fog_soft_edge, 1);
        CHECK_EQ(strcmp(back->fog_patches[0].name, "Crypt"), 0);
        CHECK_EQ(back->fog_patches[0].reveal, FOG_REVEAL_MANUAL);
        CHECK_EQ(back->fog_patches[1].x0, 9);              /* extents rebuilt from the rows */
        map_free(back);
    }

    CASE("a fogpatch line after its section, or twice, still leaves the ground hidden");
    {
        char late[700];
        snprintf(late, sizeof late, "%s/late.vtt", sb.dir);
        FILE *lf = fopen(late, "w");
        if (lf) {
            fputs("VTT 6\nname x\nsize 3 1\ntiles\n...\nfog on\nfog\n.AA\n"
                  "fogpatch 1 Hall reveal 2 memory on\nfogpatch 1 Hall reveal 2 memory on\n", lf);
            fclose(lf);
        }
        Map *lm = mapio_load(late, err, sizeof err);
        CHECK(lm != NULL);
        if (lm) {
            CHECK_EQ(fog_count(lm, 1, NULL), 2);
            CHECK_EQ(fog_any(lm), 1);
            CHECK_EQ(fog_ground_hidden(lm, 2, 0), 1);
            map_free(lm);
        }
    }

    CASE("a row naming a patch no line created is no fog");
    {
        char bad[700];
        snprintf(bad, sizeof bad, "%s/bad.vtt", sb.dir);
        FILE *bf = fopen(bad, "w");
        if (bf) {
            fputs("VTT 6\nname x\nsize 3 1\ntiles\n...\nfogpatch 1 Hall reveal 2 memory on\n"
                  "fog\nAC?\n", bf);
            fclose(bf);
        }
        Map *bm = mapio_load(bad, err, sizeof err);
        CHECK(bm != NULL);
        if (bm) {
            CHECK_EQ(bm->fog[0], 1);
            CHECK_EQ(bm->fog[1], 0);
            CHECK_EQ(bm->fog[2], 0);
            map_free(bm);
        }
    }

    CASE("remove scrubs a patch for good, and its number is never handed out again this session");
    press(&a, ":fog Crate delete\r");                   /* the old word says the new one */
    CHECK(fog_find(m, "Crate") != 0);
    CHECK(strstr(a.status, ":fog Crate remove") != NULL);
    press(&a, ":fog Crate remove\r");
    CHECK_EQ(fog_at(m, 9, 0), 0);
    CHECK_EQ(fog_find(m, "Crate"), 0);
    CHECK(strstr(a.status, "removed") != NULL);
    press(&a, "u");                                        /* back comes a tile of a dead patch... */
    press(&a, ":fog Newt\r");
    CHECK_EQ(fog_find(m, "Newt"), 3);                      /* ...and slot 2 is not reused */
    CHECK_EQ(fog_ground_hidden(m, 9, 0), 0);               /* a dead number hides nothing */

    CASE("a resize keeps the fog that is still on the map");
    press(&a, ":resize 8x4\r");
    CHECK_EQ(m->w, 8);
    CHECK_EQ(fog_at(m, 7, 3) & FOG_ID, 1);
    CHECK(m->fog_patches[0].x1 <= 7);

    CASE(":fog all is a patch over the whole map, called All exactly");
    press(&a, ":fog Allies\r");
    int allies = fog_find(m, "Allies");
    press(&a, ":fog all 3\r");
    CHECK_EQ(fog_count(m, allies, NULL), 0);
    int all = fog_find(m, "All");
    CHECK(all > 0);
    CHECK_EQ(fog_count(m, all, NULL), 32);
    CHECK_EQ(m->fog_patches[all - 1].reveal, 3);

    CASE("a walled room whose floor alone is painted shows no outline to the players");
    {
        char room[700];
        snprintf(room, sizeof room, "%s/room.vtt", sb.dir);
        FILE *rf = fopen(room, "w");
        if (rf) {
            fputs("VTT 6\nname room\nsize 6 3\nzoom 1\ntiles\n. ... \n. ... \n. ... \n"
                  "vedges\n  |   | \n  |   | \n  |   | \nhedges\n  --- \n      \n      \n  --- \n"
                  "fog on\nfogpatch 1 Room reveal 2 memory on\nfog\n..AAA.\n..AAA.\n..AAA.\n", rf);
            fclose(rf);
        }
        App b;
        Renderer rr;
        rnd_init(&rr);
        rnd_resize(&rr, 40, 12);
        app_init(&b, NULL, &rr);
        CHECK_EQ(app_open_map(&b, room), 0);
        app_key(&b, f2);
        rnd_begin(&rr); app_draw_view(&b, VIEW_PLAYERS);
        ByteBuf rb;
        bb_init(&rb, 8192); rnd_dump(&rr, &rb); bb_putc(&rb, '\0');
        CHECK(strstr(rb.data, "\u2503") == NULL);          /* no heavy wall anywhere */
        CHECK(strstr(rb.data, "\u2501") == NULL);
        bb_free(&rb);
        rnd_begin(&rr); app_draw_view(&b, VIEW_GM);
        bb_init(&rb, 8192); rnd_dump(&rr, &rb); bb_putc(&rb, '\0');
        CHECK(strstr(rb.data, "\u2503") != NULL);          /* the GM's has it */
        bb_free(&rb);
        app_free(&b);
        rnd_free(&rr);
    }

    CASE("a full table refuses the sixteenth");
    char cmd[32];
    for (int i = 0; i < 12; i++) { snprintf(cmd, sizeof cmd, ":fog P%d\r", i); press(&a, cmd); }
    press(&a, ":fog Extra\r");
    CHECK(strstr(a.status, "no room") != NULL);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* The random maps below share fogdiff's generator (defined with it); each
 * suite seeds it itself, so their order does not change either's maps. */
static uint64_t g_fd_rng;

static unsigned fd_rand(unsigned n);

/* The line walk as it was before sight_walk was shared, verbatim, so the
 * refactor answers to the original rather than to itself. */
static int ref_sight_blocked(const Map *m, int x0, int y0, int x1, int y1)
{
    int x = x0, y = y0;
    int dx = x1 > x ? x1 - x : x - x1;
    int dy = y1 > y ? y1 - y : y - y1;
    int sx = x < x1 ? 1 : -1;
    int sy = y < y1 ? 1 : -1;
    int err = dx - dy;
    while (x != x1 || y != y1) {
        int e2 = 2 * err;
        int stepx = 0, stepy = 0;
        if (e2 > -dy) { err -= dy; stepx = sx; }
        if (e2 <  dx) { err += dx; stepy = sy; }
        if (stepx && stepy) {
            int via_x = map_edge_opaque(m, x, y, stepx, 0) || map_edge_opaque(m, x + stepx, y, 0, stepy);
            int via_y = map_edge_opaque(m, x, y, 0, stepy) || map_edge_opaque(m, x, y + stepy, stepx, 0);
            if (via_x && via_y) return 1;
        } else if (map_edge_opaque(m, x, y, stepx, stepy)) {
            return 1;
        }
        x += stepx;
        y += stepy;
    }
    return 0;
}

/* sight_blocked is now sight_walk over the map: every pair of squares on
 * random walled maps, every answer the same as the walk it replaced. */
void test_sight_walk(void)
{
    CASE("the shared walk and the original agree on every line");
    g_fd_rng = 0xD1B54A32D192ED03ull;
    static const uint8_t kinds[] = { EDGE_NONE, EDGE_NONE, EDGE_NONE, EDGE_WALL, EDGE_DOOR_CLOSED,
                                     EDGE_DOOR_OPEN, EDGE_WINDOW, EDGE_SECRET_CLOSED, EDGE_SECRET_OPEN };
    long pairs = 0, blocked = 0, bad = 0;
    for (int trial = 0; trial < 60; trial++) {
        Map *m = map_new(16, 16, "walk");
        int dense = 3 + (int)fd_rand(6);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x <= 16; x++)
                if (!fd_rand((unsigned)dense)) map_set_vedge(m, x, y, kinds[fd_rand(9)]);
        for (int y = 0; y <= 16; y++)
            for (int x = 0; x < 16; x++)
                if (!fd_rand((unsigned)dense)) map_set_hedge(m, x, y, kinds[fd_rand(9)]);
        for (int ay = 0; ay < 16; ay++)
            for (int ax = 0; ax < 16; ax++)
                for (int by = 0; by < 16; by++)
                    for (int bx = 0; bx < 16; bx++) {
                        int ref = ref_sight_blocked(m, ax, ay, bx, by);
                        pairs++;
                        blocked += ref;
                        bad += sight_blocked(m, ax, ay, bx, by) != ref;
                    }
        map_free(m);
    }
    CHECK_EQ(bad, 0);
    CHECK(pairs > 1000000);
    CHECK(blocked > 0 && blocked < pairs);
}

/* ---------------------------------------------------------- fog, differential
 *
 * Random maps, random keystrokes, and after every op the fog bits checked
 * square by square against a brute force written from the definition --
 * no rectangles, no fast paths, nothing shared with fog.c but the line
 * test the ruler also uses. Permanent, so any later change to how sight is
 * worked out answers to the same oracle. VTT_FOGDIFF_OPS sets the length
 * (12,000 by default; 36,000 is the long run). */

static unsigned fd_rand(unsigned n)
{
    g_fd_rng ^= g_fd_rng << 13; g_fd_rng ^= g_fd_rng >> 7; g_fd_rng ^= g_fd_rng << 17;
    return n ? (unsigned)(g_fd_rng % n) : 0;
}

static void fd_write_map(const char *path)
{
    int w = 12 + (int)fd_rand(13), h = 9 + (int)fd_rand(8);
    FILE *f = fopen(path, "w");
    if (!f) return;
    static const char *metric[] = { "chebyshev", "euclidean", "alt", "manhattan" };
    fprintf(f, "VTT 6\nname diff\nsize %d %d\nzoom 1\nmetric %s\ntiles\n", w, h, metric[fd_rand(4)]);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) fputc(fd_rand(12) ? '.' : ' ', f);
        fputc('\n', f);
    }
    /* Mostly open, with every kind of boundary somewhere. */
    static const uint8_t kinds[] = { EDGE_WALL, EDGE_WALL, EDGE_WALL, EDGE_DOOR_CLOSED,
                                     EDGE_DOOR_OPEN, EDGE_WINDOW, EDGE_SECRET_CLOSED, EDGE_SECRET_OPEN };
    fputs("vedges\n", f);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x <= w; x++)
            fputc(fd_rand(6) ? ' ' : edge_file_char(kinds[fd_rand(8)]), f);
        fputc('\n', f);
    }
    fputs("hedges\n", f);
    for (int y = 0; y <= h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t k = fd_rand(6) ? EDGE_NONE : kinds[fd_rand(8)];
            fputc(k == EDGE_WALL ? '-' : edge_file_char(k), f);
        }
        fputc('\n', f);
    }
    /* Up to six creatures, sizes mostly 1, never overlapping. */
    int nt = 1 + (int)fd_rand(6);
    static uint8_t used[64 * 64];
    memset(used, 0, sizeof used);
    for (int i = 0; i < nt; i++) {
        int sz = fd_rand(5) ? 1 : 2 + (int)fd_rand(2);
        int x = (int)fd_rand((unsigned)(w - sz + 1)), y = (int)fd_rand((unsigned)(h - sz + 1));
        int clash = 0;
        for (int yy = y; yy < y + sz; yy++)
            for (int xx = x; xx < x + sz; xx++) clash |= used[yy * 64 + xx];
        if (clash) continue;
        for (int yy = y; yy < y + sz; yy++)
            for (int xx = x; xx < x + sz; xx++) used[yy * 64 + xx] = 1;
        fprintf(f, "token %s %d %d %d \"T%d\"\n", fd_rand(3) ? "player" : "enemy", x, y, sz, i);
    }
    /* One to three patches over rectangles that may overlap (the later one
     * wins the square), with every kind of setting. */
    int np = 1 + (int)fd_rand(3);
    static const int reveals[] = { 0, 1, 2, 3, 6, -1, 0, 1, 2, 3, 6, -1, 12, 40, 99 };
    fprintf(f, "fog on\n%s", fd_rand(2) ? "fog soft-edge\n" : "");
    for (int i = 1; i <= np; i++) {
        int r = reveals[fd_rand(15)];
        char rv[16];
        if (r < 0) snprintf(rv, sizeof rv, "manual"); else snprintf(rv, sizeof rv, "%d", r);
        fprintf(f, "fogpatch %d P%d reveal %s memory %s%s%s\n", i, i, rv,
                fd_rand(3) ? "on" : "off", fd_rand(3) ? "" : " soft-edge on",
                i > 1 && !fd_rand(5) ? " disabled" : "");
    }
    static char fog[64 * 64];
    memset(fog, '.', sizeof fog);
    for (int i = 1; i <= np; i++) {
        int x0 = (int)fd_rand((unsigned)w), y0 = (int)fd_rand((unsigned)h);
        int x1 = x0 + (int)fd_rand((unsigned)(w - x0)), y1 = y0 + (int)fd_rand((unsigned)(h - y0));
        if (i == 1) { x0 = 0; y0 = 0; x1 = w - 1; y1 = h - 1; }      /* the first covers it all */
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) fog[y * 64 + x] = (char)('A' + i - 1);
    }
    fputs("fog\n", f);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) fputc(fog[y * 64 + x], f);
        fputc('\n', f);
    }
    fclose(f);
}

/* What can change sight or the fog, to check that every change to it moved
 * Map.gen -- the one signal sight is recomputed on. */
typedef struct {
    int      w, h, n, metric, fog_on, soft;
    Token    tok[16];
    uint8_t  v[64 * 65], hz[65 * 64], ids[64 * 64];
    FogPatch pat[FOG_PATCH_MAX];
    unsigned gen;
} FdSnap;

static void fd_snap(const Map *m, FdSnap *s)
{
    memset(s, 0, sizeof *s);
    s->w = m->w; s->h = m->h; s->n = m->tokens.n; s->metric = m->metric;
    s->fog_on = m->fog_on; s->soft = m->fog_soft_edge; s->gen = m->gen;
    for (int i = 0; i < m->tokens.n && i < 16; i++) {
        s->tok[i] = m->tokens.v[i];
        /* A position, a size, a kind: the rest of a token is not sight's. */
        Token *t = &s->tok[i];
        Token keep = { 0 };
        keep.x = t->x; keep.y = t->y; keep.size = t->size; keep.kind = t->kind;
        *t = keep;
    }
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x <= m->w; x++) s->v[y * 65 + x] = map_vedge(m, x, y);
    for (int y = 0; y <= m->h; y++)
        for (int x = 0; x < m->w; x++) s->hz[y * 64 + x] = map_hedge(m, x, y);
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            s->ids[y * 64 + x] = fog_at(m, x, y) & (FOG_ID | FOG_HELD);
    memcpy(s->pat, m->fog_patches, sizeof s->pat);
}

static int fd_snap_differs(const FdSnap *a, const FdSnap *b)
{
    FdSnap x = *a, y = *b;
    x.gen = y.gen = 0;
    return memcmp(&x, &y, sizeof x) != 0;
}

/* The definition. */
static int fd_lit(const Map *m, int x, int y)
{
    int id = fog_at(m, x, y) & FOG_ID;
    if (!m->fog_on || !fog_patch_live(m, id)) return 0;
    int n = m->fog_patches[id - 1].reveal;
    if (n < 0) return 0;
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        if (t->kind != TOKEN_PLAYER) continue;
        int tx1 = t->x + t->size - 1, ty1 = t->y + t->size - 1;
        int ox = iclamp(x, t->x, tx1), oy = iclamp(y, t->y, ty1);
        if (dist_tiles((DistMetric)m->metric, x - ox, y - oy) > (double)n + 1e-9) continue;
        for (int fy = t->y; fy <= ty1; fy++)
            for (int fx = t->x; fx <= tx1; fx++)
                if (!sight_blocked(m, fx, fy, x, y)) return 1;
    }
    return 0;
}

static int fd_step_clear(const Map *m, int x, int y, int dx, int dy)
{
    if (!dx || !dy) return !map_edge_opaque(m, x, y, dx, dy);
    return !map_edge_opaque(m, x, y, dx, 0) && !map_edge_opaque(m, x, y, 0, dy) &&
           !map_edge_opaque(m, x + dx, y, 0, dy) && !map_edge_opaque(m, x, y + dy, dx, 0);
}

/* 0 when the map agrees with the definition; else the first square that
 * does not, as y*w+x+1, with what differed in *what. */
static int fd_check(const Map *m, const uint8_t *seen_before, int pure, const char **what)
{
    static uint8_t lit[64 * 64];
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) lit[y * m->w + x] = (uint8_t)fd_lit(m, x, y);
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            uint8_t f = fog_at(m, x, y);
            int id = f & FOG_ID, L = lit[y * m->w + x], R = 0;
            if (m->fog_on && !L && fog_patch_live(m, id))
                for (int dy = -1; dy <= 1 && !R; dy++)
                    for (int dx = -1; dx <= 1 && !R; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if ((!dx && !dy) || !map_in_bounds(m, nx, ny)) continue;
                        R = lit[ny * m->w + nx] && fd_step_clear(m, nx, ny, -dx, -dy);
                    }
            int mem = id && fog_patch_live(m, id) && m->fog_patches[id - 1].memory;
            if (!!(f & FOG_LIT) != L) { *what = "lit"; return y * m->w + x + 1; }
            if (!!(f & FOG_RIM) != R) { *what = "rim"; return y * m->w + x + 1; }
            if (L && mem && !(f & FOG_SEEN)) { *what = "seen missing"; return y * m->w + x + 1; }
            if (pure && seen_before) {
                int want = seen_before[y * m->w + x] || (L && mem);
                if (!!(f & FOG_SEEN) != want) {
                    static char why[96];
                    snprintf(why, sizeof why, "seen changed (was %d now %d, lit %d, mem %d, held %d, id %d)",
                             seen_before[y * m->w + x], !!(f & FOG_SEEN), L, mem, !!(f & FOG_HELD), id);
                    *what = why;
                    return y * m->w + x + 1;
                }
            }
        }
    return 0;
}

/* A pure op -- one that never takes SEEN away -- pressed a key at a time,
 * with the whole check after each: a carry lights the squares it passes,
 * and memory keeps them, so only per key is "seen grows by exactly what is
 * lit" a statement that can be checked. */
static int g_fd_fails;

static void fd_press(App *a, const char *keys, int op, int kind)
{
    static uint8_t seen[64 * 64];
    for (const char *k = keys; *k; k++) {
        Map *m = a->map;
        int  w = m->w, h = m->h;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) seen[y * w + x] = !!(fog_at(m, x, y) & FOG_SEEN);
        char one[2] = { *k, 0 };
        press(a, one);
        m = a->map;
        const char *what = "";
        int bad = fd_check(m, m->w == w && m->h == h ? seen : NULL, 1, &what);
        if (bad && g_fd_fails < 3) {
            g_fd_fails++;
            CHECK(!"fog disagrees with the definition");
            fprintf(stderr, "    op %d kind %d key %d: %s at (%d,%d)\n", op, kind, (unsigned char)*k,
                    what, (bad - 1) % m->w, (bad - 1) / m->w);
        }
    }
}

void test_fog_diff(void)
{
    Sandbox sb = sandbox_enter("fogdiff");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    const char *env = getenv("VTT_FOGDIFF_OPS");
    int ops = env ? atoi(env) : 12000;
    g_fd_rng = 0x9E3779B97F4A7C15ull;

    char path[600];
    snprintf(path, sizeof path, "%s/d.vtt", sb.dir);
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    Key f1 = { KEY_F1, 0, 0 }, f2 = { KEY_F2, 0, 0 };
    int open = 0, fails = 0, maps = 0, gen_fails = 0;
    int mix[22] = { 0 };

    CASE("a wall, a delete and an add in one batch: three touches, three creatures elsewhere, and no step");
    {
        Map *m = map_new(12, 8, "gap");
        m->fog_on = 1;
        int id = fog_create(m, "P1");
        m->fog_patches[id - 1].reveal = 3;
        m->fog_patches[id - 1].memory = 0;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 12; x++) map_fog_set(m, x, y, (uint8_t)id);
        Token pc = { 0 };
        pc.size = 1; pc.kind = TOKEN_PLAYER;
        static const int at[4][2] = { { 1, 1 }, { 5, 5 }, { 9, 1 }, { 9, 6 } };
        for (int i = 0; i < 4; i++) { pc.x = (int16_t)at[i][0]; pc.y = (int16_t)at[i][1]; tokens_add(&m->tokens, pc); }
        Undo u;
        undo_init(&u);
        fog_recompute(m);
        CHECK(fog_at(m, 3, 1) & FOG_LIT);
        undo_begin(&u);
        undo_set_vedge(&u, m, 3, 1, EDGE_WALL);              /* between (2,1) and (3,1) */
        undo_del_token(&u, m, 1);                            /* indices 1..3 shift */
        pc.x = 5; pc.y = 6;
        undo_add_token(&u, m, pc);
        undo_end(&u);
        unsigned st0, fu0, st1, fu1;
        fog_sight_counts(&st0, &fu0);
        fog_recompute(m);
        fog_sight_counts(&st1, &fu1);
        CHECK_EQ(st1 - st0, 0u);
        CHECK_EQ(fog_at(m, 3, 1) & FOG_LIT, 0);             /* behind the new wall */
        CHECK_EQ(fog_at(m, 4, 1) & FOG_LIT, 0);
        undo_undo(&u, m);
        fog_recompute(m);
        CHECK(fog_at(m, 3, 1) & FOG_LIT);
        undo_free(&u);
        map_free(m);
    }

    unsigned steps0, fulls0;
    fog_sight_counts(&steps0, &fulls0);

    CASE("random keystrokes on random maps: fog agrees with the definition after every one");
    g_fd_fails = 0;
    for (int op = 0; op < ops && fails + g_fd_fails < 3 && gen_fails < 3; op++) {
        if (!open || fd_rand(400) == 0) {                   /* a fresh map now and then */
            if (open) app_free(&a);
            fd_write_map(path);
            app_init(&a, NULL, &r);
            if (app_open_map(&a, path) != 0) { open = 0; continue; }
            app_key(&a, f2);
            open = 1;
            maps++;
        }
        Map *m = a.map;
        FdSnap before;
        fd_snap(m, &before);

        int kind = (int)fd_rand(22);
        mix[kind]++;
        char keys[64];
        int  n = m->tokens.n;
        const Token *t = n ? &m->tokens.v[fd_rand((unsigned)n)] : NULL;
        switch (kind) {
        case 0: case 1: case 2: case 3:                      /* carry a creature */
            if (!t) break;
            a.ed.cx = t->x; a.ed.cy = t->y;
            snprintf(keys, sizeof keys, "\r%s%s\r",
                     (const char *[]){ "h", "j", "k", "l", "hh", "jl", "kk", "lj" }[fd_rand(8)],
                     (const char *[]){ "", "h", "j", "k", "l" }[fd_rand(5)]);
            fd_press(&a, keys, op, kind);
            break;
        case 4:                                              /* a group, by box */
            a.ed.cx = (int)fd_rand((unsigned)m->w); a.ed.cy = (int)fd_rand((unsigned)m->h);
            snprintf(keys, sizeof keys, "v%s\r%s\r", (const char *[]){ "ll", "jj", "lljj", "hhkk" }[fd_rand(4)],
                     (const char *[]){ "j", "k", "h", "l" }[fd_rand(4)]);
            fd_press(&a, keys, op, kind);
            break;
        case 5:                                              /* add one */
            a.ed.cx = (int)fd_rand((unsigned)m->w); a.ed.cy = (int)fd_rand((unsigned)m->h);
            if (n < 10) fd_press(&a, fd_rand(3) ? "ipN\r" : "ieN\r", op, kind);
            fd_press(&a, "\x1b", op, kind);
            break;
        case 6:                                              /* delete, resize, change side */
            if (!t) break;
            {
                int idx = (int)(t - m->tokens.v);
                int what = (int)fd_rand(3);
                if (what == 0) { a.ed.cx = t->x; a.ed.cy = t->y; fd_press(&a, "d", op, kind); }
                else if (what == 1) { play_focus(&a.play, idx); fd_press(&a, "b\x1b", op, kind); }
                else {
                    Token nt = *t;
                    nt.kind = nt.kind == TOKEN_PLAYER ? TOKEN_ENEMY : TOKEN_PLAYER;
                    undo_begin(&a.undo); undo_edit_token(&a.undo, m, idx, nt); undo_end(&a.undo);
                    app_fog_sync(&a);
                }
            }
            break;
        case 7:                                              /* a door */
            a.ed.cx = (int)fd_rand((unsigned)m->w); a.ed.cy = (int)fd_rand((unsigned)m->h);
            fd_press(&a, fd_rand(2) ? "o" : "O", op, kind);
            break;
        case 8:                                              /* a wall, in build mode */
            app_key(&a, f1);
            a.ed.cx = (int)fd_rand((unsigned)m->w); a.ed.cy = (int)fd_rand((unsigned)m->h);
            fd_press(&a, (const char *[]){ "H", "J", "K", "L", "tH", "ttJ", "tttK" }[fd_rand(7)], op, kind);
            app_key(&a, f2);
            break;
        case 9:                                              /* paint or scrub fog */
            app_key(&a, f1);
            a.ed.cx = (int)fd_rand((unsigned)m->w); a.ed.cy = (int)fd_rand((unsigned)m->h);
            snprintf(keys, sizeof keys, ":fog P%u\r%s", 1 + fd_rand(3), fd_rand(3) ? "gf" : "gc");
            press(&a, keys);
            press(&a, "\x1b");
            app_key(&a, f2);
            break;
        case 10:                                             /* the GM's hand */
            a.ed.cx = (int)fd_rand((unsigned)m->w); a.ed.cy = (int)fd_rand((unsigned)m->h);
            press(&a, (const char *[]){ "gr", "gh", "gR", "gH" }[fd_rand(4)]);
            break;
        case 11:                                             /* a patch's settings */
            snprintf(keys, sizeof keys, ":fog P%u %s\r", 1 + fd_rand(3),
                     (const char *[]){ "0", "1", "2", "3", "6", "manual", "--soft-edge", "--no-soft-edge",
                                       "disable", "enable", "memory on" }[fd_rand(11)]);
            fd_press(&a, keys, op, kind);
            break;
        case 12:                                             /* memory off, clear, hide, delete */
            snprintf(keys, sizeof keys, ":fog P%u %s\r", 1 + fd_rand(3),
                     (const char *[]){ "memory off", "clear", "hide", "delete" }[fd_rand(fd_rand(8) ? 3 : 4)]);
            press(&a, keys);
            break;
        case 13:                                             /* the switch, the metric */
            fd_press(&a, (const char *[]){ ":fog off\r", ":fog on\r", ":fog --soft-edge\r", ":metric chebyshev\r",
                                        ":metric euclidean\r", ":metric alt\r", ":metric manhattan\r" }[fd_rand(7)], op, kind);
            break;
        case 14: case 15: case 16:                           /* undo, redo */
            press(&a, fd_rand(3) ? "u" : "\x12");
            break;
        case 17:                                             /* resize */
            snprintf(keys, sizeof keys, ":resize %dx%d\r", iclamp(m->w + (int)fd_rand(5) - 2, 8, 30),
                     iclamp(m->h + (int)fd_rand(5) - 2, 6, 20));
            press(&a, keys);
            break;
        case 18:                                             /* save and open again */
            {
                char err[128];
                if (mapio_save(m, path, err, sizeof err) == 0) {
                    app_free(&a);
                    app_init(&a, NULL, &r);
                    if (app_open_map(&a, path) != 0) { open = 0; continue; }
                    app_key(&a, f2);
                }
            }
            break;
        case 20: case 21:                                    /* a batch no key makes today */
            if (!t || a.play.grabbed) break;
            {
                /* Moves mixed with other changes in one undo batch: the
                 * step path must see through every one of them. */
                int idx = (int)(t - m->tokens.v), v = (int)fd_rand(5);
                int nx = iclamp(t->x + (int)fd_rand(3) - 1, 0, m->w - t->size);
                int ny = iclamp(t->y + (int)fd_rand(3) - 1, 0, m->h - t->size);
                int ex = (int)fd_rand((unsigned)m->w + 1), ey = (int)fd_rand((unsigned)m->h);
                undo_begin(&a.undo);
                if (v == 0) {                                /* a move and a wall */
                    undo_move_token(&a.undo, m, idx, nx, ny);
                    undo_set_vedge(&a.undo, m, ex, ey, map_vedge(m, ex, ey) ? EDGE_NONE : EDGE_WALL);
                } else if (v == 1) {                         /* a move and a stroke of paint */
                    undo_move_token(&a.undo, m, idx, nx, ny);
                    fog_paint(m, &a.undo, (int)fd_rand((unsigned)m->w), (int)fd_rand((unsigned)m->h),
                              (int)fd_rand(4));
                } else if (v == 2 && n >= 2) {               /* a wall, a delete, an add: indices shift */
                    Token nt = m->tokens.v[n - 1];
                    nt.x = (int16_t)nx; nt.y = (int16_t)ny;
                    undo_set_vedge(&a.undo, m, ex, ey, map_vedge(m, ex, ey) ? EDGE_NONE : EDGE_WALL);
                    undo_del_token(&a.undo, m, idx);
                    undo_add_token(&a.undo, m, nt);
                } else if (v == 3 && n >= 2) {               /* two creatures swap places */
                    int j = (idx + 1) % n;
                    int ax = m->tokens.v[idx].x, ay = m->tokens.v[idx].y;
                    undo_move_token(&a.undo, m, idx, m->tokens.v[j].x, m->tokens.v[j].y);
                    undo_move_token(&a.undo, m, j, ax, ay);
                } else {                                     /* there and back */
                    int ox = t->x, oy = t->y;
                    undo_move_token(&a.undo, m, idx, nx, ny);
                    undo_move_token(&a.undo, m, idx, ox, oy);
                }
                undo_end(&a.undo);
                app_fog_sync(&a);
            }
            break;
        default:                                             /* stray keys, canceled */
            a.ed.cx = (int)fd_rand((unsigned)m->w); a.ed.cy = (int)fd_rand((unsigned)m->h);
            press(&a, (const char *[]){ "\r", "\rl", "v", "f", "F", "t", "\x1b" }[fd_rand(7)]);
            press(&a, "\x1b");
            break;
        }
        m = a.map;

        FdSnap after;
        fd_snap(m, &after);
        if (kind != 18 && fd_snap_differs(&before, &after) && after.gen == before.gen) {
            gen_fails++;
            CHECK(!"a change to the map that did not move Map.gen");
            fprintf(stderr, "    op %d kind %d\n", op, kind);
        }
        const char *what = "";
        int bad = fd_check(m, NULL, 0, &what);
        if (bad) {
            fails++;
            CHECK(!"fog disagrees with the definition");
            fprintf(stderr, "    op %d kind %d: %s at (%d,%d)\n", op, kind, what,
                    (bad - 1) % m->w, (bad - 1) / m->w);
        }
    }
    CHECK_EQ(fails + g_fd_fails, 0);
    CHECK_EQ(gen_fails, 0);
    CHECK(maps >= 2);

    CASE("both ways of working sight out were exercised");
    unsigned steps, fulls;
    fog_sight_counts(&steps, &fulls);
    steps -= steps0; fulls -= fulls0;
    CHECK(steps > (unsigned)ops / 10);
    CHECK(fulls > 0);
    if (env) {
        fprintf(stderr, "    fogdiff: %u step recomputes, %u full\n", steps, fulls);
        fprintf(stderr, "    fogdiff: %d ops over %d maps; mix", ops, maps);
        for (int i = 0; i < 22; i++) fprintf(stderr, " %d", mix[i]);
        fprintf(stderr, "\n");
    }
    if (open) app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* The cell a boundary draws its middle in: the vertical one west of tile
 * (tx,ty), or the horizontal one north of it. */
static const Cell *edge_cell(const Renderer *r, const App *a, int tx, int ty, int vertical)
{
    int sx, sy;
    grid_tile_screen(&a->ed.view, tx, ty, &sx, &sy);
    if (vertical) sy += 1 + ZOOM[a->ed.view.zoom].ih / 2;
    else          sx += 1 + ZOOM[a->ed.view.zoom].iw / 2;
    return &r->back[(size_t)sy * (size_t)r->w + (size_t)sx];
}

/* Fog, part three: the soft edge. */
void test_fog_edge(void)
{
    Sandbox sb = sandbox_enter("fogedge");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    write_sight_map(sb.dir, "e.vtt", 1, 0);
    char path[600];
    snprintf(path, sizeof path, "%s/e.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 16);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Map *m = a.map;
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    m->tiles[(size_t)2 * (size_t)m->w + 1] = TILE_WATER;    /* on the rim, west */
    m->tiles[(size_t)1 * (size_t)m->w + 2] = TILE_WATER;    /* lit */

    /* Aria at (3,2) lights x 2..4, y 1..3; the rim is the ring round that,
     * out to (5,*) against the wall, whose door is at (6,2). */
    a.ed.cx = 3; a.ed.cy = 2;
    press(&a, "ipAria\r");
    a.ed.cx = 5; a.ed.cy = 3;
    press(&a, "ieOgre\r");                                 /* on the rim */
    a.ed.cx = 0; a.ed.cy = 2;
    press(&a, "ieImp\r");                                  /* beyond it */
    press(&a, "\x1b");
    a.ed.cx = 3; a.ed.cy = 2;                               /* the cursor on Aria, in the light */
    CHECK(fog_at(m, 5, 2) & FOG_RIM);
    CHECK(fog_at(m, 5, 3) & FOG_RIM);
    CHECK_EQ(fog_at(m, 0, 2) & FOG_RIM, 0);

    CASE("off by default: the rim is as dark as the rest");
    CHECK_EQ(fog_rim_shown(m, 5, 2), 0);
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    ByteBuf fr;
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "[?]") == NULL);
    CHECK(strstr(fr.data, "[O]") == NULL);
    bb_free(&fr);
    CHECK_EQ(edge_cell(&r, &a, 6, 2, 1)->ch, ' ');          /* the door, dark both sides */

    CASE(":fog --soft-edge: a creature on the rim is a silhouette, neutral, nameless");
    press(&a, ":fog --soft-edge\r");
    CHECK_EQ(fog_rim_shown(m, 5, 3), 1);
    CHECK_EQ(fog_token_silhouette(m, &m->tokens.v[1]), 1);
    CHECK_EQ(fog_token_silhouette(m, &m->tokens.v[2]), 0);
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "[?]") != NULL);
    CHECK(strstr(fr.data, "[O]") == NULL);
    CHECK(strstr(fr.data, "[I]") == NULL);                  /* the Imp, past the rim */
    CHECK(strstr(fr.data, "Ogre") == NULL);
    bb_free(&fr);
    const Cell *mark = tile_cell(&r, &a, 5, 3);             /* "[?]" starts in the interior */
    CHECK_EQ(mark[0].ch, (uint32_t)'[');
    CHECK_EQ(mark[1].ch, (uint32_t)'?');
    CHECK_EQ(mark[1].fg, a.th->dim);
    int red = 0;
    for (size_t i = 0; i < (size_t)r.w * (size_t)r.h; i++)
        red += r.back[i].fg == a.th->enemy || r.back[i].bg == a.th->enemy;
    CHECK_EQ(red, 0);

    CASE("a big creature with one square on the rim is a silhouette, all of it");
    Token *imp = &m->tokens.v[2];
    imp->x = 0; imp->y = 3; imp->size = 2;                  /* (1,3) is rim, the rest dark */
    CHECK_EQ(fog_token_hidden(m, imp), 1);
    CHECK_EQ(fog_token_silhouette(m, imp), 1);
    imp->x = 0; imp->y = 0; imp->size = 1;                  /* back out of the way, in the dark */
    CHECK_EQ(fog_token_silhouette(m, imp), 0);

    CASE("drawn, the big one is gray across the dark squares it covers too");
    imp->x = 0; imp->y = 3; imp->size = 2;
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    CHECK_EQ(tile_cell(&r, &a, 1, 4)->bg, a.th->dim);      /* (0,3)'s cell is the square's inset */
    imp->x = 0; imp->y = 0; imp->size = 1;

    CASE("a silhouette is a square whatever it is: a circle would say player");
    m->tokens.v[1].kind = TOKEN_PLAYER;                     /* drawn as it stands, no keystroke */
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    CHECK_EQ(tile_cell(&r, &a, 5, 3)[0].ch, (uint32_t)'[');
    m->tokens.v[1].kind = TOKEN_ENEMY;

    CASE("selected, acting and in the fight, a silhouette has no ring, no bars and no side color in the panel");
    m->tokens.v[0].turn = TURN_IN; m->tokens.v[0].init = 12;
    m->tokens.v[1].turn = TURN_IN | TURN_ACTING; m->tokens.v[1].init = 10;
    play_focus(&a.play, 1);
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    red = 0;
    int bars = 0;
    for (size_t i = 0; i < (size_t)r.w * (size_t)r.h; i++) {
        red  += r.back[i].fg == a.th->enemy || r.back[i].bg == a.th->enemy ||
                r.back[i].fg == a.th->enemy_sel;
    }
    int ox, oy;
    grid_tile_screen(&a.ed.view, 5, 3, &ox, &oy);           /* the lattice above the Ogre */
    bars = r.back[(size_t)oy * (size_t)r.w + (size_t)ox + 1].fg == a.th->turn;
    CHECK_EQ(red, 0);
    CHECK_EQ(bars, 0);
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "Ogre") == NULL);
    bb_free(&fr);
    m->tokens.v[1].turn = TURN_IN;                          /* Aria acts: the Ogre's row is plain */
    m->tokens.v[0].turn = TURN_IN | TURN_ACTING;
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    red = 0;
    for (size_t i = 0; i < (size_t)r.w * (size_t)r.h; i++) red += r.back[i].fg == a.th->enemy;
    CHECK_EQ(red, 0);
    rnd_begin(&r); app_draw_view(&a, VIEW_GM);              /* the GM's panel still colors it */
    red = 0;
    for (size_t i = 0; i < (size_t)r.w * (size_t)r.h; i++) red += r.back[i].fg == a.th->enemy;
    CHECK(red > 0);
    m->tokens.v[0].turn = 0; m->tokens.v[1].turn = 0;
    play_focus(&a.play, -1);

    CASE("the GM's frame draws the Ogre as itself");
    rnd_begin(&r); app_draw_view(&a, VIEW_GM);
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "[O]") != NULL);
    CHECK(strstr(fr.data, "[?]") == NULL);
    bb_free(&fr);

    CASE("at the rim a door is a dimmed wall; terrain is not drawn; nothing past the rim is");
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    const Cell *door = edge_cell(&r, &a, 6, 2, 1);
    CHECK_EQ(door->ch, (uint32_t)0x2503u);                   /* a heavy wall, not a door */
    CHECK_EQ(door->fg, a.th->dim);
    CHECK_EQ(edge_cell(&r, &a, 6, 1, 1)->fg, a.th->dim);     /* the wall beside it, dimmed */
    CHECK(tile_cell(&r, &a, 1, 2)->bg != a.th->terrain_bg[TILE_WATER]);
    CHECK_EQ(tile_cell(&r, &a, 2, 1)->bg, a.th->terrain_bg[TILE_WATER]);
    CHECK_EQ(edge_cell(&r, &a, 6, 4, 1)->fg, a.th->dim);     /* (5,4) is rim, diagonally */
    CHECK_EQ(edge_cell(&r, &a, 0, 2, 0)->ch, ' ');          /* between two dark squares */

    CASE("no grid lines on the rim: it shows walls, not floor");
    CHECK_EQ(edge_cell(&r, &a, 1, 0, 1)->ch, ' ');          /* between rim (1,0) and dark (0,0) */

    CASE("lit, the door is a door again, in its own color");
    play_focus(&a.play, 0);
    press(&a, "\rl\r");                                   /* Aria to (4,2): (5,2) is lit */
    CHECK(fog_at(m, 5, 2) & FOG_LIT);
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    door = edge_cell(&r, &a, 6, 2, 1);
    CHECK_EQ(door->fg, a.th->edge_door);

    CASE("a patch can take its own setting over the map's, and the listing says so");
    press(&a, ":fog Dark --no-soft-edge\r");
    CHECK_EQ(fog_rim_shown(m, 6, 1), 0);
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    bb_init(&fr, 65536); rnd_dump(&r, &fr); bb_putc(&fr, '\0');
    CHECK(strstr(fr.data, "[?]") == NULL);
    bb_free(&fr);
    press(&a, ":fog\r");
    CHECK(strstr(a.status, "soft") == NULL);
    press(&a, ":fog Dark --soft-edge\r");
    press(&a, ":fog --no-soft-edge\r");
    CHECK_EQ(fog_patch_soft(m, 1), 1);
    press(&a, ":fog\r");
    CHECK(strstr(a.status, "soft") != NULL);

    CASE("the setting is saved with the patch");
    char err[128];
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    app_free(&a);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    m = a.map;
    CHECK_EQ(m->fog_soft_edge, 0);
    CHECK_EQ(fog_patch_soft(m, 1), 1);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* Fog, part two: player creatures light what they can see, as they move. */
void test_fog_sight(void)
{
    Sandbox sb = sandbox_enter("fogsight");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    write_sight_map(sb.dir, "s.vtt", 2, 1);
    char path[600];
    snprintf(path, sizeof path, "%s/s.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 16);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Map *m = a.map;
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);

    CASE("with nobody on the map nothing is lit");
    CHECK_EQ(fog_ground_hidden(m, 2, 2), 1);

    CASE("a player creature lights its reveal, in the map's metric, and no further");
    a.ed.cx = 2; a.ed.cy = 2;
    press(&a, "ipAria\r");
    CHECK(fog_at(m, 2, 2) & FOG_LIT);
    CHECK(fog_at(m, 4, 4) & FOG_LIT);                       /* chebyshev 2 */
    CHECK(fog_at(m, 0, 0) & FOG_LIT);
    CHECK_EQ(fog_at(m, 5, 2) & FOG_LIT, 0);                 /* 3 away */
    CHECK_EQ(fog_ground_hidden(m, 5, 2), 1);
    CHECK_EQ(fog_token_hidden(m, &m->tokens.v[0]), 0);

    CASE("an enemy lights nothing");
    a.ed.cx = 9; a.ed.cy = 2;
    press(&a, "ieOgre\r");
    CHECK_EQ(fog_at(m, 9, 2) & FOG_LIT, 0);
    CHECK_EQ(fog_token_hidden(m, &m->tokens.v[1]), 1);

    CASE("walking up to the wall lights up to it, never through it");
    press(&a, "\x1b");
    play_focus(&a.play, 0);
    a.ed.cx = 2; a.ed.cy = 2;
    press(&a, "\r lll\r");                                  /* carry Aria to (5,2) */
    press(&a, "\r");
    CHECK_EQ(m->tokens.v[0].x, 5);
    CHECK(fog_at(m, 5, 0) & FOG_LIT);
    CHECK_EQ(fog_at(m, 6, 2) & FOG_LIT, 0);                 /* beyond the closed door */
    CHECK_EQ(fog_at(m, 7, 1) & FOG_LIT, 0);

    CASE("memory keeps the ground she left drawn, and nothing standing on it");
    CHECK(fog_at(m, 1, 2) & FOG_SEEN);
    CHECK_EQ(fog_at(m, 1, 2) & FOG_LIT, 0);                 /* 4 away now */
    CHECK_EQ(fog_ground_hidden(m, 1, 2), 0);
    CHECK_EQ(fog_creature_hidden(m, 1, 2), 1);

    CASE("opening the door lights what is beyond it, and the Ogre with it");
    a.ed.cx = 5; a.ed.cy = 2;
    press(&a, "o");
    CHECK(fog_at(m, 7, 2) & FOG_LIT);
    CHECK(fog_at(m, 6, 3) & FOG_LIT);                        /* round the door's frame, diagonally */
    press(&a, "o");                                         /* closed again: dark again */
    CHECK_EQ(fog_at(m, 7, 2) & FOG_LIT, 0);
    CHECK(fog_at(m, 7, 2) & FOG_SEEN);                       /* but remembered */

    CASE("undo of a step works sight out again");
    press(&a, "u");                                         /* the door */
    CHECK(fog_at(m, 7, 2) & FOG_LIT);
    press(&a, "u");
    CHECK_EQ(fog_at(m, 7, 2) & FOG_LIT, 0);

    CASE("the rim is the unlit fog beside the light, and not through the wall");
    CHECK(fog_at(m, 2, 2) & FOG_RIM);                       /* 3 west of her, beside lit (3,2) */
    CHECK_EQ(fog_at(m, 3, 2) & FOG_RIM, 0);                 /* lit, so not rim */
    CHECK_EQ(fog_at(m, 6, 1) & FOG_RIM, 0);                 /* beside lit (5,1), across the wall */
    CHECK_EQ(fog_at(m, 6, 2) & FOG_RIM, 0);                 /* across the closed door */

    CASE("the GM's light stays where it was put, whoever walks away");
    a.ed.cx = 10; a.ed.cy = 4;
    press(&a, "gr");
    CHECK(fog_at(m, 10, 4) & FOG_HELD);
    play_focus(&a.play, 0);
    a.ed.cx = 5; a.ed.cy = 2;
    press(&a, "\rh\r");
    CHECK(fog_at(m, 10, 4) & FOG_HELD);
    CHECK_EQ(fog_ground_hidden(m, 10, 4), 0);

    CASE("two creatures side by side: one walking away does not put out the other's light");
    a.ed.cx = 2; a.ed.cy = 0;
    press(&a, "ipBram\r");
    press(&a, "\x1b");
    CHECK(fog_at(m, 0, 0) & FOG_LIT);                        /* Bram's */
    play_focus(&a.play, 0);                                 /* Aria, at (4,2) */
    a.ed.cx = 4; a.ed.cy = 2;
    press(&a, "\rjj\r");
    CHECK(fog_at(m, 0, 0) & FOG_LIT);
    CHECK(fog_at(m, 3, 1) & FOG_LIT);                        /* both of theirs */

    CASE("a carry's steps relight only the creature that moved; picking up and putting down do not move it");
    unsigned st0, fu0, st1, fu1;
    play_focus(&a.play, 0);
    a.ed.cx = m->tokens.v[0].x; a.ed.cy = m->tokens.v[0].y;
    fog_sight_counts(&st0, &fu0);
    press(&a, "\r");
    fog_sight_counts(&st1, &fu1);
    press(&a, "l");
    unsigned st2, fu2;
    fog_sight_counts(&st2, &fu2);
    CHECK_EQ(st2 - st1, 1u);
    CHECK_EQ(fu2 - fu1, 0u);
    press(&a, "h\r");
    fog_sight_counts(&st1, &fu1);
    CHECK(st1 - st0 >= 2);

    CASE("sight is bounded: each creature's cache covers its reach and no more");
    CHECK_EQ(m->sight.n, m->tokens.n);
    for (int i = 0; i < m->tokens.n; i++) {
        const Token     *t = &m->tokens.v[i];
        const SightEntry *e = &m->sight.e[i];
        if (t->kind != TOKEN_PLAYER) { CHECK(e->x1 < e->x0); continue; }
        CHECK(e->x0 >= t->x - 2 && e->x1 <= t->x + 2 && e->y0 >= t->y - 2 && e->y1 <= t->y + 2);
    }

    CASE("reveal manual lights nothing by itself; the master switch and disable put everything out");
    press(&a, ":fog Dark manual\r");
    CHECK_EQ(fog_at(m, 0, 0) & FOG_LIT, 0);
    press(&a, ":fog Dark 2\r");
    CHECK(fog_at(m, 0, 0) & FOG_LIT);
    press(&a, ":fog off\r");
    CHECK_EQ(fog_at(m, 0, 0) & FOG_LIT, 0);
    press(&a, ":fog on\r");
    press(&a, ":fog Dark disable\r");
    CHECK_EQ(fog_at(m, 0, 0) & FOG_LIT, 0);
    press(&a, ":fog Dark enable\r");
    CHECK(fog_at(m, 0, 0) & FOG_LIT);

    CASE("lit and rim are never saved, and the map opens with sight worked out");
    char err[128];
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    app_free(&a);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    m = a.map;
    CHECK(fog_at(m, 0, 0) & FOG_LIT);

    CASE("memory off: the dark closes behind");
    app_free(&a);
    write_sight_map(sb.dir, "l.vtt", 1, 0);
    snprintf(path, sizeof path, "%s/l.vtt", sb.dir);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    m = a.map;
    app_key(&a, f2);
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "ipAria\r");
    CHECK(fog_at(m, 0, 0) & FOG_LIT);
    press(&a, "\x1b");
    play_focus(&a.play, 0);
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "\rlll\r");
    CHECK_EQ(fog_at(m, 0, 0) & (FOG_LIT | FOG_SEEN), 0);
    CHECK_EQ(fog_ground_hidden(m, 0, 0), 1);

    CASE("switching memory off forgets what the patch remembered, apart from what the GM holds");
    a.ed.cx = 10; a.ed.cy = 4;
    press(&a, "gr");
    press(&a, ":fog Dark memory on\r");
    play_focus(&a.play, 0);
    a.ed.cx = 4; a.ed.cy = 1;
    press(&a, "\rhhh\r");                                  /* back west: remembers (4,1)..(5,*) */
    CHECK(fog_at(m, 5, 1) & FOG_SEEN);
    press(&a, ":fog Dark memory off\r");
    CHECK_EQ(fog_at(m, 5, 1) & FOG_SEEN, 0);
    CHECK(fog_at(m, 10, 4) & FOG_SEEN);                      /* held: kept */
    CHECK(fog_at(m, 0, 0) & FOG_LIT);                        /* what she sees now stays lit */

    CASE("a big creature lights from all of its squares");
    a.ed.cx = 1; a.ed.cy = 3;
    press(&a, "2bipOx\r");
    CHECK(fog_at(m, 0, 4) & FOG_LIT);
    CHECK(fog_at(m, 3, 4) & FOG_LIT);                       /* one past its right edge */

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}
