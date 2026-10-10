/* Tests: named areas, links, floors. */

#include "harness.h"

void test_areas(void)
{
    char err[256];
    CASE("area names: never a square, a quote or a colon; found ignoring case");
    CHECK_EQ(map_area_name_ok("Crypt"), 1);
    CHECK_EQ(map_area_name_ok("Great Hall"), 1);
    CHECK_EQ(map_area_name_ok("C3"), 0);
    CHECK_EQ(map_area_name_ok("AB12"), 0);
    CHECK_EQ(map_area_name_ok("Cell1"), 1);            /* a fourth letter is past any map */
    CHECK_EQ(map_area_name_ok("Room2"), 1);
    CHECK_EQ(map_area_name_ok("5"), 0);                 /* a row */
    CHECK_EQ(map_area_name_ok("a:b"), 0);
    CHECK_EQ(map_area_name_ok("say \"hi\""), 0);
    CHECK_EQ(map_area_name_ok(""), 0);
    CHECK_EQ(map_area_name_ok(" Hall"), 0);
    Map *m = map_new(10, 8, "a");
    CHECK_EQ(map_area_set(m, "Crypt", 5, 4, 1, 1), 0);          /* either order */
    CHECK(m->areas[0].x0 == 1 && m->areas[0].x1 == 5 && m->areas[0].y1 == 4);
    CHECK_EQ(map_area_find(m, "crypt"), 0);
    CHECK_EQ(map_area_at(m, 3, 3), 0);
    CHECK_EQ(map_area_at(m, 6, 3), -1);
    CHECK_EQ(map_area_set(m, "Off", 20, 20, 30, 30), -1);       /* nothing on the map */
    CHECK_EQ(map_area_set(m, "Edge", 8, 6, 20, 20), 1);         /* clipped */
    CHECK(m->areas[1].x1 == 9 && m->areas[1].y1 == 7);

    CASE("resizing cuts areas to what is left, and drops one left with nothing");
    map_resize(m, 7, 5);
    CHECK_EQ(m->nareas, 1);
    CHECK(m->areas[0].x1 == 5 && m->areas[0].y1 == 4);

    CASE("areas through the undo log: named, renamed by case, taken off, and each undone");
    {
        Undo u;
        undo_init(&u);
        undo_begin(&u); CHECK_EQ(undo_set_area(&u, m, "Vault", 0, 0, 1, 1), 1); undo_end(&u);
        undo_begin(&u); CHECK_EQ(undo_set_area(&u, m, "VAULT", 0, 0, 2, 2), 1); undo_end(&u);
        CHECK_EQ(strcmp(m->areas[1].name, "VAULT"), 0);
        undo_begin(&u); CHECK_EQ(undo_remove_area(&u, m, "Crypt"), 1); undo_end(&u);
        CHECK_EQ(m->nareas, 1);
        undo_undo(&u, m);
        CHECK_EQ(map_area_find(m, "Crypt"), 0);         /* back in its place, first */
        undo_undo(&u, m);
        int v = map_area_find(m, "vault");
        CHECK(v >= 0 && !strcmp(m->areas[v].name, "Vault") && m->areas[v].x1 == 1);
        undo_undo(&u, m);
        CHECK_EQ(map_area_find(m, "Vault"), -1);
        undo_redo(&u, m);
        undo_redo(&u, m);
        CHECK_EQ(map_area_find(m, "Vault") >= 0, 1);
        undo_free(&u);
    }

    CASE("the file: version 7 with areas, 6 or less without; read back the same");
    {
        Sandbox sb = sandbox_enter("areas");
        char path[600];
        snprintf(path, sizeof path, "%s/a.vtt", sb.dir);
        CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
        FILE *f = fopen(path, "r");
        char first[32] = "";
        if (f) { if (!fgets(first, sizeof first, f)) first[0] = 0; fclose(f); }
        CHECK_EQ(strcmp(first, "VTT 7\n"), 0);
        Map *l = mapio_load(path, err, sizeof err);
        CHECK(l && l->nareas == m->nareas);
        if (l) {
            for (int i = 0; i < l->nareas; i++)
                CHECK(!strcmp(l->areas[i].name, m->areas[i].name) && l->areas[i].x0 == m->areas[i].x0 &&
                      l->areas[i].y1 == m->areas[i].y1);
            map_free(l);
        }
        Map *plain = map_new(3, 3, "p");
        CHECK_EQ(mapio_write(plain, path, err, sizeof err), 0);
        f = fopen(path, "r");
        if (f) { if (!fgets(first, sizeof first, f)) first[0] = 0; fclose(f); }
        CHECK(strcmp(first, "VTT 7\n") != 0);
        map_free(plain);
        f = fopen(path, "w");
        fputs("VTT 7\nsize 4 4\ntiles\n....\n....\n....\n....\n"
              "area 0 0 1 1 \"Hall\"\narea 2 2 3 3 \"hall\"\narea 0 0 1 1 \"C3\"\n", f);
        fclose(f);
        l = mapio_load(path, err, sizeof err);
        CHECK(l && l->nareas == 1);                     /* a second Hall and a square's name are dropped */
        map_free(l);
        sandbox_leave(&sb);
    }
    map_free(m);
}

static Link mklink(int num, int kind, int size, int ax, int ay, int bx, int by)
{
    Link l;
    memset(&l, 0, sizeof l);
    l.num = (uint8_t)num; l.kind = (uint8_t)kind; l.size = (uint8_t)size;
    l.x[0] = (int16_t)ax; l.y[0] = (int16_t)ay; l.x[1] = (int16_t)bx; l.y[1] = (int16_t)by;
    return l;
}

