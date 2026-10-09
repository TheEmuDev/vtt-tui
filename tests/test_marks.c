/* Tests: what the GM keeps on a creature: markers, notes, counters, hidden creatures. */

#include "harness.h"

void test_status_draw(void)
{
    Map *m = map_new(8, 8, "draw");
    map_fill_tiles(m, 0, 0, 7, 7, TILE_FLOOR);

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 60, 24);

    GridView g;
    memset(&g, 0, sizeof g);
    g.zoom = 1;                            /* a 1x1 token is three cells wide */
    g.view = rect(0, 0, 60, 24);

    Token t;
    memset(&t, 0, sizeof t);
    t.x = 2; t.y = 2; t.size = 1; t.kind = TOKEN_ENEMY;
    str_lcpy(t.label, "G", sizeof t.label);

    Rect a;
    grid_token_area(&g, t.x, t.y, t.size, &a);

    CASE("an unmarked token draws no markers");
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 0);
    CHECK_EQ(rnd_at(&r, a.x, a.y - 1)->ch, ' ');

    /* Above the token, never on it: the label has to stay readable. */
    CASE("markers sit on the boundary row, not on the token");
    token_add_status(&t, 0, "Poisoned");
    token_add_status(&t, 3, "Marked");
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 0);
    CHECK_EQ(rnd_at(&r, a.x, a.y - 1)->ch, 'P');
    CHECK_EQ(rnd_at(&r, a.x + 1, a.y - 1)->ch, 'M');
    CHECK_EQ(rnd_at(&r, a.x, a.y)->ch, ' ');        /* the token row is untouched */

    CASE("each marker takes its own color from the palette");
    CHECK_EQ(rnd_at(&r, a.x, a.y - 1)->fg, THEME_DARK.status[0]);
    CHECK_EQ(rnd_at(&r, a.x + 1, a.y - 1)->fg, THEME_DARK.status[3]);

    /* Three cells is fewer than a token can carry, so the fourth has to go
     * somewhere: the row below. */
    CASE("markers past the top edge continue underneath");
    token_add_status(&t, 4, "Burning");
    token_add_status(&t, 5, "Slowed");
    CHECK_EQ(t.nstatus, 4);
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 0);
    CHECK_EQ(rnd_at(&r, a.x + 2, a.y - 1)->ch, 'B');
    CHECK_EQ(rnd_at(&r, a.x, a.y + a.h)->ch, 'S');

    CASE("a wider token fits them all on one row");
    g.zoom = 2;
    grid_token_area(&g, t.x, t.y, t.size, &a);
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 0);
    CHECK_EQ(rnd_at(&r, a.x + 3, a.y - 1)->ch, 'S');

    CASE("ascii mode keeps letters and swaps the dot");
    g.zoom = 1;
    token_clear_status(&t);
    token_add_status(&t, 0, "Poisoned");
    token_add_status(&t, 1, "");
    grid_token_area(&g, t.x, t.y, t.size, &a);
    rnd_begin(&r);
    grid_draw_token_status(&r, &g, &t, &THEME_DARK, 1);
    CHECK_EQ(rnd_at(&r, a.x, a.y - 1)->ch, 'P');
    CHECK_EQ(rnd_at(&r, a.x + 1, a.y - 1)->ch, '*');

    rnd_free(&r);
    map_free(m);
}

/* Notes: the GM's own text on a creature or a square, read and written
 * through one prompt, hinted at but never shown on the mirrored status
 * line, marked on the map in build mode only. */
