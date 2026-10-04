/* The players' camera (docs/CAMERA.md): :player camera follow|party|hold --
 * what their frame looks at, what it leaves out, where a tap lands, and
 * how long a held view lasts. */

#include "harness.h"
#include "app_priv.h"

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

/* An open 60x20 map: Aria and Bram near the top left, a Ghoul far off. */
static void write_field(const char *path)
{
    char text[4096];
    int  off = snprintf(text, sizeof text, "VTT 9\nname field\nsize 60 20\nzoom 1\ntiles\n");
    for (int y = 0; y < 20; y++)
        off += snprintf(text + off, sizeof text - (size_t)off, "%s\n",
                        "............................................................");
    snprintf(text + off, sizeof text - (size_t)off,
             "token player 5 5 1 \"Aria\"\ntoken player 7 6 1 \"Bram\"\ntoken enemy 50 15 1 \"Ghoul\"\n");
    write_text(path, text);
}

static char *gm_text(App *a, Renderer *r)
{
    rnd_begin(r);
    app_draw(a);
    ByteBuf f;
    bb_init(&f, 65536);
    rnd_dump(r, &f);
    bb_putc(&f, '\0');
    return (char *)f.data;
}

static int token_at(const Map *m, const char *label)
{
    for (int i = 0; i < m->tokens.n; i++) if (!strcmp(m->tokens.v[i].label, label)) return i;
    return -1;
}

/* Every square from (x0,y0) to (x1,y1) is on the players' screen. The GM's
 * frame first, as app_frame draws them: it lays out the view theirs is cut
 * to. */
static int players_see(App *a, Renderer *r, int x0, int y0, int x1, int y1)
{
    rnd_begin(r);
    app_draw(a);
    free(players_text(a, r));
    int vx0, vy0, vx1, vy1;
    grid_visible_tiles(&a->pview, a->map, &vx0, &vy0, &vx1, &vy1);
    return vx0 <= x0 && vy0 <= y0 && vx1 >= x1 && vy1 >= y1;
}