void test_links(void)
{
    char err[256];
    Map *m = map_new(12, 8, "links");
    map_fill_tiles(m, 0, 0, 4, 7, TILE_FLOOR);             /* two floors, void between */
    map_fill_tiles(m, 7, 0, 11, 7, TILE_FLOOR);

    CASE("where a link may go: on ground, ends apart, clear of other links");
    Link l = mklink(1, LINK_STAIRS, 1, 1, 1, 8, 1);
    CHECK(link_problem(m, &l) == NULL);
    Link bad = mklink(2, LINK_STAIRS, 1, 5, 1, 8, 3);
    CHECK(link_problem(m, &bad) != NULL);                   /* void */
    bad = mklink(2, LINK_PORTAL, 2, 1, 1, 2, 2);
    CHECK(link_problem(m, &bad) != NULL);                   /* ends overlap */
    bad = mklink(2, LINK_PORTAL, 2, 3, 7, 8, 6);
    CHECK(link_problem(m, &bad) != NULL);                   /* off the bottom */
    CHECK_EQ(link_put(m, &l), 0);
    bad = mklink(2, LINK_PORTAL, 2, 0, 0, 9, 5);
    CHECK(link_problem(m, &bad) != NULL);                   /* meets link 1 at B2 */
    Link same = l; same.kind = LINK_LADDER;
    CHECK(link_problem(m, &same) == NULL);                  /* not in its own way */
    CHECK_EQ(link_kind_from_name("Portal"), LINK_PORTAL);
    CHECK_EQ(link_kind_from_name("door"), -1);

    CASE("numbers: the lowest free one, kept when others go");
    Link two = mklink(link_free_num(m), LINK_PORTAL, 2, 0, 4, 8, 4);
    CHECK_EQ(two.num, 2);
    CHECK(link_problem(m, &two) == NULL);
    link_put(m, &two);
    link_remove(m, 1);
    CHECK_EQ(m->links[0].num, 2);
    CHECK_EQ(link_free_num(m), 1);
    link_put(m, &l);
    CHECK(m->links[0].num == 1 && m->links[1].num == 2);    /* number order */
    int end = -1;
    CHECK_EQ(link_at(m, 1, 5, &end), 1);                    /* inside the 2x2 */
    CHECK_EQ(end, 0);
    CHECK_EQ(link_at(m, 9, 5, &end), 1);
    CHECK_EQ(end, 1);
    CHECK_EQ(link_at(m, 2, 2, NULL), -1);

    CASE("through the undo log: made, changed, removed, and each undone");
    {
        Undo u;
        undo_init(&u);
        Link three = mklink(3, LINK_TRAPDOOR, 1, 3, 0, 10, 0);
        undo_begin(&u); CHECK_EQ(undo_set_link(&u, m, &three), 1); undo_end(&u);
        three.oneway = 1; three.secret = 1;
        undo_begin(&u); CHECK_EQ(undo_set_link(&u, m, &three), 1); undo_end(&u);
        undo_begin(&u); CHECK_EQ(undo_remove_link(&u, m, 1), 1); undo_end(&u);
        CHECK_EQ(m->nlinks, 2);
        undo_undo(&u, m);
        CHECK_EQ(link_find(m, 1), 0);
        undo_undo(&u, m);
        int i = link_find(m, 3);
        CHECK(i >= 0 && !m->links[i].oneway && !m->links[i].secret);
        undo_undo(&u, m);
        CHECK_EQ(link_find(m, 3), -1);
        undo_redo(&u, m); undo_redo(&u, m);
        i = link_find(m, 3);
        CHECK(i >= 0 && m->links[i].oneway && m->links[i].secret);
        undo_free(&u);
    }

    CASE("the file: version 8 with links, read back the same; a bad one dropped with a finding");
    {
        Sandbox sb = sandbox_enter("links");
        char path[600];
        snprintf(path, sizeof path, "%s/l.vtt", sb.dir);
        CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
        FILE *f = fopen(path, "r");
        char first[32] = "";
        if (f) { if (!fgets(first, sizeof first, f)) first[0] = 0; fclose(f); }
        CHECK_EQ(strcmp(first, "VTT 8\n"), 0);
        Map *back = mapio_load(path, err, sizeof err);
        CHECK(back && back->nlinks == m->nlinks);
        if (back) {
            for (int i = 0; i < back->nlinks; i++)
                CHECK(!memcmp(&back->links[i], &m->links[i], sizeof(Link)));
            map_free(back);
        }
        f = fopen(path, "w");
        fputs("VTT 8\nsize 4 1\n"
              "link 1 stairs 1 0 0 3 0\n"                 /* before the tiles: still read */
              "tiles\n.. .\n"
              "link 2 portal 1 0 0 3 0\n"                 /* on link 1's squares */
              "link 3 ladder 1 1 0 2 0\n"                 /* C1 is void */
              "link 1 ladder 1 1 0 3 0\n"                 /* a number used twice */
              "link 4 rope 1 1 0 3 0\n"                   /* not a kind */
              "link 5 stairs 1 1 0 3 0 sideways\n", f);  /* not a word it knows */
        fclose(f);
        back = mapio_load(path, err, sizeof err);
        CHECK(back && back->nlinks == 2 && back->links[0].num == 1 && back->links[1].num == 3);
        map_free(back);                                   /* link 3 over void stays; check says so */
        char *out = NULL;
        size_t n = 0;
        FILE *o = open_memstream(&out, &n);
        CHECK_EQ(maptools_check(o, path, 0), 1);
        fclose(o);
        CHECK(out && strstr(out, "W023") && strstr(out, "link 2 dropped: another link is already there") &&
              strstr(out, "W150") && strstr(out, "ladder 3 has an end on void"));
        free(out);
        sandbox_leave(&sb);
    }

    CASE("ground gone from under an end: the link stays through a save, and --check says W150");
    {
        Sandbox sb = sandbox_enter("linkvoid");
        Map *v = map_new(6, 1, "v");
        map_fill_tiles(v, 0, 0, 5, 0, TILE_FLOOR);
        Link st = mklink(1, LINK_STAIRS, 1, 0, 0, 5, 0);
        link_put(v, &st);
        map_set_tile(v, 5, 0, TILE_VOID);
        char path[600];
        snprintf(path, sizeof path, "%s/v.vtt", sb.dir);
        CHECK_EQ(mapio_write(v, path, err, sizeof err), 0);
        Map *back = mapio_load(path, err, sizeof err);
        CHECK(back && back->nlinks == 1);
        map_free(back);
        char *out = NULL;
        size_t n = 0;
        FILE *o = open_memstream(&out, &n);
        CHECK_EQ(maptools_check(o, path, 0), 1);
        fclose(o);
        CHECK(out && strstr(out, "W150") && strstr(out, "stairs 1 has an end on void"));
        free(out);
        map_free(v);
        sandbox_leave(&sb);
    }

    CASE("the fixture: one-way and secret survive a load and a save");
    {
        Map *fx = mapio_load("tests/fixtures/links.vtt", err, sizeof err);
        CHECK(fx && fx->nlinks == 2);
        if (fx) {
            int i1 = link_find(fx, 1), i2 = link_find(fx, 2);
            CHECK(i1 >= 0 && fx->links[i1].secret && fx->links[i1].kind == LINK_LADDER);
            CHECK(i2 >= 0 && fx->links[i2].oneway && fx->links[i2].size == 2);
            map_free(fx);
        }
    }

    CASE("the players' status line does not name a far end fog hides");
    {
        Map *fm = map_new(12, 2, "f");
        map_fill_tiles(fm, 0, 0, 11, 1, TILE_FLOOR);
        Link st = mklink(1, LINK_STAIRS, 1, 0, 0, 10, 0);
        link_put(fm, &st);
        str_lcpy(fm->fog_patches[0].name, "Up", sizeof fm->fog_patches[0].name);
        fm->fog_patches[0].reveal = FOG_REVEAL_MANUAL;
        fm->fog_on = 1;
        for (int x = 8; x < 12; x++) map_fog_set(fm, x, 0, 1);
        char st_line[64];
        link_status(fm, 0, 0, 0, st_line, sizeof st_line);
        CHECK(strstr(st_line, "stairs 1") && !strstr(st_line, "K1"));
        link_status(fm, 0, 0, 1, st_line, sizeof st_line);
        CHECK(strstr(st_line, "stairs 1 to K1") != NULL);
        map_free(fm);
    }

    CASE("resizing drops a link with an end cut off");
    {
        Map *c = map_new(12, 8, "c");
        map_fill_tiles(c, 0, 0, 11, 7, TILE_FLOOR);
        Link a1 = mklink(1, LINK_STAIRS, 1, 0, 0, 2, 0), a2 = mklink(2, LINK_STAIRS, 2, 0, 3, 9, 6);
        link_put(c, &a1); link_put(c, &a2);
        map_resize(c, 10, 7);
        CHECK(c->nlinks == 1 && c->links[0].num == 1);
        map_free(c);
    }

    CASE("a trip: everyone on the near end goes, in formation, or nobody does");
    {
        Map *t = map_new(12, 8, "t");
        map_fill_tiles(t, 0, 0, 4, 7, TILE_FLOOR);
        map_fill_tiles(t, 7, 0, 11, 7, TILE_FLOOR);
        Link p = mklink(1, LINK_PORTAL, 3, 0, 0, 8, 2);
        link_put(t, &p);
        Token a = { 0, 0, 1, TOKEN_PLAYER, "Aria" }, b = { 2, 2, 1, TOKEN_PLAYER, "Bram" };
        Token og = { 2, 1, 2, TOKEN_ENEMY, "Ogre" };    /* half on the portal: it goes too */
        Token far = { 3, 5, 1, TOKEN_PLAYER, "Far" };
        tokens_add(&t->tokens, a); tokens_add(&t->tokens, b);
        tokens_add(&t->tokens, og); tokens_add(&t->tokens, far);
        LinkTrip tr;
        /* The Ogre runs past the portal's edge; its far side is still ground. */
        CHECK_EQ(link_trip(t, 0, 0, 1, &tr), 3);
        CHECK(tr.dx == 8 && tr.dy == 2);
        CHECK(tr.idx[0] == 0 && tr.idx[1] == 1 && tr.idx[2] == 2);
        Token sit = { 10, 4, 1, TOKEN_ENEMY, "Guard" };  /* where Bram would land */
        tokens_add(&t->tokens, sit);
        CHECK_EQ(link_trip(t, 0, 0, 1, &tr), 0);
        CHECK(strstr(tr.why, "K5 is taken by Guard") != NULL);
        CHECK_EQ(link_trip(t, 0, 0, 0, &tr), 3);             /* ctrl-w: allowed */
        CHECK_EQ(link_trip(t, 0, 1, 1, &tr), 0);              /* nobody over there but the Guard... */
        t->tokens.n = 4;
        CHECK_EQ(link_trip(t, 0, 1, 1, &tr), 0);
        CHECK(strstr(tr.why, "nobody on this end of portal 1") != NULL);
        t->links[0].oneway = 1;
        CHECK_EQ(link_trip(t, 0, 1, 1, &tr), 0);
        CHECK(strstr(tr.why, "one-way") != NULL);
        t->links[0].oneway = 0;
        t->tokens.v[2].x = 0; t->tokens.v[2].y = 3;         /* the Ogre off the portal: stays */
        CHECK_EQ(link_trip(t, 0, 0, 1, &tr), 2);
        t->tokens.v[0].x = 2; t->tokens.v[0].size = 3;      /* Aria, a 3x3 over the portal's edge */
        t->tokens.v[1].x = 0; t->tokens.v[1].y = 2;
        CHECK_EQ(link_trip(t, 0, 0, 1, &tr), 0);             /* her far side runs off the map */
        CHECK(strstr(tr.why, "Aria would land off the map") != NULL);
        t->tokens.v[0].size = 2;
        t->tokens.v[0].x = 1; t->tokens.v[0].y = 6;
        Link low = mklink(2, LINK_LADDER, 1, 2, 7, 7, 1);   /* her left column lands on void */
        link_put(t, &low);
        CHECK_EQ(link_trip(t, 1, 0, 1, &tr), 0);
        CHECK(strstr(tr.why, "Aria would land on void at G1") != NULL);
        map_free(t);
    }

    CASE("rooms joined only by a link are reachable; a one-way link leads one way");
    {
        Map *r = map_new(12, 4, "r");
        map_fill_tiles(r, 0, 0, 3, 3, TILE_FLOOR);
        map_fill_tiles(r, 8, 0, 11, 3, TILE_FLOOR);
        map_rect_walls(r, 0, 0, 3, 3, EDGE_WALL);
        map_rect_walls(r, 8, 0, 11, 3, EDGE_WALL);
        Token hero = { 1, 1, 1, TOKEN_PLAYER, "Hero" };
        tokens_add(&r->tokens, hero);
        char *d = describe_text(r, 0, NULL);
        CHECK(d && strstr(d, "NOT REACHABLE"));
        free(d);
        Link st = mklink(1, LINK_STAIRS, 1, 2, 2, 9, 2);
        link_put(r, &st);
        d = describe_text(r, 0, NULL);
        CHECK(d && !strstr(d, "NOT REACHABLE") && strstr(d, "stairs 1     C3       to J3, room 2"));
        free(d);
        r->links[0].oneway = 1;
        r->links[0].x[0] = 9; r->links[0].x[1] = 2;          /* from the far room to the party's */
        d = describe_text(r, 0, NULL);
        CHECK(d && strstr(d, "NOT REACHABLE") && strstr(d, "comes from"));
        free(d);
        d = describe_text(r, 1, NULL);
        CHECK(d && json_valid(d) && strstr(d, "\"links\":[{\"num\":1,\"kind\":\"stairs\""));
        free(d);
        char *dump = tool_text(r, 0, 0, r->w - 1, r->h - 1, NULL);
        CHECK(dump && strstr(dump, "\nlinks\n  stairs 1     J3 -> C3  one-way\n"));
        free(dump);
        map_free(r);
    }

    CASE("stamps carry a link with both ends inside, turn it, and number it afresh");
    {
        Map *s = stamp_copy(m, 0, 1, 11, 1);              /* stairs 1 (B2-I2) alone */
        CHECK(s && s->nlinks == 1);
        Map *turned = stamp_turned(s, 1);
        CHECK(turned && turned->nlinks == 1 && turned->links[0].x[0] == 0 && turned->links[0].y[0] == 1 &&
              turned->links[0].x[1] == 0 && turned->links[0].y[1] == 8);
        Map *dst = map_new(12, 12, "dst");
        map_fill_tiles(dst, 0, 0, 11, 11, TILE_FLOOR);
        Link mine = mklink(1, LINK_LADDER, 1, 11, 11, 11, 9);
        link_put(dst, &mine);
        Undo u;
        undo_init(&u);
        CHECK_EQ(stamp_place(dst, &u, turned, 0, 0, err, sizeof err), 1);
        CHECK(dst->nlinks == 2 && link_find(dst, 2) >= 0);
        CHECK_EQ(stamp_place(dst, &u, turned, 0, 0, err, sizeof err), 0);   /* on stairs 2 now */
        CHECK(strstr(err, "a link would end on stairs 2") != NULL);
        undo_undo(&u, dst);
        CHECK_EQ(dst->nlinks, 1);
        undo_free(&u);
        map_free(dst); map_free(turned); map_free(s);
    }
    map_free(m);
}

