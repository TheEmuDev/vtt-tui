/* Tests: dice and named rolls, the session log, clocks, the turn order. */

#include "harness.h"

/* --------------------------------------------------------------- dice */

void test_dice(void)
{
    CASE("a seed makes the dice repeatable");
    dice_seed(42);
    int first[8];
    for (int i = 0; i < 8; i++) first[i] = dice_one(20);
    dice_seed(42);
    int same = 1;
    for (int i = 0; i < 8; i++) if (dice_one(20) != first[i]) same = 0;
    CHECK_EQ(same, 1);

    CASE("every face comes up, and nothing off the die does");
    dice_seed(7);
    int seen[7] = { 0 };
    int off = 0;
    for (int i = 0; i < 6000; i++) {
        int v = dice_one(6);
        if (v < 1 || v > 6) off++; else seen[v]++;
    }
    CHECK_EQ(off, 0);
    for (int f = 1; f <= 6; f++) CHECK(seen[f] > 800);   /* ~1000 each */
    CHECK_EQ(dice_one(1), 1);
    CHECK_EQ(dice_one(0), 1);

    CASE("an expression is the sum of its dice and constants");
    DiceResult r;
    char err[64];
    dice_seed(3);
    CHECK_EQ(dice_roll_expr("2d6+3", &r, err, sizeof err), 0);
    CHECK_EQ(r.nrolls, 2);
    CHECK_EQ(r.total, r.rolls[0] + r.rolls[1] + 3);
    CHECK_EQ(dice_roll_expr(" 4d6 + 1d4 - 1 ", &r, err, sizeof err), 0);
    CHECK_EQ(r.nrolls, 5);
    int sum = -1;
    for (int i = 0; i < 5; i++) sum += r.rolls[i];
    CHECK_EQ(r.total, sum);
    CHECK_EQ(dice_roll_expr("d20", &r, err, sizeof err), 0);   /* a bare d is one die */
    CHECK_EQ(r.nrolls, 1);
    CHECK_EQ(dice_roll_expr("-d4+10", &r, err, sizeof err), 0);
    CHECK(r.total >= 6 && r.total <= 9);
    CHECK_EQ(dice_roll_expr("5", &r, err, sizeof err), 0);
    CHECK_EQ(r.total, 5);
    CHECK_EQ(r.nrolls, 0);

    CASE("what is not an expression says why");
    CHECK_EQ(dice_roll_expr("", &r, err, sizeof err), -1);
    CHECK_EQ(dice_roll_expr("d", &r, err, sizeof err), -1);
    CHECK(strstr(err, "sides") != NULL);
    CHECK_EQ(dice_roll_expr("0d6", &r, err, sizeof err), -1);
    CHECK_EQ(dice_roll_expr("101d6", &r, err, sizeof err), -1);
    CHECK_EQ(dice_roll_expr("2d1", &r, err, sizeof err), -1);
    CHECK_EQ(dice_roll_expr("2d1001", &r, err, sizeof err), -1);
    CHECK_EQ(dice_roll_expr("2d6+", &r, err, sizeof err), -1);
    CHECK_EQ(dice_roll_expr("2d6 3", &r, err, sizeof err), -1);
    CHECK_EQ(dice_roll_expr("abc", &r, err, sizeof err), -1);
    CHECK_EQ(dice_roll_expr("1d6+1d6+1d6+1d6+1d6+1d6+1d6+1d6+1d6", &r, err, sizeof err), -1);

    CASE("the readout shows the expression, the total and each die");
    r.total = 9; r.nrolls = 2; r.rolls[0] = 4; r.rolls[1] = 2;
    char buf[160];
    dice_format("2d6 + 3", &r, buf, sizeof buf);
    CHECK_EQ(strcmp(buf, "2d6+3 = 9  [4 2]"), 0);
    r.nrolls = 0; r.total = 5;
    dice_format("5", &r, buf, sizeof buf);
    CHECK_EQ(strcmp(buf, "5 = 5"), 0);
    CHECK_EQ(dice_roll_expr("100d6", &r, err, sizeof err), 0);
    CHECK_EQ(r.nrolls, 100);
    dice_format("100d6", &r, buf, sizeof buf);
    CHECK(strstr(buf, "...]") != NULL);                     /* past the 64 kept */

    /* Daggerheart: two d12s, Hope and Fear, read against each other. */
    CASE("duality reads Hope, Fear, or a critical when the dice match");
    CHECK_EQ(strcmp(dice_duality_verdict(9, 6), "with Hope"), 0);
    CHECK_EQ(strcmp(dice_duality_verdict(3, 11), "with Fear"), 0);
    CHECK_EQ(strcmp(dice_duality_verdict(7, 7), "critical success"), 0);
    DualityRoll d;
    dice_seed(11);
    dice_duality(2, &d);
    CHECK(d.hope >= 1 && d.hope <= 12);
    CHECK(d.fear >= 1 && d.fear <= 12);
    CHECK_EQ(d.total, d.hope + d.fear + 2);
    d.hope = 9; d.fear = 6; d.mod = 2; d.total = 17;
    dice_duality_format(&d, buf, sizeof buf, NULL);
    CHECK_EQ(strcmp(buf, "Duality +2 = 17 with Hope  [hope 9, fear 6]"), 0);
    d.hope = 7; d.fear = 7; d.mod = 0; d.total = 14;
    dice_duality_format(&d, buf, sizeof buf, NULL);
    CHECK_EQ(strcmp(buf, "Duality = 14 critical success  [hope 7, fear 7]"), 0);
}

static int count_lines(const char *s)
{
    int n = 0;
    for (; *s; s++) if (*s == '\n') n++;
    return n;
}

void test_session_log(void)
{
    Sandbox sb = sandbox_enter("slog");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "fight.vtt");
    char path[600], logpath[600];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);
    snprintf(logpath, sizeof logpath, "%s/fight.log", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);

    CASE("off by default, and nothing is written while it is");
    CHECK_EQ(slog_on(&a.slog), 0);
    a.ed.cx = 0; a.ed.cy = 0;
    press(&a, "ipAria\r");                    /* place a player at a1 */
    CHECK_EQ(a.map->tokens.n, 1);
    press(&a, "\rl\r");                       /* pick up, a step, drop */
    CHECK(strstr(a.status, "dropped after 1 step") != NULL);
    CHECK(slurp(logpath) == NULL);

    CASE(":log turns it on beside the map, with a header naming it");
    press(&a, ":log\r");
    CHECK_EQ(slog_on(&a.slog), 1);
    CHECK(strstr(a.status, "logging to") != NULL);
    CHECK(strstr(a.status, "fight.log") != NULL);
    char *text = slurp(logpath);
    CHECK(text != NULL);
    if (text) {
        CHECK(strstr(text, "log on:") != NULL);
        CHECK(strstr(text, a.map->name) != NULL);
        free(text);
    }

    CASE("what happens is written, timestamped, one line each");
    press(&a, "\rh\r");                        /* pick up, a step back, drop */
    press(&a, ":roll 2d6+3\r");
    text = slurp(logpath);
    CHECK(text != NULL);
    if (text) {
        CHECK(strstr(text, "] dropped after 1 step") != NULL);
        CHECK(strstr(text, "] 2d6+3 = ") != NULL);
        CHECK_EQ(count_lines(text), 3);         /* header + 2 events */
        CHECK(text[0] == '-');
        CHECK(strchr(text, '[') != NULL && strchr(text, '[')[3] == ':');   /* [HH:MM:SS] */
        free(text);
    }

    CASE("hints and errors stay off the log");
    press(&a, ":roll nonsense\r");
    CHECK(strstr(a.status, "no roll called nonsense") != NULL);
    press(&a, ":roll 2x6\r");
    CHECK(strstr(a.status, ":roll -") != NULL);
    press(&a, "i");                             /* a prefix waiting: a hint */
    press(&a, "\x1b");
    text = slurp(logpath);
    if (text) { CHECK_EQ(count_lines(text), 3); free(text); }

    CASE("a second :log turns it off and says where the file is");
    press(&a, ":log\r");
    CHECK_EQ(slog_on(&a.slog), 0);
    CHECK(strstr(a.status, "log off") != NULL);
    text = slurp(logpath);
    if (text) { CHECK(strstr(text, "log off ---") != NULL); free(text); }
    press(&a, ":roll d6\r");
    text = slurp(logpath);
    if (text) { CHECK_EQ(count_lines(text), 4); free(text); }   /* nothing after the footer */

    CASE(":log on/off and :log path are explicit");
    press(&a, ":log off\r");
    CHECK(strstr(a.status, "already off") != NULL);
    char other[600];
    snprintf(other, sizeof other, "%s/elsewhere.log", sb.dir);
    press(&a, ":log ");
    press(&a, other);
    press(&a, "\r");
    CHECK_EQ(slog_on(&a.slog), 1);
    CHECK_EQ(strcmp(a.slog.path, other), 0);
    press(&a, ":log on\r");                     /* moves to the default path */
    CHECK_EQ(strcmp(a.slog.path, logpath), 0);
    press(&a, ":log on\r");
    CHECK(strstr(a.status, "already logging") != NULL);

    CASE("closing the map closes the log");
    press(&a, ":q!\r");
    CHECK_EQ(slog_on(&a.slog), 0);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* Clocks: named, sized, ticked through the undo log, drawn under the turn
 * order, and saved as version 5. */
