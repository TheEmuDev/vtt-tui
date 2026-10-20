/* Jobs and the review (docs/CONFLICTS.md, step 3): :ask over a box, the
 * tints, a proposal handed in, :review with the change drawn in place, accept
 * whole or by box, scrap, feedback, :ask!, and none of it on the phones. */
#include <sys/stat.h>
#include "harness.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "changeset.h"
#include "grid.h"
#include "undo.h"

/* The square's background in the frame just drawn. */
static uint32_t square_bg(App *a, Renderer *r, int x, int y)
{
    int sx, sy;
    grid_tile_interior(&a->ed.view, x, y, &sx, &sy);
    const Cell *c = rnd_at(r, sx, sy);
    return c ? c->bg : 0;
}

static void draw(App *a, Renderer *r)
{
    rnd_begin(r);
    app_draw(a);
}

/* What an agent would propose for job `slot`: water over x0..x1 on row y. */
static void propose_water(App *a, int slot, int x0, int x1, int y, const char *line)
{
    Map *c = map_copy(a->world.map);
    Undo u;
    undo_init(&u);
    undo_begin(&u);
    for (int x = x0; x <= x1; x++) undo_set_tile(&u, c, x, y, TILE_WATER);
    undo_end(&u);
    ChangeSet cs;
    cs_init(&cs);
    cs_diff(&cs, a->world.map, c, &u);
    world_job_set_proposal(&a->world, slot, &cs, line);
    map_free(c);
    undo_free(&u);
}

static int job_slot(App *a, int num)
{
    for (int i = 0; i < JOB_MAX; i++)
        if (a->world.jobs[i].used && a->world.jobs[i].num == num) return i;
    return -1;
}

void test_jobs(void)
{
    Sandbox sb = sandbox_enter("jobs");
    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 20, 12));
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.world.map;
    const Theme *th = a.th;

    CASE(":ask over a v box: job #1, asked, tinted in the working color, labeled, the GM's alone");
    a.ed.cx = 2; a.ed.cy = 2;
    press(&a, "v3l2j:ask a flooded crypt\r");
    int s1 = job_slot(&a, 1);
    CHECK(s1 >= 0);
    if (s1 < 0) goto out;
    CHECK_EQ(a.world.jobs[s1].state, JOB_ASKED);
    CHECK(a.world.jobs[s1].has_box && a.world.jobs[s1].box.x0 == 2 && a.world.jobs[s1].box.x1 == 5 && a.world.jobs[s1].box.y1 == 4);
    CHECK(strstr(a.status, "#1 asked in C3:F5") != NULL);
    CHECK(a.status_gm);
    draw(&a, &r);
    CHECK_EQ(square_bg(&a, &r, 4, 3), th->job_work_bg);
    CHECK(square_bg(&a, &r, 8, 8) != th->job_work_bg);
    CHECK(app_view_differs(&a));

    CASE("in play mode the tint is the GM's: the players' frame has none of it");
    Key f2 = { KEY_F2, 0, 0 }, f1 = { KEY_F1, 0, 0 };
    app_key(&a, f2);
    draw(&a, &r);
    CHECK_EQ(square_bg(&a, &r, 4, 3), th->job_work_bg);
    rnd_begin(&r);
    app_draw_view(&a, VIEW_PLAYERS);
    CHECK(square_bg(&a, &r, 4, 3) != th->job_work_bg);
    CASE(":ask works in play mode too, without a box; :review there is refused with a hint");
    press(&a, ":ask goblins in the hall\r");
    int s2 = job_slot(&a, 2);
    CHECK(s2 >= 0 && !a.world.jobs[s2].has_box);
    press(&a, ":review\r");
    CHECK(strstr(a.status, "build mode's") != NULL);
    app_key(&a, f1);

    CASE(":ask N remove takes it away; :ask N off says so");
    press(&a, ":ask 2 off\r");
    CHECK(strstr(a.status, ":ask 2 remove") != NULL);
    press(&a, ":ask 2 remove\r");
    CHECK_EQ(job_slot(&a, 2), -1);
    a.world.job_last = 1;                                     /* the cases below were written with 2 next */
    press(&a, ":jobs\r");
    CHECK(strstr(a.status, "#1 asked C3:F5 a flooded crypt") != NULL);

    CASE("a proposal comes in: ready, said on the status line, its squares tinted ready");
    propose_water(&a, s1, 3, 5, 3, "a pool, two ghouls to follow");
    CHECK_EQ(a.world.jobs[s1].state, JOB_READY);
    CHECK(strstr(a.status, "#1 ready: ground in D4:F4") != NULL);
    CHECK(strstr(a.status, ":review 1") != NULL);
    CHECK(strstr(a.status, "two ghouls") != NULL);
    draw(&a, &r);
    CHECK_EQ(square_bg(&a, &r, 4, 3), th->job_ready_bg);
    CHECK_EQ(map_tile(m, 4, 3), TILE_FLOOR);                    /* nothing landed */

    CASE(":review draws the change in place without touching the map; red where the GM changed it since");
    undo_begin(&a.world.undo);
    undo_set_tile(&a.world.undo, m, 5, 3, TILE_HAZARD);              /* the GM, after the agent looked */
    undo_end(&a.world.undo);
    unsigned gen = m->gen;
    int depth = a.world.undo.depth;
    press(&a, ":review\r");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    CHECK(strstr(a.status, "in red") != NULL);
    draw(&a, &r);
    CHECK_EQ(m->gen, gen);
    CHECK_EQ(map_tile(m, 4, 3), TILE_FLOOR);
    CHECK_EQ(square_bg(&a, &r, 5, 3), th->job_conflict_bg);
    CHECK_EQ(square_bg(&a, &r, 4, 3), th->job_ready_bg);
    {
        int sx, sy;
        grid_tile_interior(&a.ed.view, 4, 3, &sx, &sy);
        CHECK_EQ(rnd_at(&r, sx, sy)->fg, th->terrain_fg[TILE_WATER]);   /* the preview's water */
    }

    CASE("u in review undoes the GM's own step and stays in review; the conflict goes");
    press(&a, "u");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    CHECK_EQ(map_tile(m, 5, 3), TILE_FLOOR);
    draw(&a, &r);
    CHECK_EQ(square_bg(&a, &r, 5, 3), th->job_ready_bg);
    depth = a.world.undo.depth;

    CASE("a box accepts only its part, as one undo step; the rest stays waiting");
    a.ed.cx = 3; a.ed.cy = 3;
    press(&a, "vl\r");
    CHECK_EQ(map_tile(m, 3, 3), TILE_WATER);
    CHECK_EQ(map_tile(m, 4, 3), TILE_WATER);
    CHECK_EQ(map_tile(m, 5, 3), TILE_FLOOR);
    CHECK_EQ(a.world.undo.depth, depth + 1);
    CHECK_EQ(a.world.jobs[s1].state, JOB_READY);
    CHECK_EQ(a.world.jobs[s1].cs.ncells, 1);
    CHECK_EQ(a.ed.mode, ED_REVIEW);

    CASE("esc drops the box first, then leaves; the change stays waiting");
    press(&a, "v\x1b");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    CHECK_EQ(a.rv_box, 0);
    press(&a, "\x1b");
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    CHECK(strstr(a.status, "#1 stays ready") != NULL);

    CASE("enter accepts the rest; u takes it back");
    press(&a, ":review 1\r\r");
    CHECK_EQ(map_tile(m, 5, 3), TILE_WATER);
    CHECK_EQ(a.world.jobs[s1].state, JOB_ACCEPTED);
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    press(&a, "u");
    CHECK_EQ(map_tile(m, 5, 3), TILE_FLOOR);
    CHECK(!app_jobs_shown(&a));

    CASE("scrap: the change goes, :review N brings it back");
    press(&a, ":ask a bridge\r");
    int s3 = job_slot(&a, 2);
    CHECK(s3 >= 0);
    if (s3 < 0) goto out;
    propose_water(&a, s3, 10, 12, 8, "");
    press(&a, ":review 2\rd");
    CHECK_EQ(a.world.jobs[s3].state, JOB_SCRAPPED);
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    CHECK_EQ(map_tile(m, 11, 8), TILE_FLOOR);
    press(&a, ":review 2\r");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    CHECK_EQ(a.world.jobs[s3].state, JOB_READY);

    CASE("feedback: the line goes in the job's history, the change goes, the job is the agent's again");
    press(&a, "cmake it stone, not water\r");
    CHECK_EQ(a.world.jobs[s3].state, JOB_WORKING);
    CHECK(!a.world.jobs[s3].has_cs);
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    int found = 0;
    for (int t = 0; t < a.world.jobs[s3].nthread; t++)
        found |= a.world.jobs[s3].thread[t].who == 'G' && !strcmp(a.world.jobs[s3].thread[t].text, "make it stone, not water");
    CHECK(found);
    press(&a, ":jobs 2\r");
    CHECK_EQ(a.modal, MODAL_MESSAGE);
    CHECK(strstr(a.modal_body, "you: make it stone, not water") != NULL);
    press(&a, "\x1b");

    CASE("n and N move between the changes waiting");
    propose_water(&a, s3, 10, 12, 8, "");
    press(&a, ":ask a well\r");
    int s4 = job_slot(&a, 3);
    CHECK(s4 >= 0);
    if (s4 < 0) goto out;
    propose_water(&a, s4, 14, 15, 10, "");
    press(&a, ":review 2\r");
    CHECK_EQ(a.review, s3);
    press(&a, "n");
    CHECK_EQ(a.review, s4);
    press(&a, "N");
    CHECK_EQ(a.review, s3);
    press(&a, "\x1b");

    CASE(":ask! lands as soon as it comes; in play mode it waits, and lands back in build mode");
    press(&a, ":ask! a door\r");
    int s5 = job_slot(&a, 4);
    CHECK(s5 >= 0 && a.world.jobs[s5].at_once);
    if (s5 < 0) goto out;
    propose_water(&a, s5, 1, 1, 10, "");
    CHECK_EQ(a.world.jobs[s5].state, JOB_ACCEPTED);
    CHECK_EQ(map_tile(m, 1, 10), TILE_WATER);
    press(&a, ":ask! a second door\r");
    int s6 = job_slot(&a, 4);
    if (s6 < 0) s6 = job_slot(&a, 5);
    CHECK(s6 >= 0);
    if (s6 < 0) goto out;
    app_key(&a, f2);
    propose_water(&a, s6, 2, 2, 10, "");
    CHECK_EQ(a.world.jobs[s6].state, JOB_READY);
    CHECK_EQ(map_tile(m, 2, 10), TILE_FLOOR);
    app_key(&a, f1);
    CHECK_EQ(a.world.jobs[s6].state, JOB_ACCEPTED);
    CHECK_EQ(map_tile(m, 2, 10), TILE_WATER);

    CASE("every way out of a review leaves none behind: F2, :play, removing the job reviewed");
    press(&a, ":ask a lake\r");
    int s7 = job_slot(&a, 5);
    if (s7 < 0) s7 = job_slot(&a, 4);
    CHECK(s7 >= 0);
    if (s7 < 0) goto out;
    propose_water(&a, s7, 6, 8, 6, "");
    press(&a, ":review\r");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    app_key(&a, f2);
    CHECK_EQ(a.review, -1);
    CHECK(a.ed.mode != ED_REVIEW);
    app_key(&a, f1);
    press(&a, ":review\r:play\r");
    CHECK_EQ(a.review, -1);
    app_key(&a, f1);
    char rm[32];
    snprintf(rm, sizeof rm, ":review %d\r", a.world.jobs[s7].num);       /* by number: #2 and #3 still wait */
    press(&a, rm);
    CHECK_EQ(a.review, s7);
    snprintf(rm, sizeof rm, ":ask %d remove\r", a.world.jobs[s7].num);
    press(&a, rm);
    CHECK_EQ(a.review, -1);
    CHECK_EQ(a.ed.mode, ED_NORMAL);

    CASE("a : command over a review goes back to it; esc on the feedback prompt keeps it");
    press(&a, ":ask a moat\r");
    int s8 = -1;
    for (int i = 0; i < JOB_MAX; i++)
        if (a.world.jobs[i].used && !strcmp(a.world.jobs[i].text, "a moat")) s8 = i;
    CHECK(s8 >= 0);
    if (s8 < 0) goto out;
    propose_water(&a, s8, 6, 8, 7, "");
    char rv[32];
    snprintf(rv, sizeof rv, ":review %d\r", a.world.jobs[s8].num);
    press(&a, rv);
    CHECK_EQ(a.review, s8);
    press(&a, ":jobs\r");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    press(&a, "c\x1b");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    CHECK_EQ(a.world.jobs[s8].state, JOB_READY);

    CASE("a count is for moving: 3 then v leaves none behind; [ with a review box is refused");
    press(&a, "3v");
    CHECK_EQ(a.ed.count, 0);
    press(&a, "[");
    CHECK(strstr(a.status, "esc first") != NULL);
    press(&a, "\x1b");
    CHECK_EQ(a.rv_box, 0);

    CASE("the label says #N in the corner of what the change touches");
    draw(&a, &r);
    {
        int sx, sy;
        grid_tile_interior(&a.ed.view, 6, 7, &sx, &sy);
        char want[8];
        snprintf(want, sizeof want, "#%d", a.world.jobs[s8].num);
        CHECK_EQ(rnd_at(&r, sx, sy)->ch, (uint32_t)'#');
        CHECK_EQ(rnd_at(&r, sx + 1, sy)->ch, (uint32_t)want[1]);
    }

    CASE("a proposal for another job while one is reviewed: the review stays, the other waits");
    press(&a, ":ask a ford\r");
    int s9 = -1;
    for (int i = 0; i < JOB_MAX; i++)
        if (a.world.jobs[i].used && !strcmp(a.world.jobs[i].text, "a ford")) s9 = i;
    CHECK(s9 >= 0);
    if (s9 < 0) goto out;
    press(&a, rv);                                          /* s8 again */
    int under = a.review;
    propose_water(&a, s9, 6, 8, 9, "");
    CHECK_EQ(a.review, under);
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    CHECK_EQ(a.world.jobs[s9].state, JOB_READY);

    CASE("accepted, then u: :review N brings the change back");
    press(&a, "\r");
    CHECK_EQ(a.world.jobs[s8].state, JOB_ACCEPTED);
    press(&a, "\x1b");                                     /* apart: esc and a letter at once is Alt */
    press(&a, "u");
    CHECK_EQ(map_tile(m, 7, 7), TILE_FLOOR);
    press(&a, rv);
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    CHECK_EQ(a.world.jobs[s8].state, JOB_READY);
    press(&a, "\x1b");

    CASE("closing the map closes its jobs");
    app_close_map(&a);
    CHECK(!app_jobs_shown(&a));
    int left = 0;
    for (int i = 0; i < JOB_MAX; i++) left += a.world.jobs[i].used;
    CHECK_EQ(left, 0);