/* The glyph drawn on a square's link mark: the first non-blank cell of its
 * interior's middle row. */
static uint32_t link_cell(Renderer *r, const App *a, int x, int y)
{
    int sx, sy;
    grid_tile_interior(&a->ed.view, x, y, &sx, &sy);
    sy += (ZOOM[a->ed.view.zoom].ih - 1) / 2;
    for (int i = 0; i < ZOOM[a->ed.view.zoom].iw; i++) {
        Cell *c = rnd_at(r, sx + i, sy);
        if (c && c->ch != ' ' && c->ch != 0) return c->ch;
    }
    return 0;
}

void test_link_keys(void)
{
    Sandbox sb = sandbox_enter("linkkeys");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[1200];
    snprintf(path, sizeof path, "%s/two.vtt", sb.dir);
    FILE *f = fopen(path, "w");
    /* Two floors of 5x4 with void between. */
    fputs("VTT 3\nname two\nsize 12 4\nzoom 1\ntiles\n"
          ".....  .....\n.....  .....\n.....  .....\n.....  .....\n", f);
    fclose(f);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f1 = { KEY_F1, 0, 0 }, f2 = { KEY_F2, 0, 0 };
    app_key(&a, f1);
    Map *m = a.map;

    CASE("g l on one end and g l on the other makes stairs 1; the first end is checked alone");
    a.ed.cx = 5; a.ed.cy = 0;
    press(&a, "gl");
    CHECK(strstr(a.status, "void") != NULL);
    CHECK_EQ(a.ed.link_on, 0);
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "gl");
    CHECK_EQ(a.ed.link_on, 1);
    CHECK(strstr(a.status, "stairs from B2") != NULL);
    a.ed.cx = 2; a.ed.cy = 1;
    press(&a, "gl");                                      /* the same floor is fine; on top is not */
    CHECK_EQ(m->nlinks, 1);
    press(&a, "u");
    CHECK_EQ(m->nlinks, 0);
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "gl");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "gl");
    CHECK(strstr(a.status, "the two ends overlap") != NULL);
    CHECK_EQ(a.ed.link_on, 1);                           /* still waiting for a good end */
    a.ed.cx = 8; a.ed.cy = 1;
    press(&a, "gl");
    CHECK(m->nlinks == 1 && m->links[0].num == 1 && m->links[0].kind == LINK_STAIRS &&
          m->links[0].x[1] == 8 && m->links[0].size == 1);
    CHECK(strstr(a.status, "stairs 1") && strstr(a.status, "B2 <-> I2"));

    CASE("esc lets go of a first end; the brush sets the size; :link picks the kind");
    a.ed.cx = 0; a.ed.cy = 2;
    press(&a, "gl\x1b");
    CHECK_EQ(a.ed.link_on, 0);
    CHECK(strstr(a.status, "link canceled") != NULL);
    press(&a, ":link portal\r");
    CHECK(strstr(a.status, "g l makes portal") != NULL);
    press(&a, "2b");
    a.ed.cx = 3; a.ed.cy = 2;
    press(&a, "gl");
    a.ed.cx = 10; a.ed.cy = 2;
    press(&a, "gl");
    CHECK(m->nlinks == 2 && m->links[1].kind == LINK_PORTAL && m->links[1].size == 2 &&
          m->links[1].num == 2);
    press(&a, "1b");

    CASE("the ends are drawn: the glyph on every square, the number on the first");
    rnd_begin(&r); app_draw(&a);
    CHECK_EQ(link_cell(&r, &a, 1, 1), 0x2261u);
    CHECK_EQ(link_cell(&r, &a, 11, 3), 0x25CEu);
    {
        int sx, sy;
        grid_tile_interior(&a.ed.view, 3, 2, &sx, &sy);
        Cell *c = rnd_at(&r, sx + 1, sy);
        CHECK(c && c->ch == '2');
    }

    CASE(":link N changes it, off removes it, u puts each back");
    press(&a, ":link 1 ladder oneway secret\r");
    CHECK(m->links[0].kind == LINK_LADDER && m->links[0].oneway && m->links[0].secret);
    press(&a, ":link 1 sideways\r");
    CHECK(strstr(a.status, "not something a link is") != NULL);
    press(&a, ":link 9 oneway\r");
    CHECK(strstr(a.status, "no link 9") != NULL);
    press(&a, ":link 1 off\r");                          /* the old word removes nothing */
    CHECK_EQ(m->nlinks, 2);
    CHECK(strstr(a.status, ":link 1 remove") != NULL);
    press(&a, ":link 1 remove\r");
    CHECK_EQ(m->nlinks, 1);
    press(&a, "u");
    CHECK(m->nlinks == 2 && m->links[0].secret);
    press(&a, "u");
    CHECK(m->nlinks == 2 && !m->links[0].secret && m->links[0].kind == LINK_STAIRS);
    press(&a, ":links\r");
    CHECK(strstr(a.status, "2 links: stairs 1 B2-I2, portal 2 D3-E4-K3-L4") != NULL);
    press(&a, ":link 2\r");
    CHECK(a.ed.cx == 3 && a.ed.cy == 2);
    press(&a, ":link 2\r");
    CHECK(a.ed.cx == 10 && a.ed.cy == 2);

    CASE("the status line says where a link leads");
    a.ed.cx = 1; a.ed.cy = 1;
    char st[256];
    ed_status(&a.ed, m, st, sizeof st);
    CHECK(strstr(st, "stairs 1 to I2") != NULL);

    CASE("play mode: g o sends the creature on an end through, the cursor with it, one u back");
    app_key(&a, f2);
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "ipAria\r");
    press(&a, "go");
    CHECK(m->tokens.v[0].x == 8 && m->tokens.v[0].y == 1);
    CHECK(a.ed.cx == 8 && a.ed.cy == 1);
    CHECK(strstr(a.status, "Aria takes stairs 1 to I2") != NULL);
    press(&a, "go");
    CHECK(m->tokens.v[0].x == 1);
    press(&a, "u");
    CHECK(m->tokens.v[0].x == 8);
    press(&a, "u");
    CHECK(m->tokens.v[0].x == 1);

    CASE("carried onto the stairs, g o puts it down and takes it through");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "\rl");                                     /* pick up, step east to C2 */
    press(&a, "h");                                       /* and back onto the stairs */
    CHECK_EQ(a.play.grabbed, 1);
    press(&a, "go");
    CHECK_EQ(a.play.grabbed, 0);
    CHECK(m->tokens.v[0].x == 8 && m->tokens.v[0].y == 1);

    CASE("a party on the portal goes in formation; one landing square taken refuses it all");
    a.ed.cx = 3; a.ed.cy = 2; press(&a, "ipBram\r");
    a.ed.cx = 4; a.ed.cy = 3; press(&a, "ipCora\r");
    a.ed.cx = 11; a.ed.cy = 3; press(&a, "ieGuard\r");  /* where Cora would land */
    a.ed.cx = 3; a.ed.cy = 2;
    press(&a, "\x1b");
    press(&a, "go");
    CHECK(strstr(a.status, "L4 is taken by Guard") != NULL);
    CHECK(m->tokens.v[1].x == 3);
    a.ed.cx = 11; a.ed.cy = 3; press(&a, "d");
    a.ed.cx = 3; a.ed.cy = 2;
    press(&a, "go");
    CHECK(strstr(a.status, "2 creatures take portal 2 to K3-L4") != NULL);
    int bram = -1, cora = -1;
    for (int i = 0; i < m->tokens.n; i++) {
        if (!strcmp(m->tokens.v[i].label, "Bram")) bram = i;
        if (!strcmp(m->tokens.v[i].label, "Cora")) cora = i;
    }
    CHECK(bram >= 0 && m->tokens.v[bram].x == 10 && m->tokens.v[bram].y == 2);
    CHECK(cora >= 0 && m->tokens.v[cora].x == 11 && m->tokens.v[cora].y == 3);

    CASE("one-way: refused from the far end; g l in play mode points to build mode");
    press(&a, ":link 2 oneway\r");
    a.ed.cx = 10; a.ed.cy = 2;
    press(&a, "go");
    CHECK(strstr(a.status, "one-way") != NULL);
    CHECK(m->tokens.v[bram].x == 10);
    press(&a, "gl");
    CHECK(strstr(a.status, "build mode") != NULL);
    a.ed.cx = 6; a.ed.cy = 0;
    press(&a, "go");
    CHECK(strstr(a.status, "no link here") != NULL);

    CASE("the channel: link from squares or rooms, links to read, link N to change or take off");
    app_key(&a, f1);                                            /* edits are build mode's */
    {
        char *ans = ctl_ask(&a, "link A1 B1\n");                 /* A1 is ground, B1 too: fine */
        CHECK(ans && !strncmp(ans, "ok", 2));
        free(ans);
        CHECK(link_find(m, 3) >= 0 && m->links[link_find(m, 3)].kind == LINK_STAIRS);
        ans = ctl_ask(&a, "link 3 portal oneway\nlinks\n");
        CHECK(ans && strstr(ans, "portal 3     A1 -> B1  one-way"));
        free(ans);
        ans = ctl_ask(&a, "link A1 H1\n");
        CHECK(ans && strstr(ans, "error: line 1: another link is already there"));
        free(ans);
        ans = ctl_ask(&a, "link C1 F1\n");
        CHECK(ans && strstr(ans, "error: line 1: an end is on void"));
        free(ans);
        ans = ctl_ask(&a, "link C1 H4 rope\n");
        CHECK(ans && strstr(ans, "rope: a link is stairs"));
        free(ans);
        ans = ctl_ask(&a, "link 3 remove\nlink 9 remove\n");
        CHECK(ans && strstr(ans, "there is no link 9"));
        CHECK(link_find(m, 3) >= 0);                              /* all or nothing */
        free(ans);
        ans = ctl_ask(&a, "links json\n");
        CHECK(ans && json_valid(strchr(ans, '\n') + 1) && strstr(ans, "\"num\":3,\"kind\":\"portal\""));
        free(ans);
        ans = ctl_ask(&a, "link 3 off\n");                         /* the old word removes nothing */
        CHECK(ans && strstr(ans, "link 3 remove") && link_find(m, 3) >= 0);
        free(ans);
        ans = ctl_ask(&a, "link 3 remove\n");
        CHECK(ans && !strncmp(ans, "ok", 2) && link_find(m, 3) < 0);
        free(ans);
        ans = ctl_ask(&a, "area Low H1:L4\nlink Low Low trapdoor\n");  /* both ends in one room */
        CHECK(ans && !strncmp(ans, "ok", 2));
        free(ans);
        int li = link_find(m, 3);
        CHECK(li >= 0 && m->links[li].kind == LINK_TRAPDOOR &&
              (m->links[li].x[0] != m->links[li].x[1] || m->links[li].y[0] != m->links[li].y[1]));
        ans = ctl_ask(&a, "link 3 remove\n");
        free(ans);
    }
    app_key(&a, f2);

    CASE("a secret link: never drawn in play mode, and the players' line does not name it");
    press(&a, ":link 1 secret\r");
    rnd_begin(&r); app_draw(&a);
    CHECK(link_cell(&r, &a, 1, 1) != 0x2261u);
    CHECK_EQ(link_cell(&r, &a, 11, 2), 0x25CEu);          /* the portal still is */
    a.ed.cx = 5; a.ed.cy = 0;
    char line[256];
    a.ed.cx = 1; a.ed.cy = 1;
    play_status(&a.play, m, &a.ed, 0, line, sizeof line);
    CHECK(strstr(line, "stairs") == NULL);
    play_status(&a.play, m, &a.ed, 1, line, sizeof line);
    CHECK(strstr(line, "secret stairs 1 to I2") != NULL);
    CHECK_EQ(app_view_differs(&a), 1);                    /* so the GM's line is not copied out */
    a.ed.cx = 6; a.ed.cy = 3;
    a.status[0] = 0;
    a.agent_ring.until_ms = 0;                            /* the channel's ring, from above */
    CHECK_EQ(app_view_differs(&a), 0);

    CASE("g o is the cursor's: a creature selected elsewhere stays put");
    a.ed.cx = 3; a.ed.cy = 3;
    press(&a, "ipDain\r");
    int dain = m->tokens.n - 1;
    a.ed.cx = 1; a.ed.cy = 1;                             /* move Dain onto the (secret) stairs */
    m->tokens.v[dain].x = 1; m->tokens.v[dain].y = 1;
    play_focus(&a.play, dain);
    a.ed.cx = 3; a.ed.cy = 0;
    press(&a, "go");
    CHECK(m->tokens.v[dain].x == 1 && a.ed.cx == 3 && a.ed.cy == 0);
    CHECK(strstr(a.status, "no link here") != NULL);

    CASE("a half-made link does not survive a trip to play mode and back");
    app_key(&a, f1);
    a.ed.cx = 0; a.ed.cy = 3;
    press(&a, "gl");
    CHECK_EQ(a.ed.link_on, 1);
    app_key(&a, f2);
    app_key(&a, f1);
    CHECK_EQ(a.ed.link_on, 0);

    CASE(":links says how many it could not fit");
    for (int k = 0, num = 10; k < 20; k++) {                /* every free left square to the right */
        Link x = mklink(num, LINK_LADDER, 1, k % 5, k / 5, 7 + k % 5, k / 5);
        if (!link_problem(m, &x)) { link_put(m, &x); num++; }
    }
    CHECK(m->nlinks >= 8);
    press(&a, ":links\r");
    CHECK(strstr(a.status, "more") != NULL && strlen(a.status) < sizeof a.status - 1);

    app_free(&a);
    rnd_free(&r);
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", sb.dir);
    sandbox_leave(&sb);
    if (system(cmd) != 0) { }
}