void test_clocks(void)
{
    Sandbox sb = sandbox_enter("clocks");
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
    Map *m = a.map;

    CASE("no clocks to begin with, and the panel stays away");
    CHECK_EQ(clock_count(m), 0);
    CHECK_EQ(clock_panel_rows(m), 0);
    press(&a, ":clock\r");
    CHECK(strstr(a.status, "no clocks") != NULL);
    press(&a, ":tick\r");
    CHECK(strstr(a.status, "no clocks") != NULL);

    CASE(":clock NAME SIZE starts one");
    press(&a, ":clock Dragon 6\r");
    CHECK_EQ(clock_count(m), 1);
    CHECK_EQ(m->clocks[0].size, 6);
    CHECK_EQ(m->clocks[0].value, 0);
    CHECK(strstr(a.status, "clock Dragon 0/6 started") != NULL);
    CHECK_EQ(m->modified, 1);

    CASE("a bare :tick fills the clock in hand; the last started or ticked");
    press(&a, ":tick\r");
    CHECK_EQ(m->clocks[0].value, 1);
    CHECK(strstr(a.status, "Dragon 1/6") != NULL);
    press(&a, ":tick 2\r");
    CHECK_EQ(m->clocks[0].value, 3);
    press(&a, ":tick -1\r");
    CHECK_EQ(m->clocks[0].value, 2);
    press(&a, ":tick =5\r");
    CHECK_EQ(m->clocks[0].value, 5);

    CASE("a tick is one undo step");
    press(&a, "u");
    CHECK_EQ(m->clocks[0].value, 2);
    press(&a, "u");
    CHECK_EQ(m->clocks[0].value, 3);
    press(&a, "\x12");                                    /* ctrl-r */
    CHECK_EQ(m->clocks[0].value, 2);

    CASE("filling it says so, and it will not go past full or below empty");
    press(&a, ":tick =6\r");
    CHECK(strstr(a.status, "Dragon 6/6 - full") != NULL);
    press(&a, ":tick\r");
    CHECK(strstr(a.status, "Dragon is full") != NULL);
    CHECK_EQ(m->clocks[0].value, 6);
    press(&a, ":tick =0\r");
    press(&a, ":tick -1\r");
    CHECK(strstr(a.status, "Dragon is at its start") != NULL);
    press(&a, ":tick 3\r");
    press(&a, ":tick reset\r");
    CHECK_EQ(m->clocks[0].value, 0);

    CASE("names match by prefix, case aside, and an exact name beats a longer one");
    press(&a, ":clock Ritual 4\r");
    press(&a, ":clock Rite 8\r");
    CHECK_EQ(clock_count(m), 3);
    press(&a, ":tick dr\r");
    CHECK_EQ(m->clocks[0].value, 1);
    press(&a, ":tick R\r");
    CHECK(strstr(a.status, "more than one clock") != NULL);
    press(&a, ":tick rite\r");
    CHECK_EQ(m->clocks[2].value, 1);
    press(&a, ":tick\r");                                   /* Rite is now in hand */
    CHECK_EQ(m->clocks[2].value, 2);
    press(&a, ":tick Nothing\r");
    CHECK(strstr(a.status, "no clock called") != NULL);

    CASE(":clock lists them; :clock NAME SIZE resizes; :clock NAME remove drops");
    press(&a, ":clock\r");
    CHECK(strstr(a.status, "Dragon 1/6, Ritual 0/4, Rite 2/8") != NULL);
    press(&a, ":clock Rite 2\r");
    CHECK_EQ(m->clocks[2].size, 2);
    CHECK_EQ(m->clocks[2].value, 2);                        /* kept, clamped */
    CHECK(strstr(a.status, "resized") != NULL);
    press(&a, ":clock Ritual off\r");                    /* the old word drops nothing */
    CHECK_EQ(clock_count(m), 3);
    CHECK(strstr(a.status, ":clock Ritual remove drops a clock") != NULL);
    press(&a, ":clock Ritual remove\r");
    CHECK_EQ(clock_count(m), 2);
    CHECK_EQ(m->clocks[1].name[0], '\0');                   /* the slot stays empty */
    press(&a, ":clock Sun 3\r");                            /* and is taken by the next */
    CHECK_EQ(strcmp(m->clocks[1].name, "Sun"), 0);
    press(&a, ":clock 7up 3\r");
    CHECK(strstr(a.status, "starts with a letter") != NULL);
    press(&a, ":clock Big 99\r");
    CHECK(strstr(a.status, "1 to 24 segments") != NULL);

    CASE("an undo recorded against a dropped slot touches nothing, not even its successor");
    press(&a, ":tick Sun\r");                               /* Sun 1/3, in slot 1 */
    press(&a, ":clock Sun remove\r");
    press(&a, "u");                                         /* the tick's op names slot 1, now empty */
    CHECK_EQ(m->clocks[1].name[0], '\0');
    press(&a, ":clock Moon 3\r");                           /* slot 1 again, a new generation */
    press(&a, "\x12");                                      /* redo: Sun's tick must not land on Moon */
    CHECK_EQ(m->clocks[1].value, 0);
    press(&a, "u");
    CHECK_EQ(m->clocks[1].value, 0);

    CASE(":tick NAME = takes a count, not a direction");
    press(&a, ":tick Dragon =2\r");
    CHECK_EQ(m->clocks[0].value, 2);
    press(&a, ":tick Dragon =-1\r");
    CHECK(strstr(a.status, ":tick NAME") != NULL);
    CHECK_EQ(m->clocks[0].value, 2);
    press(&a, ":tick Dragon =1\r");                        /* back to where the cases below expect it */

    CASE("the panel shows the clocks under the turn order, dots for segments, a full one lit");
    press(&a, ":clock Moon remove\r");
    press(&a, ":tick Rite =2\r");
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf frame;
    bb_init(&frame, 65536);
    rnd_dump(&r, &frame);
    bb_putc(&frame, '\0');
    CHECK(strstr(frame.data, "Clocks") != NULL);
    CHECK(strstr(frame.data, "Dragon ●○○○○○") != NULL);
    CHECK(strstr(frame.data, "Rite   ●●") != NULL);
    CHECK(strstr(frame.data, "Turn order") == NULL);       /* no fight: no order block */
    CHECK_EQ(a.ed.view.view.x + a.ed.view.view.w, r.w - TURN_PANEL_W);
    int lit = 0;
    for (int y = 0; y < r.h; y++) {
        const Cell *c = &r.back[(size_t)y * (size_t)r.w + (size_t)(r.w - TURN_PANEL_W + 2)];
        if (c->ch == 'R' && c->fg == a.th->turn) lit++;
    }
    CHECK_EQ(lit, 1);
    bb_free(&frame);

    CASE("in build mode the panel is not drawn");
    Key f1 = { KEY_F1, 0, 0 };
    app_key(&a, f1);
    rnd_begin(&r);
    app_draw(&a);
    CHECK_EQ(a.ed.view.view.x + a.ed.view.view.w, r.w);
    app_key(&a, f2);

    CASE("a wide clock is a fraction, not dots");
    press(&a, ":clock Siege 24\r");
    press(&a, ":tick 5\r");
    rnd_begin(&r);
    app_draw(&a);
    bb_init(&frame, 65536);
    rnd_dump(&r, &frame);
    bb_putc(&frame, '\0');
    CHECK(strstr(frame.data, "Siege  5/24") != NULL);
    bb_free(&frame);

    CASE("clocks are saved as version 5 and read back, in order");
    char err[128];
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    char *text = slurp(path);
    CHECK(text != NULL);
    if (text) {
        CHECK_EQ(strncmp(text, "VTT 5\n", 6), 0);
        CHECK(strstr(text, "clock Dragon 1 6\n") != NULL);
        CHECK(strstr(text, "clock Siege 5 24\n") != NULL);
        free(text);
    }
    Map *back = mapio_load(path, err, sizeof err);
    CHECK(back != NULL);
    if (back) {
        CHECK_EQ(clock_count(back), 3);
        CHECK_EQ(strcmp(back->clocks[0].name, "Dragon"), 0);
        CHECK_EQ(back->clocks[0].value, 1);
        CHECK_EQ(strcmp(back->clocks[1].name, "Siege"), 0);   /* the first empty slot */
        CHECK_EQ(back->clocks[1].size, 24);
        map_free(back);
    }

    /* Daggerheart's countdowns run the other way: they start full and a
     * tick brings them down. The ruleset decides the default, a word after
     * the size decides outright, and a die names the size and rolls the
     * start. Nothing ticks by itself: this is for a table that rolls its
     * own dice. */
    CASE("under daggerheart a new clock counts down: full at the start, done at nothing");
    press(&a, ":ruleset daggerheart\r");
    press(&a, ":clock Ambush 4\r");
    int amb = clock_find(m, "Ambush");
    CHECK(amb >= 0);
    CHECK_EQ(m->clocks[amb].down, 1);
    CHECK_EQ(m->clocks[amb].value, 4);
    CHECK(strstr(a.status, "Ambush 4/4 started - :tick counts it down") != NULL);
    press(&a, ":tick\r");
    CHECK_EQ(m->clocks[amb].value, 3);
    press(&a, ":tick 2\r");
    CHECK_EQ(m->clocks[amb].value, 1);
    press(&a, ":tick -1\r");                               /* back towards the start */
    CHECK_EQ(m->clocks[amb].value, 2);
    press(&a, ":tick =0\r");
    CHECK(strstr(a.status, "Ambush 0/4 - done") != NULL);
    CHECK_EQ(clock_done(&m->clocks[amb]), 1);
    press(&a, ":tick\r");
    CHECK(strstr(a.status, "Ambush is done") != NULL);
    press(&a, "u");
    CHECK_EQ(m->clocks[amb].value, 2);

    CASE("a loop is a reset by hand, and a resize keeps counting the same way");
    press(&a, ":tick =0\r");
    press(&a, ":tick reset\r");
    CHECK_EQ(m->clocks[amb].value, 4);
    press(&a, ":clock Ambush 5\r");                        /* the loop that grows */
    CHECK_EQ(m->clocks[amb].size, 5);
    CHECK_EQ(m->clocks[amb].value, 4);
    CHECK_EQ(m->clocks[amb].down, 1);
    press(&a, ":tick reset\r");
    CHECK_EQ(m->clocks[amb].value, 5);

    CASE("\"up\" and \"down\" after the size say which way, whatever the game");
    press(&a, ":clock Heist 6 up\r");
    int h = clock_find(m, "Heist");
    CHECK_EQ(m->clocks[h].down, 0);
    CHECK_EQ(m->clocks[h].value, 0);
    press(&a, ":clock Ambush 5 up\r");                     /* a change of direction starts over */
    CHECK_EQ(m->clocks[amb].down, 0);
    CHECK_EQ(m->clocks[amb].value, 0);
    press(&a, ":clock Ambush 5 sideways\r");
    CHECK(strstr(a.status, "\"up\" or \"down\"") != NULL);
    press(&a, ":ruleset none\r");
    press(&a, ":clock Fuse 3 down\r");
    CHECK_EQ(m->clocks[clock_find(m, "Fuse")].down, 1);
    CHECK_EQ(m->clocks[clock_find(m, "Fuse")].value, 3);

    CASE("a die for the size starts the clock at the roll");
    dice_seed(3);
    int expect = dice_one(8);
    dice_seed(3);
    press(&a, ":clock Storm d8 down\r");
    int st = clock_find(m, "Storm");
    CHECK_EQ(m->clocks[st].size, 8);
    CHECK_EQ(m->clocks[st].value, expect);
    CHECK(strstr(a.status, "started at the d8's") != NULL);
    CHECK(strstr(a.status, "counting down") != NULL);

    CASE("the direction is saved, and a countdown at nothing is lit");
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    text = slurp(path);
    if (text) {
        CHECK(strstr(text, "clock Fuse 3 3 down\n") != NULL);
        CHECK(strstr(text, "clock Heist 0 6\n") != NULL);
        free(text);
    }
    back = mapio_load(path, err, sizeof err);
    CHECK(back != NULL);
    if (back) {
        int f = clock_find(back, "Fuse");
        CHECK(f >= 0 && back->clocks[f].down == 1 && back->clocks[f].value == 3);
        map_free(back);
    }
    press(&a, ":tick Fuse =0\r");
    rnd_begin(&r);
    app_draw(&a);
    lit = 0;
    for (int y = 0; y < r.h; y++) {
        const Cell *c = &r.back[(size_t)y * (size_t)r.w + (size_t)(r.w - TURN_PANEL_W + 2)];
        if (c->ch == 'F' && c->fg == a.th->turn) lit++;
    }
    CHECK_EQ(lit, 1);
    press(&a, ":clock Ambush remove\r");
    press(&a, ":clock Heist remove\r");
    press(&a, ":clock Fuse remove\r");
    press(&a, ":clock Storm remove\r");

    CASE("with the clocks gone the file is version 3 again");
    press(&a, ":clock Dragon remove\r");
    press(&a, ":clock Rite remove\r");
    press(&a, ":clock Siege remove\r");
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    text = slurp(path);
    if (text) { CHECK_EQ(strncmp(text, "VTT 3\n", 6), 0); free(text); }

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

void test_roll_command(void)
{
    Sandbox sb = sandbox_enter("roll");
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
    dice_seed(5);

    CASE(":roll takes an expression on any map");
    press(&a, ":roll 2d6+3\r");
    CHECK(strstr(a.status, "2d6+3 = ") != NULL);
    press(&a, ":roll 2d12\r");
    CHECK(strstr(a.status, "2d12 = ") != NULL);
    CHECK(strstr(a.status, "Hope") == NULL);    /* plain dice carry no verdict */

    CASE("without a ruleset there is no action roll to be bare about");
    a.map->ruleset[0] = '\0';
    press(&a, ":roll\r");
    CHECK(strstr(a.status, "roll what") != NULL);
    press(&a, ":roll +2\r");
    CHECK(strstr(a.status, "needs a ruleset") != NULL);

    CASE("duality can be asked for by name anywhere");
    press(&a, ":roll duality +1\r");
    CHECK(strstr(a.status, "Duality +1 = ") != NULL);
    CHECK(strstr(a.status, "[hope ") != NULL);

    /* The rules-aware part: under Daggerheart a bare roll is the duality
     * roll -- two d12s, Hope and Fear -- and a modifier rides on it. */
    CASE("under daggerheart a bare :roll is the duality roll");
    press(&a, ":ruleset daggerheart\r");
    press(&a, ":roll\r");
    CHECK(strstr(a.status, "Duality = ") != NULL);
    int verdict = strstr(a.status, "with Hope") != NULL || strstr(a.status, "with Fear") != NULL
               || strstr(a.status, "critical success") != NULL;
    CHECK_EQ(verdict, 1);
    press(&a, ":roll +3\r");
    CHECK(strstr(a.status, "Duality +3 = ") != NULL);
    press(&a, ":roll -1\r");
    CHECK(strstr(a.status, "Duality -1 = ") != NULL);
    press(&a, ":roll +x\r");
    CHECK(strstr(a.status, "modifier is a number") != NULL);
    press(&a, ":roll 2d12\r");                  /* an expression is still plain dice */
    CHECK(strstr(a.status, "2d12 = ") != NULL);
    CHECK(strstr(a.status, "Duality") == NULL);

    /* Gold for Hope and purple for Fear, on the digits themselves. The log
     * and the status text stay plain; only the drawing knows about color. */
    CASE("the hope die is drawn gold and the fear die purple");
    {
        DualityRoll dr = { 12, 3, 2, 17 };
        DualitySpans sp;
        char text[96];
        dice_duality_format(&dr, text, sizeof text, &sp);
        CHECK_EQ(strncmp(text + sp.hope_at, "12", 2), 0);
        CHECK_EQ(sp.hope_len, 2);
        CHECK_EQ(strncmp(text + sp.fear_at, "3]", 2), 0);
        CHECK_EQ(sp.fear_len, 1);

        press(&a, ":roll +2\r");
        CHECK_EQ(a.nstatus_span, 2);
        rnd_begin(&r);
        app_draw(&a);
        int gold = 0, purple = 0;
        for (int x = 0; x < r.w; x++) {
            const Cell *c = &r.back[(size_t)(r.h - 2) * (size_t)r.w + (size_t)x];
            if (c->fg == a.th->hope && c->ch >= '0' && c->ch <= '9') gold++;
            if (c->fg == a.th->fear && c->ch >= '0' && c->ch <= '9') purple++;
        }
        CHECK(gold >= 1 && gold <= 2);
        CHECK(purple >= 1 && purple <= 2);

        press(&a, ":roll 2d6\r");                /* plain dice: no color left behind */
        CHECK_EQ(a.nstatus_span, 0);

        CHECK(contrast(a.th->hope, a.th->bg) > 7.0);
        CHECK(contrast(a.th->fear, a.th->bg) > 5.0);
        CHECK(contrast(a.th->hope, a.th->fear) > 1.8);   /* apart without hue */
    }

    CASE("the total is the dice plus the modifier");
    dice_seed(9);
    DualityRoll d;
    dice_duality(3, &d);
    dice_seed(9);
    press(&a, ":roll +3\r");
    char want[32];
    snprintf(want, sizeof want, "= %d ", d.total);
    CHECK(strstr(a.status, want) != NULL);

    CASE("a roll can be saved under a name, and rolled by it or a prefix of it");
    press(&a, ":rolls\r");
    CHECK(strstr(a.status, "no named rolls") != NULL);
    press(&a, ":roll attack = 2d12+3\r");
    CHECK(strstr(a.status, "attack = 2d12+3") != NULL);
    CHECK_EQ(a.map->modified, 1);
    press(&a, ":roll attack\r");
    CHECK(strstr(a.status, "attack: 2d12+3 = ") != NULL);
    press(&a, ":roll att\r");
    CHECK(strstr(a.status, "attack: 2d12+3 = ") != NULL);
    press(&a, ":roll bite=d8 + 1\r");                 /* spaces around = are optional */
    press(&a, ":rolls\r");
    CHECK(strstr(a.status, "attack = 2d12+3, bite = d8 + 1") != NULL);

    CASE("a saved roll may be the action roll, and keeps its colors");
    press(&a, ":roll swing = duality +2\r");
    press(&a, ":roll swing\r");
    CHECK(strstr(a.status, "swing: Duality +2 = ") != NULL);
    CHECK_EQ(a.nstatus_span, 2);
    CHECK(a.status_span[0].at > 7);                    /* shifted past the name */
    CHECK_EQ(a.status[a.status_span[0].at - 1] != '\0', 1);
    press(&a, ":roll raise = +1\r");
    press(&a, ":roll raise\r");
    CHECK(strstr(a.status, "raise: Duality +1 = ") != NULL);

    CASE("plain dice always win over a name, and a name may not be dice");
    press(&a, ":roll d20 = 3d6\r");
    CHECK(strstr(a.status, "already a roll of its own") != NULL);
    press(&a, ":roll duality = 3d6\r");
    CHECK(strstr(a.status, "already a roll of its own") != NULL);
    press(&a, ":roll 2d12+3\r");
    CHECK(strstr(a.status, "attack:") == NULL);
    press(&a, ":roll bad = 2x6\r");
    CHECK(strstr(a.status, ":roll -") != NULL);
    press(&a, ":roll 7up = d6\r");
    CHECK(strstr(a.status, "starting with a letter") != NULL);
    press(&a, ":roll arrow = d6\r");
    press(&a, ":roll a\r");
    CHECK(strstr(a.status, "more than one roll") != NULL);
    press(&a, ":roll arrow remove\r");
    press(&a, ":roll nothing\r");
    CHECK(strstr(a.status, "no roll called nothing") != NULL);

    CASE("named rolls are saved as version 5 and read back");
    {
        char err[128];
        CHECK_EQ(mapio_save(a.map, path, err, sizeof err), 0);
        char *text = slurp(path);
        if (text) {
            CHECK_EQ(strncmp(text, "VTT 5\n", 6), 0);
            CHECK(strstr(text, "roll attack \"2d12+3\"\n") != NULL);
            CHECK(strstr(text, "roll swing \"duality +2\"\n") != NULL);
            free(text);
        }
        Map *back = mapio_load(path, err, sizeof err);
        CHECK(back != NULL);
        if (back) {
            CHECK_EQ(strcmp(back->rolls[0].name, "attack"), 0);
            CHECK_EQ(strcmp(back->rolls[1].expr, "d8 + 1"), 0);
            map_free(back);
        }
    }

    CASE(":roll NAME remove removes the roll; NAME = with nothing after it only asks");
    press(&a, ":roll bite =\r");
    CHECK(strstr(a.status, ":roll bite remove removes it") != NULL);
    press(&a, ":roll bite\r");
    CHECK(strstr(a.status, "no roll called bite") == NULL);   /* still there */
    press(&a, ":roll bite remove\r");
    CHECK(strstr(a.status, "removed roll bite") != NULL);
    press(&a, ":roll bite\r");
    CHECK(strstr(a.status, "no roll called bite") != NULL);
    press(&a, ":roll att remove\r");                  /* a prefix will not do for removing */
    CHECK(strstr(a.status, "no roll called att") != NULL);
    {
        int kept = 0;
        for (int i = 0; i < ROLL_MAX; i++) kept |= !strcmp(a.map->rolls[i].name, "attack");
        CHECK(kept);
    }

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* ---------------------------------------------------------- turn order */

void test_turns(void)
{
    Map *m = map_new(12, 8, "turns");
    map_fill_tiles(m, 0, 0, 11, 7, TILE_FLOOR);
    Undo u;
    undo_init(&u);

    Token aria = { 1, 1, 1, TOKEN_PLAYER, "Aria" };
    Token ogre = { 5, 1, 2, TOKEN_ENEMY,  "Ogre" };
    Token bram = { 1, 3, 1, TOKEN_PLAYER, "Bram" };
    Token dax  = { 1, 5, 1, TOKEN_PLAYER, "Dax" };
    Token eel  = { 8, 5, 1, TOKEN_ENEMY,  "Eel" };
    tokens_add(&m->tokens, aria);   /* 0 */
    tokens_add(&m->tokens, ogre);   /* 1 */
    tokens_add(&m->tokens, bram);   /* 2 */
    tokens_add(&m->tokens, dax);    /* 3 */
    tokens_add(&m->tokens, eel);    /* 4 */

    /* Nobody has rolled initiative: the walk is the list, exactly as the
     * cycle keys always went. */
    CASE("with no fight the walk is list order, either way, per kind");
    CHECK_EQ(turn_count(m), 0);
    CHECK_EQ(turn_acting(m), -1);
    CHECK_EQ(turn_walk(m, -1, 1, TOKEN_ANY_KIND), 0);
    CHECK_EQ(turn_walk(m, 0, 1, TOKEN_ANY_KIND), 1);
    CHECK_EQ(turn_walk(m, 4, 1, TOKEN_ANY_KIND), 0);           /* wraps */
    CHECK_EQ(turn_walk(m, 0, -1, TOKEN_ANY_KIND), 4);
    CHECK_EQ(turn_walk(m, 1, 1, TOKEN_ENEMY), 4);
    CHECK_EQ(turn_walk(m, 3, 1, TOKEN_PLAYER), 0);
    CHECK_EQ(turn_advance(m, &u, 1), TURN_NO_ORDER);
    char buf[200];
    turn_status(m, buf, sizeof buf);
    CHECK_EQ(buf[0], '\0');

    CASE("joining gives a creature a number; highest first, ties by who was placed first");
    turn_join(m, &u, 2, 15);
    turn_join(m, &u, 1, 15);
    turn_join(m, &u, 0, 18);
    CHECK_EQ(turn_count(m), 3);
    CHECK_EQ(turn_walk(m, -1, 1, TOKEN_ANY_KIND), 0);           /* Aria 18 */
    CHECK_EQ(turn_walk(m, 0, 1, TOKEN_ANY_KIND), 1);            /* Ogre 15, placed before... */
    CHECK_EQ(turn_walk(m, 1, 1, TOKEN_ANY_KIND), 2);            /* ...Bram 15 */
    CHECK_EQ(turn_walk(m, 2, 1, TOKEN_ANY_KIND), 3);            /* then those not in it */
    CHECK_EQ(turn_walk(m, 4, 1, TOKEN_ANY_KIND), 0);
    CHECK_EQ(turn_walk(m, 0, 1, TOKEN_PLAYER), 2);              /* the friendly track skips the ogre */
    CHECK_EQ(turn_walk(m, 0, -1, TOKEN_ANY_KIND), 4);
    turn_status(m, buf, sizeof buf);
    CHECK(strstr(buf, "3 in the order") != NULL);

    CASE("the number can change, and the order with it");
    turn_join(m, &u, 2, 20);                                    /* Bram jumps the queue */
    CHECK_EQ(turn_walk(m, -1, 1, TOKEN_ANY_KIND), 2);
    turn_join(m, &u, 2, 15);
    CHECK_EQ(turn_walk(m, -1, 1, TOKEN_ANY_KIND), 0);

    CASE("the first advance starts round 1 at the top; a lap is a new round");
    CHECK_EQ(turn_advance(m, &u, 1), 0);
    CHECK_EQ(m->round, 1);
    CHECK_EQ(turn_acting(m), 0);
    CHECK_EQ(turn_advance(m, &u, 1), 1);
    CHECK_EQ(turn_advance(m, &u, 1), 2);
    CHECK_EQ(m->round, 1);
    CHECK_EQ(turn_advance(m, &u, 1), 0);
    CHECK_EQ(m->round, 2);
    int acting = 0;
    for (int i = 0; i < m->tokens.n; i++) if (m->tokens.v[i].turn & TURN_ACTING) acting++;
    CHECK_EQ(acting, 1);

    CASE("the readouts name the round, the actor and who is next");
    turn_status(m, buf, sizeof buf);
    CHECK_EQ(strcmp(buf, "Round 2 - Aria's turn, then Ogre, Bram"), 0);
    turn_list(m, buf, sizeof buf);
    CHECK_EQ(strcmp(buf, "Round 2: Aria 18*, Ogre 15, Bram 15"), 0);

    CASE("stepping back unwinds the lap, and stops at the start of the fight");
    CHECK_EQ(turn_advance(m, &u, -1), 2);
    CHECK_EQ(m->round, 1);
    CHECK_EQ(turn_advance(m, &u, -2), 0);
    CHECK_EQ(turn_advance(m, &u, -1), TURN_AT_START);
    CHECK_EQ(turn_acting(m), 0);
    CHECK_EQ(m->round, 1);
    CHECK_EQ(turn_advance(m, &u, 2), 2);
    CHECK_EQ(turn_advance(m, &u, -5), TURN_AT_START);           /* all or nothing */
    CHECK_EQ(turn_acting(m), 2);

    CASE("a count moves several places, laps and all");
    CHECK_EQ(turn_advance(m, &u, 4), 0);                        /* Bram -> Aria, Ogre, Bram, Aria */
    CHECK_EQ(m->round, 3);

    CASE("an advance is one undo step, round included");
    CHECK_EQ(turn_advance(m, &u, 1), 1);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(turn_acting(m), 0);
    CHECK_EQ(m->round, 3);
    CHECK_EQ(undo_undo(&u, m), 1);                              /* the count-of-four advance */
    CHECK_EQ(turn_acting(m), 2);
    CHECK_EQ(m->round, 1);
    CHECK_EQ(undo_redo(&u, m), 1);
    CHECK_EQ(turn_acting(m), 0);
    CHECK_EQ(m->round, 3);

    /* A game that passes a spotlight needs only this half. */
    CASE("the turn can be handed to anyone, in the order or not");
    turn_take(m, &u, 3);                                        /* Dax: not in the order */
    CHECK_EQ(turn_acting(m), 3);
    CHECK_EQ(m->tokens.v[0].turn & TURN_ACTING, 0);
    turn_status(m, buf, sizeof buf);
    CHECK_EQ(strcmp(buf, "Round 3 - Dax's turn"), 0);
    CHECK_EQ(turn_advance(m, &u, -1), TURN_AT_START);           /* no place to step back from */
    CHECK_EQ(turn_advance(m, &u, 1), 0);                        /* on from an outsider: the top */
    CHECK_EQ(m->round, 3);                                      /* and not a lap */
    turn_take(m, &u, 2);
    CHECK_EQ(turn_advance(m, &u, 1), 0);                        /* from Bram, the last: a lap */
    CHECK_EQ(m->round, 4);

    CASE("leaving the order on your own turn passes it on first");
    turn_take(m, &u, 1);
    turn_leave(m, &u, 1);
    CHECK_EQ(m->tokens.v[1].turn, 0);
    CHECK_EQ(turn_acting(m), 2);
    CHECK_EQ(turn_count(m), 2);
    CHECK_EQ(undo_undo(&u, m), 1);                              /* one step back: all of it */
    CHECK_EQ(turn_acting(m), 1);
    CHECK_EQ(turn_count(m), 3);

    CASE("removing the actor passes the turn, in the same undo step");
    turn_take(m, &u, 2);                                        /* Bram, last in the order */
    int round_before = m->round;
    undo_begin(&u);
    turn_before_remove(m, &u, 2);
    undo_del_token(&u, m, 2);
    turn_settle(m, &u);
    undo_end(&u);
    CHECK_EQ(m->tokens.n, 4);
    CHECK_EQ(turn_acting(m), 0);                                /* round the corner to Aria */
    CHECK_EQ(m->round, round_before + 1);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(m->tokens.n, 5);
    CHECK_EQ(turn_acting(m), 2);
    CHECK_EQ(m->round, round_before);
    CHECK_EQ(strcmp(m->tokens.v[2].label, "Bram"), 0);
    CHECK_EQ(m->tokens.v[2].init, 15);

    char  err[128];
    char *text = NULL;
    Map  *back = NULL;

    CASE("the last one out ends the fight");
    Map *solo = map_new(4, 4, "solo");
    tokens_add(&solo->tokens, aria);
    Undo su;
    undo_init(&su);
    turn_join(solo, &su, 0, 10);
    CHECK_EQ(turn_advance(solo, &su, 1), 0);
    CHECK_EQ(turn_advance(solo, &su, 1), 0);                    /* alone: every turn is a lap */
    CHECK_EQ(solo->round, 2);
    turn_status(solo, buf, sizeof buf);
    CHECK_EQ(strcmp(buf, "Round 2 - Aria's turn"), 0);
    turn_leave(solo, &su, 0);
    CHECK_EQ(turn_acting(solo), -1);
    CHECK_EQ(solo->round, 0);
    undo_free(&su);
    map_free(solo);

    CASE("ending the fight clears everyone, and is one step to take back");
    CHECK_EQ(turn_clear(m, &u), 3);
    CHECK_EQ(turn_count(m), 0);
    CHECK_EQ(turn_acting(m), -1);
    CHECK_EQ(m->round, 0);
    CHECK_EQ(undo_undo(&u, m), 1);
    CHECK_EQ(turn_count(m), 3);
    CHECK_EQ(turn_acting(m), 2);
    CHECK_EQ(m->round, round_before);

    /* A fight is combat state, like the markers version 3 was for: a file
     * that holds one says 4 so an older build refuses it instead of quietly
     * dropping whose turn it is. A map with no fight still says 3. */
    /* A game with no initiative: the turn is a side. */
    CASE("under a spotlight ruleset the turn passes between the players and the GM");
    str_lcpy(m->ruleset, "daggerheart", sizeof m->ruleset);
    turn_clear(m, &u);
    CHECK_EQ(turn_spotlight_ruleset(m), 1);
    CHECK_EQ(m->spotlight, SPOTLIGHT_PLAYERS);
    turn_status(m, buf, sizeof buf);
    CHECK_EQ(strcmp(buf, "Players' spotlight"), 0);
    turn_flip_spotlight(m, &u);
    CHECK_EQ(m->spotlight, SPOTLIGHT_GM);
    turn_status(m, buf, sizeof buf);
    CHECK_EQ(strcmp(buf, "GM spotlight"), 0);
    turn_take(m, &u, 0);                                        /* Aria, a player */
    CHECK_EQ(m->spotlight, SPOTLIGHT_PLAYERS);                  /* the side follows the creature */
    turn_status(m, buf, sizeof buf);
    CHECK_EQ(strcmp(buf, "Players' spotlight - Aria"), 0);
    turn_take(m, &u, 1);                                        /* the ogre */
    CHECK_EQ(m->spotlight, SPOTLIGHT_GM);
    turn_flip_spotlight(m, &u);                                 /* across, and nobody holds it */
    CHECK_EQ(m->spotlight, SPOTLIGHT_PLAYERS);
    CHECK_EQ(turn_acting(m), -1);
    CHECK_EQ(undo_undo(&u, m), 1);                              /* one step: side and holder */
    CHECK_EQ(m->spotlight, SPOTLIGHT_GM);
    CHECK_EQ(turn_acting(m), 1);
    CHECK_EQ(turn_panel_wanted(m), 1);

    CASE("a spotlight fight is version 4 too, and ends with the fight");
    CHECK_EQ(mapio_save(m, "/tmp/vtt-spot.vtt", err, sizeof err), 0);
    text = slurp("/tmp/vtt-spot.vtt");
    if (text) {
        CHECK_EQ(strncmp(text, "VTT 4\n", 6), 0);
        CHECK(strstr(text, "spotlight gm\n") != NULL);
        free(text);
    }
    back = mapio_load("/tmp/vtt-spot.vtt", err, sizeof err);
    CHECK(back != NULL);
    if (back) { CHECK_EQ(back->spotlight, SPOTLIGHT_GM); CHECK_EQ(turn_acting(back), 1); map_free(back); }
    unlink("/tmp/vtt-spot.vtt");
    turn_clear(m, &u);
    CHECK_EQ(m->spotlight, SPOTLIGHT_PLAYERS);
    m->ruleset[0] = '\0';
    CHECK_EQ(turn_panel_wanted(m), 0);

    /* Back on numbers for the file tests below. */
    turn_join(m, &u, 0, 18); turn_join(m, &u, 1, 15); turn_join(m, &u, 2, 15);
    turn_advance(m, &u, 3);

    CASE("a fight round-trips through the file, as version 4");
    turn_take(m, &u, 3);                                        /* an outsider holds the turn */
    char path[128];
    snprintf(path, sizeof path, "/tmp/vtt-turns-%ld.vtt", (long)getpid());
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    text = slurp(path);
    CHECK(text != NULL);
    if (text) {
        CHECK_EQ(strncmp(text, "VTT 4\n", 6), 0);
        CHECK(strstr(text, "tokenturn 18\n") != NULL);
        CHECK(strstr(text, "tokenturn - acting\n") != NULL);
        char want[32];
        snprintf(want, sizeof want, "round %d\n", m->round);
        CHECK(strstr(text, want) != NULL);
        free(text);
    }
    back = mapio_load(path, err, sizeof err);
    CHECK(back != NULL);
    if (back) {
        CHECK_EQ(back->round, m->round);
        CHECK_EQ(back->tokens.n, m->tokens.n);
        for (int i = 0; i < m->tokens.n && i < back->tokens.n; i++)
            CHECK_EQ(token_equal(&back->tokens.v[i], &m->tokens.v[i]), 1);
        map_free(back);
    }

    CASE("a map with no fight is still written as version 3");
    turn_clear(m, &u);
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    text = slurp(path);
    if (text) {
        CHECK_EQ(strncmp(text, "VTT 3\n", 6), 0);
        CHECK(strstr(text, "tokenturn") == NULL);
        CHECK(strstr(text, "round") == NULL);
        free(text);
    }

    CASE("a file claiming two actors loads with one");
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) {
        fputs("VTT 4\nname x\nsize 2 2\nzoom 1\ntiles\n..\n..\nvedges\n   \n   \nhedges\n  \n  \n  \n"
              "token player 0 0 1 \"A\"\ntokenturn 12 acting\n"
              "token enemy 1 0 1 \"B\"\ntokenturn 9 acting\n"
              "token enemy 1 1 1 \"C\"\ntokenturn nonsense\nround 5\n", f);
        fclose(f);
        back = mapio_load(path, err, sizeof err);
        CHECK(back != NULL);
        if (back) {
            CHECK_EQ(turn_acting(back), 0);
            CHECK_EQ(back->tokens.v[1].turn, TURN_IN);
            CHECK_EQ(back->tokens.v[2].turn, 0);                /* a bad number joins nothing */
            CHECK_EQ(back->round, 5);
            map_free(back);
        }
    }
    unlink(path);

    undo_free(&u);
    map_free(m);
}

void test_turn_keys(void)
{
    Sandbox sb = sandbox_enter("turnkeys");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    write_map_file(sb.dir, "fight.vtt");
    char path[600];
    snprintf(path, sizeof path, "%s/fight.vtt", sb.dir);

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 100, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    Key f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);

    a.ed.cx = 0; a.ed.cy = 0; press(&a, "ipAria\r");    /* 0 */
    a.ed.cx = 1; a.ed.cy = 0; press(&a, "ieOgre\r");    /* 1 */
    a.ed.cx = 0; a.ed.cy = 1; press(&a, "ipBram\r");    /* 2 */
    CHECK_EQ(a.map->tokens.n, 3);

    CASE("a with no order says how to make one; a and A are no longer retired");
    press(&a, "a");
    CHECK(strstr(a.status, "no turn order") != NULL);
    CHECK(strstr(a.status, "s i") != NULL);
    press(&a, "A");
    CHECK(strstr(a.status, "gone") == NULL);

    CASE("s i prompts for a number and puts the creature in the order");
    press(&a, "si");                                     /* Bram is still selected */
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK_EQ(a.prompt_what, PROMPT_INITIATIVE);
    press(&a, "9\r");
    CHECK_EQ(a.map->tokens.v[2].turn, TURN_IN);
    CHECK_EQ(a.map->tokens.v[2].init, 9);
    CHECK(strstr(a.status, "Bram joins the turn order at 9") != NULL);
    play_focus(&a.play, 0); press(&a, "si18\r");
    play_focus(&a.play, 1); press(&a, "si12\r");
    press(&a, "sinope\r");
    CHECK(strstr(a.status, "initiative is a number") != NULL);
    CHECK_EQ(a.map->tokens.v[1].init, 12);
    press(&a, "si");                                     /* the prompt opens on the old number */
    CHECK_EQ(strcmp(a.prompt.buf, "12"), 0);
    press(&a, "\x1b");

    CASE("t, f and e walk in turn order once there is one");
    play_focus(&a.play, -1);
    press(&a, "t"); CHECK_EQ(a.play.sel, 0);             /* Aria 18 */
    press(&a, "t"); CHECK_EQ(a.play.sel, 1);             /* Ogre 12 */
    press(&a, "t"); CHECK_EQ(a.play.sel, 2);             /* Bram 9 */
    press(&a, "T"); CHECK_EQ(a.play.sel, 1);
    press(&a, "f"); CHECK_EQ(a.play.sel, 2);
    press(&a, "f"); CHECK_EQ(a.play.sel, 0);
    CHECK_EQ(turn_acting(a.map), -1);                    /* looking is free: nobody's turn yet */

    CASE("a moves the fight on, selects whoever is up, and says so up top");
    press(&a, "a");
    CHECK_EQ(turn_acting(a.map), 0);
    CHECK_EQ(a.play.sel, 0);
    CHECK_EQ(a.map->round, 1);
    CHECK(strstr(a.status, "round 1 - Aria's turn") != NULL);
    press(&a, "2a");
    CHECK_EQ(turn_acting(a.map), 2);
    CHECK_EQ(a.play.sel, 2);
    press(&a, "a");
    CHECK_EQ(a.map->round, 2);
    rnd_begin(&r);
    app_draw(&a);
    ByteBuf frame;
    bb_init(&frame, 32768);
    rnd_dump(&r, &frame);
    bb_putc(&frame, '\0');
    CHECK(strstr(frame.data, "Round 2 - Aria's turn, then Ogre, Bram") != NULL);
    bb_free(&frame);

    CASE("the actor wears the turn color above and below");
    int bars = 0;
    for (size_t i = 0; i < r.ncells; i++) if (r.back[i].fg == a.th->turn) bars++;
    CHECK(bars >= 6);
    CHECK(contrast(a.th->turn, a.th->bg) > 10.0);

    CASE("A steps back, u takes an advance back, and the start is the start");
    press(&a, "A");
    CHECK_EQ(turn_acting(a.map), 2);
    CHECK_EQ(a.map->round, 1);
    press(&a, "a");
    press(&a, "u");
    CHECK_EQ(turn_acting(a.map), 2);
    CHECK_EQ(a.map->round, 1);
    press(&a, "9A");
    CHECK(strstr(a.status, "start of the fight") != NULL);
    CHECK_EQ(turn_acting(a.map), 2);

    CASE("s t hands the turn over out of order");
    play_focus(&a.play, 1);
    press(&a, "st");
    CHECK_EQ(turn_acting(a.map), 1);
    CHECK(strstr(a.status, "Ogre takes the turn") != NULL);

    CASE("the turn cannot move while a creature is in hand");
    a.ed.cx = 1; a.ed.cy = 0;                            /* the cursor, not just the focus */
    press(&a, "\r");                                     /* pick the ogre up */
    CHECK_EQ(a.play.sel, 1);
    CHECK_EQ(a.play.grabbed, 1);
    press(&a, "a");
    CHECK(strstr(a.status, "put it down first") != NULL);
    CHECK_EQ(turn_acting(a.map), 1);
    press(&a, "\x1b");

    CASE("removing the actor passes the turn; u brings both back");
    press(&a, "d");
    CHECK_EQ(a.map->tokens.n, 2);
    CHECK_EQ(turn_acting(a.map), 1);                     /* Bram, who was next, now index 1 */
    CHECK_EQ(strcmp(a.map->tokens.v[1].label, "Bram"), 0);
    press(&a, "u");
    CHECK_EQ(a.map->tokens.n, 3);
    CHECK_EQ(turn_acting(a.map), 1);
    CHECK_EQ(strcmp(a.map->tokens.v[1].label, "Ogre"), 0);

    CASE("a copy keeps its number and never the turn");
    a.ed.cx = 1; a.ed.cy = 0;
    play_focus(&a.play, 1);
    press(&a, "y");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "p");
    CHECK_EQ(a.map->tokens.n, 4);
    CHECK_EQ(a.map->tokens.v[3].turn, TURN_IN);
    CHECK_EQ(a.map->tokens.v[3].init, 12);
    int actors = 0;
    for (int i = 0; i < a.map->tokens.n; i++) if (a.map->tokens.v[i].turn & TURN_ACTING) actors++;
    CHECK_EQ(actors, 1);

    CASE("a blank answer to s i leaves the order");
    play_focus(&a.play, 3);
    press(&a, "si\025\r");                               /* ctrl-u clears the old number */
    CHECK_EQ(a.map->tokens.v[3].turn, 0);
    CHECK(strstr(a.status, "leaves the turn order") != NULL);

    CASE(":turns reads the order out, :turns end ends the fight, u undoes that too");
    press(&a, ":turns\r");
    CHECK(strstr(a.status, "Aria 18, Ogre 12*, Bram 9") != NULL);
    press(&a, ":turns off\r");                           /* the old word ends nothing */
    CHECK(turn_count(a.map) > 0);
    CHECK(strstr(a.status, ":turns end ends the fight") != NULL);
    press(&a, ":turns end\r");
    CHECK_EQ(turn_count(a.map), 0);
    CHECK_EQ(a.map->round, 0);
    CHECK(strstr(a.status, "the fight is over") != NULL);
    press(&a, ":turns end\r");
    CHECK(strstr(a.status, "no fight to end") != NULL);
    press(&a, ":turns\r");
    CHECK(strstr(a.status, "no turn order") != NULL);
    press(&a, "u");
    CHECK_EQ(turn_count(a.map), 3);
    CHECK_EQ(turn_acting(a.map), 1);

    CASE("the panel appears with the fight, takes its width from the map, and can be turned off");
    press(&a, ":turns end\r");                           /* a known fight: Aria 18, Ogre 12 */
    play_focus(&a.play, 0); press(&a, "si18\r");
    play_focus(&a.play, 1); press(&a, "si12\r");
    CHECK_EQ(a.map->tokens.n, 4);
    press(&a, "a");
    rnd_begin(&r);
    app_draw(&a);
    CHECK_EQ(a.ed.view.view.x + a.ed.view.view.w, r.w - TURN_PANEL_W);
    bb_init(&frame, 32768);
    rnd_dump(&r, &frame);
    bb_putc(&frame, '\0');
    CHECK(strstr(frame.data, "Turn order") != NULL);
    CHECK(strstr(frame.data, "Round 1") != NULL);
    CHECK(strstr(frame.data, "\u25b6  18  Aria") != NULL);
    CHECK(strstr(frame.data, "   12  Ogre") != NULL);
    CHECK(strstr(frame.data, "2 not in the fight") != NULL);
    bb_free(&frame);
    press(&a, ":panel off\r");
    rnd_begin(&r);
    app_draw(&a);
    CHECK_EQ(a.ed.view.view.x + a.ed.view.view.w, r.w);
    press(&a, ":panel\r");
    rnd_begin(&r);
    app_draw(&a);
    CHECK_EQ(a.ed.view.view.x + a.ed.view.view.w, r.w - TURN_PANEL_W);
    press(&a, ":turns end\r");
    rnd_begin(&r);
    app_draw(&a);
    CHECK_EQ(a.ed.view.view.x + a.ed.view.view.w, r.w);   /* no fight, no panel */
    press(&a, "u");

    /* Daggerheart: no numbers, a passes the spotlight across, s t hands it
     * to a creature and the side follows, and the panel shows the sides. */
    CASE("under daggerheart a passes the spotlight and the panel shows the sides");
    press(&a, ":turns end\r");
    press(&a, ":ruleset daggerheart\r");
    rnd_begin(&r);
    app_draw(&a);
    bb_init(&frame, 32768);
    rnd_dump(&r, &frame);
    bb_putc(&frame, '\0');
    CHECK(strstr(frame.data, "Spotlight") != NULL);
    CHECK(strstr(frame.data, "\u25b6 Players") != NULL);
    CHECK(strstr(frame.data, "Players' spotlight") != NULL);   /* the title bar too */
    bb_free(&frame);
    press(&a, "a");
    CHECK_EQ(a.map->spotlight, SPOTLIGHT_GM);
    CHECK(strstr(a.status, "GM has the spotlight") != NULL);
    press(&a, "3A");                                     /* a count means nothing here */
    CHECK_EQ(a.map->spotlight, SPOTLIGHT_PLAYERS);
    play_focus(&a.play, 1);                              /* the ogre */
    press(&a, "st");
    CHECK_EQ(a.map->spotlight, SPOTLIGHT_GM);
    rnd_begin(&r);
    app_draw(&a);
    bb_init(&frame, 32768);
    rnd_dump(&r, &frame);
    bb_putc(&frame, '\0');
    CHECK(strstr(frame.data, "\u25b6 GM") != NULL);
    CHECK(strstr(frame.data, "    Ogre") != NULL);
    CHECK(strstr(frame.data, "GM spotlight - Ogre") != NULL);
    bb_free(&frame);
    press(&a, "u");
    CHECK_EQ(a.map->spotlight, SPOTLIGHT_PLAYERS);
    press(&a, ":ruleset none\r");

    CASE("the s prefix lists its new members");
    press(&a, "s");
    CHECK(strstr(a.status, "initiative") != NULL);
    press(&a, "z");
    CHECK(strstr(a.status, "i initiative") != NULL);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* ------------------------------------------------------------------ scenes */