out:
    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* The channel's side of a job (step 4): jobs, job N take/area/say/propose/
 * drop/dump/check/describe, propose, the agent's undo of an accepted change,
 * and :agent accept auto|review. */
void test_jobs_ctl(void)
{
    Sandbox sb = sandbox_enter("jobsctl");
    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 20, 12));
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.world.map;
    char *t;

    CASE("jobs with none; the job requests' errors");
    t = ctl_ask(&a, "jobs");
    CHECK(t && !strcmp(t, "ok\nno jobs\n"));
    free(t);
    t = ctl_ask(&a, "job 4 take");
    CHECK(t && strstr(t, "error missing: line 1: no job #4 - jobs lists them"));
    free(t);
    t = ctl_ask(&a, "propose");
    CHECK(t && !strcmp(t, "error usage: propose wants edit lines after it\n"));
    free(t);
    t = ctl_ask(&a, "tile A1 water\npropose");
    CHECK(t && strstr(t, "error usage: line 2: propose comes once, before the request's edits"));
    free(t);
    t = ctl_ask(&a, "tile A1 water\njobs");
    CHECK(t && strstr(t, "error usage: line 2: jobs comes before the request's propose line and edits"));
    free(t);
    CHECK_EQ(map_tile(m, 0, 0), TILE_FLOOR);          /* all or nothing, as ever */

    CASE(":ask, then the agent takes it, says where, says a line: the GM sees each");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "v3l3j:ask a crypt\r");
    t = ctl_ask(&a, "jobs");
    CHECK(t && strstr(t, "ok\n#1 asked in B2:E5, asked by the GM: a crypt\n  gm: a crypt\n"));
    free(t);
    t = ctl_ask(&a, "jobs json");
    CHECK(t && strstr(t, "ok\n[{\"num\":1,\"state\":\"asked\",\"from\":\"gm\",") == t);
    CHECK(t && strstr(t, "\"box\":\"B2:E5\",\"area\":null,\"thread\":[{\"who\":\"gm\",\"text\":\"a crypt\"}]"));
    CHECK(t && json_valid(t + 3));
    free(t);
    t = ctl_ask(&a, "job 1 take");
    CHECK(t && strstr(t, "ok\n#1 working in B2:E5"));
    free(t);
    CHECK(strstr(a.status, "#1 taken by an agent") != NULL);
    CHECK(a.status_gm);
    t = ctl_ask(&a, "job 1 area C3:D4");
    CHECK(t && !strcmp(t, "ok\n#1 area C3:D4\n"));
    free(t);
    CHECK(strstr(a.status, "#1: the agent is working in C3:D4") != NULL);
    t = ctl_ask(&a, "job 1 area Z99");
    CHECK(t && ctl_refused(t, "usage", 1));
    free(t);
    t = ctl_ask(&a, "job 1 say \"two ghouls coming\"");
    CHECK(t && !strcmp(t, "ok\n"));
    free(t);
    CHECK(strstr(a.status, "#1, the agent: two ghouls coming") != NULL);

    CASE("job N propose: run on a copy, so the map, its log, gen and modified wait for the GM");
    unsigned gen = m->gen, stamp = a.world.undo.stamp;
    int modified = m->modified, depth = a.world.undo.depth;
    t = ctl_ask(&a, "job 1 propose \"the crypt, walled\"\nroom B2:E5\ntile C4 water\ntoken add enemy C3 \"Ghoul\"\n");
    CHECK(t && !strncmp(t, "ok\nproposal #1: ", 16));
    CHECK(t && strstr(t, "\nwaiting for the GM's review - :review 1\n"));
    free(t);
    CHECK_EQ(m->gen, gen);
    CHECK_EQ(a.world.undo.stamp, stamp);
    CHECK_EQ(a.world.undo.depth, depth);
    CHECK_EQ(m->modified, modified);
    CHECK_EQ(map_tile(m, 2, 3), TILE_FLOOR);
    CHECK_EQ(map_vedge(m, 1, 1), EDGE_NONE);
    CHECK_EQ(m->tokens.n, 0);
    int s1 = job_slot(&a, 1);
    CHECK(s1 >= 0);
    if (s1 < 0) goto out;
    CHECK_EQ(a.world.jobs[s1].state, JOB_READY);
    CHECK(strstr(a.status, "#1 ready: ") && strstr(a.status, "the crypt, walled"));
    t = ctl_ask(&a, "jobs");
    CHECK(t && strstr(t, "  agent: the crypt, walled\n") && strstr(t, "  proposal: "));
    CHECK(t && !strstr(t, "conflict"));
    free(t);
    t = ctl_ask(&a, "job 1 area C3");
    CHECK(t && ctl_refused(t, "state", 1) && strstr(t, "has a proposal waiting"));
    free(t);

    CASE("jobs json with a proposal, a thread of several lines and a conflict: still one valid document (HEALTH 3)");
    {
        uint8_t  was = map_tile(m, 2, 3);
        unsigned gen0 = m->gen;                            /* the cases after this count from it */
        map_set_tile(m, 2, 3, TILE_ROUGH);                 /* the GM changes C4 under the proposal */
        t = ctl_ask(&a, "jobs json");
        CHECK(t && !strncmp(t, "ok\n", 3) && json_valid(t + 3));
        CHECK(t && strstr(t, "\"state\":\"ready\"") && strstr(t, "\"conflicts\":1"));
        CHECK(t && strstr(t, "{\"who\":\"gm\",\"text\":\"a crypt\"}") && strstr(t, "two ghouls coming") && strstr(t, "the crypt, walled"));
        free(t);
        t = ctl_ask(&a, "jobs");
        CHECK(t && strstr(t, "(1 conflict: changed since)"));
        free(t);
        map_set_tile(m, 2, 3, was);
        m->gen = gen0;
    }

    CASE("job N say with the wrong number of words, and :agent accept with no word, say how (HEALTH 4)");
    t = ctl_ask(&a, "job 1 say");
    CHECK(t && ctl_refused(t, "usage", 1) && strstr(t, "job N say \"a line for the GM\""));
    free(t);
    t = ctl_ask(&a, "job 1");
    CHECK(t && ctl_refused(t, "usage", 1) && strstr(t, "job N take, area REGION, say"));
    free(t);
    t = ctl_ask(&a, "job 1 say two words");
    CHECK(t && ctl_refused(t, "usage", 1) && strstr(t, "job N say \"a line for the GM\""));
    free(t);
    {
        int was = a.world.ctl_auto;
        press(&a, ":agent accept\r");
        CHECK(strstr(a.status, ":agent accept auto lands an agent's changes at once") != NULL);
        CHECK(a.status_gm);
        CHECK_EQ(a.world.ctl_auto, was);
    }

    CASE("job N dump, check, describe: the map as accepting it would make it; the live map untouched");
    t = ctl_ask(&a, "job 1 dump B2:E5");
    CHECK(t && !strncmp(t, "ok\n", 3) && strstr(t, "~") != NULL);   /* the water */
    free(t);
    t = ctl_ask(&a, "job 1 describe json");
    CHECK(t && !strncmp(t, "ok\n", 3) && json_valid(t + 3) && strstr(t, "Ghoul"));
    free(t);
    t = ctl_ask(&a, "job 1 check");
    CHECK(t && !strncmp(t, "ok\n", 3));
    free(t);
    t = ctl_ask(&a, "job 1 dump Q1:Z99");
    CHECK(t && ctl_refused(t, "usage", 1));
    free(t);
    CHECK_EQ(map_tile(m, 2, 3), TILE_FLOOR);
    CHECK_EQ(m->tokens.n, 0);
    CHECK_EQ(m->gen, gen);

    CASE("a GM's edit inside the proposal since is a conflict, counted for the agent");
    undo_begin(&a.world.undo);
    undo_set_tile(&a.world.undo, m, 2, 3, TILE_HAZARD);         /* the GM's, where the water goes */
    undo_end(&a.world.undo);
    t = ctl_ask(&a, "jobs");
    CHECK(t && strstr(t, "conflict"));
    free(t);
    press(&a, "u");

    CASE("the GM accepts in review; the agent's undo takes it back while nothing happened since");
    press(&a, ":review 1\r");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    press(&a, "\r");
    CHECK_EQ(a.world.jobs[s1].state, JOB_ACCEPTED);
    CHECK_EQ(map_tile(m, 2, 3), TILE_WATER);
    CHECK_EQ(m->tokens.n, 1);
    t = ctl_ask(&a, "job 1 take");
    CHECK(t && strstr(t, "#1 was accepted"));
    free(t);
    t = ctl_ask(&a, "job 1 propose\ntile A1 water");
    CHECK(t && strstr(t, "#1 was accepted - propose alone offers a new change"));
    free(t);
    t = ctl_ask(&a, "undo");
    CHECK(t && !strcmp(t, "ok\ntook back the last change\n"));
    free(t);
    CHECK_EQ(map_tile(m, 2, 3), TILE_FLOOR);
    CHECK_EQ(m->tokens.n, 0);
    press(&a, "\x12");                                   /* the GM's ctrl-r */
    CHECK_EQ(map_tile(m, 2, 3), TILE_WATER);
    t = ctl_ask(&a, "undo");                              /* the step is back, and still the agent's */
    CHECK(t && !strcmp(t, "ok\ntook back the last change\n"));
    free(t);
    CHECK_EQ(map_tile(m, 2, 3), TILE_FLOOR);
    t = ctl_ask(&a, "undo");                              /* once is all: nothing of its is applied */
    CHECK(t && strstr(t, "there is no change of the agent's to take back"));
    free(t);
    press(&a, "\x12");                                   /* as the case found it */
    CHECK_EQ(map_tile(m, 2, 3), TILE_WATER);

    CASE("propose: an agent's own idea is a job of its own; job N drop withdraws it");
    t = ctl_ask(&a, "propose \"a pool\"\ntile H8 water\n");
    CHECK(t && !strncmp(t, "ok\nproposal #2: ", 16));
    free(t);
    int s2 = job_slot(&a, 2);
    CHECK(s2 >= 0 && a.world.jobs[s2].from == JOB_FROM_AGENT && a.world.jobs[s2].state == JOB_READY);
    CHECK(s2 >= 0 && !strcmp(a.world.jobs[s2].text, "a pool"));
    t = ctl_ask(&a, "job 2 drop");
    CHECK(t && !strcmp(t, "ok\n#2 withdrawn\n"));
    free(t);
    CHECK(s2 >= 0 && a.world.jobs[s2].state == JOB_SCRAPPED && a.world.jobs[s2].has_cs);   /* kept: rule 9 */
    CHECK(strstr(a.status, "#2 withdrawn by the agent - :review 2 brings it back") != NULL);
    press(&a, ":ask 2 remove\r");                       /* the GM's way to destroy it */
    CHECK_EQ(job_slot(&a, 2), -1);
    a.world.job_last = 1;
    t = ctl_ask(&a, "tile H8 water\n");                  /* no header: the same */
    CHECK(t && !strncmp(t, "ok\nproposal #2: ", 16));
    free(t);
    t = ctl_ask(&a, "tile H8 floor\n");                   /* nothing to change */
    CHECK(t && !strcmp(t, "ok\nno change: the map already looked like that\n"));
    free(t);

    CASE("job N drop on the GM's job gives it back as asked, its proposal gone");
    press(&a, ":ask a door\r");
    int s3 = job_slot(&a, 3);
    CHECK(s3 >= 0);
    if (s3 < 0) goto out;
    t = ctl_ask(&a, "job 3 propose\nedge B2|C2 door\n");
    free(t);
    CHECK_EQ(a.world.jobs[s3].state, JOB_READY);
    t = ctl_ask(&a, "job 3 drop");
    CHECK(t && !strcmp(t, "ok\n#3 given back, as asked\n"));
    free(t);
    CHECK(a.world.jobs[s3].state == JOB_ASKED && !a.world.jobs[s3].has_cs);
    t = ctl_ask(&a, "job 3 dump");
    CHECK(t && strstr(t, "#3 has no proposal"));
    free(t);

    CASE(":agent accept auto: a proposal lands at once, one undo step, the agent's to take back");
    press(&a, ":agent accept auto\r");
    CHECK_EQ(a.world.ctl_auto, 1);
    CHECK(strstr(a.status, "land at once") != NULL);
    depth = a.world.undo.depth;
    t = ctl_ask(&a, "job 3 propose\nedge B2|C2 door\n");
    CHECK(t && strstr(t, "\nchanged ") && strstr(t, "one undo step"));
    free(t);
    CHECK_EQ(a.world.jobs[s3].state, JOB_ACCEPTED);
    CHECK_EQ(a.world.undo.depth, depth + 1);
    t = ctl_ask(&a, "undo");
    CHECK(t && !strncmp(t, "ok\ntook back", 12));
    free(t);
    CHECK_EQ(a.world.undo.depth, depth);
    press(&a, ":agent accept review\r");
    CHECK_EQ(a.world.ctl_auto, 0);
    press(&a, ":agent accept maybe\r");
    CHECK(strstr(a.status, ":agent accept auto lands") != NULL);

    CASE("a full table of waiting changes refuses the next, and changes nothing");
    jobs_reset(&a);
    for (int k = 0; k < JOB_MAX; k++) {
        char req[64];
        snprintf(req, sizeof req, "tile %c10 water\n", 'A' + k);
        t = ctl_ask(&a, req);
        CHECK(t && !strncmp(t, "ok\nproposal #", 13));
        free(t);
    }
    t = ctl_ask(&a, "tile A11 water\n");
    CHECK(t && strstr(t, "error limit: 16 changes are waiting for the GM"));
    free(t);
    CHECK_EQ(map_tile(m, 0, 10), TILE_FLOOR);

    CASE("a full table makes room by a finished job, never a waiting one");
    {
        int fin = job_slot(&a, 5);
        CHECK(fin >= 0);
        if (fin >= 0) a.world.jobs[fin].state = JOB_SCRAPPED;
        t = ctl_ask(&a, "tile A11 water\n");
        CHECK(t && !strncmp(t, "ok\nproposal #17: ", 17));     /* the next number, not the freed one */
        free(t);
        int ready = 0;
        for (int k = 0; k < JOB_MAX; k++) ready += a.world.jobs[k].used && a.world.jobs[k].state == JOB_READY;
        CHECK_EQ(ready, JOB_MAX);
        jobs_reset(&a);
    }

    CASE("reads about the GM's side mid-proposal read the live map: marked, status");
    a.world.ctl_auto = 1;
    t = ctl_ask(&a, "token add enemy B9 \"Imp\"\ntoken add enemy D9 \"Orc\"\n");
    free(t);
    a.world.ctl_auto = 0;
    jobs_reset(&a);
    app_key(&a, (Key){ KEY_F2, 0, 0 });
    a.play.ngroup = 1;
    a.play.group[0] = a.play.sel = tokens_find_label(&m->tokens, "Imp", -1);
    char *plain = ctl_ask(&a, "marked");
    t = ctl_ask(&a, "token del Imp\nmarked");
    CHECK(plain && t && strstr(plain, "selected Imp B9\n"));
    CHECK(t && !strncmp(t, plain, strlen(plain)));      /* the same, then the proposal */
    CHECK(t && strstr(t, "Orc") == NULL);
    free(t);
    free(plain);
    jobs_reset(&a);
    app_key(&a, (Key){ KEY_F1, 0, 0 });
    char want[64];
    snprintf(want, sizeof want, "undo %d back, ", a.world.undo.depth);
    t = ctl_ask(&a, "tile A12 water\nstatus");
    CHECK(t && strstr(t, want) && strstr(t, "and this request's changes proposed"));
    free(t);
    jobs_reset(&a);

    CASE("job lines after a propose line are refused: the job it names cannot go from under it");
    press(&a, ":ask a well\r");
    t = ctl_ask(&a, "job 1 propose\njob 1 drop\ntile H9 water");
    CHECK(t && strstr(t, "error usage: line 2: job comes before the request's propose line and edits"));
    free(t);

    CASE("a proposal for the job under review never lands under the GM's eyes, even at once");
    t = ctl_ask(&a, "job 1 propose\ntile H9 water");
    free(t);
    press(&a, ":review 1\r");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    a.world.ctl_auto = 1;
    t = ctl_ask(&a, "job 1 propose\ntile H9 hazard");
    CHECK(t && strstr(t, "\nlands when the GM is back"));
    free(t);
    CHECK_EQ(map_tile(m, 7, 8), TILE_FLOOR);
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    press(&a, "j");                                    /* a key: still under review */
    CHECK_EQ(map_tile(m, 7, 8), TILE_FLOOR);
    press(&a, "\r");                                   /* the GM's enter lands it */
    CHECK_EQ(map_tile(m, 7, 8), TILE_HAZARD);
    a.world.ctl_auto = 0;

    CASE("job N dump reads only a ready proposal: an accepted one applied again would double it");
    t = ctl_ask(&a, "job 1 dump");
    CHECK(t && strstr(t, "#1 has no proposal waiting"));
    free(t);

    CASE("with no GM (--apply), propose is refused: the plan is the file's");
    a.world.ctl_direct = 1;
    t = ctl_ask(&a, "propose\ntile A1 water");
    CHECK(t && strstr(t, "there is no GM to propose to"));
    free(t);
    a.world.ctl_direct = 0;