/* Three floors of 6x4 side by side with void between: Cellar -1 at A1,
 * Ground 0 at H1, Upper 1 at O1. */
static Map *floors_fixture(void)
{
    Map *m = map_new(20, 4, "floors");
    map_fill_tiles(m, 0, 0, 5, 3, TILE_FLOOR);
    map_fill_tiles(m, 7, 0, 12, 3, TILE_FLOOR);
    map_fill_tiles(m, 14, 0, 19, 3, TILE_FLOOR);
    map_area_set(m, "Upper", 14, 0, 19, 3);
    map_area_set(m, "Cellar", 0, 0, 5, 3);
    map_area_set(m, "Ground", 7, 0, 12, 3);
    map_area_set(m, "Hall", 8, 1, 10, 2);                  /* a room on the ground floor */
    m->areas[0].floor = 1; m->areas[0].level = 1;
    m->areas[1].floor = 1; m->areas[1].level = -1;
    m->areas[2].floor = 1; m->areas[2].level = 0;
    return m;
}

void test_floors(void)
{
    char err[256];
    Map *m = floors_fixture();

    CASE("floors in level order; stepping up and down; the whole map steps to the ends");
    int order[MAP_AREAS_MAX];
    CHECK_EQ(floor_order(m, order), 3);
    CHECK(order[0] == 1 && order[1] == 2 && order[2] == 0);
    CHECK_EQ(floor_step(m, 2, 1), 0);
    CHECK_EQ(floor_step(m, 2, -1), 1);
    CHECK_EQ(floor_step(m, 0, 1), -1);
    CHECK_EQ(floor_step(m, -1, 1), 1);
    CHECK_EQ(floor_step(m, -1, -1), 0);

    CASE("which floor holds a square; rooms are named by their own area, not the floor");
    CHECK_EQ(floor_at(m, 9, 1), 2);
    CHECK_EQ(floor_at(m, 6, 0), -1);
    CHECK_EQ(map_area_at(m, 9, 1), 3);
    CHECK_EQ(map_area_at(m, 7, 0), -1);                    /* the floor is not a room */
    CHECK(strcmp(floor_name(m, -1), "the whole map") == 0);

    CASE("a floor may not overlap another");
    map_area_set(m, "Wing", 12, 0, 15, 3);
    int wing = map_area_find(m, "Wing");
    CHECK(floor_problem(m, wing) != NULL);
    map_area_set(m, "Wing", 13, 0, 13, 3);
    CHECK(floor_problem(m, wing) == NULL);

    CASE("marked and unmarked through the undo log");
    {
        Undo u;
        undo_init(&u);
        undo_begin(&u); CHECK_EQ(undo_set_floor(&u, m, "Wing", 1, 5), 1); undo_end(&u);
        CHECK(m->areas[wing].floor && m->areas[wing].level == 5);
        undo_begin(&u); CHECK_EQ(undo_set_floor(&u, m, "Upper", 0, 0), 1); undo_end(&u);
        CHECK_EQ(m->areas[0].floor, 0);
        undo_undo(&u, m);
        CHECK(m->areas[0].floor && m->areas[0].level == 1);
        undo_undo(&u, m);
        CHECK_EQ(m->areas[wing].floor, 0);
        undo_redo(&u, m);
        CHECK(m->areas[wing].floor && m->areas[wing].level == 5);
        undo_begin(&u); undo_remove_area(&u, m, "Wing"); undo_end(&u);
        undo_undo(&u, m);                                  /* comes back a floor */
        wing = map_area_find(m, "Wing");
        CHECK(wing >= 0 && m->areas[wing].floor && m->areas[wing].level == 5);
        undo_free(&u);
        map_area_remove(m, "Wing");
    }

    CASE("the party's floor: stay, the most, the last, the lowest");
    {
        Token a = { 1, 1, 1, TOKEN_PLAYER, "A" }, b = { 8, 1, 1, TOKEN_PLAYER, "B" };
        Token c = { 15, 1, 1, TOKEN_PLAYER, "C" }, d = { 16, 1, 1, TOKEN_PLAYER, "D" };
        Token e = { 2, 2, 1, TOKEN_ENEMY, "E" };
        tokens_add(&m->tokens, a); tokens_add(&m->tokens, b);
        tokens_add(&m->tokens, c); tokens_add(&m->tokens, d); tokens_add(&m->tokens, e);
        CHECK_EQ(floor_pick(m, TOKEN_PLAYER, 1, -1), 1);    /* A is still on the Cellar: stay */
        CHECK_EQ(floor_pick(m, TOKEN_PLAYER, -1, -1), 0);   /* Upper has two */
        m->tokens.v[3].x = 9;                               /* D to the ground: 1 / 2 / 1 */
        CHECK_EQ(floor_pick(m, TOKEN_PLAYER, -1, -1), 2);
        m->tokens.v[1].x = 17;                              /* B up: 1 / 1 / 2... */
        m->tokens.v[3].x = 3;                               /* D down: 2 / 0 / 2, a tie */
        CHECK_EQ(floor_pick(m, TOKEN_PLAYER, -1, 0), 0);    /* the last moved on wins */
        CHECK_EQ(floor_pick(m, TOKEN_PLAYER, -1, -1), 1);   /* else the lowest */
        CHECK_EQ(floor_pick(m, TOKEN_PLAYER, -1, 2), 1);    /* a last that is not tied: ignored */
        CHECK_EQ(floor_pick(m, TOKEN_ENEMY, 0, -1), 1);     /* the enemy's side, the same rule */
        m->tokens.v[2].hidden = 1;                          /* C hidden upstairs: not counted */
        CHECK_EQ(floor_pick(m, TOKEN_PLAYER, -1, 0), 1);    /* Cellar 2, Upper 0 */
        m->tokens.v[2].hidden = 0;
        m->tokens.n = 0;
        CHECK_EQ(floor_pick(m, TOKEN_PLAYER, 0, 0), -1);
    }

    CASE("the file: version 9 with floors, read back; a bad floor line dropped with a finding");
    {
        Sandbox sb = sandbox_enter("floors");
        char path[600];
        snprintf(path, sizeof path, "%s/f.vtt", sb.dir);
        CHECK_EQ(mapio_write(m, path, err, sizeof err), 0);
        FILE *f = fopen(path, "r");
        char first[32] = "";
        if (f) { if (!fgets(first, sizeof first, f)) first[0] = 0; fclose(f); }
        CHECK_EQ(strcmp(first, "VTT 9\n"), 0);
        Map *back = mapio_load(path, err, sizeof err);
        CHECK(back != NULL);
        if (back) {
            int o2[MAP_AREAS_MAX];
            CHECK_EQ(floor_order(back, o2), 3);
            CHECK(!strcmp(back->areas[o2[0]].name, "Cellar") && back->areas[o2[0]].level == -1);
            map_free(back);
        }
        f = fopen(path, "w");
        fputs("VTT 9\nsize 6 1\ntiles\n......\n"
              "floor \"Low\" 0\n"                                /* before its area: still read */
              "area 0 0 2 0 \"Low\"\narea 2 0 5 0 \"High\"\narea 4 0 5 0 \"Top\"\n"
              "floor \"High\" 1\n"                               /* overlaps Low */
              "floor \"Nowhere\" 2\n"
              "floor \"Top\" 200\n"                              /* out of range */
              "floor \"Low\" 3\n", f);                          /* twice */
        fclose(f);
        back = mapio_load(path, err, sizeof err);
        CHECK(back && back->areas[0].floor && !back->areas[1].floor && !back->areas[2].floor &&
              back->areas[0].level == 0);
        map_free(back);
        char *out = NULL;
        size_t n = 0;
        FILE *o = open_memstream(&out, &n);
        CHECK_EQ(maptools_check(o, path, 0), 1);
        fclose(o);
        CHECK(out && strstr(out, "floor High dropped: it would overlap another floor") &&
              strstr(out, "floor Nowhere dropped: no area has that name") &&
              strstr(out, "floor Low dropped: it is already a floor") && strstr(out, "E014"));
        free(out);
        sandbox_leave(&sb);
    }

    CASE("--check finds overlapping floors made after the fact; --describe names each room's floor");
    {
        map_area_set(m, "Cellar", 0, 0, 8, 3);              /* grown into Ground */
        char *out = NULL;
        size_t n = 0;
        FILE *o = open_memstream(&out, &n);
        maptools_check_map(o, m, 0);
        fclose(o);
        CHECK(out && strstr(out, "W160"));
        free(out);
        map_area_set(m, "Cellar", 0, 0, 5, 3);
        char *d = describe_text(m, 0, NULL);
        CHECK(d && strstr(d, "floors Cellar (level -1) A1:F4, Ground (level 0) H1:M4, Upper (level 1) O1:T4"));
        CHECK(d && strstr(d, "on Ground"));
        free(d);
        d = describe_text(m, 1, NULL);
        CHECK(d && json_valid(d) && strstr(d, "\"floors\":[{\"name\":\"Cellar\",\"level\":-1"));
        free(d);
    }
    map_free(m);
}