void test_camera(void)
{
    Sandbox sb = sandbox_enter("camera");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[1200], other[1200];
    snprintf(path, sizeof path, "%s/field.vtt", sb.dir);
    snprintf(other, sizeof other, "%s/other.vtt", sb.dir);
    write_field(path);
    write_field(other);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    Map *m = a.map;
    int aria = token_at(m, "Aria"), bram = token_at(m, "Bram");

    CASE("follow, the default: their frame is the GM's, and the title says nothing");
    CHECK_EQ(a.pcam, PCAM_FOLLOW);
    CHECK_EQ(app_players_own_camera(&a), 0);
    {
        char *t = gm_text(&a, &r);
        CHECK(strstr(t, "CAM ") == NULL);
        free(t);
        app_set_status(&a, "hello table");
        t = players_text(&a, &r);
        CHECK(strstr(t, "hello table") != NULL);
        free(t);
    }

    CASE(":player camera alone says which, GM-only; a wrong word too");
    press(&a, ":player camera\r");
    CHECK(strstr(a.status, "on follow") && a.status_gm);
    press(&a, ":player camera wobble\r");
    CHECK(strstr(a.status, "follow|party|hold") && a.status_gm);
    CHECK_EQ(a.pcam, PCAM_FOLLOW);

    CASE("party: their own camera, always drawn apart, the party on it with its margin");
    press(&a, ":player camera party\r");
    CHECK_EQ(a.pcam, PCAM_PARTY);
    CHECK(a.status_gm);
    CHECK_EQ(app_players_own_camera(&a), 1);
    CHECK_EQ(app_view_differs(&a), 1);
    CHECK(players_see(&a, &r, 5 - 2, 5 - 2, 7 + 2, 6 + 2));
    CHECK_EQ(a.pview.zoom, a.ed.view.zoom);

    CASE("the GM's title says CAM PARTY; theirs does not, nor the status message");
    {
        char *t = gm_text(&a, &r);
        CHECK(strstr(t, "CAM PARTY") != NULL);
        free(t);
        app_set_status(&a, "hello table");
        t = players_text(&a, &r);
        CHECK(strstr(t, "CAM ") == NULL);
        CHECK(strstr(t, "hello table") == NULL);
        free(t);
    }

    CASE("the GM's cursor, away from the party, does not move their camera");
    {
        int cx = a.pview.cam_x, cy = a.pview.cam_y;
        a.ed.cx = 50; a.ed.cy = 15;
        rnd_begin(&r); app_draw(&a);
        free(players_text(&a, &r));
        CHECK(a.pview.cam_x == cx && a.pview.cam_y == cy);
    }

    CASE("a step inside the margin moves nothing; the party walking moves it now and then, never off screen");
    {
        int cx = a.pview.cam_x, cy = a.pview.cam_y;
        m->tokens.v[bram].x = 6; m->gen++;
        free(players_text(&a, &r));
        CHECK(a.pview.cam_x == cx && a.pview.cam_y == cy);
        m->tokens.v[bram].x = 7; m->gen++;
        int moves = 0, steps = 30, lost = 0;
        for (int s = 0; s < steps; s++) {
            m->tokens.v[aria].x++; m->tokens.v[bram].x++; m->gen++;
            int px = a.pview.cam_x;
            const Token *ta = &m->tokens.v[aria], *tb = &m->tokens.v[bram];
            if (!players_see(&a, &r, imax(0, ta->x - 2), ta->y, imin(59, tb->x + 2), tb->y)) lost++;
            moves += a.pview.cam_x != px;
        }
        CHECK_EQ(lost, 0);
        CHECK(moves > 0 && moves <= steps / 3);
        m->tokens.v[aria].x = 5; m->tokens.v[bram].x = 7; m->gen++;
    }

    CASE("spread out: the closest zoom out that fits them, the GM's own untouched");
    {
        int gz = a.ed.view.zoom;
        m->tokens.v[bram].x = 30; m->gen++;
        CHECK(players_see(&a, &r, 3, 3, 32, 8));
        CHECK(a.pview.zoom < gz);
        CHECK_EQ(a.ed.view.zoom, gz);
        CASE("a frame with nothing changed keeps that zoom and view");
        {
            int z = a.pview.zoom, cx = a.pview.cam_x, cy = a.pview.cam_y;
            CHECK(players_see(&a, &r, 3, 3, 32, 8));
            CHECK(a.pview.zoom == z && a.pview.cam_x == cx && a.pview.cam_y == cy);
        }
        CASE("close together again: back to the GM's zoom, never closer in");
        m->tokens.v[bram].x = 7; m->gen++;
        free(players_text(&a, &r));
        CHECK_EQ(a.pview.zoom, gz);
        press(&a, "-");                                     /* the GM zooms out: so do they */
        free(players_text(&a, &r));
        CHECK_EQ(a.pview.zoom, a.ed.view.zoom);
        press(&a, "+");
    }

    CASE("a hidden creature is not the party's: its square does not widen the view");
    {
        m->tokens.v[bram].x = 40; m->tokens.v[bram].hidden = 1; m->gen++;
        free(players_text(&a, &r));
        CHECK_EQ(a.pview.zoom, a.ed.view.zoom);
        m->tokens.v[bram].hidden = 0; m->gen++;
        free(players_text(&a, &r));
        CHECK(a.pview.zoom < a.ed.view.zoom);
        m->tokens.v[bram].x = 7; m->gen++;
    }

    CASE("a big creature's whole footprint is kept in view");
    {
        m->tokens.v[bram].size = 3; m->tokens.v[bram].x = 15; m->gen++;
        CHECK(players_see(&a, &r, 3, 3, 17 + 2, 8 + 2));
        m->tokens.v[bram].size = 1; m->tokens.v[bram].x = 7; m->gen++;
    }

    CASE("a party that only just fits still moves the camera every third step at most");
    {
        /* 18 apart: with its margin, 23 of the 24 squares at zoom 1 */
        m->tokens.v[aria].x = 2; m->tokens.v[bram].x = 2 + 18; m->gen++;
        free(players_text(&a, &r));
        int moves = 0, steps = 30;
        for (int s = 0; s < steps; s++) {
            m->tokens.v[aria].x++; m->tokens.v[bram].x++; m->gen++;
            int px = a.pview.cam_x;
            free(players_text(&a, &r));
            moves += a.pview.cam_x != px;
        }
        CHECK(moves <= steps / 3 + 1);
        m->tokens.v[aria].x = 5; m->tokens.v[bram].x = 7; m->gen++;
    }

    CASE("their status line is blank, not only the message: it describes the GM's cursor");
    {
        a.ed.cx = 6; a.ed.cy = 6;                         /* on nothing: the square is named */
        char *g = gm_text(&a, &r);
        CHECK(strstr(g, "G7") != NULL);
        free(g);
        char *t = players_text(&a, &r);
        CHECK(strstr(t, "G7") == NULL);
        free(t);
    }

    CASE("under :player preview the frame is cut to the screen as it is now");
    {
        press(&a, ":player preview\r");
        CHECK_EQ(a.preview, 1);
        /* Fits at zoom 1 on the screen it was, not on the one it is. */
        m->tokens.v[bram].x = 17; m->gen++;
        rnd_resize(&r, 60, 30);
        CHECK(players_see(&a, &r, 3, 3, 19, 8));
        rnd_resize(&r, 100, 30);
        press(&a, "q");
        CHECK_EQ(a.preview, 0);
        m->tokens.v[bram].x = 7; m->gen++;
    }

    CASE("too far apart even for the farthest zoom: the one whose turn it is is framed");
    {
        m->tokens.v[bram].x = 58; m->tokens.v[bram].y = 18; m->gen++;
        a.ed.cx = 58; a.ed.cy = 18; press(&a, "si20\r");    /* Bram, first */
        a.ed.cx = 5;  a.ed.cy = 5;  press(&a, "si10\r");
        play_focus(&a.play, -1);
        press(&a, "a");
        CHECK_EQ(turn_acting(m), bram);
        CHECK(players_see(&a, &r, 58, 18, 58, 18));
        CHECK_EQ(a.pview.zoom, 0);
        press(&a, ":turns end\r");
        m->tokens.v[bram].x = 7; m->tokens.v[bram].y = 6; m->gen++;
    }

    CASE("a tap lands through their camera");
    {
        free(players_text(&a, &r));
        int sx, sy;
        grid_tile_interior(&a.pview, 6, 6, &sx, &sy);
        a.npings = 0;
        CHECK_EQ(app_ping_cell(&a, 3, sx, sy), 1);
        CHECK(a.npings == 1 && a.pings[0].x0 == 6 && a.pings[0].y0 == 6);
    }

    CASE("hold: the GM's view now, kept while the GM looks elsewhere");
    {
        a.ed.cx = 40; a.ed.cy = 12;
        rnd_begin(&r); app_draw(&a);
        press(&a, ":player camera hold\r");
        CHECK_EQ(a.pcam, PCAM_HOLD);
        CHECK(a.pview.cam_x == a.ed.view.cam_x && a.pview.cam_y == a.ed.view.cam_y);
        int hx = a.pview.cam_x, hy = a.pview.cam_y, hz = a.pview.zoom;
        a.ed.cx = 2; a.ed.cy = 2;
        press(&a, "+");
        rnd_begin(&r); app_draw(&a);
        CHECK(players_see(&a, &r, 40, 12, 40, 12));
        CHECK(a.pview.cam_x == hx && a.pview.cam_y == hy && a.pview.zoom == hz);
        press(&a, "-");
        char *t = gm_text(&a, &r);
        CHECK(strstr(t, "CAM HOLD") != NULL);
        free(t);

        CASE("hold again: where the GM is now");
        a.ed.cx = 50; a.ed.cy = 15;
        rnd_begin(&r); app_draw(&a);
        press(&a, ":player camera hold\r");
        CHECK(players_see(&a, &r, 50, 15, 50, 15));
        CHECK(a.pview.cam_x == a.ed.view.cam_x && a.pview.cam_y == a.ed.view.cam_y);
    }

    CASE("held, the GM's cursor is not on their frame, though it is on the GM's");
    {
        a.ed.cx = 48; a.ed.cy = 14;                       /* inside the held view, nothing there */
        rnd_begin(&r); app_draw(&a);
        int sx, sy;
        grid_tile_interior(&a.ed.view, 48, 14, &sx, &sy);
        CHECK_EQ(rnd_at(&r, sx, sy)->bg, a.th->cursor_bg);
        free(players_text(&a, &r));
        grid_tile_interior(&a.pview, 48, 14, &sx, &sy);
        CHECK(rnd_at(&r, sx, sy)->bg != a.th->cursor_bg);
        CASE("in party, the cursor inside their frame is drawn: the table sees what the GM moves");
        press(&a, ":player camera party\r");
        a.ed.cx = 6; a.ed.cy = 5;
        rnd_begin(&r); app_draw(&a);
        free(players_text(&a, &r));
        grid_tile_interior(&a.pview, 6, 5, &sx, &sy);
        CHECK_EQ(rnd_at(&r, sx, sy)->bg, a.th->cursor_bg);
    }

    CASE("held, a tap lands through the held camera");
    {
        press(&a, ":player camera hold\r");
        free(players_text(&a, &r));
        int sx, sy;
        grid_tile_interior(&a.pview, 7, 6, &sx, &sy);
        a.npings = 0;
        CHECK_EQ(app_ping_cell(&a, 3, sx, sy), 1);
        CHECK(a.npings == 1 && a.pings[0].x0 == 7 && a.pings[0].y0 == 6);
    }

    CASE("follow again: the GM's camera, nothing of their own");
    press(&a, ":player camera follow\r");
    CHECK_EQ(app_players_own_camera(&a), 0);

    CASE("another map: hold becomes follow; party stays");
    press(&a, ":player camera hold\r");
    CHECK_EQ(app_open_map(&a, other), 0);
    CHECK_EQ(a.pcam, PCAM_FOLLOW);
    press(&a, ":player camera party\r");
    CHECK_EQ(app_open_map(&a, path), 0);
    CHECK_EQ(a.pcam, PCAM_PARTY);
    app_key(&a, f2);
    CASE("a trip: hold becomes follow there too");
    {
        press(&a, ":player camera hold\r");
        char err[256];
        Map *d = mapio_load(other, err, sizeof err);
        CHECK(d != NULL);
        if (d) app_travel_to(&a, d);
        CHECK_EQ(a.pcam, PCAM_FOLLOW);
    }

    app_free(&a);
    rnd_free(&r);
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", sb.dir);
    sandbox_leave(&sb);
    if (system(cmd) != 0) { }
}