out:
    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* The slot of the job made `back` jobs before the newest (0: the newest). */
static int job_slot_last(const App *a, int back)
{
    int nums[JOB_MAX], n = 0;
    for (int i = 0; i < JOB_MAX; i++) if (a->world.jobs[i].used) nums[n++] = a->world.jobs[i].num;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (nums[j] > nums[i]) { int t = nums[i]; nums[i] = nums[j]; nums[j] = t; }
    for (int i = 0; i < JOB_MAX; i++) if (back < n && a->world.jobs[i].used && a->world.jobs[i].num == nums[back]) return i;
    return 0;
}

/* Events and wait (step 5), without a socket: here `wait` is answered at
 * once, as --apply, the bench and the fuzzer get it. The held wait is
 * test_ctl_live's. */
void test_events(void)
{
    Sandbox sb = sandbox_enter("events");
    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    char *t;

    CASE("with no map, holds answers no");
    t = ctl_ask(&a, "holds 1 2");
    CHECK(t && !strcmp(t, "ok\nno\n"));
    free(t);

    CASE("with no map, wait still answers; nothing has happened");
    t = ctl_ask(&a, "wait");
    CHECK(t && !strcmp(t, "ok\nseq 0\n"));
    free(t);

    CHECK(ctl_blank_map(&a, sb.dir, 20, 12));
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.world.map;

    CASE("a map never saved is no file's: holds answers no, whatever the numbers (HEALTH 13)");
    {
        struct stat st;
        char req[96], kept[sizeof m->path];
        CHECK(stat(m->path, &st) == 0);
        snprintf(req, sizeof req, "holds %llu %llu", (unsigned long long)st.st_dev, (unsigned long long)st.st_ino);
        t = ctl_ask(&a, req);
        CHECK(t && !strcmp(t, "ok\nholds\n"));
        free(t);
        strcpy(kept, m->path);
        m->path[0] = '\0';
        t = ctl_ask(&a, req);
        CHECK(t && !strcmp(t, "ok\nno\n"));
        free(t);
        strcpy(m->path, kept);
    }

    CASE("wait's words: a number, for SECONDS, alone in its request");
    t = ctl_ask(&a, "wait soon");
    CHECK(t && strstr(t, "is not an event's number"));
    free(t);
    t = ctl_ask(&a, "wait 0 for 601");
    CHECK(t && ctl_refused(t, "limit", 1) && strstr(t, "at most 600 seconds"));
    free(t);
    /* Only a time too long is a limit; a clause written wrong is usage. */
    static const char *const WRONG[] = { "wait for abc", "wait 0 fro 5", "wait for", "wait 0 for 5 more", "wait soon" };
    for (size_t k = 0; k < sizeof WRONG / sizeof *WRONG; k++) {
        t = ctl_ask(&a, WRONG[k]);
        CHECK(t && ctl_refused(t, "usage", 1));
        free(t);
    }
    t = ctl_ask(&a, "wait\nstatus");
    CHECK(t && !strcmp(t, "error usage: wait goes in a request of its own\n"));
    free(t);

    CASE("each turn of a job is an event, numbered in order: asked, accepted in part, scrapped, feedback, removed");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "v3l3j:ask a crypt\r");
    t = ctl_ask(&a, "wait 0");
    CHECK(t && !strcmp(t, "ok\nseq 1\n1 job 1 asked: in B2:E5: a crypt\n"));
    free(t);
    t = ctl_ask(&a, "wait");                               /* from now: nothing yet */
    CHECK(t && !strcmp(t, "ok\nseq 1\n"));
    free(t);
    t = ctl_ask(&a, "job 1 propose\ntile B2:C2 water\n");
    free(t);
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, ":review 1\rv\r");                           /* the box is B2 alone */
    CHECK_EQ(map_tile(m, 1, 1), TILE_WATER);
    t = ctl_ask(&a, "wait 1");
    /* The proposal was event 2, the agent's own: not told to it, and its number is passed over. */
    CHECK(t && strstr(t, "ok\nseq 3\n3 job 1 part: B2: ground in B2 - the rest waits\n") == t);
    /* What --wait makes of the words as they are really sent (HEALTH 16). */
    unsigned vseq = 0;
    char     vline[200];
    CHECK_EQ(ctl_verdict_in(t, 1, &vseq, vline, sizeof vline), 0);
    CHECK(!strcmp(vline, "job 1 part: B2: ground in B2 - the rest waits") && vseq == 3);
    CHECK_EQ(ctl_verdict_in(t, 2, &vseq, vline, sizeof vline), -1);
    free(t);
    press(&a, "c");
    press(&a, "deeper\r");
    t = ctl_ask(&a, "wait 3");
    CHECK(t && !strcmp(t, "ok\nseq 4\n4 job 1 feedback: deeper\n"));
    CHECK_EQ(ctl_verdict_in(t, 1, &vseq, vline, sizeof vline), 5);
    CHECK(!strcmp(vline, "deeper"));
    free(t);
    t = ctl_ask(&a, "job 1 propose\ntile C2 hazard\n");
    free(t);
    press(&a, ":review 1\rd");
    press(&a, ":review 1\r");                              /* back, then whole */
    press(&a, "\r");
    press(&a, ":ask 1 remove\r");
    a.world.job_last = 0;
    t = ctl_ask(&a, "wait 4");
    CHECK(t && strstr(t, "6 job 1 scrapped\n7 job 1 back: for review\n8 job 1 accepted: ground in C2\n"
                         "9 job 1 removed\n"));
    CHECK_EQ(ctl_verdict_in(t, 1, &vseq, vline, sizeof vline), 4);
    CHECK(!strcmp(vline, "job 1 scrapped"));
    CHECK_EQ(ctl_verdict_in(strstr(t, "7 job 1"), 1, &vseq, vline, sizeof vline), 0);      /* back is no verdict; accepted is */
    CHECK(!strcmp(vline, "job 1 accepted: ground in C2"));
    CHECK_EQ(ctl_verdict_in(strstr(t, "9 job 1"), 1, &vseq, vline, sizeof vline), 4);
    free(t);

    CASE("asked, in its four forms: with a box or none, to land at once or not");
    {
        unsigned s0 = a.world.event_seq;
        char want[200];
        int last0 = a.world.job_last;
        press(&a, ":ask a well\r");
        press(&a, ":ask! a pit\r");
        a.ed.cx = 1; a.ed.cy = 1;
        press(&a, "v2l:ask! a moat\r");
        int n1 = a.world.jobs[job_slot_last(&a, 2)].num, n2 = a.world.jobs[job_slot_last(&a, 1)].num, n3 = a.world.jobs[job_slot_last(&a, 0)].num;
        snprintf(want, sizeof want, "%u job %d asked: a well\n%u job %d asked: to land at once: a pit\n"
                                    "%u job %d asked: in B2:D2, to land at once: a moat\n", s0 + 1, n1, s0 + 2, n2, s0 + 3, n3);
        char req[32];
        snprintf(req, sizeof req, "wait %u", s0);
        t = ctl_ask(&a, req);
        CHECK(t && strstr(t, want));
        if (t && !strstr(t, want)) printf("    got: %s    want: %s", t, want);
        free(t);
        for (int k = 0; k < 3; k++) { char c[40]; snprintf(c, sizeof c, ":ask %d remove\r", k == 0 ? n1 : k == 1 ? n2 : n3); press(&a, c); }
        a.world.job_last = last0;                                /* the cases after this count jobs from where they were */
        CHECK_EQ(a.world.event_seq, s0 + 6);                     /* three asked, three removed */
        for (unsigned q = a.world.event_seq; q > s0; q--) a.world.events[q % EVENT_MAX].text[0] = '\0';
        a.world.event_seq = s0;
    }

    CASE("a number past the newest event is another vtt's: answered at once, with everything kept");
    t = ctl_ask(&a, "wait 99");
    CHECK(t && strstr(t, "ok\nseq 9\nreset 99: ") == t && strstr(t, "\n1 job 1 asked: in B2:E5: a crypt\n")
            && strstr(t, "\n9 job 1 removed\n"));
    /* Every event sent has a kind the one table knows, and names its job. */
    {
        int seen = 0, kinds = 0;
        for (const char *p = t; p && *p; ) {
            size_t ll = strcspn(p, "\n"), tl;
            const char *text;
            int num = -1, k = ctl_event_parse(p, ll, &num, &text, &tl);
            if (*p >= '0' && *p <= '9') { seen++; CHECK(k >= 0 && num == 1); kinds |= 1 << k; }
            else CHECK_EQ(k, -1);
            p += ll + (p[ll] == '\n');
        }
        CHECK_EQ(seen, 7);
        CHECK_EQ(kinds, 1 << CTL_EV_ASKED | 1 << CTL_EV_PART | 1 << CTL_EV_FEEDBACK | 1 << CTL_EV_SCRAPPED |
                        1 << CTL_EV_BACK | 1 << CTL_EV_ACCEPTED | 1 << CTL_EV_REMOVED);
    }
    free(t);
    CHECK_EQ(world_events_after(&a.world, 99, NULL), 1);                 /* so a socket never holds it */

    CASE("the client reads how long a wait may be held: past comments, clamped, nothing else");
    CHECK_EQ(ctl_held_ms("status\n"), 0);
    CHECK_EQ(ctl_held_ms("waiter\n"), 0);
    CHECK_EQ(ctl_held_ms("wait\n"), CTL_WAIT_DEFAULT_S * 1000);
    CHECK_EQ(ctl_held_ms("wait 41 for 300\n"), 300000);
    CHECK_EQ(ctl_held_ms("# the agent's loop\n\n  wait 41 for 5\n"), 5000);
    CHECK_EQ(ctl_held_ms("wait for 9999"), CTL_WAIT_MAX_S * 1000);
    CHECK_EQ(ctl_held_ms("dump\n# wait for 300\n"), 0);

    CASE("no agent listening: no checkpoint runs");
    a.world.agent_seen_ms = 0;
    app_tick(&a, 1000);
    CHECK(m->cp == NULL);

    CASE("the map changed: one event once it has been quiet 1.5 s, and the checkpoint moves on");
    t = ctl_ask(&a, "wait");                               /* an agent is listening */
    free(t);
    app_tick(&a, 2000);
    CHECK(m->cp != NULL);
    unsigned seq0 = a.world.event_seq;
    undo_begin(&a.world.undo);
    undo_set_tile(&a.world.undo, m, 9, 9, TILE_ROUGH);
    undo_end(&a.world.undo);
    CHECK_EQ(app_events_due(&a, 3000), 0);                 /* wake: the tick has not stamped it */
    app_tick(&a, 3000);                                    /* seen */
    CHECK_EQ(app_events_due(&a, 3000), AUTOSAVE_QUIET_MS);
    app_tick(&a, 3000 + AUTOSAVE_QUIET_MS - 1);
    CHECK_EQ(a.world.event_seq, seq0);
    app_tick(&a, 3000 + AUTOSAVE_QUIET_MS);
    CHECK_EQ(a.world.event_seq, seq0 + 1);
    CHECK_EQ(app_events_due(&a, 9000), -1);                /* nothing owed: the loop sleeps */
    char want[96];
    snprintf(want, sizeof want, "%u map changed: ground in J10\n", seq0 + 1);
    t = ctl_ask(&a, "wait 0");
    CHECK(t && strstr(t, want));
    free(t);
    app_tick(&a, 20000);                                   /* told once */
    CHECK_EQ(a.world.event_seq, seq0 + 1);

    CASE("undone again before it is quiet: nothing changed, no event");
    undo_begin(&a.world.undo);
    undo_set_tile(&a.world.undo, m, 10, 9, TILE_ROUGH);
    undo_end(&a.world.undo);
    app_tick(&a, 21000);
    press(&a, "u");
    app_tick(&a, 22000);
    app_tick(&a, 30000);
    CHECK_EQ(a.world.event_seq, seq0 + 1);

    CASE("an accept is its own event, not also a map change; the GM's edit before it is told apart, first");
    a.world.ctl_auto = 1;
    undo_begin(&a.world.undo);
    undo_set_tile(&a.world.undo, m, 11, 9, TILE_ROUGH);          /* the GM's, not yet quiet */
    undo_end(&a.world.undo);
    t = ctl_ask(&a, "tile A12 water\n");                   /* lands at once */
    free(t);
    a.world.ctl_auto = 0;
    app_tick(&a, 31000);
    app_tick(&a, 40000);
    char two[160];
    /* Before the two is the agent's own proposal (seq0 + 2), not told to it. */
    snprintf(two, sizeof two, "%u map changed: ground in L10\n%u job 1 accepted: ground in A12\n", seq0 + 3, seq0 + 4);
    t = ctl_ask(&a, "wait 0");
    CHECK(t && strstr(t, two));
    CHECK_EQ(a.world.event_seq, seq0 + 4);
    free(t);

    CASE("changes: the events after SEQ, then the map as it is now in the pieces they changed, and only those");
    {
        /* 20x12: pieces of 2x1 squares. J10 and L10 are in two side by side, sent as one region (I10:L10); A12 is A12:B12. */
        char rq[48], all[400];
        snprintf(rq, sizeof rq, "changes %u", seq0);
        t = ctl_ask(&a, rq);
        snprintf(all, sizeof all, "ok\nseq %u\n%u map changed: ground in J10\n%u map changed: ground in L10\n"
                                    "%u job 1 accepted: ground in A12\n"
                                    "changed I10:L10\n    I J K L\n\n10  . : . :\n\n"
                                    "changed A12:B12\n    A B\n\n12  ~ .\n\n",
                 seq0 + 4, seq0 + 1, seq0 + 3, seq0 + 4);
        CHECK(t && !strcmp(t, all));
        if (t && strcmp(t, all)) printf("    got:\n%s    want:\n%s", t, all);
        free(t);
        snprintf(rq, sizeof rq, "changes %u", seq0 + 2);    /* from later: the accept alone */
        t = ctl_ask(&a, rq);
        CHECK(t && strstr(t, "\nchanged A12:B12\n") && !strstr(t, "\nchanged I10"));
        free(t);
        snprintf(rq, sizeof rq, "changes %u A1:D12", seq0); /* a region: what changed inside it */
        t = ctl_ask(&a, rq);
        CHECK(t && strstr(t, "\nchanged A12:B12\n") && !strstr(t, "\nchanged I10"));
        free(t);
        snprintf(rq, sizeof rq, "changes %u J10", seq0);
        t = ctl_ask(&a, rq);
        CHECK(t && strstr(t, "\nchanged J10\n") && !strstr(t, "\nchanged A12"));
        free(t);
        /* Something on no square is said, not dumped. */
        unsigned s1 = a.world.event_seq;
        world_event(&a.world, CTL_EV_CHANGED, 0, "roll attack");
        CsWhere nowhere = { { 0 } };
        world_event_where(&a.world, &nowhere, 1);
        snprintf(rq, sizeof rq, "changes %u", s1);
        t = ctl_ask(&a, rq);
        CHECK(t && !strstr(t, "\nchanged ") && strstr(t, "\nalso: something on no square changed"));
        free(t);
        /* A number that is another vtt's: everything kept, as wait gives it. */
        t = ctl_ask(&a, "changes 9999");
        CHECK(t && strstr(t, "reset 9999: ") && strstr(t, "\nchanged I10:L10\n") && strstr(t, "\nchanged A12:B12\n"));
        free(t);
    }

    CASE("changes: a part accepted by box says its box; changes after an edit line is refused");
    {
        unsigned s2 = a.world.event_seq;
        int last0 = a.world.job_last;
        char rq[64];
        t = ctl_ask(&a, "propose\ntile E2:F2 water\ntile Q2 water\n");
        const char *hash = t ? strstr(t, "proposal #") : NULL;
        int num = hash ? atoi(hash + 10) : 0;
        CHECK(num > 0);
        free(t);
        a.ed.cx = 4; a.ed.cy = 1;
        snprintf(rq, sizeof rq, ":review %d\rvl\r", num);    /* the box is E2:F2: Q2 waits */
        press(&a, rq);
        CHECK_EQ(map_tile(m, 4, 1), TILE_WATER);
        CHECK(map_tile(m, 16, 1) != TILE_WATER);
        snprintf(rq, sizeof rq, "changes %u", s2);
        t = ctl_ask(&a, rq);
        CHECK(t && strstr(t, " part: E2:F2: ") && strstr(t, "\nchanged E2:F2\n   E F\n\n2  ~ ~\n") && !strstr(t, "\nchanged Q2"));
        free(t);
        press(&a, "d");                                      /* the rest is scrapped */
        press(&a, "\x1b");
        t = ctl_ask(&a, "tile A1 water\nchanges 0");
        CHECK(ctl_refused(t, "usage", 2) && strstr(t, "changes comes before"));
        free(t);
        snprintf(rq, sizeof rq, ":ask %d remove\r", num);
        press(&a, rq);
        a.world.job_last = last0;                                 /* the cases after this count jobs from where they were */
    }

    CASE("a change while no agent listened is not missed: back with an old number, an agent is told to read the map");
    {
        unsigned s3 = a.world.event_seq;
        uint64_t later = 50000 + (uint64_t)CTL_WAIT_MAX_S * 1000 + 1000;
        app_tick(&a, 50000);
        CHECK(m->cp != NULL);
        for (int i = 0; i < JOB_MAX; i++) if (a.world.jobs[i].used && a.world.jobs[i].state <= JOB_READY) a.world.jobs[i].state = JOB_SCRAPPED;
        app_tick(&a, later);                                 /* ten minutes of nobody: the checkpoint stops */
        CHECK(m->cp == NULL);
        undo_begin(&a.world.undo);
        undo_set_tile(&a.world.undo, m, 9, 4, TILE_HAZARD);         /* J5, with nobody to tell */
        undo_end(&a.world.undo);
        app_tick(&a, later + 1000);
        app_tick(&a, later + 5000);
        CHECK_EQ(a.world.event_seq, s3);
        char rq[64];
        a.world.now_ms = later + 6000;
        snprintf(rq, sizeof rq, "changes %u J5", s3);
        t = ctl_ask(&a, rq);
        CHECK(t && strstr(t, " map changed: while no agent was listening - read the map again\n"));
        CHECK(t && strstr(t, "\nchanged J5\n") && strstr(t, "\n5  ^\n") && strstr(t, "\nalso: "));
                free(t);
        CHECK(m->cp != NULL);
        unsigned s4 = a.world.event_seq;
        snprintf(rq, sizeof rq, "changes %u", s4);            /* told once */
        t = ctl_ask(&a, rq);
        CHECK(t && !strstr(t, "changed"));
        free(t);
        /* Nothing changed while nobody listened: nothing is said. */
        app_tick(&a, later + 6000 + (uint64_t)CTL_WAIT_MAX_S * 1000 + 1000);
        CHECK(m->cp == NULL);
        a.world.now_ms = later + 6000 + (uint64_t)CTL_WAIT_MAX_S * 1000 + 2000;
        t = ctl_ask(&a, "wait 0 for 0");
        CHECK_EQ(a.world.event_seq, s4);
        free(t);
    }

    CASE("more events than the ring keeps: the gap is said");
    for (int k = 0; k < EVENT_MAX + 5; k++) world_event(&a.world, CTL_EV_CHANGED, 0, "test %d", k);
    t = ctl_ask(&a, "wait 0");
    snprintf(want, sizeof want, "lost 1-%u: ", a.world.event_seq - EVENT_MAX);
    CHECK(t && strstr(t, want) && strstr(t, ": test 4\n") == NULL && strstr(t, " map changed: test 36\n"));
    free(t);
    t = ctl_ask(&a, "changes 99999");                      /* another vtt's number, and more than is kept: no regions */
    CHECK(t && strstr(t, "reset 99999: ") && strstr(t, "\nlost 1-") && !strstr(t, "\nchanged "));
    free(t);
    t = ctl_ask(&a, "changes 1");                          /* older than is kept: nobody knows where; read the map */
    CHECK(t && strstr(t, "\nlost 2-") && !strstr(t, "\nchanged ") && !strstr(t, "also:"));
    free(t);

    CASE("closing the map is an event; wait hears it with no map open");
    seq0 = a.world.event_seq;
    press(&a, ":ask a well\r");
    app_close_map(&a);
    snprintf(want, sizeof want, "wait %u", seq0);
    t = ctl_ask(&a, want);
    CHECK(t && strstr(t, " job 2 asked: a well\n") && strstr(t, " map closed: Blank - its jobs went with it\n"));
    free(t);

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* :agent command (step 8): the command started for each :ask and each job
 * sent back, the job on its stdin, answering over the channel. */