void test_floor_view(void)
{
    Sandbox sb = sandbox_enter("floorview");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[1200];
    snprintf(path, sizeof path, "%s/tower.vtt", sb.dir);
    FILE *f = fopen(path, "w");
    /* Cellar A1:F4 (-1), Ground H1:M4 (0), Upper O1:T4 (1); stairs 1 from
     * Ground's I2 to Upper's P2, the same place in each box. */
    fputs("VTT 9\nname tower\nsize 20 4\nzoom 1\ntiles\n"
          "...... ...... ......\n...... ...... ......\n...... ...... ......\n...... ...... ......\n"
          "area 0 0 5 3 \"Cellar\"\narea 7 0 12 3 \"Ground\"\narea 14 0 19 3 \"Upper\"\n"
          "floor \"Cellar\" -1\nfloor \"Ground\" 0\nfloor \"Upper\" 1\n"
          "link 1 stairs 1 8 1 15 1\n", f);
    fclose(f);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f1 = { KEY_F1, 0, 0 }, f2 = { KEY_F2, 0, 0 };
    app_key(&a, f1);
    Map *m = a.map;

    CASE("a map opens on the whole of it; ] shows the lowest floor, the cursor on it");
    CHECK_EQ(app_floor_shown(&a), -1);
    press(&a, "]");
    CHECK_EQ(app_floor_shown(&a), map_area_find(m, "Cellar"));
    CHECK(a.ed.cx >= 0 && a.ed.cx <= 5);
    CHECK(strstr(a.status, "Cellar, level -1") != NULL);
    press(&a, "[");
    CHECK(strstr(a.status, "Cellar is the bottom floor") != NULL);

    CASE("the cursor stays on the floor: hjkl, counts, $ and G stop at its edge");
    press(&a, "20l");
    CHECK_EQ(a.ed.cx, 5);
    press(&a, "0");
    CHECK_EQ(a.ed.cx, 0);
    press(&a, "$G");
    CHECK(a.ed.cx == 5 && a.ed.cy == 3);
    press(&a, "gg");
    CHECK_EQ(a.ed.cy, 0);

    CASE("] keeps the cursor's place in the box, so stairs line up floor to floor");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "]");
    CHECK(app_floor_shown(&a) == map_area_find(m, "Ground") && a.ed.cx == 8 && a.ed.cy == 1);
    char st[256];
    ed_status(&a.ed, m, st, sizeof st);
    CHECK(strstr(st, "I2  on Ground") && strstr(st, "stairs 1 to Upper P2"));

    CASE("only the floor is drawn, and a screen cell off it names no square");
    rnd_begin(&r); app_draw(&a);
    {
        int sx, sy, tx, ty;
        grid_tile_interior(&a.ed.view, 8, 1, &sx, &sy);
        CHECK(grid_screen_to_tile(&a.ed.view, m, sx, sy, &tx, &ty) && tx == 8);
        CHECK_EQ(link_cell(&r, &a, 8, 1), 0x2261u);
        CHECK_EQ(link_cell(&r, &a, 15, 1), 0);               /* Upper's end is not on screen */
        grid_tile_interior(&a.ed.view, 15, 1, &sx, &sy);
        CHECK_EQ(grid_screen_to_tile(&a.ed.view, m, sx, sy, &tx, &ty), 0);
    }

    CASE("a jump off the floor takes the view to the floor it lands on, or the whole map");
    press(&a, ":p3\r");
    CHECK(app_floor_shown(&a) == map_area_find(m, "Upper") && a.ed.cx == 15);
    press(&a, ":g2\r");                                        /* G2 is between floors */
    CHECK_EQ(app_floor_shown(&a), -1);
    press(&a, ":floor Ground\r");
    CHECK_EQ(app_floor_shown(&a), map_area_find(m, "Ground"));
    press(&a, ":floor all\r");
    CHECK_EQ(app_floor_shown(&a), -1);
    press(&a, ":floor Nowhere\r");
    CHECK(strstr(a.status, "no area called Nowhere") != NULL);

    CASE("g o through the stairs takes the view upstairs with the creature");
    press(&a, ":floor Ground\r");
    app_key(&a, f2);
    a.ed.cx = 8; a.ed.cy = 1;
    press(&a, "ipAria\r");
    press(&a, "go");
    CHECK(m->tokens.v[0].x == 15 && app_floor_shown(&a) == map_area_find(m, "Upper"));
    play_focus(&a.play, -1);                                 /* the bare square's line */
    a.ed.cx = 16;
    play_status(&a.play, m, &a.ed, 1, st, sizeof st);
    CHECK(strstr(st, "Q2 on Upper") != NULL);
    play_status(&a.play, m, &a.ed, 0, st, sizeof st);
    CHECK(strstr(st, "Upper") == NULL);                      /* the players' line: no area names */

    CASE("a carried creature stops at the floor's edge");
    a.ed.cx = 15; a.ed.cy = 1;
    press(&a, "\r");
    press(&a, "hhhh");
    CHECK_EQ(m->tokens.v[0].x, 14);
    CHECK(strstr(a.status, "edge of the floor") != NULL);
    press(&a, "\r");

    CASE(":floor marks and unmarks, refusing an overlap; u puts it back");
    press(&a, ":area Wing\r");                                 /* play mode: jumps, so it must exist */
    app_key(&a, f1);
    press(&a, ":floor all\r");
    a.ed.cx = 12; a.ed.cy = 0;
    press(&a, "v");
    a.ed.cx = 14; a.ed.cy = 3;
    press(&a, ":area Wing\r");
    press(&a, ":floor Wing 2\r");
    CHECK(strstr(a.status, "cannot be a floor: it would overlap another floor") != NULL);
    press(&a, ":floor Upper off\r");
    CHECK_EQ(m->areas[map_area_find(m, "Upper")].floor, 0);
    press(&a, "u");
    CHECK_EQ(m->areas[map_area_find(m, "Upper")].floor, 1);
    press(&a, ":floors\r");
    CHECK(strstr(a.status, "floors: Upper 1, Ground 0, Cellar -1") != NULL);

    CASE("review fixes: wall mode's far edge, the brush, [ ] with a box, re-boxing a floor");
    press(&a, ":floor Ground\r");
    press(&a, "w");
    press(&a, "20l");
    CHECK(app_floor_shown(&a) == map_area_find(m, "Ground") && a.ed.wx == 13);
    press(&a, "\x1b");
    press(&a, "\x1b");
    CHECK(a.ed.mode == ED_NORMAL);
    a.ed.cx = 12; a.ed.cy = 0;
    press(&a, "3b");
    press(&a, "x");                                          /* over the gap and into Upper */
    CHECK(map_tile(m, 12, 0) == TILE_VOID && map_tile(m, 14, 0) == TILE_FLOOR);
    press(&a, "u1b");
    a.ed.cx = 8; a.ed.cy = 1;
    press(&a, "v");
    press(&a, "]");
    CHECK(strstr(a.status, "esc first") != NULL && app_floor_shown(&a) == map_area_find(m, "Ground"));
    press(&a, "l");
    press(&a, ":area Ground\r");                            /* the v box: I2:J2, fine */
    CHECK(m->areas[map_area_find(m, "Ground")].x1 == 9);
    press(&a, "u");
    press(&a, ":floor all\r");
    a.ed.cx = 5; a.ed.cy = 0;
    press(&a, "v");
    a.ed.cx = 14; a.ed.cy = 3;
    press(&a, ":area Ground\r");                            /* over Cellar and Upper */
    CHECK(strstr(a.status, "Ground is a floor, and it would overlap another floor") != NULL);
    CHECK(m->areas[map_area_find(m, "Ground")].x0 == 7);
    press(&a, ":floor Ground  0 \r");                       /* stray spaces */
    CHECK(strstr(a.status, "no area called") == NULL);

    CASE("a shown floor that stops being one: back to the whole map");
    press(&a, ":floor Upper\r");
    press(&a, ":floor Upper off\r");
    CHECK_EQ(app_floor_shown(&a), -1);
    CHECK_EQ(a.ed.view.bounded, 0);

    CASE("the channel: floor NAME LEVEL and off, floors, and status names the floor shown");
    {
        press(&a, "u");                                     /* Upper a floor again */
        press(&a, ":floor Ground\r");
        char *ans = ctl_ask(&a, "floor Wing 2\n");
        CHECK(ans && strstr(ans, "Wing cannot be a floor: it would overlap another floor"));
        free(ans);
        ans = ctl_ask(&a, "floor Cellar off\nfloor Cellar -2\nfloors\nstatus\n");
        CHECK(ans && !strncmp(ans, "ok", 2) && strstr(ans, "Cellar           level  -2  A1:F4") &&
              strstr(ans, "Ground           level   0  H1:M4  (the GM is looking at it)") &&
              strstr(ans, "floor Ground\n"));
        free(ans);
        ans = ctl_ask(&a, "floor Cellar 500\n");
        CHECK(ans && strstr(ans, "a level is -99 to 99"));
        free(ans);
        ans = ctl_ask(&a, "marked json\n");
        CHECK(ans && strstr(ans, "\"floor\":\"Ground\""));
        free(ans);
    }

    app_free(&a);
    rnd_free(&r);
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", sb.dir);
    sandbox_leave(&sb);
    if (system(cmd) != 0) { }
}

