/* The protocol on a bare World (docs/V2.md, D2 and *Verification* 3): the
 * channel's requests and the jobs run with no App behind them -- no editor,
 * no renderer, no terminal -- against a host that only records what it was
 * asked. This file includes no app header, and `make test` also builds it
 * alone, with no src/app*.c linked: a handler that reached for the app
 * would not link (build/protocol). */

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "request.h"
#include "world.h"
#include "world_ops.h"
#include "util.h"

void test_protocol(void);

typedef struct {
    World      *w;
    char        notes[12][256];
    int         logged[12], nnotes;
    const char *busy;
    int         landed, landed_creatures, tagged_at_landing;
    int         reviewing, review_left;
    int         gone[8], ngone;
    int         deaf;
    int         redraws;
    int         changed[12][4], nchanged;
    int         ring[4], rings;
    int         screen_lines, marked, marked_json, floor_asked;
    char        refused[300];
    int         nrefused;
    char        heard[CTL_NAME_MAX];
} Rec;

static void rec_note(void *gm, const char *text, int logged)
{
    Rec *r = gm;
    if (r->nnotes == 12) return;
    str_lcpy(r->notes[r->nnotes], text, sizeof r->notes[0]);
    r->logged[r->nnotes++] = logged;
}
static const char *rec_busy(void *gm) { return ((Rec *)gm)->busy; }
static void rec_landed(void *gm, int creatures)
{
    Rec *r = gm;
    r->landed++;
    r->landed_creatures = creatures;
    r->tagged_at_landing = undo_tag_top(&r->w->undo) != NULL;      /* the screen's work comes before the step is tagged */
}
static int  rec_reviewing(void *gm) { return ((Rec *)gm)->reviewing; }
static void rec_review_leave(void *gm) { ((Rec *)gm)->review_left++; ((Rec *)gm)->reviewing = -1; }
static void rec_job_gone(void *gm, int num) { Rec *r = gm; if (r->ngone < 8) r->gone[r->ngone++] = num; }
static int  rec_deaf(void *gm) { return ((Rec *)gm)->deaf; }
static void rec_redraw(void *gm) { ((Rec *)gm)->redraws++; }
static void rec_changed(void *gm, int what, int i, int x, int y)
{
    Rec *r = gm;
    if (r->nchanged == 12) return;
    int *c = r->changed[r->nchanged++];
    c[0] = what; c[1] = i; c[2] = x; c[3] = y;
}
static void rec_ring(void *gm, int x0, int y0, int x1, int y1)
{
    Rec *r = gm;
    r->rings++;
    r->ring[0] = x0; r->ring[1] = y0; r->ring[2] = x1; r->ring[3] = y1;
}
static void rec_screen_lines(void *gm, FILE *out) { ((Rec *)gm)->screen_lines++; fputs("screen recorded\n", out); }
static void rec_marked(void *gm, FILE *out, int json) { Rec *r = gm; r->marked++; r->marked_json = json; fputs("marked recorded\n", out); }
static int  rec_floor_shown(void *gm) { ((Rec *)gm)->floor_asked++; return -1; }
static void rec_refused(void *gm, const char *verdict) { Rec *r = gm; r->nrefused++; str_lcpy(r->refused, verdict, sizeof r->refused); }
static void rec_heard(void *gm, const char *who) { str_lcpy(((Rec *)gm)->heard, who, CTL_NAME_MAX); }

static const WorldHost REC_HOST = {
    rec_note, rec_busy, rec_landed, rec_reviewing, rec_review_leave, rec_job_gone, rec_deaf,
    rec_redraw, rec_changed, rec_ring, rec_screen_lines, rec_marked, rec_floor_shown, rec_refused, rec_heard,
};

/* What the host heard since the last look is forgotten; what it answers stays. */
static void rec_reset(Rec *r)
{
    World *w = r->w;
    const char *busy = r->busy;
    int reviewing = r->reviewing, deaf = r->deaf;
    memset(r, 0, sizeof *r);
    r->w = w; r->busy = busy; r->reviewing = reviewing; r->deaf = deaf;
}

static char *ask(World *w, const char *req)
{
    size_t len = strlen(req);
    char  *ans = request_exec(w, req, &len);
    CHECK(ans != NULL && strlen(ans) == len);
    return ans;
}

static int note_has(const Rec *r, const char *needle, int logged)
{
    for (int i = 0; i < r->nnotes; i++)
        if (strstr(r->notes[i], needle) && r->logged[i] == logged) return 1;
    return 0;
}