/* Floors: hold takes the GM's floor with it, and their floor changing
 * centers them on the party there. */
void test_camera_floors(void)
{
    Sandbox sb = sandbox_enter("camfloors");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[1200];
    snprintf(path, sizeof path, "%s/tower.vtt", sb.dir);
    char text[4096];
    int  off = snprintf(text, sizeof text, "VTT 9\nname tower\nsize 61 10\nzoom 1\ntiles\n");
    for (int y = 0; y < 10; y++)
        off += snprintf(text + off, sizeof text - (size_t)off, "%s\n",
                        ".............................. ..............................");
    snprintf(text + off, sizeof text - (size_t)off,
             "token player 3 3 1 \"Aria\"\ntoken player 4 4 1 \"Bram\"\ntoken enemy 50 5 1 \"Wraith\"\n"
             "area 0 0 29 9 \"Ground\"\narea 31 0 60 9 \"Upper\"\n"
             "floor \"Ground\" 0\nfloor \"Upper\" 1\n");
    write_text(path, text);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 60, 16);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    Map *m = a.map;
    int ground = map_area_find(m, "Ground"), upper = map_area_find(m, "Upper");
    CHECK_EQ(app_players_floor(&a), ground);

    CASE("hold on a floor the players are not on: they are shown it, pinned there");
    press(&a, ":floor Upper\r");
    a.ed.cx = 50; a.ed.cy = 5;
    rnd_begin(&r); app_draw(&a);
    press(&a, ":player camera hold\r");
    CHECK_EQ(app_players_floor(&a), upper);
    CHECK(players_see(&a, &r, 50, 5, 50, 5));

    CASE("their floor changing under hold: centered on the party there, held after");
    press(&a, ":player floor Ground\r");
    CHECK_EQ(app_players_floor(&a), ground);
    CHECK(players_see(&a, &r, 3, 3, 4, 4));
    CHECK_EQ(a.pcam, PCAM_HOLD);

    CASE("party on a floor: only that floor's creatures count");
    press(&a, ":player camera party\r");
    CHECK(players_see(&a, &r, 3, 3, 4, 4));
    CHECK(a.pview.bounded && a.pview.bx1 == 29);

    CASE("party on a floor with none of the party: the floor's middle, not where it last was");
    press(&a, ":player floor Upper\r");
    CHECK(players_see(&a, &r, 45, 4, 46, 5));

    CASE("the pin a hold made goes when hold does; a pin of the GM's own stays");
    press(&a, ":player floor auto\r");
    press(&a, ":floor Upper\r");
    press(&a, ":player camera hold\r");
    CHECK_EQ(app_players_floor(&a), upper);
    press(&a, ":player camera follow\r");
    press(&a, ":floor Ground\r");
    CHECK_EQ(app_players_floor(&a), ground);
    press(&a, ":player floor Upper\r");
    press(&a, ":player camera hold\r");
    press(&a, ":player floor Upper\r");                 /* the GM pins it, under hold */
    press(&a, ":player camera follow\r");
    CHECK_EQ(app_players_floor(&a), upper);
    press(&a, ":player floor auto\r");

    CASE("hold with every floor shown: the floor under the GM's cursor, and what the GM sees of it");
    press(&a, ":floor all\r");
    CHECK_EQ(app_floor_shown(&a), -1);
    a.ed.cx = 50; a.ed.cy = 5;
    rnd_begin(&r); app_draw(&a);
    press(&a, ":player camera hold\r");
    CHECK_EQ(app_players_floor(&a), upper);
    CHECK(players_see(&a, &r, 50, 5, 50, 5));

    app_free(&a);
    rnd_free(&r);
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", sb.dir);
    sandbox_leave(&sb);
    if (system(cmd) != 0) { }
}