void test_floor_players(void)
{
    Sandbox sb = sandbox_enter("floorplayers");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[1200];
    snprintf(path, sizeof path, "%s/tower.vtt", sb.dir);
    FILE *f = fopen(path, "w");
    /* The party: Aria and Bram on Ground, Cora in the Cellar. A Ghoul in the
     * Cellar, a Wraith upstairs. */
    fputs("VTT 9\nname tower\nsize 20 4\nzoom 1\nruleset daggerheart\ntiles\n"
          "...... ...... ......\n...... ...... ......\n...... ...... ......\n...... ...... ......\n"
          "token player 8 1 1 \"Aria\"\ntoken player 9 2 1 \"Bram\"\ntoken player 1 1 1 \"Cora\"\n"
          "token enemy 3 2 1 \"Ghoul\"\ntoken enemy 16 1 1 \"Wraith\"\n"
          "area 0 0 5 3 \"Cellar\"\narea 7 0 12 3 \"Ground\"\narea 14 0 19 3 \"Upper\"\n"
          "floor \"Cellar\" -1\nfloor \"Ground\" 0\nfloor \"Upper\" 1\n", f);
    fclose(f);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    Map *m = a.map;
    int cellar = map_area_find(m, "Cellar"), ground = map_area_find(m, "Ground"), upper = map_area_find(m, "Upper");

    CASE("the players see the party's floor: most of them are on Ground");
    CHECK_EQ(app_players_floor(&a), ground);
    press(&a, ":floor Upper\r");
    CHECK_EQ(app_players_split(&a), 1);
    CHECK_EQ(app_view_differs(&a), 1);

    CASE("their frame is drawn through their own camera: Ground's creatures, not the GM's floor");
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    {
        int sx, sy, tx, ty;
        grid_tile_interior(&a.pview, 8, 1, &sx, &sy);
        Cell *c = rnd_at(&r, sx + 1, sy);
        CHECK(c && c->ch != ' ');                          /* Aria is drawn */
        CHECK(grid_screen_to_tile(&a.pview, m, sx, sy, &tx, &ty) && tx == 8 && ty == 1);
        CASE("a tap maps through their camera, and the GM hears where");
        a.npings = 0;
        CHECK_EQ(app_ping_cell(&a, 3, sx, sy), 1);
        CHECK(a.npings == 1 && a.pings[0].x0 == 8 && a.pings[0].y0 == 1);
        CHECK(strstr(a.status, "ping on Ground at I2") != NULL);
    }
    CHECK_EQ(app_floor_shown(&a), upper);                  /* the GM's view is untouched */

    CASE("s t waits while a creature is carried");
    a.ed.cx = 8; a.ed.cy = 1;
    press(&a, "\r");
    CHECK_EQ(a.play.grabbed, 1);
    press(&a, "st");
    CHECK(a.play.grabbed == 1 && strstr(a.status, "put it down first"));
    press(&a, "\x1b");
    play_focus(&a.play, -1);

    CASE("a pin holds the players on a floor, and stays when the party moves");
    press(&a, ":player floor Cellar\r");
    CHECK_EQ(app_players_floor(&a), cellar);
    press(&a, ":player floor auto\r");
    CHECK_EQ(app_players_floor(&a), ground);                /* picked afresh: the most */
    press(&a, ":player floor Nowhere\r");
    CHECK(strstr(a.status, "no floor called Nowhere") != NULL);

    CASE("initiative: an enemy's turn moves the GM, a player's moves both and lifts a pin");
    a.ed.cx = 3; a.ed.cy = 2; press(&a, "si20\r");         /* the Ghoul, in the Cellar */
    a.ed.cx = 1; a.ed.cy = 1; press(&a, "si10\r");         /* Cora, in the Cellar */
    press(&a, ":floor Upper\r");
    press(&a, ":player floor Upper\r");
    play_focus(&a.play, -1);
    press(&a, "a");                                        /* the Ghoul's turn */
    CHECK_EQ(app_floor_shown(&a), cellar);
    CHECK_EQ(app_players_floor(&a), upper);                /* the pin holds on an enemy's turn */
    press(&a, "a");                                        /* Cora's turn */
    CHECK_EQ(app_floor_shown(&a), cellar);
    CHECK_EQ(app_players_floor(&a), cellar);
    CHECK_EQ(a.ppin[0], '\0');
    press(&a, ":turns end\r");

    CASE("the spotlight: to the GM, the GM's view goes where the enemies are; the players stay");
    press(&a, ":floor Ground\r");
    CHECK_EQ(m->spotlight, SPOTLIGHT_PLAYERS);
    int before = app_players_floor(&a);
    press(&a, "a");
    CHECK_EQ(m->spotlight, SPOTLIGHT_GM);
    /* One enemy in the Cellar, one upstairs, none on Ground: a tie, broken
     * by the lowest. */
    CHECK_EQ(app_floor_shown(&a), cellar);
    CHECK_EQ(app_players_floor(&a), before);

    CASE("to the players: both go where the players are, staying put when they still can");
    press(&a, "a");
    CHECK_EQ(m->spotlight, SPOTLIGHT_PLAYERS);
    CHECK_EQ(app_floor_shown(&a), app_players_floor(&a));

    CASE("the last side to move breaks a tie");
    {
        int ai = -1;
        for (int i = 0; i < m->tokens.n; i++) if (!strcmp(m->tokens.v[i].label, "Aria")) ai = i;
        m->tokens.v[ai].x = 15; m->tokens.v[ai].y = 0;      /* one on each floor: a tie */
        str_lcpy(a.pfloor, "", sizeof a.pfloor);            /* nowhere to stay */
        a.side_floor[0][0] = '\0';
        CHECK_EQ(app_players_floor(&a), cellar);            /* no last move: the lowest */
        a.pfloor[0] = '\0';
        str_lcpy(a.side_floor[0], "Upper", sizeof a.side_floor[0]);
        CHECK_EQ(app_players_floor(&a), upper);             /* a player last moved upstairs */
    }

    app_free(&a);
    rnd_free(&r);
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", sb.dir);
    sandbox_leave(&sb);
    if (system(cmd) != 0) { }
}