void test_protocol(void)
{
    static World w;                    /* its jobs and events are a megabyte: not the stack's */
    static Rec   rec;
    memset(&w, 0, sizeof w);
    memset(&rec, 0, sizeof rec);
    undo_init(&w.undo);
    undo_init(&w.ctl_sundo);
    w.map  = map_new(20, 12, "Bare");
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 20; x++) map_set_tile(w.map, x, y, TILE_FLOOR);
    w.host = &REC_HOST;
    w.gm   = &rec;
    rec.w  = &w;
    rec.reviewing = -1;
    Map  *live = w.map;
    char *t;

    CASE("the reads: answered from the world alone, the screen's part asked of the host");
    t = ask(&w, "status");
    CHECK(t && !strncmp(t, "ok\nmap Bare  20x12\n", 19) && strstr(t, "\nscreen recorded\n") && strstr(t, "\nyou cli\n") &&
          strstr(t, "\nagent off\nseq 0\n"));
    CHECK_EQ(rec.screen_lines, 1);
    free(t);
    t = ask(&w, "dump B2:C3");
    CHECK(t && !strcmp(t, "ok\n   B C\n\n2  . .\n\n3  . .\n\n\nmap  Bare  20x12  scale 5 ft  metric alt  ruleset none\nregion B2:C3\n"));
    free(t);
    t = ask(&w, "marked json");
    CHECK(t && !strcmp(t, "ok\nmarked recorded\n") && rec.marked == 1 && rec.marked_json == 1);
    free(t);
    t = ask(&w, "help undo");
    CHECK(t && !strncmp(t, "ok\nundo\n", 8));
    free(t);
    t = ask(&w, "jobs");
    CHECK(t && !strcmp(t, "ok\nno jobs\n"));
    free(t);
    CHECK(rec.nnotes == 0 && rec.landed == 0 && rec.nrefused == 0 && w.map == live);

    CASE("who is asking is the world's to know, and the host hears of it");
    t = ask(&w, "agent job-3\nstatus");
    CHECK(t && strstr(t, "\nyou job-3\n") && !strcmp(rec.heard, "job-3"));
    free(t);

    CASE("a refusal: the verdict to the caller, and the same line to the host for the log");
    t = ask(&w, "tile Z99 water");
    CHECK(t && !strncmp(t, "error usage: line 1: ", 21) && rec.nrefused == 1);
    CHECK(t && !strncmp(rec.refused, t, strlen(rec.refused)) && strlen(rec.refused) + 1 == strlen(t));
    free(t);
    CHECK(w.map == live && map_tile(live, 1, 1) == TILE_FLOOR && rec.landed == 0);

    CASE("edits are a proposal: a job made ready, a line for the GM, nothing landed, the live map as it was");
    rec_reset(&rec);
    t = ask(&w, "agent mapper\ntile B2 water");
    CHECK(t && !strncmp(t, "ok\nproposal #1: ", 16) && strstr(t, "waiting for the GM's review"));
    free(t);
    CHECK(w.map == live && map_tile(live, 1, 1) == TILE_FLOOR);
    CHECK(w.jobs[0].used && w.jobs[0].state == JOB_READY && !strcmp(w.jobs[0].taker, "mapper"));
    CHECK(note_has(&rec, "#1 ready: ", 1) && rec.landed == 0 && rec.rings == 0 && rec.redraws == 1);
    CHECK(w.event_seq == 1 && !strcmp(w.events[1].text, "job 1 proposed: ground in B2") && !strcmp(w.events[1].by, "mapper"));
    t = ask(&w, "agent other\njobs");
    CHECK(t && !strcmp(t, "ok\n#1 ready, an agent's own, taken by mapper\n  -: proposed: ground in B2\n  proposal: ground in B2\n"));
    free(t);

    CASE("the GM's accept is the world's landing: the screen first, then the step tagged, the event, the line");
    rec_reset(&rec);
    world_job_land(&w, &w.jobs[0], NULL);
    CHECK_EQ(map_tile(live, 1, 1), TILE_WATER);
    CHECK(rec.landed == 1 && rec.landed_creatures == 0 && rec.tagged_at_landing == 0);
    CHECK(undo_tag_top(&w.undo) && undo_tag_top(&w.undo)->job == 1 && !strcmp(undo_tag_top(&w.undo)->who, "mapper"));
    CHECK(w.jobs[0].state == JOB_ACCEPTED && note_has(&rec, "#1 accepted: ", 1));
    CHECK_EQ(rec.nnotes, 1);                             /* one line for one landing */
    CHECK(!strcmp(w.events[w.event_seq % EVENT_MAX].text, "job 1 accepted: ground in B2") && !w.events[w.event_seq % EVENT_MAX].by[0]);

    CASE("its taker takes it back: the host is told the log stepped back, the others are told who");
    rec_reset(&rec);
    t = ask(&w, "agent other\njob 1 undo");
    CHECK(t && !strncmp(t, "error state: line 1: ", 21) && rec.nrefused == 1);
    free(t);
    rec.busy = "the GM is answering a prompt";
    t = ask(&w, "agent mapper\njob 1 undo");
    CHECK(t && !strcmp(t, "busy: the GM is answering a prompt\n") && map_tile(live, 1, 1) == TILE_WATER);
    free(t);
    rec.busy = NULL;
    rec.reviewing = 0;                                   /* the GM looking at that very job: not from under their eyes */
    t = ask(&w, "agent mapper\njob 1 undo");
    CHECK(t && !strcmp(t, "busy: the GM is reviewing the rest of that job\n") && map_tile(live, 1, 1) == TILE_WATER);
    free(t);
    rec.reviewing = -1;
    rec_reset(&rec);
    t = ask(&w, "agent mapper\njob 1 undo");
    CHECK(t && !strcmp(t, "ok\ntook back #1\n") && map_tile(live, 1, 1) == TILE_FLOOR);
    free(t);
    CHECK_EQ(rec.nnotes, 1);
    CHECK(rec.nchanged == 1 && rec.changed[0][0] == WORLD_UNDONE && note_has(&rec, "#1 taken back by mapper", 1));
    CHECK(!strcmp(w.events[w.event_seq % EVENT_MAX].text, "job 1 undone: taken back by mapper"));
    CHECK_EQ(world_events_after(&w, w.event_seq - 1, "mapper"), 0);
    CHECK_EQ(world_events_after(&w, w.event_seq - 1, "other"), 1);

    CASE("to land at once: only when the GM is not busy and not looking at that job; creatures say so to the screen");
    w.ctl_auto = 1;
    rec_reset(&rec);
    t = ask(&w, "agent mapper\ntoken add enemy D4 \"Ghoul\"");
    CHECK(t && strstr(t, "proposal #2: ") && strstr(t, "\nchanged D4: 1 line, one undo step\n"));
    free(t);
    CHECK(rec.landed == 1 && rec.landed_creatures == 1 && live->tokens.n == 1 && rec.nnotes == 1);
    CHECK(rec.rings == 1 && rec.ring[0] == 3 && rec.ring[1] == 3 && rec.ring[2] == 3 && rec.ring[3] == 3);
    rec_reset(&rec);
    rec.busy = "the GM is in play mode - edits are build mode's";
    t = ask(&w, "agent mapper\ntile F6 water");
    CHECK(t && strstr(t, "proposal #3: ") && strstr(t, "lands when the GM is back: the GM is in play mode"));
    free(t);
    CHECK(rec.landed == 0 && map_tile(live, 5, 5) == TILE_FLOOR && note_has(&rec, "#3 ready: ", 1));
    int s3 = world_job_find(&w, 3);
    rec.busy = NULL;
    rec.reviewing = s3;                                  /* on the GM's screen: never under their eyes */
    world_jobs_land_waiting(&w);
    CHECK(rec.landed == 0 && map_tile(live, 5, 5) == TILE_FLOOR);
    rec.reviewing = -1;
    world_jobs_land_waiting(&w);
    CHECK(rec.landed == 1 && map_tile(live, 5, 5) == TILE_WATER);
    /* And a proposal that comes in for the job on the GM's screen waits there, at once or not. */
    rec_reset(&rec);
    int s5 = world_job_new(&w, JOB_FROM_GM, "a pit", NULL, 1);
    rec.reviewing = s5;
    char pq[64];
    snprintf(pq, sizeof pq, "agent mapper\njob %d propose\ntile J10 water", w.jobs[s5].num);
    t = ask(&w, pq);
    CHECK(t && !strncmp(t, "ok\n", 3));
    free(t);
    CHECK(rec.landed == 0 && map_tile(live, 9, 9) == TILE_FLOOR && w.jobs[s5].state == JOB_READY && rec.nnotes == 1);
    rec.reviewing = -1;
    world_job_clear(&w, &w.jobs[s5]);
    w.job_last = 3;
    w.ctl_auto = 0;

    CASE("a job asked by the GM: taken, given back, and each a redraw and an event for the others");
    rec_reset(&rec);
    int s4 = world_job_new(&w, JOB_FROM_GM, "a well", NULL, 0);
    CHECK(s4 >= 0 && w.jobs[s4].num == 4);
    unsigned e0 = w.event_seq;
    t = ask(&w, "agent mapper\njob 4 take");
    CHECK(t && !strncmp(t, "ok\n#4 working, asked by the GM, taken by mapper: a well\n", 55));
    free(t);
    CHECK(rec.redraws == 1 && note_has(&rec, "#4 taken by mapper", 1));
    CHECK(w.event_seq == e0 + 1 && !strcmp(w.events[w.event_seq % EVENT_MAX].text, "job 4 taken: by mapper"));
    t = ask(&w, "agent other\njob 4 drop");
    CHECK(t && !strncmp(t, "error state: line 1: #4 is mapper's, and you are other", 54));
    free(t);
    t = ask(&w, "agent mapper\njob 4 drop");
    CHECK(t && !strcmp(t, "ok\n#4 given back, as asked\n") && w.jobs[s4].state == JOB_ASKED && !w.jobs[s4].taker[0]);
    free(t);

    CASE("a job going tells the host its number, once, and the review of it is put away");
    rec_reset(&rec);
    rec.reviewing = s4;
    world_job_clear(&w, &w.jobs[s4]);
    CHECK(rec.ngone == 1 && rec.gone[0] == 4 && !w.jobs[s4].used);
    world_job_clear(&w, &w.jobs[s4]);                    /* an unused slot is nobody's */
    CHECK_EQ(rec.ngone, 1);
    rec.reviewing = 2;
    world_jobs_clear(&w);
    CHECK(rec.review_left == 1 && rec.ngone == 4);       /* jobs 1, 2 and 3 went too */
    for (int i = 0; i < JOB_MAX; i++) CHECK(!w.jobs[i].used);

    CASE("hearing: a world that is deaf starts no checkpoint; one that listens does");
    rec.deaf = 1;
    world_events_hear(&w);
    CHECK(live->cp == NULL);
    rec.deaf = 0;
    world_events_hear(&w);
    CHECK(live->cp != NULL);

    CASE("with no GM (--apply on a file) the edits are the map's own, and the screen is told how it moved");
    w.ctl_direct = 1;
    rec_reset(&rec);
    t = ask(&w, "token add enemy H8 \"Wight\"");
    CHECK(t && !strncmp(t, "ok\nchanged H8: 1 line, one undo step\n", 37));
    free(t);
    CHECK(rec.nchanged == 1 && rec.changed[0][0] == WORLD_EDITED && rec.rings == 1 && rec.landed == 0);
    int wight = -1;
    for (int i = 0; i < live->tokens.n; i++) if (!strcmp(live->tokens.v[i].label, "Wight")) wight = i;
    rec_reset(&rec);
    t = ask(&w, "token del Wight\ntile Z99 water");          /* the second line fails: all of it is taken back */
    CHECK(t && !strncmp(t, "error usage: line 2: ", 21));
    free(t);
    CHECK(rec.nchanged == 3 && rec.changed[0][0] == WORLD_TOKEN_GONE && rec.changed[0][1] == wight &&
          rec.changed[0][2] == 7 && rec.changed[0][3] == 7 && rec.changed[1][0] == WORLD_ROLLED_BACK &&
          rec.changed[2][0] == WORLD_EDITED);
    CHECK(wight >= 0 && live->tokens.n == 2 && rec.nrefused == 1);
    w.ctl_direct = 0;

    CASE("the GM's and the channel's operations on a bare World: areas named, refused with a class, taken off");
    {
        WorldErr e;
        int box[4];
        CHECK(world_area_name("C3", &e) < 0 && e.cls == CTL_E_USAGE && strstr(e.text, "an area's name is"));
        undo_begin(&w.undo);
        CHECK_EQ(world_area_set(&w, "Vault", 0, 0, 2, 2, &e), 0);
        undo_end(&w.undo);
        CHECK(map_area_find(live, "vault") >= 0);
        CHECK(world_area_remove(&w, "Nope", box, &e) < 0 && e.cls == CTL_E_MISSING && !strcmp(e.text, "no area called Nope"));
        int had = live->nareas;
        undo_begin(&w.undo);
        for (int k = live->nareas; k < MAP_AREAS_MAX; k++) {
            char name[16];
            snprintf(name, sizeof name, "Fill%d", k);
            CHECK_EQ(world_area_set(&w, name, 5, 5, 5, 5, &e), 0);
        }
        CHECK(world_area_set(&w, "OneMore", 5, 5, 5, 5, &e) < 0 && e.cls == CTL_E_LIMIT);
        CHECK(!strcmp(e.text, "no room: a map holds 64 named areas"));
        undo_end(&w.undo);
        undo_undo(&w.undo, live);
        CHECK_EQ(live->nareas, had);
        undo_begin(&w.undo);
        CHECK(world_area_remove(&w, "VAULT", box, &e) == 0 && box[0] == 0 && box[2] == 2 && box[3] == 2);
        undo_end(&w.undo);
        CHECK_EQ(map_area_find(live, "Vault"), -1);
    }

    world_jobs_clear(&w);
    if (w.ctl_scratch) map_free(w.ctl_scratch);
    undo_free(&w.undo);
    undo_free(&w.ctl_sundo);
    map_free(live);
}

#ifdef PROTOCOL_MAIN
int g_checks, g_fails;
const char *g_case = "";

int main(void)
{
    test_protocol();
    printf("protocol, with no app linked: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
#endif