void test_agent_command(void)
{
    Sandbox sb = sandbox_enter("agentcmd");
    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 20, 12));
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    const char *self_was = app_self_path;
    char self[700];
    snprintf(self, sizeof self, "%s/vtt", sb.cwd);
    app_self_path = self;

    char out[700], script[700], cmd[800];
    snprintf(out, sizeof out, "%s/agent.out", sb.dir);
    snprintf(script, sizeof script, "%s/agent.sh", sb.dir);
    {
        /* An agent in four lines: keep what it was told, take the job,
         * propose, and say it is done. */
        FILE *f = fopen(script, "w");
        fprintf(f, "cat >> '%s'\n"
                   "\"$VTT\" --ctl-pid \"$VTT_PID\" --ctl \"job $VTT_JOB take\" > /dev/null\n"
                   "printf 'job %%s propose \"from the script\"\\ntile A1 water\\n' \"$VTT_JOB\" | \"$VTT\" --ctl-pid \"$VTT_PID\" --ctl > /dev/null\n"
                   "echo \"done job=$VTT_JOB pid=$VTT_PID agent=$VTT_AGENT\" >> '%s'\n", out, out);
        fclose(f);
    }
    snprintf(cmd, sizeof cmd, ":agent command sh %s\r", script);

    CASE(":agent command with none set says so; off with none set too");
    press(&a, ":agent command\r");
    CHECK(strstr(a.status, "none is set") != NULL && a.status_gm);
    press(&a, ":agent command off\r");
    CHECK(strstr(a.status, "no agent command is set") != NULL);
    a.settings.agent = AGENT_KIND_UNSET;                         /* a GM who never chose (main's default) */
    press(&a, ":ask nothing starts\r");
    CHECK_EQ(a.world.agent_on, 0);                               /* no command: nothing started, nothing opened */
    CHECK_EQ(a.modal, MODAL_CHOOSE_AGENT);                       /* no agent ever chosen: the GM is asked (docs/APIAGENT.md) */
    press(&a, "\x1b");
    CHECK(strstr(a.status, "#1 waits - no agent chosen yet") != NULL && a.status_gm);
    jobs_reset(&a);

    CASE("set: each :ask starts it, the job and how to answer on its stdin; the channel comes on for it");
    press(&a, cmd);
    CHECK(strstr(a.status, "each :ask now starts: sh ") != NULL);
    press(&a, ":agent command\r");
    CHECK(strstr(a.status, "each :ask starts: sh ") != NULL);
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "v3l3j:ask a flooded crypt\r");
    CHECK_EQ(a.world.agent_on, 1);
    CHECK_EQ(ctl_active(&a.ctl), 1);
    char want[64];
    snprintf(want, sizeof want, "done job=1 pid=%ld agent=job-1", (long)getpid());   /* its name is its job's */
    CHECK(pump_until_file(&a, out, want));
    CHECK(file_has(out, "Job #1: a flooded crypt\n"));
    CHECK(file_has(out, "Where: B2:E5 (the box the GM drew)\n"));
    CHECK(file_has(out, "Map: Blank, 20x12 squares"));
    char line[900];
    snprintf(line, sizeof line, "'%s' --ctl-pid %ld --ctl 'job 1 take'\n", self, (long)getpid());
    CHECK(file_has(out, line));
    CHECK(file_has(out, "job 1 propose \"one line for the GM\"\n"));

    CASE("the started agent answered like any other: the job is ready, with its line");
    CHECK_EQ(a.world.jobs[0].state, JOB_READY);
    CHECK(a.world.jobs[0].has_cs && a.world.jobs[0].cs.ncells == 1);
    CHECK(!strcmp(a.world.jobs[0].taker, "job-1"));               /* taken and proposed under the name vtt gave it */
    CHECK(strstr(a.status, "#1 ready: ") && strstr(a.status, "from the script"));
    CHECK_EQ(map_tile(a.world.map, 0, 0), TILE_FLOOR);           /* a proposal, as ever */

    CASE("vtt's child, reaped by the tick: the command leaves no zombie behind, and a job done is left as it is");
    CHECK(pump_until_reaped(&a));
    errno = 0;
    CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    CHECK_EQ(a.world.jobs[0].state, JOB_READY);
    CHECK(strstr(a.status, "its agent stopped") == NULL);

    CASE("sent back with feedback: started again, the thread on its stdin with the feedback last");
    press(&a, ":review 1\rc");
    press(&a, "not there - the east half\r");
    CHECK(pump_until_file(&a, out, "  gm: not there - the east half\n"));
    CHECK(file_has(out, "The job so far (the last gm: line is what to do now):\n  gm: a flooded crypt\n"));
    {
        char *t = slurp(out);
        int runs = 0;
        for (const char *p = t; p && (p = strstr(p, "You are an agent for the GM")) != NULL; p++) runs++;
        CHECK_EQ(runs, 2);
        free(t);
    }

    CASE("a job with no box says so; :agent command off: the next :ask starts nothing");
    {
        char body[4096] = "";
        FILE *f = fmemopen(body, sizeof body - 1, "w");
        Job j;
        memset(&j, 0, sizeof j);
        j.used = 1; j.num = 7;
        str_lcpy(j.text, "more goblins", sizeof j.text);
        app_agent_prompt(&a, &j, f);
        fclose(f);
        CHECK(strstr(body, "Where: the GM drew no box") != NULL);
        CHECK(strstr(body, "--ctl 'dump'\n") != NULL);
        CHECK(strstr(body, "job 7 area") == NULL);          /* no box of the GM's to name */
        /* The verbs come from the channel's table: once kept by hand here, the list fell a verb behind. */
        CHECK(strstr(body, "The lines that change a map: room, area, ") != NULL);
        CHECK(strstr(body, ", env, ") != NULL);
        CHECK(strstr(body, "--ctl 'help token'\n") != NULL);
        CHECK(strstr(body, " --agent-guide\n") != NULL);
    }
    pump_until_file(&a, out, "\x01never");                  /* let the second run finish */
    press(&a, ":agent command off\r");
    CHECK(strstr(a.status, "agent command off") != NULL);
    {
        char *before = slurp(out);
        press(&a, ":ask and another\r");
        pump_until_file(&a, out, "Job #2");
        char *after = slurp(out);
        CHECK(before && after && !strcmp(before, after));
        free(before);
        free(after);
    }

    CASE("REVIEW 1: only a job the GM asked for starts it: feedback on an agent's own idea does not");
    {
        char runs_file[720], c2[900];
        snprintf(runs_file, sizeof runs_file, "%s/runs.out", sb.dir);
        snprintf(c2, sizeof c2, ":agent command cat > /dev/null; echo run-$VTT_JOB >> %s\r", runs_file);
        press(&a, c2);
        jobs_reset(&a);
        char *t = ctl_ask(&a, "propose \"my own idea\"\ntile A2 water\n");
        free(t);
        CHECK(a.world.jobs[0].used && a.world.jobs[0].from == JOB_FROM_AGENT);
        press(&a, ":review\rc");
        press(&a, "rougher\r");
        CHECK_EQ(a.world.jobs[0].state, JOB_WORKING);
        pump_until_file(&a, runs_file, "run-");
        CHECK(!file_has(runs_file, "run-"));               /* nothing was started */
        jobs_reset(&a);

        CASE("REVIEW 4: the channel coming on for the command is said on the GM's line, once");
        press(&a, ":agent off\r");
        CHECK_EQ(a.world.agent_on, 0);
        press(&a, ":ask a well\r");
        CHECK_EQ(a.world.agent_on, 1);
        CHECK(strstr(a.status, "agent channel on") != NULL && a.status_gm);
        CHECK(strstr(a.status, "started") != NULL);
        CHECK(pump_until_file(&a, runs_file, "run-1"));
        press(&a, ":ask another\r");
        CHECK(strstr(a.status, "agent channel on") == NULL);
        CHECK(pump_until_file(&a, runs_file, "run-2"));
        jobs_reset(&a);

        CASE("REVIEW 5: the command does not inherit SIGPIPE ignored (a pipeline in it must die quietly)");
        char sig_file[720];
        snprintf(sig_file, sizeof sig_file, "%s/sig.out", sb.dir);
        snprintf(c2, sizeof c2, ":agent command cat > /dev/null; grep SigIgn /proc/self/status > %s; echo end >> %s\r", sig_file, sig_file);
        press(&a, c2);
        void (*was)(int) = signal(SIGPIPE, SIG_IGN);       /* as the terminal sets it */
        press(&a, ":ask a wall\r");
        CHECK(pump_until_file(&a, sig_file, "end"));
        signal(SIGPIPE, was);
        {
            char *st = slurp(sig_file);
            unsigned long long ign = st ? strtoull(st + 7, NULL, 16) : ~0ull;
            CHECK((ign & (1ull << (SIGPIPE - 1))) == 0);
            free(st);
        }
        jobs_reset(&a);

        CASE("REVIEW 8: off switches it off and on brings it back as it was (KEYS.md rule 9)");
        press(&a, ":agent command off\r");
        press(&a, ":ask nothing\r");
        CHECK(strstr(a.status, "started") == NULL);
        press(&a, ":agent command on\r");
        CHECK(strstr(a.status, "each :ask now starts: cat > /dev/null; grep SigIgn") != NULL);
        press(&a, ":agent command off\r");
        jobs_reset(&a);
    }

    CASE("REVIEW 6: the prompt's lines run as printed: the path quoted, the heredoc's end at the margin");
    {
        const char *keep = app_self_path;
        app_self_path = "/opt/my games/it's vtt";
        char body[4096] = "";
        FILE *f = fmemopen(body, sizeof body - 1, "w");
        Job j;
        memset(&j, 0, sizeof j);
        j.used = 1; j.num = 7;
        str_lcpy(j.text, "more goblins", sizeof j.text);
        app_agent_prompt(&a, &j, f);
        fclose(f);
        CHECK(strstr(body, "'/opt/my games/it'\\''s vtt' --ctl-pid") != NULL);
        CHECK(strstr(body, "\nEOF\n") != NULL && strstr(body, "  EOF\n") == NULL);
        CHECK(strstr(body, "no job #7") != NULL);            /* what to do when it is gone */
        app_self_path = keep;
    }

    CASE("a command too long for its field is refused, and none is set");
    {
        /* Past what the : line takes, so asked of the command's own check
         * (VTT_AGENT_COMMAND and a longer : line would reach it). */
        char big[400];
        memset(big, 'x', 300);
        big[300] = '\0';
        char had[AGENT_CMD_MAX];
        str_lcpy(had, a.agent_cmd, sizeof had);
        app_agent_command_cmd(&a, big);
        CHECK(strstr(a.status, "put it in a script") != NULL);
        CHECK(!strcmp(a.agent_cmd, had));                   /* what was there is left */
    }

    CASE("a command that cannot run: its job waits as asked again, and the GM's line says exit 127");
    {
        jobs_reset(&a);
        CHECK(pump_until_reaped(&a));                        /* the cases above left runs of their own */
        press(&a, ":agent command no-such-command-anywhere-xyz\r");
        unsigned s0 = a.world.event_seq;
        press(&a, ":ask a moat\r");
        CHECK_EQ(a.nagents, 1);
        CHECK(a.world.jobs[0].used && a.world.jobs[0].state == JOB_ASKED);
        CHECK(pump_until_reaped(&a));
        CHECK_EQ(a.world.jobs[0].state, JOB_ASKED);
        CHECK(strstr(a.status, "#1: its agent stopped with nothing proposed (exit 127: the command could not be run)") && a.status_gm);
        CHECK(strstr(a.world.events[a.world.event_seq % EVENT_MAX].text, "job 1 asked: its agent stopped (exit 127") != NULL);
        CHECK_EQ(a.world.event_seq, s0 + 2);                       /* asked, and asked again: nothing starts it a second time */
        CHECK(!strcmp(a.world.jobs[0].thread[a.world.jobs[0].nthread - 1].text, "its agent stopped (exit 127: the command could not be run)"));
        CHECK_EQ(a.nagents, 0);
    }

    CASE("an agent that takes its job and dies gives it back: asked, nobody's, with its exit status");
    {
        jobs_reset(&a);
        press(&a, ":agent command cat > /dev/null; \"$VTT\" --ctl-pid \"$VTT_PID\" --ctl \"job $VTT_JOB area B2:C3\" > /dev/null; exit 3\r");
        press(&a, ":ask a keep\r");
        for (int spin = 0; spin < 400 && a.world.jobs[0].state != JOB_WORKING && a.nagents; spin++) {
            struct pollfd fds[1 + CTL_SLOTS];
            int n = ctl_pollfds(&a.ctl, fds, 1 + CTL_SLOTS);
            poll(fds, (nfds_t)n, 5);
            ctl_service(&a.ctl, fds, n, prof_now_ns() / 1000000u);
            app_tick(&a, prof_now_ns() / 1000000u);
        }
        CHECK(pump_until_reaped(&a));
        CHECK(a.world.jobs[0].state == JOB_ASKED && !a.world.jobs[0].taker[0] && !a.world.jobs[0].has_area);
        CHECK(strstr(a.status, "(exit 3)") != NULL);
        /* One another name took is that name's: its agent's end changes nothing. */
        jobs_reset(&a);
        press(&a, ":agent command cat > /dev/null; sleep 0.2; exit 4\r");
        press(&a, ":ask a wall\r");
        char *t = ctl_ask(&a, "agent scout\njob 1 take");
        free(t);
        CHECK(pump_until_reaped(&a));
        CHECK(a.world.jobs[0].state == JOB_WORKING && !strcmp(a.world.jobs[0].taker, "scout"));
        CHECK(strstr(a.status, "exit 4") == NULL);
    }

    CASE("an agent silent past its deadline is stopped, its whole session, and the job waits as asked");
    {
        jobs_reset(&a);
        char mark[720], c3[900];
        snprintf(mark, sizeof mark, "%s/late", sb.dir);
        snprintf(c3, sizeof c3, ":agent command cat > /dev/null; sleep 30; touch %s\r", mark);
        press(&a, c3);
        a.agent_deadline_ms = 60;
        press(&a, ":ask a tower\r");
        CHECK_EQ(a.nagents, 1);
        long pid = a.agents[0].pid;
        CHECK_EQ(app_agents_due(&a, 5), 0);                    /* not yet seen by a tick: due at once, to be */
        app_tick(&a, prof_now_ns() / 1000000u);
        CHECK_EQ(app_agents_due(&a, a.agents[0].heard_ms), 60);
        CHECK_EQ(app_agents_due(&a, a.agents[0].heard_ms + 100), 0);
        CHECK(pump_until_reaped(&a));
        a.agent_deadline_ms = AGENT_DEADLINE_MS;
        CHECK_EQ(app_agents_due(&a, 0), -1);
        CHECK(pid > 0 && kill((pid_t)pid, 0) < 0);            /* gone, and reaped */
        int group_gone = 0;                                   /* and the sleep it started with it (init reaps that one: a moment) */
        for (int spin = 0; spin < 200 && !(group_gone = kill((pid_t)-pid, 0) < 0); spin++) poll(NULL, 0, 5);
        CHECK(group_gone);
        CHECK(a.world.jobs[0].state == JOB_ASKED && strstr(a.status, "stopped by vtt after") != NULL);
        CHECK(access(mark, F_OK) != 0);
    }

    CASE("the deadline is silence: a request under its name, or a wait it holds, keeps an agent; one deaf to TERM is killed");
    {
        char alive[720], c5[1100];
        snprintf(alive, sizeof alive, "%s/alive", sb.dir);
        /* Three times the deadline, a request every 20 ms. */
        jobs_reset(&a);
        snprintf(c5, sizeof c5, ":agent command cat > /dev/null; for i in 1 2 3 4 5 6 7 8 9 10 11 12; do \"$VTT\" --ctl-pid \"$VTT_PID\" "
                                "--ctl status > /dev/null; sleep 0.02; done; echo talked >> %s\r", alive);
        press(&a, c5);
        a.agent_deadline_ms = 120;
        press(&a, ":ask a bridge\r");
        CHECK(pump_until_reaped(&a));
        CHECK(file_has(alive, "talked") && strstr(a.status, "stopped by vtt") == NULL && strstr(a.status, "(exit 0)") != NULL);
        /* Silent but for one held wait, three times the deadline long. */
        jobs_reset(&a);
        snprintf(c5, sizeof c5, ":agent command cat > /dev/null; \"$VTT\" --ctl-pid \"$VTT_PID\" --ctl 'wait for 1' > /dev/null; "
                                "echo waited >> %s\r", alive);
        press(&a, c5);
        a.agent_deadline_ms = 300;
        press(&a, ":ask a gate\r");
        CHECK(pump_until_reaped(&a));
        CHECK(file_has(alive, "waited") && strstr(a.status, "stopped by vtt") == NULL);
        /* Deaf to TERM: told, then made to, and until then nothing is due but the grace. */
        jobs_reset(&a);
        press(&a, ":agent command trap '' TERM; cat > /dev/null; sleep 30\r");
        a.agent_deadline_ms = 40;
        a.agent_grace_ms = 150;
        press(&a, ":ask a wall\r");
        long pid = a.agents[0].pid;
        uint64_t t0 = prof_now_ns() / 1000000u;
        app_tick(&a, t0);
        app_tick(&a, t0 + 50);                               /* past the deadline: TERM, which it does not hear */
        CHECK(a.nagents == 1 && a.agents[0].stopping == 1);
        CHECK_EQ(app_agents_due(&a, t0 + 60), 140);           /* the grace, from when it was told */
        poll(NULL, 0, 30);
        app_tick(&a, t0 + 100);
        CHECK(a.nagents == 1 && kill((pid_t)pid, 0) == 0);    /* still there */
        app_tick(&a, t0 + 250);                              /* KILL */
        CHECK_EQ(a.agents[0].stopping, 2);
        CHECK_EQ(app_agents_due(&a, t0 + 9999), -1);          /* nothing more to do: its end wakes the loop */
        for (int spin = 0; spin < 400 && a.nagents; spin++) { poll(NULL, 0, 5); app_tick(&a, t0 + 260); }
        CHECK(a.nagents == 0 && strstr(a.status, "stopped by vtt after") != NULL);
        a.agent_grace_ms = AGENT_GRACE_MS;

        /* Sixteen runs at most: jobs asked and removed leave runs with no job, which make room once they stop. */
        jobs_reset(&a);
        press(&a, ":agent command cat > /dev/null; sleep 5\r");
        a.agent_deadline_ms = AGENT_DEADLINE_MS;
        for (int k = 0; k < JOB_MAX; k++) {
            press(&a, ":ask more\r");
            char rm[40];
            snprintf(rm, sizeof rm, ":ask %d remove\r", a.world.job_last);
            press(&a, rm);
        }
        CHECK_EQ(a.nagents, JOB_MAX);
        press(&a, ":ask one too many\r");
        CHECK(a.nagents == JOB_MAX && strstr(a.status, "16 agents are running already") != NULL);
        a.agent_deadline_ms = 20;                            /* and they are stopped, all of them */
        CHECK(pump_until_reaped(&a));
        a.agent_deadline_ms = AGENT_DEADLINE_MS;
        errno = 0;
        CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    }

    CASE("one agent a job: feedback while its agent still runs starts the next one only when that one stops");
    {
        jobs_reset(&a);
        char runs[720], sc[720], c4[900];
        snprintf(runs, sizeof runs, "%s/held.out", sb.dir);
        snprintf(sc, sizeof sc, "%s/slow.sh", sb.dir);
        FILE *f = fopen(sc, "w");
        fprintf(f, "cat > /dev/null\n"
                   "printf 'job %%s propose \"slow\"\ntile C1 water\n' \"$VTT_JOB\" | \"$VTT\" --ctl-pid \"$VTT_PID\" --ctl > /dev/null\n"
                   "echo started >> '%s'\nsleep 0.4\necho stopped >> '%s'\n", runs, runs);
        fclose(f);
        snprintf(c4, sizeof c4, ":agent command sh %s\r", sc);
        press(&a, c4);
        press(&a, ":ask a pool\r");
        CHECK(pump_until_file(&a, runs, "started"));
        CHECK(a.world.jobs[0].state == JOB_READY && a.nagents == 1);
        press(&a, ":review 1\rc");
        press(&a, "deeper\r");                               /* the first is still asleep */
        CHECK(a.nagents == 1 && a.agents[0].again);
        CHECK(strstr(a.status, "its agent is still running") != NULL);
        CHECK(!file_has(runs, "stopped"));
        CHECK(pump_until_file(&a, runs, "stopped\nstarted"));  /* the second starts after the first has stopped */
        CHECK(pump_until_reaped(&a));
        char *t = slurp(runs);
        CHECK(t && !strcmp(t, "started\nstopped\nstarted\nstopped\n"));
        free(t);
        CHECK_EQ(a.world.jobs[0].state, JOB_READY);
        /* Held, and the job done before the first run stops: nobody is started for it. */
        press(&a, ":review 1\rc");
        press(&a, "wider\r");                                /* a third run, */
        CHECK(pump_until_file(&a, runs, "stopped\nstarted\nstopped\nstarted"));
        press(&a, ":review 1\rc");
        press(&a, "wider still\r");                          /* and feedback on its proposal while it is still asleep */
        CHECK(a.nagents == 1 && a.agents[0].again);
        char *p2 = ctl_ask(&a, "agent job-1\njob 1 propose\ntile C2 water");   /* it proposes late, */
        free(p2);
        press(&a, ":review 1\r");
        press(&a, "\r");                                     /* and the GM takes that */
        press(&a, "\x1b");
        CHECK_EQ(a.world.jobs[0].state, JOB_ACCEPTED);
        CHECK(pump_until_reaped(&a));
        t = slurp(runs);
        CHECK(t && !strcmp(t, "started\nstopped\nstarted\nstopped\nstarted\nstopped\n"));   /* no fourth */
        free(t);
        /* A job removed while its agent runs: the run answers to no job, and is still reaped. */
        jobs_reset(&a);
        press(&a, ":agent command cat > /dev/null; sleep 0.2; exit 9\r");
        press(&a, ":ask a ditch\r");
        press(&a, ":ask 1 remove\r");
        CHECK(a.nagents == 1 && a.agents[0].job == 0);
        unsigned s9 = a.world.event_seq;
        CHECK(pump_until_reaped(&a));
        CHECK(strstr(a.status, "its agent stopped") == NULL && a.world.event_seq == s9);
        errno = 0;
        CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
        press(&a, ":agent command off\r");
    }

    app_self_path = self_was;
    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}