void test_floor_big_camera(void)
{
    Sandbox sb = sandbox_enter("floorcam");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[1200];
    snprintf(path, sizeof path, "%s/big.vtt", sb.dir);
    FILE *f = fopen(path, "w");
    fputs("VTT 9\nname big\nsize 40 30\nzoom 1\ntiles\n", f);
    for (int y = 0; y < 30; y++) fputs(y < 12 || (y >= 15 && y < 27) ? "........................................\n"
                                                                        : "                                        \n", f);
    fputs("token player 2 3 1 \"Aria\"\ntoken player 3 4 1 \"Bram\"\n"
          "area 0 0 39 11 \"Ground\"\narea 0 15 39 26 \"Upper\"\n"
          "floor \"Ground\" 0\nfloor \"Upper\" 1\n", f);
    fclose(f);
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 60, 16);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);

    CASE("the players' camera stays on the party, not on the GM's cursor on another floor");
    press(&a, ":floor Upper\r");
    a.ed.cx = 38; a.ed.cy = 25;
    rnd_begin(&r); app_draw(&a);
    for (int i = 0; i < 2; i++) {
        rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
        int x0, y0, x1, y1;
        grid_visible_tiles(&a.pview, a.map, &x0, &y0, &x1, &y1);
        CHECK(x0 <= 2 && x1 >= 3 && y0 <= 3 && y1 >= 4);
    }
    press(&a, "+");
    rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
    {
        int x0, y0, x1, y1;
        grid_visible_tiles(&a.pview, a.map, &x0, &y0, &x1, &y1);
        CHECK(x0 <= 2 && x1 >= 3);
    }
    app_free(&a);
    rnd_free(&r);
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", sb.dir);
    sandbox_leave(&sb);
    if (system(cmd) != 0) { }
}

/* ------------------------------------------------------ links to another map */

/* A map file of w x h floor, with the named area given (x0..y1), at dir/name.vtt. */
static void write_floor_map(const char *dir, const char *name, int w, int h, const char *area,
                            int x0, int y0, int x1, int y1)
{
    char path[700];
    snprintf(path, sizeof path, "%s/%s.vtt", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "VTT 7\nname %s\nsize %d %d\ntiles\n", name, w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) fputc('.', f);
        fputc('\n', f);
    }
    if (area) fprintf(f, "area %d %d %d %d \"%s\"\n", x0, y0, x1, y1, area);
    fclose(f);
}

static void count_e014(void *ctx, int line, int col, const char *code,
                       const char *slug, const char *msg)
{
    (void)line; (void)col; (void)slug; (void)msg;
    if (!strcmp(code, "E014")) ++*(int *)ctx;
}