void test_notes(void)
{
    Sandbox sb = sandbox_enter("notes");
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
    Map *m = a.map;
    a.ed.cx = 0; a.ed.cy = 0;
    press(&a, "ipAria\r");
    CHECK_EQ(m->tokens.n, 1);

    CASE("s n on a creature opens its note; the players' frame would differ while it is open");
    CHECK_EQ(app_remote_live(&a), 1);
    CHECK_EQ(app_view_differs(&a), 0);
    press(&a, "sn");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK_EQ(a.prompt_what, PROMPT_NOTE);
    CHECK(strstr(a.prompt.title, "note on Aria") != NULL);
    CHECK_EQ(app_remote_live(&a), 1);              /* no freeze: the prompt is simply not in their frame */
    CHECK_EQ(app_view_differs(&a), 1);
    press(&a, "wants the amulet\r");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK_EQ(app_view_differs(&a), 1);             /* the selected creature has a note: (note) is GM-only */
    CHECK_EQ(strcmp(m->tokens.v[0].note, "wants the amulet"), 0);
    CHECK(strstr(a.status, "noted on Aria") != NULL);
    CHECK(strstr(a.status, "amulet") == NULL);            /* the text stays off the line */
    CHECK_EQ(m->modified, 1);

    CASE("the readout says there is a note, not what it says -- and only to the GM");
    char line[192];
    play_status(&a.play, m, &a.ed, 1, line, sizeof line);
    CHECK(strstr(line, "(note)") != NULL);
    CHECK(strstr(line, "amulet") == NULL);
    play_status(&a.play, m, &a.ed, 0, line, sizeof line);
    CHECK(strstr(line, "(note)") == NULL);

    CASE("the prompt stops where the note does, so nothing typed is lost on the way in");
    press(&a, "sn\025");
    for (int i = 0; i < 80; i++) press(&a, "x");
    CHECK_EQ(a.prompt.len, TOKEN_NOTE_MAX - 1);
    press(&a, "\r");
    CHECK_EQ((int)strlen(m->tokens.v[0].note), TOKEN_NOTE_MAX - 1);
    press(&a, "sn\025wants the amulet\r");

    CASE("the prompt opens holding the note, so it is the reader too");
    press(&a, "sn");
    CHECK_EQ(strcmp(a.prompt.buf, "wants the amulet"), 0);
    press(&a, " and the ring\r");
    CHECK_EQ(strcmp(m->tokens.v[0].note, "wants the amulet and the ring"), 0);

    CASE("a creature's note undoes, and ctrl-u then enter takes it off");
    press(&a, "u");
    CHECK_EQ(strcmp(m->tokens.v[0].note, "wants the amulet"), 0);
    press(&a, "\x12");
    CHECK_EQ(strcmp(m->tokens.v[0].note, "wants the amulet and the ring"), 0);
    press(&a, "sn\025\r");
    CHECK_EQ(m->tokens.v[0].note[0], '\0');
    CHECK(strstr(a.status, "note taken off Aria") != NULL);
    press(&a, "sn\r");
    CHECK(strstr(a.status, "nothing noted") != NULL);

    CASE("with no creature under the cursor the note goes on the square");
    press(&a, "\x1b");                                     /* deselect */
    a.ed.cx = 1; a.ed.cy = 1;
    CHECK_EQ(a.play.sel, -1);
    press(&a, "sn");
    CHECK(strstr(a.prompt.title, "note on B2") != NULL);
    press(&a, "pressure plate\r");
    CHECK(map_note_at(m, 1, 1) != NULL);
    CHECK_EQ(strcmp(map_note_at(m, 1, 1), "pressure plate"), 0);
    CHECK(strstr(a.status, "noted on B2") != NULL);
    CHECK_EQ(m->nnotes, 1);
    play_status(&a.play, m, &a.ed, 1, line, sizeof line);
    CHECK(strstr(line, "(note)") != NULL);
    a.ed.cx = 0; a.ed.cy = 1;
    play_status(&a.play, m, &a.ed, 1, line, sizeof line);
    CHECK(strstr(line, "(note)") == NULL);

    CASE(":notes says where they are");
    press(&a, "sn");
    press(&a, "loose flagstone\r");                        /* A2 */
    a.ed.cx = 0; a.ed.cy = 0;
    press(&a, "t");                                        /* select Aria */
    press(&a, "sn");
    press(&a, "afraid of fire\r");
    press(&a, ":notes\r");
    CHECK(strstr(a.status, "notes on Aria, B2, A2") != NULL);
    CHECK(strstr(a.status, "flagstone") == NULL);

    CASE("in play mode nothing marks a noted square; in build mode a quote does");
    rnd_begin(&r);
    app_draw(&a);
    int marks = 0;
    for (size_t i = 0; i < (size_t)r.w * (size_t)r.h; i++) marks += r.back[i].ch == 0x201Du;
    CHECK_EQ(marks, 0);
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    rnd_begin(&r);
    app_draw(&a);
    marks = 0;
    for (size_t i = 0; i < (size_t)r.w * (size_t)r.h; i++) marks += r.back[i].ch == 0x201Du;
    CHECK_EQ(marks, 2);
    int sx, sy;
    grid_tile_interior(&a.ed.view, 1, 1, &sx, &sy);
    CHECK_EQ(r.back[(size_t)sy * (size_t)r.w + (size_t)(sx + ZOOM[a.ed.view.zoom].iw - 1)].ch, 0x201Du);

    CASE("build mode has s n too, on the square, and says so");
    a.ed.cx = 1; a.ed.cy = 0;
    press(&a, "s");
    CHECK(strstr(a.status, "s n") != NULL);
    press(&a, "n");
    CHECK(strstr(a.prompt.title, "note on B1") != NULL);
    press(&a, "the altar\r");
    CHECK_EQ(m->nnotes, 3);
    ed_status(&a.ed, m, line, sizeof line);
    CHECK(strstr(line, "(note)") != NULL);
    press(&a, "sx");
    CHECK(strstr(a.status, "s wants n") != NULL);
    app_key(&a, f2);

    CASE("notes are saved as version 5, on the creature and on the squares, and read back");
    char err[128];
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    char *text = slurp(path);
    CHECK(text != NULL);
    if (text) {
        CHECK_EQ(strncmp(text, "VTT 5\n", 6), 0);
        CHECK(strstr(text, "token player 0 0 1 \"Aria\"\ntokennote \"afraid of fire\"\n") != NULL);
        CHECK(strstr(text, "note 1 1 \"pressure plate\"\n") != NULL);
        free(text);
    }
    Map *back = mapio_load(path, err, sizeof err);
    CHECK(back != NULL);
    if (back) {
        CHECK_EQ(strcmp(back->tokens.v[0].note, "afraid of fire"), 0);
        CHECK_EQ(back->nnotes, 3);
        CHECK_EQ(strcmp(map_note_at(back, 1, 0), "the altar"), 0);
        CHECK_EQ(back->modified, 0);

        CASE("a shrink drops the notes it leaves outside");
        CHECK_EQ(map_resize(back, 1, 1), 0);
        CHECK_EQ(back->nnotes, 0);
        map_free(back);
    }

    CASE("a copied creature carries its note, and equality sees it");
    Token t1 = m->tokens.v[0], t2 = t1;
    CHECK_EQ(token_equal(&t1, &t2), 1);
    str_lcpy(t2.note, "other", sizeof t2.note);
    CHECK_EQ(token_equal(&t1, &t2), 0);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

void test_counters(void)
{
    CASE("the parser: set, step, name, remove, and refuse nonsense");
    Token t;
    memset(&t, 0, sizeof t);
    char cur[COUNTER_NAME_MAX] = "HP", msg[160];
    const char *dh = "HP Stress Armor";
    CHECK_EQ(counter_apply(&t, "hp 6", dh, cur, sizeof cur, msg, sizeof msg), 0);
    CHECK_EQ(t.ncounters, 1);
    CHECK_EQ(strcmp(t.counters[0].name, "HP"), 0);          /* the ruleset's spelling */
    CHECK_EQ(t.counters[0].value, 6);
    CHECK_EQ(t.counters[0].max, 6);
    CHECK_EQ(counter_apply(&t, "hp -2, stress 0/6", dh, cur, sizeof cur, msg, sizeof msg), 0);
    CHECK_EQ(t.counters[0].value, 4);
    CHECK_EQ(strcmp(t.counters[1].name, "Stress"), 0);
    CHECK_EQ(strcmp(cur, "Stress"), 0);                      /* the last one named */
    CHECK_EQ(strcmp(msg, "HP 4/6  Stress 0/6"), 0);
    CHECK_EQ(counter_apply(&t, "hp +9", dh, cur, sizeof cur, msg, sizeof msg), 0);
    CHECK_EQ(t.counters[0].value, 6);                        /* clamped at the maximum */
    CHECK_EQ(counter_apply(&t, "hp 3/8", dh, cur, sizeof cur, msg, sizeof msg), 0);
    CHECK_EQ(t.counters[0].max, 8);
    CHECK_EQ(counter_apply(&t, "hp", dh, cur, sizeof cur, msg, sizeof msg), 0);
    CHECK_EQ(strcmp(cur, "HP"), 0);
    CHECK(strstr(msg, "the counter < and > step") != NULL);
    CHECK_EQ(counter_apply(&t, "Wounds 2", NULL, cur, sizeof cur, msg, sizeof msg), 0);
    CHECK_EQ(strcmp(t.counters[2].name, "Wounds"), 0);       /* any name, as typed */
    CHECK_EQ(counter_apply(&t, "-stress", dh, cur, sizeof cur, msg, sizeof msg), 0);
    CHECK_EQ(t.ncounters, 2);
    CHECK_EQ(strcmp(t.counters[1].name, "Wounds"), 0);       /* the rest close up */
    CHECK_EQ(counter_apply(&t, "armor -1", dh, cur, sizeof cur, msg, sizeof msg), -1);
    CHECK(strstr(msg, "no Armor yet") != NULL);
    CHECK_EQ(counter_apply(&t, "armor 0", dh, cur, sizeof cur, msg, sizeof msg), -1);
    CHECK(strstr(msg, "needs its maximum") != NULL);
    CHECK_EQ(counter_apply(&t, "hp 3/0", dh, cur, sizeof cur, msg, sizeof msg), -1);
    CHECK_EQ(counter_apply(&t, "hp x", dh, cur, sizeof cur, msg, sizeof msg), -1);
    CHECK_EQ(counter_apply(&t, "3 hp", dh, cur, sizeof cur, msg, sizeof msg), -1);
    CHECK_EQ(counter_apply(&t, "toolongname 3", dh, cur, sizeof cur, msg, sizeof msg), -1);
    CHECK_EQ(counter_apply(&t, "-nothing", dh, cur, sizeof cur, msg, sizeof msg), -1);
    CHECK_EQ(counter_apply(&t, "hp +2147483647", dh, cur, sizeof cur, msg, sizeof msg), 0);
    CHECK_EQ(t.counters[0].value, t.counters[0].max);        /* bounded, not overflowed */
    CHECK_EQ(counter_apply(&t, "hp -9999999999", dh, cur, sizeof cur, msg, sizeof msg), 0);
    CHECK_EQ(t.counters[0].value, 0);
    CHECK_EQ(counter_apply(&t, "a 1, b 1, c 1", NULL, cur, sizeof cur, msg, sizeof msg), -1);
    CHECK(strstr(msg, "at most 4") != NULL);
    char def[COUNTER_NAME_MAX];
    counter_default(dh, def, sizeof def);   CHECK_EQ(strcmp(def, "HP"), 0);
    counter_default(NULL, def, sizeof def); CHECK_EQ(strcmp(def, "HP"), 0);
    counter_default("Wounds Grit", def, sizeof def); CHECK_EQ(strcmp(def, "Wounds"), 0);

    Sandbox sb = sandbox_enter("counters");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    write_map_file(sb.dir, "fight.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    press(&a, ":ruleset daggerheart\r");
    a.ed.cx = a.ed.cy = 0;
    press(&a, "ieOgre\r");
    Map *m = a.map;

    CASE("s v opens the prompt with the ruleset's counters offered");
    press(&a, "sv");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK_EQ(a.prompt_what, PROMPT_COUNTERS);
    CHECK(strstr(a.prompt.title, "counters on Ogre") != NULL);
    CHECK(strstr(a.prompt.hint, "HP Stress Armor") != NULL);
    press(&a, "hp 6, stress 0/3\r");
    CHECK_EQ(m->tokens.v[0].ncounters, 2);
    CHECK(strstr(a.status, "Ogre: HP 6/6  Stress 0/3") != NULL);
    CHECK_EQ(a.status_gm, 1);
    CHECK_EQ(m->modified, 1);

    CASE("< and > step the current counter -- the last named, here Stress -- and a count names how far");
    press(&a, ">");
    CHECK_EQ(m->tokens.v[0].counters[1].value, 1);
    press(&a, "svhp\r");                                   /* HP is current now */
    press(&a, "2<");
    CHECK_EQ(m->tokens.v[0].counters[0].value, 4);
    CHECK(strstr(a.status, "Ogre HP 4/6") != NULL);
    press(&a, "9<");
    CHECK_EQ(m->tokens.v[0].counters[0].value, 0);
    press(&a, "<");
    CHECK(strstr(a.status, "already 0/6") != NULL);

    CASE("a hit is one undo step");
    press(&a, "u");
    CHECK_EQ(m->tokens.v[0].counters[0].value, 4);
    press(&a, "\x12");
    CHECK_EQ(m->tokens.v[0].counters[0].value, 0);
    press(&a, "sv\025hp 5\r");

    CASE("the GM's status line shows the counters; the players' never does");
    char line[192];
    play_status(&a.play, m, &a.ed, 1, line, sizeof line);
    CHECK(strstr(line, "HP 5/6") != NULL);
    play_status(&a.play, m, &a.ed, 0, line, sizeof line);
    CHECK(strstr(line, "HP") == NULL);

    CASE("a creature with counters selected makes the two frames differ, and the phone sees no number");
    CHECK_EQ(app_view_differs(&a), 1);
    press(&a, ":serve\r");
    int w = net_connect(a.net.port);
    CHECK(w >= 0);
    CHECK_EQ((int)write(w, "VTT1\n", 5), 5);
    net_pump(&a.net, 0);
    press(&a, ">");                                        /* a GM-only message on the line */
    app_frame(&a, NULL, 0);
    ByteBuf gm, pl;
    bb_init(&gm, 65536); front_text(&r, &gm); bb_putc(&gm, '\0');
    bb_init(&pl, 65536); front_text(&a.net.players, &pl); bb_putc(&pl, '\0');
    CHECK(strstr(gm.data, "6/6") != NULL);
    CHECK(strstr(pl.data, "6/6") == NULL);
    CHECK(strstr(pl.data, "HP") == NULL);
    CHECK(strstr(pl.data, "PLAY") != NULL);
    bb_free(&gm); bb_free(&pl);

    CASE("a prompt that changes nothing, or is refused, still keeps its numbers off the phone");
    play_focus(&a.play, 0);
    press(&a, "sv\025hp 6\r");                             /* already 6/6 */
    CHECK(strstr(a.status, "HP 6/6") != NULL);
    CHECK_EQ(a.status_gm, 1);
    press(&a, "\x1b");                                     /* deselect; the cursor still names Ogre */
    a.ed.cx = 0; a.ed.cy = 0;
    press(&a, "sv\025hp 4/\r");                            /* refused, echoing what was typed */
    CHECK(strstr(a.status, "maximum") != NULL);
    CHECK_EQ(a.status_gm, 1);
    a.ed.cx = 3; a.ed.cy = 3;                                /* nothing else GM-only in view */
    CHECK_EQ(app_view_differs(&a), 1);
    app_frame(&a, NULL, 0);
    bb_init(&pl, 65536); front_text(&a.net.players, &pl); bb_putc(&pl, '\0');
    CHECK(strstr(pl.data, "maximum") == NULL);
    CHECK(strstr(pl.data, "4/") == NULL);
    bb_free(&pl);

    CASE("the panel shows the actor's counter to the GM only");
    play_focus(&a.play, 0);
    press(&a, "si12\r");
    press(&a, "a");
    press(&a, "\x1b");
    a.ed.cx = 3; a.ed.cy = 3;
    app_frame(&a, NULL, 0);
    bb_init(&gm, 65536); front_text(&r, &gm); bb_putc(&gm, '\0');
    bb_init(&pl, 65536); front_text(&a.net.players, &pl); bb_putc(&pl, '\0');
    CHECK(strstr(gm.data, "Ogre") != NULL);
    CHECK(strstr(gm.data, "6/6") != NULL);
    CHECK(strstr(pl.data, "Ogre") != NULL);                /* the order is the table's */
    CHECK(strstr(pl.data, "6/6") == NULL);                 /* the number is not */
    bb_free(&gm); bb_free(&pl);
    close(w);
    press(&a, ":serve off\r");

    CASE("copy and paste carry the counters along");
    play_focus(&a.play, 0);
    a.ed.cx = 0; a.ed.cy = 0;
    press(&a, "y");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "p");
    CHECK_EQ(m->tokens.n, 2);
    CHECK_EQ(m->tokens.v[1].ncounters, 2);
    CHECK_EQ(m->tokens.v[1].counters[0].value, 6);

    CASE("counters are saved as version 6 and read back; without them the file says what it did before");
    char err[128];
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    char *text = slurp(path);
    CHECK(text != NULL);
    if (text) {
        CHECK_EQ(strncmp(text, "VTT 6\n", 6), 0);
        CHECK(strstr(text, "tokencounter HP 6 6\ntokencounter Stress 1 3\n") != NULL);
        free(text);
    }
    Map *back = mapio_load(path, err, sizeof err);
    CHECK(back != NULL);
    if (back) {
        CHECK_EQ(back->tokens.v[0].ncounters, 2);
        CHECK_EQ(token_equal(&back->tokens.v[0], &m->tokens.v[0]), 1);
        map_free(back);
    }

    CASE("an overlong counter name in a file is refused, not cut short with its tail read as a number");
    {
        char bad[700];
        snprintf(bad, sizeof bad, "%s/bad.vtt", sb.dir);
        FILE *bf = fopen(bad, "w");
        if (bf) {
            fputs("VTT 6\nname x\nsize 2 2\ntiles\n..\n..\ntoken enemy 0 0 1 \"Ogre\"\n"
                  "tokencounter Stamina2 4 6\ntokencounter Grit 2 5\n", bf);
            fclose(bf);
        }
        Map *bm = mapio_load(bad, err, sizeof err);
        CHECK(bm != NULL);
        if (bm) {
            CHECK_EQ(bm->tokens.v[0].ncounters, 1);
            CHECK_EQ(strcmp(bm->tokens.v[0].counters[0].name, "Grit"), 0);
            map_free(bm);
        }
    }
    press(&a, "u");                                        /* the paste */
    play_focus(&a.play, 0);
    press(&a, "sv\025-hp, -stress\r");
    CHECK_EQ(m->tokens.v[0].ncounters, 0);
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    text = slurp(path);
    if (text) { CHECK_EQ(strncmp(text, "VTT 4\n", 6), 0); free(text); }   /* still a fight */

    CASE("with no creature, s v and < say so");
    press(&a, ":turns end\r");
    press(&a, "\x1b");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "sv");
    CHECK(strstr(a.status, "no creature") != NULL);
    press(&a, "<");
    CHECK(strstr(a.status, "no creature") != NULL);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* -------------------------------------------------------- hidden creatures */

void test_hidden(void)
{
    Sandbox sb = sandbox_enter("hidden");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[1200];
    snprintf(path, sizeof path, "%s/ambush.vtt", sb.dir);
    FILE *f = fopen(path, "w");
    fputs("VTT 3\nname ambush\nsize 12 6\nzoom 1\ntiles\n"
          "............\n............\n............\n............\n............\n............\n"
          "token player 1 1 1 \"Aria\"\ntoken enemy 6 2 1 \"Zorkmid\"\ntokenstatus red \"Poisoned\"\n", f);
    fclose(f);
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    a.ctl_auto = 1;                       /* edits land at once (decision 6) */
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    Map *m = a.map;

    CASE("s h hides the creature under the cursor; the GM still sees it, dimmed");
    a.ed.cx = 6; a.ed.cy = 2;
    press(&a, "sh");
    CHECK_EQ(m->tokens.v[1].hidden, 1);
    CHECK(strstr(a.status, "Zorkmid hidden from the players") != NULL);
    CHECK_EQ(a.status_gm, 1);
    CHECK_EQ(app_view_differs(&a), 1);
    rnd_begin(&r); app_draw(&a);
    ByteBuf g;
    bb_init(&g, 65536); rnd_dump(&r, &g); bb_putc(&g, '\0');
    CHECK(strstr((char *)g.data, "Zorkmid") != NULL || strstr((char *)g.data, "Zor") != NULL);
    bb_free(&g);

    CASE("the players' frame never learns it: body, label, marker, ring, turn, cursor, trail, range, ruler, messages");
    {
        char *t = players_text(&a, &r);
        CHECK(strstr(t, "Zor") == NULL && strstr(t, "Poisoned") == NULL);
        free(t);
        /* Selected, in the turn order and holding the turn, carried, with the
         * range on it and the ruler from it, and a message about it. */
        press(&a, "si12\r");
        a.ed.cx = 1; a.ed.cy = 1;
        press(&a, "si5\r");
        press(&a, "a");                                  /* the Zorkmid's turn */
        CHECK(turn_acting(m) == 1);
        a.ed.cx = 6; a.ed.cy = 2;
        press(&a, "\rll");                               /* carried two squares east */
        press(&a, "r");
        t = players_text(&a, &r);
        CHECK(strstr(t, "Zor") == NULL && strstr(t, "Poisoned") == NULL);
        CHECK(strstr(t, "turn") == NULL || strstr(t, "?") != NULL);
        CHECK(strstr(t, "hidden") == NULL);
        free(t);
        press(&a, "\r");                                 /* down at I3 */
        press(&a, "m");
        t = players_text(&a, &r);
        CHECK(strstr(t, "Zor") == NULL && strstr(t, "RULER") == NULL);
        free(t);
        press(&a, "\x1b");
        /* Its square draws what an empty square draws. */
        rnd_begin(&r); app_draw_view(&a, VIEW_PLAYERS);
        int sx, sy, ex, ey;
        grid_tile_interior(&a.ed.view, 8, 2, &sx, &sy);
        grid_tile_interior(&a.ed.view, 10, 4, &ex, &ey);
        for (int k = 0; k < ZOOM[a.ed.view.zoom].iw; k++) {
            Cell *c1 = rnd_at(&r, sx + k, sy), *c2 = rnd_at(&r, ex + k, ey);
            CHECK(c1 && c2 && c1->ch == c2->ch);
        }
        char line[256];
        play_status(&a.play, m, &a.ed, 0, line, sizeof line);
        CHECK(strstr(line, "Zor") == NULL && strstr(line, "token") == NULL);
    }

    CASE(":hidden lists it for the GM; s h again shows it; u hides it again");
    press(&a, ":hidden\r");
    CHECK(strstr(a.status, "hidden: Zorkmid I3") != NULL && a.status_gm);
    a.ed.cx = 8; a.ed.cy = 2;
    play_focus(&a.play, -1);
    press(&a, "sh");
    CHECK_EQ(m->tokens.v[1].hidden, 0);
    a.status[0] = '\0'; a.status_gm = 0;              /* the GM's own "shown" message aside */
    CHECK_EQ(app_view_differs(&a), 0);
    press(&a, "u");
    CHECK_EQ(m->tokens.v[1].hidden, 1);

    CASE("a v box hides all it holds, or shows them when every one is hidden");
    play_focus(&a.play, -1);
    a.ed.cx = 0; a.ed.cy = 0;
    press(&a, "v");
    a.ed.cx = 11; a.ed.cy = 5;
    press(&a, "sh");
    CHECK(m->tokens.v[0].hidden && m->tokens.v[1].hidden);
    a.ed.cx = 0; a.ed.cy = 0;
    press(&a, "v");
    a.ed.cx = 11; a.ed.cy = 5;
    press(&a, "sh");
    CHECK(!m->tokens.v[0].hidden && !m->tokens.v[1].hidden);
    press(&a, "u");                                      /* both hidden again */

    CASE("the file: version 10 with a hidden creature, read back hidden; copies keep it");
    {
        char err[256], p2[1300];
        snprintf(p2, sizeof p2, "%s/saved.vtt", sb.dir);
        CHECK_EQ(mapio_write(m, p2, err, sizeof err), 0);
        FILE *h = fopen(p2, "r");
        char first[32] = "";
        if (h) { if (!fgets(first, sizeof first, h)) first[0] = 0; fclose(h); }
        CHECK_EQ(strcmp(first, "VTT 10\n"), 0);
        Map *back = mapio_load(p2, err, sizeof err);
        CHECK(back && back->tokens.v[1].hidden == 1 && back->tokens.v[0].hidden == 1);
        map_free(back);
        Map *st = stamp_copy(m, 0, 0, 11, 5);
        CHECK(st && st->tokens.n == 2 && st->tokens.v[1].hidden);
        map_free(st);
        char *d = describe_text(m, 0, NULL);
        CHECK(d && strstr(d, "(hidden)"));
        free(d);
        d = describe_text(m, 1, NULL);
        CHECK(d && json_valid(d) && strstr(d, "\"hidden\":true"));
        free(d);
    }

    CASE("the channel: token add ... hidden, token set WHO hidden on|off");
    {
        Key f1 = { KEY_F1, 0, 0 };
        app_key(&a, f1);
        char *ans = ctl_ask(&a, "token add enemy K5 hidden \"Lurker\"\ntoken set Aria hidden on\n");
        CHECK(ans && !strncmp(ans, "ok", 2));
        free(ans);
        int lu = -1;
        for (int i = 0; i < m->tokens.n; i++) if (!strcmp(m->tokens.v[i].label, "Lurker")) lu = i;
        CHECK(lu >= 0 && m->tokens.v[lu].hidden && m->tokens.v[0].hidden);
        ans = ctl_ask(&a, "token set Aria hidden maybe\n");
        CHECK(ans && strstr(ans, "hidden on, or hidden off"));
        free(ans);
        ans = ctl_ask(&a, "dump\n");
        CHECK(ans && strstr(ans, "Lurker") && strstr(ans, "hidden"));
        free(ans);
    }

    CASE("review fixes: a hidden enemy blocks nothing; deleting the last hidden says nothing public");
    {
        char *ans = ctl_ask(&a, "token set Aria hidden off\ntoken del Lurker\ntoken move Aria B3\ntoken move Zorkmid E3\n");
        CHECK(ans && !strncmp(ans, "ok", 2));
        free(ans);
        Key f2b = { KEY_F2, 0, 0 };
        app_key(&a, f2b);
        play_focus(&a.play, -1);
        a.ed.cx = 1; a.ed.cy = 2;
        press(&a, "\r");
        press(&a, "llllll");                               /* straight through E3 */
        CHECK(m->tokens.v[0].x == 7 && a.play.steps == 6);
        press(&a, "\x1b");
        play_focus(&a.play, -1);
        a.ed.cx = 4; a.ed.cy = 2;
        press(&a, "x");                                    /* the last hidden creature goes */
        CHECK(!tokens_any_hidden(&m->tokens) && a.status_gm == 1);
        press(&a, "u");
    }

    CASE("review fixes: g p carrying a hidden 2x2 rings one square; the channel wants the label last");
    {
        m->tokens.v[1].size = 2;
        play_focus(&a.play, -1);
        a.ed.cx = 4; a.ed.cy = 2;
        press(&a, "\r");
        a.npings = 0;
        press(&a, "gp");
        CHECK(a.npings == 1 && a.pings[0].x1 == a.pings[0].x0);
        press(&a, "\x1b");
        m->tokens.v[1].size = 1;
        Key f1b = { KEY_F1, 0, 0 };
        app_key(&a, f1b);
        char *ans = ctl_ask(&a, "token add enemy K5 hidden\n");
        CHECK(ans && strstr(ans, "the label goes last"));
        free(ans);
    }

    CASE("review fixes: a hidden marker after a dropped creature line hides nobody");
    {
        char err[256], p3[1300];
        snprintf(p3, sizeof p3, "%s/drop.vtt", sb.dir);
        FILE *h = fopen(p3, "w");
        fputs("VTT 10\nsize 4 1\ntiles\n....\ntoken enemy 0 0 1 \"Goblin\"\n"
              "token enemy 9 9 1 \"Offmap\"\ntokenhidden\n", h);
        fclose(h);
        Map *back = mapio_load(p3, err, sizeof err);
        CHECK(back && back->tokens.n == 1 && back->tokens.v[0].hidden == 0);
        map_free(back);
    }

    app_free(&a);
    rnd_free(&r);
    char cmd[1300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", sb.dir);
    sandbox_leave(&sb);
    if (system(cmd) != 0) { }
}

/* ------------------------------------------------------------- the picker */