static Token scene_token(int x, int y, int size, uint8_t kind, const char *label)
{
    Token t;
    memset(&t, 0, sizeof t);
    t.x = (int16_t)x; t.y = (int16_t)y; t.size = (uint8_t)size; t.kind = kind;
    str_lcpy(t.label, label, sizeof t.label);
    return t;
}

/* Do the map's creatures equal the list, one for one, in order? */
static int tokens_same(const TokenList *a, const TokenList *b)
{
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++)
        if (!token_equal(&a->v[i], &b->v[i])) return 0;
    return 1;
}

static TokenList tokens_copy(const TokenList *l)
{
    TokenList c;
    memset(&c, 0, sizeof c);
    for (int i = 0; i < l->n; i++) tokens_add(&c, l->v[i]);
    return c;
}

void test_scenes(void)
{
    Map *m = map_new(12, 8, "Scenes");
    map_fill_tiles(m, 0, 0, 11, 7, TILE_FLOOR);
    Undo u;
    undo_init(&u);
    char err[200];

    Token aria = scene_token(1, 1, 1, TOKEN_PLAYER, "Aria");
    aria.ncounters = 1;
    str_lcpy(aria.counters[0].name, "HP", sizeof aria.counters[0].name);
    aria.counters[0].value = 6; aria.counters[0].max = 6;
    tokens_add(&m->tokens, aria);
    Token ogre = scene_token(6, 4, 2, TOKEN_ENEMY, "Ogre");
    ogre.hidden = 1;
    token_add_status(&ogre, 0, "enraged");
    tokens_add(&m->tokens, ogre);
    tokens_add(&m->tokens, scene_token(9, 1, 1, TOKEN_ENEMY, ""));
    turn_join(m, &u, 0, 18);
    turn_join(m, &u, 1, 12);
    turn_take(m, &u, 0);                                   /* Aria acts: the fight has begun */
    undo_set_round(&u, m, 1);
    m->spotlight = SPOTLIGHT_GM;

    CASE("names: 1-31 characters, spaces inside, no quote; found ignoring case");
    CHECK(scene_name_ok("before the ambush"));
    CHECK(!scene_name_ok("") && !scene_name_ok(" lead") && !scene_name_ok("trail ") && !scene_name_ok("a\"b"));
    char longname[40];
    memset(longname, 'x', 32); longname[32] = '\0';
    CHECK(!scene_name_ok(longname));
    CHECK(scene_save(m, "a\"b", NULL, err, sizeof err) < 0 && strstr(err, "no quote"));
    CHECK(!scene_name_ok("save") && !scene_name_ok("Save me") && !scene_name_ok("diff x"));
    CHECK(scene_name_ok("saved game") && scene_name_ok("differently"));

    CASE("save keeps every creature as it stands, and the round and spotlight");
    unsigned gen = m->gen;
    int s0 = scene_save(m, "Before", NULL, err, sizeof err);
    CHECK_EQ(s0, 0);
    CHECK(m->gen != gen && m->modified);
    CHECK(tokens_same(&m->scenes[0].tokens, &m->tokens));
    CHECK(m->scenes[0].round == m->round && m->round >= 1);
    CHECK_EQ(m->scenes[0].spotlight, SPOTLIGHT_GM);
    CHECK_EQ(scene_find(m, "BEFORE"), 0);
    TokenList saved = tokens_copy(&m->tokens);
    int round0 = m->round;

    CASE("diff with nothing changed says nothing");
    {
        char *buf = NULL; size_t n = 0;
        FILE *f = open_memstream(&buf, &n);
        CHECK_EQ(scene_diff(f, m, 0), 0);
        fclose(f);
        free(buf);
    }

    CASE("the fight goes on: moves, a death, a newcomer, wounds, markers, a new round");
    undo_move_token(&u, m, 0, 3, 3);
    Token hurt = m->tokens.v[0];
    hurt.counters[0].value = 2;
    undo_edit_token(&u, m, 0, hurt);
    Token calm = m->tokens.v[1];
    token_clear_status(&calm);
    calm.hidden = 0;
    undo_edit_token(&u, m, 1, calm);
    undo_del_token(&u, m, 2);
    undo_add_token(&u, m, scene_token(0, 7, 1, TOKEN_ENEMY, "Imp"));
    undo_set_round(&u, m, round0 + 2);
    undo_set_spotlight(&u, m, SPOTLIGHT_PLAYERS);

    CASE("diff says each: moved, changed, gone, new, the round and spotlight");
    {
        char *buf = NULL; size_t n = 0;
        FILE *f = open_memstream(&buf, &n);
        int lines = scene_diff(f, m, 0);
        fclose(f);
        CHECK_EQ(lines, 7);
        CHECK(strstr(buf, "moved \"Aria\" B2 -> D4\n") != NULL);
        CHECK(strstr(buf, "changed \"Aria\": HP 2/6 (was 6/6)\n") != NULL);
        CHECK(strstr(buf, "changed \"Ogre\": markers none (was enraged), hidden no (was yes)\n") != NULL);
        CHECK(strstr(buf, "gone (enemy) at J2\n") != NULL);
        CHECK(strstr(buf, "new \"Imp\" at A8\n") != NULL);
        char want[48];
        snprintf(want, sizeof want, "round %d (was %d)\n", round0 + 2, round0);
        CHECK(strstr(buf, want) != NULL);
        CHECK(strstr(buf, "spotlight players (was gm)\n") != NULL);
        free(buf);
    }

    CASE("putting it back: every creature as saved, the round and spotlight; one undo step");
    TokenList before = tokens_copy(&m->tokens);
    int depth = u.depth;
    CHECK_EQ(scene_restore(m, &u, 0, err, sizeof err), 3);
    CHECK(tokens_same(&m->tokens, &saved));
    CHECK_EQ(m->round, round0);
    CHECK_EQ(m->spotlight, SPOTLIGHT_GM);
    CHECK_EQ(u.depth, depth + 1);
    CHECK(undo_balanced(&u));

    CASE("u takes the whole of it back, redo puts it back again");
    undo_undo(&u, m);
    CHECK(tokens_same(&m->tokens, &before));
    CHECK_EQ(m->round, round0 + 2);
    undo_redo(&u, m);
    CHECK(tokens_same(&m->tokens, &saved));
    tokens_free(&before);

    CASE("a boxed scene: only the creatures meeting the box, and only they go back");
    int box[4] = { 5, 3, 8, 6 };                           /* round the Ogre */
    int s1 = scene_save(m, "Ravine", box, err, sizeof err);
    CHECK_EQ(s1, 1);
    CHECK(m->scenes[1].boxed && m->scenes[1].tokens.n == 1);
    CHECK(!strcmp(m->scenes[1].tokens.v[0].label, "Ogre"));
    undo_move_token(&u, m, 0, 10, 0);                      /* Aria wanders off, outside */
    undo_move_token(&u, m, 1, 1, 5);                       /* the Ogre leaves the ravine */
    undo_add_token(&u, m, scene_token(7, 5, 1, TOKEN_ENEMY, "Rat"));   /* a rat moves in */
    int ogres = 0;
    CHECK(scene_restore(m, &u, 1, err, sizeof err) == 1);
    for (int i = 0; i < m->tokens.n; i++) ogres += !strcmp(m->tokens.v[i].label, "Ogre");
    CHECK_EQ(ogres, 2);                                    /* the one outside stays: it no longer meets the box */
    int rat = -1, aria_i = -1;
    for (int i = 0; i < m->tokens.n; i++) {
        if (!strcmp(m->tokens.v[i].label, "Rat")) rat = i;
        if (!strcmp(m->tokens.v[i].label, "Aria")) aria_i = i;
    }
    CHECK_EQ(rat, -1);                                     /* in the box, so replaced */
    CHECK(aria_i >= 0 && m->tokens.v[aria_i].x == 10);     /* outside, untouched */

    CASE("a boxed scene keeps the turn with a creature outside that holds it");
    undo_undo(&u, m);                                      /* before that restore */
    int acting = turn_acting(m);
    CHECK(acting >= 0 && !strcmp(m->tokens.v[acting].label, "Aria"));
    Map *m2 = map_new(12, 8, "Turns");
    map_fill_tiles(m2, 0, 0, 11, 7, TILE_FLOOR);
    tokens_add(&m2->tokens, scene_token(0, 0, 1, TOKEN_PLAYER, "Aria"));
    tokens_add(&m2->tokens, scene_token(6, 4, 1, TOKEN_ENEMY, "Wolf"));
    turn_join(m2, &u, 0, 5);
    turn_join(m2, &u, 1, 10);
    turn_take(m2, &u, 1);                                  /* the wolf acts when saved */
    int wbox[4] = { 5, 3, 8, 6 };
    CHECK(scene_save(m2, "Den", wbox, err, sizeof err) == 0);
    turn_take(m2, &u, 0);                                  /* now Aria acts */
    CHECK(scene_restore(m2, &u, 0, err, sizeof err) == 1);
    CHECK_EQ(turn_acting(m2), 0);
    CHECK(m2->tokens.v[1].turn == TURN_IN);                /* the wolf keeps its place, not the turn */

    CASE("a creature outside the box under one coming back refuses the lot, changing nothing");
    Token big = scene_token(4, 3, 2, TOKEN_ENEMY, "Troll");
    tokens_add(&m2->tokens, big);
    int tbox[4] = { 5, 4, 5, 4 };                          /* meets the troll's corner only */
    CHECK(scene_save(m2, "Corner", tbox, err, sizeof err) == 1);
    int ti = m2->tokens.n - 1;
    undo_move_token(&u, m2, ti, 8, 0);                     /* the troll leaves */
    undo_add_token(&u, m2, scene_token(4, 3, 1, TOKEN_PLAYER, "Cara"));   /* Cara stands in its old corner */
    TokenList held = tokens_copy(&m2->tokens);
    unsigned g2 = m2->gen;
    CHECK(scene_restore(m2, &u, 1, err, sizeof err) < 0);
    CHECK(strstr(err, "Troll would come back onto Cara at E4") != NULL);
    CHECK(tokens_same(&m2->tokens, &held));
    CHECK_EQ(m2->gen, g2);
    tokens_free(&held);
    map_free(m2);

    CASE("saving under a name that is there replaces it; sixteen at most; remove");
    CHECK_EQ(scene_save(m, "ravine", NULL, err, sizeof err), 1);   /* same slot, now whole */
    CHECK(!m->scenes[1].boxed && !strcmp(m->scenes[1].name, "ravine"));
    for (int i = 2; i < MAP_SCENES_MAX; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "s%d", i);
        CHECK(scene_save(m, nm, NULL, err, sizeof err) == i);
    }
    CHECK(scene_save(m, "one more", NULL, err, sizeof err) < 0 && strstr(err, "remove makes room"));
    scene_remove(m, 0);
    CHECK_EQ(m->nscenes, MAP_SCENES_MAX - 1);
    CHECK(!strcmp(m->scenes[0].name, "ravine"));

    CASE("the file keeps scenes: version 11, a box, hidden, markers, counters, turns");
    char path[] = "/tmp/vtt-scenes-XXXXXX";
    int fd = mkstemp(path);
    if (fd >= 0) close(fd);
    while (m->nscenes > 1) scene_remove(m, m->nscenes - 1);
    m->scenes[0].tokens.v[1].hidden = 1;
    CHECK(scene_save(m, "Pass", box, err, sizeof err) == 1);
    CHECK_EQ(mapio_save(m, path, err, sizeof err), 0);
    char *text = slurp(path);
    CHECK(text && !strncmp(text, "VTT 11\n", 7));
    CHECK(text && strstr(text, "scene \"Pass\" 5 3 8 6\n") != NULL);
    free(text);
    Map *l = mapio_load(path, err, sizeof err);
    CHECK(l != NULL);
    if (l) {
        CHECK_EQ(l->nscenes, 2);
        CHECK(tokens_same(&l->tokens, &m->tokens));
        for (int i = 0; i < 2 && i < l->nscenes; i++) {
            CHECK(!strcmp(l->scenes[i].name, m->scenes[i].name));
            CHECK(tokens_same(&l->scenes[i].tokens, &m->scenes[i].tokens));
            CHECK_EQ(l->scenes[i].round, m->scenes[i].round);
            CHECK_EQ(l->scenes[i].spotlight, m->scenes[i].spotlight);
            CHECK_EQ(l->scenes[i].boxed, m->scenes[i].boxed);
        }
        CHECK(l->scenes[1].x0 == 5 && l->scenes[1].y1 == 6);
        map_free(l);
    }
    unlink(path);

    CASE("a shrink drops scene creatures that no longer fit and cuts the box");
    map_resize(m, 7, 5);
    for (int k = 0; k < m->nscenes; k++)
        for (int i = 0; i < m->scenes[k].tokens.n; i++) {
            const Token *t = &m->scenes[k].tokens.v[i];
            CHECK(t->x + t->size <= 7 && t->y + t->size <= 5);
        }
    CHECK(m->scenes[1].boxed && m->scenes[1].x1 == 6 && m->scenes[1].y1 == 4);
    int had = m->nscenes;
    map_resize(m, 3, 3);
    CHECK_EQ(m->nscenes, had - 1);                         /* the box went off the map: the scene goes */
    CHECK_EQ(m->scenes[0].boxed, 0);

    tokens_free(&saved);
    undo_free(&u);
    map_free(m);
}