void test_map_links(void)
{
    Sandbox sb = sandbox_enter("maplinks");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char err[256], path[700];

    CASE("a link to another map: one end, one way; its file line round-trips at version 12");
    Map *m = map_new(10, 8, "town");
    map_fill_tiles(m, 0, 0, 9, 7, TILE_FLOOR);
    Link l;
    memset(&l, 0, sizeof l);
    l.num = 4; l.kind = LINK_STAIRS; l.size = 2;
    l.x[0] = l.x[1] = 2; l.y[0] = l.y[1] = 2;
    str_lcpy(l.to_map, "crypt", sizeof l.to_map);
    str_lcpy(l.to_place, "Lower hall", sizeof l.to_place);
    CHECK(link_problem(m, &l) == NULL);                    /* its two ends are one: no overlap */
    CHECK_EQ(link_ends(&l), 1);
    Undo u;
    undo_init(&u);
    undo_begin(&u);
    CHECK(undo_set_link(&u, m, &l));
    undo_end(&u);
    snprintf(path, sizeof path, "%s/town.vtt", sb.dir);
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    char *text = slurp(path);
    CHECK(text && !strncmp(text, "VTT 12\n", 7));
    CHECK(text && strstr(text, "link 4 stairs 2 2 2 to \"crypt\" \"Lower hall\"\n") != NULL);
    free(text);
    Map *back = mapio_load(path, err, sizeof err);
    CHECK(back && back->nlinks == 1);
    if (back) {
        CHECK(!strcmp(back->links[0].to_map, "crypt") && !strcmp(back->links[0].to_place, "Lower hall"));
        CHECK(back->links[0].x[1] == 2 && back->links[0].y[1] == 2);
        map_free(back);
    }

    CASE("undo keeps a link to another map whole: its names ride in the slot too");
    undo_undo(&u, m);
    CHECK_EQ(m->nlinks, 0);
    undo_redo(&u, m);
    CHECK(m->nlinks == 1 && !strcmp(m->links[0].to_map, "crypt") && !strcmp(m->links[0].to_place, "Lower hall"));
    undo_free(&u);
    map_free(m);

    CASE("the loader drops a line that does not read, and a bad map name");
    {
        const char *bad =
            "VTT 12\nname b\nsize 4 3\ntiles\n....\n....\n....\n"
            "link 1 stairs 1 0 0 to \"crypt\"\n"
            "link 2 stairs 1 1 1 to \"../x\" \"Hall\"\n"
            "link 3 stairs 1 2 2 to \"crypt\" \"Hall\" oneway\n"
            "link 4 stairs 1 3 2 to \"crypt\" \"Hall\" secret\n";
        snprintf(path, sizeof path, "%s/bad.vtt", sb.dir);
        FILE *f = fopen(path, "w");
        if (f) { fputs(bad, f); fclose(f); }
        int dropped = 0;
        Map *b = mapio_load_diag(path, err, sizeof err, count_e014, &dropped);
        CHECK(b && b->nlinks == 1 && b->links[0].num == 4 && b->links[0].secret);
        CHECK_EQ(dropped, 3);                                 /* each one said */
        map_free(b);
    }

    CASE("landing: the formation kept, inside the area, nearest its middle; no room refuses");
    Map *d = map_new(12, 10, "crypt");
    map_fill_tiles(d, 0, 0, 11, 9, TILE_FLOOR);
    map_area_set(d, "Hall", 2, 2, 8, 6);
    Token party[2];
    memset(party, 0, sizeof party);
    party[0].x = 0; party[0].y = 0; party[0].size = 1;
    party[1].x = 1; party[1].y = 1; party[1].size = 2;       /* a 2x2 beside it: the box is 3x3 */
    int ax, ay;
    char why[200];
    CHECK(link_land(d, "Hall", party, 2, &ax, &ay, why, sizeof why));
    CHECK(ax == 4 && ay == 3);                               /* box 4..6 x 3..5, centered on 5,4 */
    tokens_add(&d->tokens, (Token){ .x = 5, .y = 4, .size = 1, .kind = TOKEN_ENEMY, .label = "Rat" });
    CHECK(link_land(d, "Hall", party, 2, &ax, &ay, why, sizeof why));
    for (int i = 0; i < 2; i++)
        CHECK(!token_meets(&d->tokens.v[0], ax + party[i].x, ay + party[i].y, party[i].size, party[i].size));
    CHECK(ax >= 2 && ay >= 2 && ax + 2 <= 8 && ay + 2 <= 6);   /* still inside */
    map_area_set(d, "Closet", 10, 8, 11, 9);
    CHECK(!link_land(d, "Closet", party, 2, &ax, &ay, why, sizeof why));
    CHECK(strstr(why, "no room in crypt's Closet for 2 creatures") != NULL);
    CHECK(link_land(d, "K9", party, 1, &ax, &ay, why, sizeof why) && ax == 10 && ay == 8);
    CHECK(!link_land(d, "Nowhere", party, 1, &ax, &ay, why, sizeof why) && strstr(why, "no area or square called Nowhere"));
    map_free(d);

    CASE("making one: :link to checks the file beside this map and the place in it");
    write_floor_map(sb.dir, "crypt", 12, 10, "Entrance", 2, 2, 5, 5);
    write_floor_map(sb.dir, "town", 12, 8, "Gate", 0, 0, 2, 2);
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */
    snprintf(path, sizeof path, "%s/town.vtt", sb.dir);
    CHECK_EQ(app_open_map(&a, path), 0);
    Map *t = a.map;
    a.ed.cx = 6; a.ed.cy = 3;
    press(&a, ":link to nowhere Entrance\r");
    CHECK(strstr(a.status, "no map nowhere beside this one") != NULL);
    press(&a, ":link to crypt Attic\r");
    CHECK(strstr(a.status, "crypt has no area or square called Attic") != NULL);
    press(&a, ":link to ../crypt Entrance\r");
    CHECK(strstr(a.status, "a map's name is its file's") != NULL);
    press(&a, ":link to crypt Entrance\r");
    CHECK_EQ(t->nlinks, 1);
    CHECK(!strcmp(t->links[0].to_map, "crypt") && t->links[0].x[0] == 6);
    CHECK(strstr(a.status, "-> crypt, Entrance") != NULL);
    press(&a, ":link 1 reverse\r");
    CHECK(strstr(a.status, "leads to another map") != NULL);
    int depth0 = a.undo.depth;
    press(&a, ":link 1 oneway\r");
    CHECK(strstr(a.status, "leads to another map") != NULL);
    CHECK_EQ(a.undo.depth, depth0);
    char ls[160];
    link_status(t, 6, 3, 0, ls, sizeof ls);
    CHECK(!strcmp(ls, "  stairs 1"));                      /* the players hear no file's name */
    link_status(t, 6, 3, 1, ls, sizeof ls);
    CHECK(!strcmp(ls, "  stairs 1 to crypt, Entrance"));

    CASE("drawn with an arrow after its number");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf f;
    bb_init(&f, 65536);
    rnd_dump(&r, &f);
    bb_putc(&f, '\0');
    CHECK(strstr((char *)f.data, "1\xe2\x86\x92") != NULL);
    bb_free(&f);

    CASE("g o: nobody on it says so; the party goes, this map is saved, the other opens with them");
    press(&a, ":w\r");
    app_key(&a, (Key){ KEY_F2, 0, 0 });
    press(&a, ":G4\rgo");
    CHECK(strstr(a.status, "nobody on stairs 1") != NULL);
    a.ed.cx = 6; a.ed.cy = 3; press(&a, "ipAria\r");
    a.ed.cx = 0; a.ed.cy = 7; press(&a, "ieBram\r");         /* stays behind */
    t->tokens.v[0].turn = TURN_IN | TURN_ACTING;
    t->tokens.v[0].init = 15;
    token_add_status(&t->tokens.v[0], 0, "blessed");
    press(&a, ":serve\r");
    CHECK(net_active(&a.net));
    press(&a, ":handout say A draft blows up the stairs\r");
    a.ed.cx = 6; a.ed.cy = 3;
    press(&a, "go");
    CHECK(a.map && !strcmp(a.map->name, "crypt"));
    CHECK_EQ(a.screen, SCREEN_PLAY);
    CHECK_EQ(a.map->tokens.n, 1);
    const Token *arr = &a.map->tokens.v[0];
    CHECK(!strcmp(arr->label, "Aria") && arr->nstatus == 1 && arr->turn == 0);
    CHECK(arr->x >= 2 && arr->x <= 5 && arr->y >= 2 && arr->y <= 5);
    CHECK(a.ed.cx == arr->x && a.ed.cy == arr->y);
    CHECK(net_active(&a.net));                              /* the phones followed */
    CHECK(!a.handout_up);
    CHECK(!a.map->modified);                                /* written where they arrived too */
    {
        char cp[700];
        snprintf(cp, sizeof cp, "%s/crypt.vtt", sb.dir);
        Map *cm0 = mapio_load(cp, err, sizeof err);
        CHECK(cm0 && cm0->tokens.n == 1 && !strcmp(cm0->tokens.v[0].label, "Aria"));
        map_free(cm0);
    }
    CHECK(strstr(a.status, "Aria took stairs 1 from town to crypt, Entrance") != NULL);
    CHECK(strstr(a.status, "no way back yet: in build mode :link to town PLACE") != NULL);
    CHECK_EQ(a.status_gm, 1);
    Map *saved = mapio_load(path, err, sizeof err);
    CHECK(saved && saved->tokens.n == 1 && !strcmp(saved->tokens.v[0].label, "Bram"));
    map_free(saved);

    CASE("the way back, made in the other map, says nothing of a missing way back");
    app_key(&a, (Key){ KEY_F1, 0, 0 });
    a.ed.cx = 8; a.ed.cy = 8;
    press(&a, ":link to town Gate\r");
    CHECK_EQ(a.map->nlinks, 1);
    press(&a, ":w\r");
    app_key(&a, (Key){ KEY_F2, 0, 0 });
    Token *aria = &a.map->tokens.v[0];
    aria->x = 8; aria->y = 8;
    map_touch(a.map);
    a.ed.cx = 8; a.ed.cy = 8;
    press(&a, "go");
    CHECK(!strcmp(a.map->name, "town"));
    CHECK(strstr(a.status, "Aria took stairs 1 from crypt to town, Gate") != NULL);
    CHECK(strstr(a.status, "no way back") == NULL);
    CHECK_EQ(a.map->tokens.n, 2);

    CASE("refused, nothing moving: no room there, a crash's autosave there, a map never saved");
    press(&a, ":w\r");
    a.ed.cx = 6; a.ed.cy = 3;
    int ai = -1;
    for (int i = 0; i < a.map->tokens.n; i++) if (!strcmp(a.map->tokens.v[i].label, "Aria")) ai = i;
    a.map->tokens.v[ai].x = 6; a.map->tokens.v[ai].y = 3;
    char cpath[700], apath[720];
    snprintf(cpath, sizeof cpath, "%s/crypt.vtt", sb.dir);
    Map *cm = mapio_load(cpath, err, sizeof err);
    snprintf(apath, sizeof apath, "%s.autosave", cpath);
    CHECK(cm && mapio_save(cm, apath, err, sizeof err) == 0);
    map_free(cm);
    struct timespec later[2] = { { 0, UTIME_NOW }, { time(NULL) + 60, 0 } };
    utimensat(AT_FDCWD, apath, later, 0);
    press(&a, "go");
    CHECK(!strcmp(a.map->name, "town"));
    CHECK(strstr(a.status, "unsaved work from a crash") != NULL);
    unlink(apath);
    {
        /* This map's own file written by someone else: the trip would save
         * over it, so nobody goes until the change is looked at. */
        char tpath[700];
        str_lcpy(tpath, a.map->path, sizeof tpath);
        Map *other = mapio_load(tpath, err, sizeof err);
        CHECK(other != NULL);
        map_set_tile(other, 0, 0, TILE_WATER);
        CHECK(mapio_write(other, tpath, err, sizeof err) == 0);
        map_free(other);
        press(&a, "go");
        CHECK(!strcmp(a.map->name, "town"));
        CHECK(strstr(a.status, "the file changed on disk") != NULL && strstr(a.status, "nobody went") != NULL);
        Map *still = mapio_load(tpath, err, sizeof err);
        CHECK(still && map_tile(still, 0, 0) == TILE_WATER);   /* theirs, not written over */
        map_free(still);
        press(&a, ":w!\r");                                /* the GM's over it: the trip is free again */
    }
    Map *full = mapio_load(cpath, err, sizeof err);
    for (int y = 2; y <= 5; y++)
        for (int x = 2; x <= 5; x++) {
            Token rat;
            memset(&rat, 0, sizeof rat);
            rat.x = (int16_t)x; rat.y = (int16_t)y; rat.size = 1; rat.kind = TOKEN_ENEMY;
            tokens_add(&full->tokens, rat);
        }
    CHECK(mapio_save(full, cpath, err, sizeof err) == 0);
    map_free(full);
    press(&a, "go");
    CHECK(!strcmp(a.map->name, "town"));
    CHECK(strstr(a.status, "no room in crypt's Entrance") != NULL);
    CHECK_EQ(a.status_gm, 1);                               /* an area's name is the GM's */

    CASE("a save that fails puts the party back, and leaves nothing to redo");
    write_floor_map(sb.dir, "crypt", 12, 10, "Entrance", 2, 2, 5, 5);   /* room again */
    if (geteuid() != 0) {                                   /* root writes anyway */
        int before_n = a.map->tokens.n;
        chmod(sb.dir, 0555);
        press(&a, "go");
        chmod(sb.dir, 0755);
        CHECK(!strcmp(a.map->name, "town"));
        CHECK_EQ(a.map->tokens.n, before_n);
        CHECK(strstr(a.status, "nobody went") != NULL);
        CHECK(!undo_can_redo(&a.undo));
    } else {
        printf("  (skipped as root: a read-only folder does not stop a save)\n");
    }
    char keep[MAP_PATH_MAX];
    str_lcpy(keep, a.map->path, sizeof keep);
    a.map->path[0] = '\0';                                  /* as a map never saved has */
    press(&a, "go");
    CHECK(!strcmp(a.map->name, "town"));
    CHECK(strstr(a.status, "save this map first") != NULL);
    str_lcpy(a.map->path, keep, sizeof a.map->path);

    CASE("--check: W151 for a link whose map or place is not there");
    {
        snprintf(path, sizeof path, "%s/lost.vtt", sb.dir);
        FILE *lf = fopen(path, "w");
        if (lf) {
            fputs("VTT 12\nname lost\nsize 4 3\ntiles\n....\n....\n....\n"
                  "link 1 stairs 1 0 0 to \"crypt\" \"Attic\"\nlink 2 portal 1 3 2 to \"nowhere\" \"Hall\"\n", lf);
            fclose(lf);
        }
        char *buf = NULL; size_t bn = 0;
        FILE *out = open_memstream(&buf, &bn);
        maptools_check(out, path, 0);
        fclose(out);
        CHECK(strstr(buf, "W151") != NULL);
        CHECK(strstr(buf, "stairs 1 to crypt, Attic leads nowhere: crypt has no area or square called Attic") != NULL);
        CHECK(strstr(buf, "portal 2 to nowhere, Hall leads nowhere: no map nowhere beside this one") != NULL);
        free(buf);
    }

    CASE("the channel makes one, checked; refuses reverse; the links read shows it");
    app_key(&a, (Key){ KEY_F1, 0, 0 });
    char *ans = ctl_ask(&a, "link K7 to crypt \"Entrance\" portal secret");
    CHECK(ans && strncmp(ans, "ok", 2) == 0);
    free(ans);
    int li = -1;
    for (int i = 0; i < a.map->nlinks; i++) if (a.map->links[i].kind == LINK_PORTAL) li = i;
    CHECK(li >= 0 && a.map->links[li].secret && !strcmp(a.map->links[li].to_place, "Entrance"));
    ans = ctl_ask(&a, "link B2 to crypt Attic");
    CHECK(ans && strstr(ans, "crypt has no area or square called Attic") != NULL);
    free(ans);
    char req[64];
    snprintf(req, sizeof req, "link %d reverse", a.map->links[li].num);
    ans = ctl_ask(&a, req);
    CHECK(ans && strstr(ans, "leads to another map") != NULL);
    free(ans);
    ans = ctl_ask(&a, "links");
    CHECK(ans && strstr(ans, "-> crypt, Entrance") != NULL);
    free(ans);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}
