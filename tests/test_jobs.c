/* Jobs and the review (docs/CONFLICTS.md, step 3): :ask over a box, the
 * tints, a proposal handed in, :review with the change drawn in place, accept
 * whole or by box, scrap, feedback, :ask!, and none of it on the phones. */
#include "harness.h"

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
    Map *c = map_copy(a->map);
    Undo u;
    undo_init(&u);
    undo_begin(&u);
    for (int x = x0; x <= x1; x++) undo_set_tile(&u, c, x, y, TILE_WATER);
    undo_end(&u);
    ChangeSet cs;
    cs_init(&cs);
    cs_diff(&cs, a->map, c, &u);
    app_job_set_proposal(a, slot, &cs, line);
    map_free(c);
    undo_free(&u);
}

static int job_slot(App *a, int num)
{
    for (int i = 0; i < JOB_MAX; i++)
        if (a->jobs[i].used && a->jobs[i].num == num) return i;
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
    if (!a.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.map;
    const Theme *th = a.th;

    CASE(":ask over a v box: job #1, asked, tinted in the working color, labeled, the GM's alone");
    a.ed.cx = 2; a.ed.cy = 2;
    press(&a, "v3l2j:ask a flooded crypt\r");
    int s1 = job_slot(&a, 1);
    CHECK(s1 >= 0);
    if (s1 < 0) goto out;
    CHECK_EQ(a.jobs[s1].state, JOB_ASKED);
    CHECK(a.jobs[s1].has_box && a.jobs[s1].box.x0 == 2 && a.jobs[s1].box.x1 == 5 && a.jobs[s1].box.y1 == 4);
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
    CHECK(s2 >= 0 && !a.jobs[s2].has_box);
    press(&a, ":review\r");
    CHECK(strstr(a.status, "build mode's") != NULL);
    app_key(&a, f1);

    CASE(":ask N remove takes it away; :ask N off says so, and the lowest free number is used again");
    press(&a, ":ask 2 off\r");
    CHECK(strstr(a.status, ":ask 2 remove") != NULL);
    press(&a, ":ask 2 remove\r");
    CHECK_EQ(job_slot(&a, 2), -1);
    press(&a, ":jobs\r");
    CHECK(strstr(a.status, "#1 asked C3:F5 a flooded crypt") != NULL);

    CASE("a proposal comes in: ready, said on the status line, its squares tinted ready");
    propose_water(&a, s1, 3, 5, 3, "a pool, two ghouls to follow");
    CHECK_EQ(a.jobs[s1].state, JOB_READY);
    CHECK(strstr(a.status, "#1 ready: ground in D4:F4") != NULL);
    CHECK(strstr(a.status, ":review 1") != NULL);
    CHECK(strstr(a.status, "two ghouls") != NULL);
    draw(&a, &r);
    CHECK_EQ(square_bg(&a, &r, 4, 3), th->job_ready_bg);
    CHECK_EQ(map_tile(m, 4, 3), TILE_FLOOR);                    /* nothing landed */

    CASE(":review draws the change in place without touching the map; red where the GM changed it since");
    undo_begin(&a.undo);
    undo_set_tile(&a.undo, m, 5, 3, TILE_HAZARD);              /* the GM, after the agent looked */
    undo_end(&a.undo);
    unsigned gen = m->gen;
    int depth = a.undo.depth;
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
    depth = a.undo.depth;

    CASE("a box accepts only its part, as one undo step; the rest stays waiting");
    a.ed.cx = 3; a.ed.cy = 3;
    press(&a, "vl\r");
    CHECK_EQ(map_tile(m, 3, 3), TILE_WATER);
    CHECK_EQ(map_tile(m, 4, 3), TILE_WATER);
    CHECK_EQ(map_tile(m, 5, 3), TILE_FLOOR);
    CHECK_EQ(a.undo.depth, depth + 1);
    CHECK_EQ(a.jobs[s1].state, JOB_READY);
    CHECK_EQ(a.jobs[s1].cs.ncells, 1);
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
    CHECK_EQ(a.jobs[s1].state, JOB_ACCEPTED);
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
    CHECK_EQ(a.jobs[s3].state, JOB_SCRAPPED);
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    CHECK_EQ(map_tile(m, 11, 8), TILE_FLOOR);
    press(&a, ":review 2\r");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    CHECK_EQ(a.jobs[s3].state, JOB_READY);

    CASE("feedback: the line goes in the job's history, the change goes, the job is the agent's again");
    press(&a, "cmake it stone, not water\r");
    CHECK_EQ(a.jobs[s3].state, JOB_WORKING);
    CHECK(!a.jobs[s3].has_cs);
    CHECK_EQ(a.ed.mode, ED_NORMAL);
    int found = 0;
    for (int t = 0; t < a.jobs[s3].nthread; t++)
        found |= a.jobs[s3].thread[t].who == 'G' && !strcmp(a.jobs[s3].thread[t].text, "make it stone, not water");
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
    CHECK(s5 >= 0 && a.jobs[s5].at_once);
    if (s5 < 0) goto out;
    propose_water(&a, s5, 1, 1, 10, "");
    CHECK_EQ(a.jobs[s5].state, JOB_ACCEPTED);
    CHECK_EQ(map_tile(m, 1, 10), TILE_WATER);
    press(&a, ":ask! a second door\r");
    int s6 = job_slot(&a, 4);
    if (s6 < 0) s6 = job_slot(&a, 5);
    CHECK(s6 >= 0);
    if (s6 < 0) goto out;
    app_key(&a, f2);
    propose_water(&a, s6, 2, 2, 10, "");
    CHECK_EQ(a.jobs[s6].state, JOB_READY);
    CHECK_EQ(map_tile(m, 2, 10), TILE_FLOOR);
    app_key(&a, f1);
    CHECK_EQ(a.jobs[s6].state, JOB_ACCEPTED);
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
    snprintf(rm, sizeof rm, ":review %d\r", a.jobs[s7].num);       /* by number: #2 and #3 still wait */
    press(&a, rm);
    CHECK_EQ(a.review, s7);
    snprintf(rm, sizeof rm, ":ask %d remove\r", a.jobs[s7].num);
    press(&a, rm);
    CHECK_EQ(a.review, -1);
    CHECK_EQ(a.ed.mode, ED_NORMAL);

    CASE("a : command over a review goes back to it; esc on the feedback prompt keeps it");
    press(&a, ":ask a moat\r");
    int s8 = -1;
    for (int i = 0; i < JOB_MAX; i++)
        if (a.jobs[i].used && !strcmp(a.jobs[i].text, "a moat")) s8 = i;
    CHECK(s8 >= 0);
    if (s8 < 0) goto out;
    propose_water(&a, s8, 6, 8, 7, "");
    char rv[32];
    snprintf(rv, sizeof rv, ":review %d\r", a.jobs[s8].num);
    press(&a, rv);
    CHECK_EQ(a.review, s8);
    press(&a, ":jobs\r");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    press(&a, "c\x1b");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    CHECK_EQ(a.jobs[s8].state, JOB_READY);

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
        snprintf(want, sizeof want, "#%d", a.jobs[s8].num);
        CHECK_EQ(rnd_at(&r, sx, sy)->ch, (uint32_t)'#');
        CHECK_EQ(rnd_at(&r, sx + 1, sy)->ch, (uint32_t)want[1]);
    }

    CASE("a proposal for another job while one is reviewed: the review stays, the other waits");
    press(&a, ":ask a ford\r");
    int s9 = -1;
    for (int i = 0; i < JOB_MAX; i++)
        if (a.jobs[i].used && !strcmp(a.jobs[i].text, "a ford")) s9 = i;
    CHECK(s9 >= 0);
    if (s9 < 0) goto out;
    press(&a, rv);                                          /* s8 again */
    int under = a.review;
    propose_water(&a, s9, 6, 8, 9, "");
    CHECK_EQ(a.review, under);
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    CHECK_EQ(a.jobs[s9].state, JOB_READY);

    CASE("accepted, then u: :review N brings the change back");
    press(&a, "\r");
    CHECK_EQ(a.jobs[s8].state, JOB_ACCEPTED);
    press(&a, "\x1b");                                     /* apart: esc and a letter at once is Alt */
    press(&a, "u");
    CHECK_EQ(map_tile(m, 7, 7), TILE_FLOOR);
    press(&a, rv);
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    CHECK_EQ(a.jobs[s8].state, JOB_READY);
    press(&a, "\x1b");

    CASE("closing the map closes its jobs");
    app_close_map(&a);
    CHECK(!app_jobs_shown(&a));
    int left = 0;
    for (int i = 0; i < JOB_MAX; i++) left += a.jobs[i].used;
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
    if (!a.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.map;
    char *t;

    CASE("jobs with none; the job requests' errors");
    t = ctl_ask(&a, "jobs");
    CHECK(t && !strcmp(t, "ok\nno jobs\n"));
    free(t);
    t = ctl_ask(&a, "job 4 take");
    CHECK(t && strstr(t, "error: line 1: no job #4 - jobs lists them"));
    free(t);
    t = ctl_ask(&a, "propose");
    CHECK(t && !strcmp(t, "error: propose wants edit lines after it\n"));
    free(t);
    t = ctl_ask(&a, "tile A1 water\npropose");
    CHECK(t && strstr(t, "error: line 2: propose comes once, before the request's edits"));
    free(t);
    t = ctl_ask(&a, "tile A1 water\njobs");
    CHECK(t && strstr(t, "error: line 2: jobs comes before the request's propose line and edits"));
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
    CHECK(t && !strncmp(t, "error: line 1: ", 15));
    free(t);
    t = ctl_ask(&a, "job 1 say \"two ghouls coming\"");
    CHECK(t && !strcmp(t, "ok\n"));
    free(t);
    CHECK(strstr(a.status, "#1, the agent: two ghouls coming") != NULL);

    CASE("job N propose: run on a copy, so the map, its log, gen and modified wait for the GM");
    unsigned gen = m->gen, stamp = a.undo.stamp;
    int modified = m->modified, depth = a.undo.depth;
    t = ctl_ask(&a, "job 1 propose \"the crypt, walled\"\nroom B2:E5\ntile C4 water\ntoken add enemy C3 \"Ghoul\"\n");
    CHECK(t && !strncmp(t, "ok\nproposal #1: ", 16));
    CHECK(t && strstr(t, "\nwaiting for the GM's review - :review 1\n"));
    free(t);
    CHECK_EQ(m->gen, gen);
    CHECK_EQ(a.undo.stamp, stamp);
    CHECK_EQ(a.undo.depth, depth);
    CHECK_EQ(m->modified, modified);
    CHECK_EQ(map_tile(m, 2, 3), TILE_FLOOR);
    CHECK_EQ(map_vedge(m, 1, 1), EDGE_NONE);
    CHECK_EQ(m->tokens.n, 0);
    int s1 = job_slot(&a, 1);
    CHECK(s1 >= 0);
    if (s1 < 0) goto out;
    CHECK_EQ(a.jobs[s1].state, JOB_READY);
    CHECK(strstr(a.status, "#1 ready: ") && strstr(a.status, "the crypt, walled"));
    t = ctl_ask(&a, "jobs");
    CHECK(t && strstr(t, "  agent: the crypt, walled\n") && strstr(t, "  proposal: "));
    CHECK(t && !strstr(t, "conflict"));
    free(t);
    t = ctl_ask(&a, "job 1 area C3");
    CHECK(t && strstr(t, "has a proposal waiting"));
    free(t);

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
    CHECK(t && !strncmp(t, "error: ", 7));
    free(t);
    CHECK_EQ(map_tile(m, 2, 3), TILE_FLOOR);
    CHECK_EQ(m->tokens.n, 0);
    CHECK_EQ(m->gen, gen);

    CASE("a GM's edit inside the proposal since is a conflict, counted for the agent");
    undo_begin(&a.undo);
    undo_set_tile(&a.undo, m, 2, 3, TILE_HAZARD);         /* the GM's, where the water goes */
    undo_end(&a.undo);
    t = ctl_ask(&a, "jobs");
    CHECK(t && strstr(t, "conflict"));
    free(t);
    press(&a, "u");

    CASE("the GM accepts in review; the agent's undo takes it back while nothing happened since");
    press(&a, ":review 1\r");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    press(&a, "\r");
    CHECK_EQ(a.jobs[s1].state, JOB_ACCEPTED);
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
    t = ctl_ask(&a, "undo");                              /* once is all */
    CHECK(t && strstr(t, "there is no change of the agent's to take back"));
    free(t);

    CASE("propose: an agent's own idea is a job of its own; job N drop withdraws it");
    t = ctl_ask(&a, "propose \"a pool\"\ntile H8 water\n");
    CHECK(t && !strncmp(t, "ok\nproposal #2: ", 16));
    free(t);
    int s2 = job_slot(&a, 2);
    CHECK(s2 >= 0 && a.jobs[s2].from == JOB_FROM_AGENT && a.jobs[s2].state == JOB_READY);
    CHECK(s2 >= 0 && !strcmp(a.jobs[s2].text, "a pool"));
    t = ctl_ask(&a, "job 2 drop");
    CHECK(t && !strcmp(t, "ok\n#2 withdrawn\n"));
    free(t);
    CHECK(s2 >= 0 && a.jobs[s2].state == JOB_SCRAPPED && a.jobs[s2].has_cs);   /* kept: rule 9 */
    CHECK(strstr(a.status, "#2 withdrawn by the agent - :review 2 brings it back") != NULL);
    press(&a, ":ask 2 remove\r");                       /* the GM's way to destroy it */
    CHECK_EQ(job_slot(&a, 2), -1);
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
    CHECK_EQ(a.jobs[s3].state, JOB_READY);
    t = ctl_ask(&a, "job 3 drop");
    CHECK(t && !strcmp(t, "ok\n#3 given back, as asked\n"));
    free(t);
    CHECK(a.jobs[s3].state == JOB_ASKED && !a.jobs[s3].has_cs);
    t = ctl_ask(&a, "job 3 dump");
    CHECK(t && strstr(t, "#3 has no proposal"));
    free(t);

    CASE(":agent accept auto: a proposal lands at once, one undo step, the agent's to take back");
    press(&a, ":agent accept auto\r");
    CHECK_EQ(a.ctl_auto, 1);
    CHECK(strstr(a.status, "land at once") != NULL);
    depth = a.undo.depth;
    t = ctl_ask(&a, "job 3 propose\nedge B2|C2 door\n");
    CHECK(t && strstr(t, "\nchanged ") && strstr(t, "one undo step"));
    free(t);
    CHECK_EQ(a.jobs[s3].state, JOB_ACCEPTED);
    CHECK_EQ(a.undo.depth, depth + 1);
    t = ctl_ask(&a, "undo");
    CHECK(t && !strncmp(t, "ok\ntook back", 12));
    free(t);
    CHECK_EQ(a.undo.depth, depth);
    press(&a, ":agent accept review\r");
    CHECK_EQ(a.ctl_auto, 0);
    press(&a, ":agent accept maybe\r");
    CHECK(strstr(a.status, ":agent accept auto lands") != NULL);

    CASE("a full table of waiting changes refuses the next, and changes nothing");
    app_jobs_clear(&a);
    for (int k = 0; k < JOB_MAX; k++) {
        char req[64];
        snprintf(req, sizeof req, "tile %c10 water\n", 'A' + k);
        t = ctl_ask(&a, req);
        CHECK(t && !strncmp(t, "ok\nproposal #", 13));
        free(t);
    }
    t = ctl_ask(&a, "tile A11 water\n");
    CHECK(t && strstr(t, "error: 16 changes are waiting for the GM"));
    free(t);
    CHECK_EQ(map_tile(m, 0, 10), TILE_FLOOR);

    CASE("a full table makes room by a finished job, never a waiting one");
    {
        int fin = job_slot(&a, 5);
        CHECK(fin >= 0);
        if (fin >= 0) a.jobs[fin].state = JOB_SCRAPPED;
        t = ctl_ask(&a, "tile A11 water\n");
        CHECK(t && !strncmp(t, "ok\nproposal #5: ", 16));
        free(t);
        int ready = 0;
        for (int k = 0; k < JOB_MAX; k++) ready += a.jobs[k].used && a.jobs[k].state == JOB_READY;
        CHECK_EQ(ready, JOB_MAX);
        app_jobs_clear(&a);
    }

    CASE("reads about the GM's side mid-proposal read the live map: marked, status");
    a.ctl_auto = 1;
    t = ctl_ask(&a, "token add enemy B9 \"Imp\"\ntoken add enemy D9 \"Orc\"\n");
    free(t);
    a.ctl_auto = 0;
    app_jobs_clear(&a);
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
    app_jobs_clear(&a);
    app_key(&a, (Key){ KEY_F1, 0, 0 });
    char want[64];
    snprintf(want, sizeof want, "undo %d back, ", a.undo.depth);
    t = ctl_ask(&a, "tile A12 water\nstatus");
    CHECK(t && strstr(t, want) && strstr(t, "and this request's changes proposed"));
    free(t);
    app_jobs_clear(&a);

    CASE("job lines after a propose line are refused: the job it names cannot go from under it");
    press(&a, ":ask a well\r");
    t = ctl_ask(&a, "job 1 propose\njob 1 drop\ntile H9 water");
    CHECK(t && strstr(t, "error: line 2: job comes before the request's propose line and edits"));
    free(t);

    CASE("a proposal for the job under review never lands under the GM's eyes, even at once");
    t = ctl_ask(&a, "job 1 propose\ntile H9 water");
    free(t);
    press(&a, ":review 1\r");
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    a.ctl_auto = 1;
    t = ctl_ask(&a, "job 1 propose\ntile H9 hazard");
    CHECK(t && strstr(t, "\nlands when the GM is back"));
    free(t);
    CHECK_EQ(map_tile(m, 7, 8), TILE_FLOOR);
    CHECK_EQ(a.ed.mode, ED_REVIEW);
    press(&a, "j");                                    /* a key: still under review */
    CHECK_EQ(map_tile(m, 7, 8), TILE_FLOOR);
    press(&a, "\r");                                   /* the GM's enter lands it */
    CHECK_EQ(map_tile(m, 7, 8), TILE_HAZARD);
    a.ctl_auto = 0;

    CASE("job N dump reads only a ready proposal: an accepted one applied again would double it");
    t = ctl_ask(&a, "job 1 dump");
    CHECK(t && strstr(t, "#1 has no proposal waiting"));
    free(t);

    CASE("with no GM (--apply), propose is refused: the plan is the file's");
    a.ctl_direct = 1;
    t = ctl_ask(&a, "propose\ntile A1 water");
    CHECK(t && strstr(t, "there is no GM to propose to"));
    free(t);
    a.ctl_direct = 0;

out:
    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
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

    CASE("with no map, wait still answers; nothing has happened");
    t = ctl_ask(&a, "wait");
    CHECK(t && !strcmp(t, "ok\nseq 0\n"));
    free(t);

    CHECK(ctl_blank_map(&a, sb.dir, 20, 12));
    if (!a.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    Map *m = a.map;

    CASE("wait's words: a number, for SECONDS, alone in its request");
    t = ctl_ask(&a, "wait soon");
    CHECK(t && strstr(t, "is not an event's number"));
    free(t);
    t = ctl_ask(&a, "wait 0 for 601");
    CHECK(t && strstr(t, "at most 600 seconds"));
    free(t);
    t = ctl_ask(&a, "wait\nstatus");
    CHECK(t && !strcmp(t, "error: wait goes in a request of its own\n"));
    free(t);

    CASE("each turn of a job is an event, numbered in order: asked, accepted in part, scrapped, feedback, removed");
    a.ed.cx = 1; a.ed.cy = 1;
    press(&a, "v3l3j:ask a crypt\r");
    t = ctl_ask(&a, "wait 0");
    CHECK(t && !strcmp(t, "ok\nseq 1\n1 job 1 asked in B2:E5: a crypt\n"));
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
    CHECK(t && strstr(t, "ok\nseq 2\n2 job 1 accepted in part, B2: ground in B2 - the rest waits\n") == t);
    free(t);
    press(&a, "c");
    press(&a, "deeper\r");
    t = ctl_ask(&a, "wait 2");
    CHECK(t && !strcmp(t, "ok\nseq 3\n3 job 1 feedback: deeper\n"));
    free(t);
    t = ctl_ask(&a, "job 1 propose\ntile C2 hazard\n");
    free(t);
    press(&a, ":review 1\rd");
    press(&a, ":review 1\r");                              /* back, then whole */
    press(&a, "\r");
    press(&a, ":ask 1 remove\r");
    t = ctl_ask(&a, "wait 3");
    CHECK(t && strstr(t, "4 job 1 scrapped\n5 job 1 brought back for review\n6 job 1 accepted: ground in C2\n"
                         "7 job 1 removed by the GM\n"));
    free(t);

    CASE("no agent listening: no checkpoint runs");
    a.agent_seen_ms = 0;
    app_tick(&a, 1000);
    CHECK(m->cp == NULL);

    CASE("the map changed: one event once it has been quiet 1.5 s, and the checkpoint moves on");
    t = ctl_ask(&a, "wait");                               /* an agent is listening */
    free(t);
    app_tick(&a, 2000);
    CHECK(m->cp != NULL);
    unsigned seq0 = a.event_seq;
    undo_begin(&a.undo);
    undo_set_tile(&a.undo, m, 9, 9, TILE_ROUGH);
    undo_end(&a.undo);
    app_tick(&a, 3000);                                    /* seen */
    CHECK_EQ(app_events_due(&a, 3000), AUTOSAVE_QUIET_MS);
    app_tick(&a, 3000 + AUTOSAVE_QUIET_MS - 1);
    CHECK_EQ(a.event_seq, seq0);
    app_tick(&a, 3000 + AUTOSAVE_QUIET_MS);
    CHECK_EQ(a.event_seq, seq0 + 1);
    CHECK_EQ(app_events_due(&a, 9000), -1);                /* nothing owed: the loop sleeps */
    char want[96];
    snprintf(want, sizeof want, "%u map changed: ground in J10\n", seq0 + 1);
    t = ctl_ask(&a, "wait 0");
    CHECK(t && strstr(t, want));
    free(t);
    app_tick(&a, 20000);                                   /* told once */
    CHECK_EQ(a.event_seq, seq0 + 1);

    CASE("undone again before it is quiet: nothing changed, no event");
    undo_begin(&a.undo);
    undo_set_tile(&a.undo, m, 10, 9, TILE_ROUGH);
    undo_end(&a.undo);
    app_tick(&a, 21000);
    press(&a, "u");
    app_tick(&a, 22000);
    app_tick(&a, 30000);
    CHECK_EQ(a.event_seq, seq0 + 1);

    CASE("an accept is its own event, not also a map change; the GM's edit before it is told apart, first");
    a.ctl_auto = 1;
    undo_begin(&a.undo);
    undo_set_tile(&a.undo, m, 11, 9, TILE_ROUGH);          /* the GM's, not yet quiet */
    undo_end(&a.undo);
    t = ctl_ask(&a, "tile A12 water\n");                   /* lands at once */
    free(t);
    a.ctl_auto = 0;
    app_tick(&a, 31000);
    app_tick(&a, 40000);
    char two[160];
    snprintf(two, sizeof two, "%u map changed: ground in L10\n%u job 1 accepted: ground in A12\n", seq0 + 2, seq0 + 3);
    t = ctl_ask(&a, "wait 0");
    CHECK(t && strstr(t, two));
    CHECK_EQ(a.event_seq, seq0 + 3);
    free(t);

    CASE("more events than the ring keeps: the gap is said");
    for (int k = 0; k < EVENT_MAX + 5; k++) app_event(&a, "test %d", k);
    t = ctl_ask(&a, "wait 0");
    snprintf(want, sizeof want, "lost 1-%u: ", a.event_seq - EVENT_MAX);
    CHECK(t && strstr(t, want) && strstr(t, " test 4\n") == NULL && strstr(t, " test 36\n"));
    free(t);

    CASE("closing the map is an event; wait hears it with no map open");
    seq0 = a.event_seq;
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
