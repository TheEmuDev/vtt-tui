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
#include "link.h"
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

    CASE("notes on a bare World: set, taken off, too long and too many refused as limits");
    {
        WorldErr e;
        char longer[NOTE_MAX + 4];
        memset(longer, 'x', sizeof longer - 1);
        longer[sizeof longer - 1] = '\0';
        CHECK(world_note_set(&w, 1, 1, longer, &e) < 0 && e.cls == CTL_E_LIMIT && strstr(e.text, "the note is over"));
        undo_begin(&w.undo);
        CHECK_EQ(world_note_set(&w, 1, 1, "trap", &e), 0);
        undo_end(&w.undo);
        CHECK(map_note_at(live, 1, 1) && !strcmp(map_note_at(live, 1, 1), "trap"));
        int had = live->nnotes;
        undo_begin(&w.undo);
        for (int i = 0; live->nnotes < MAP_NOTES_MAX; i++) CHECK_EQ(world_note_set(&w, i % 20, 2 + i / 20, "n", &e), 0);
        CHECK(world_note_set(&w, 19, 11, "one more", &e) < 0 && e.cls == CTL_E_LIMIT);
        CHECK(!strcmp(e.text, "no room: a map holds 64 notes on squares"));
        undo_end(&w.undo);
        undo_undo(&w.undo, live);
        CHECK_EQ(live->nnotes, had);
        undo_begin(&w.undo);
        CHECK_EQ(world_note_set(&w, 1, 1, "", &e), 0);
        undo_end(&w.undo);
        CHECK(!map_note_at(live, 1, 1));
    }

    CASE("links on a bare World: a number, put down, refused on void, taken away, and none left past the last");
    {
        WorldErr e;
        Link l;
        memset(&l, 0, sizeof l);
        l.num = (uint8_t)world_link_num(&w, &e);
        l.size = 1;
        l.x[0] = 2; l.y[0] = 9; l.x[1] = 6; l.y[1] = 9;
        CHECK(l.num >= 1);
        map_set_tile(live, 6, 9, TILE_VOID);
        CHECK(world_link_set(&w, &l, &e) < 0 && e.cls == CTL_E_CONFLICT && !strcmp(e.text, "an end is on void"));
        map_set_tile(live, 6, 9, TILE_FLOOR);
        undo_begin(&w.undo);
        CHECK_EQ(world_link_set(&w, &l, &e), 0);
        undo_end(&w.undo);
        CHECK(link_find(live, l.num) >= 0);
        CHECK(world_link_remove(&w, 99, &e) < 0 && e.cls == CTL_E_MISSING);
        undo_begin(&w.undo);
        CHECK_EQ(world_link_remove(&w, l.num, &e), 0);
        undo_end(&w.undo);
        CHECK(link_find(live, l.num) < 0);
        int had = live->nlinks;
        for (int num = 1; live->nlinks < MAP_LINKS_MAX; num++) {
            Link f = l;
            f.num = (uint8_t)num;
            if (link_find(live, num) < 0) link_put(live, &f);
        }
        CHECK(world_link_num(&w, &e) < 0 && e.cls == CTL_E_LIMIT && !strcmp(e.text, "no room: a map holds 64 links"));
        while (live->nlinks > had) live->nlinks--;
    }

    CASE("floors on a bare World: made one, refused out of range, overlapping or when not one, made none");
    {
        WorldErr e;
        undo_begin(&w.undo);
        CHECK_EQ(world_area_set(&w, "Low", 0, 0, 4, 4, &e), 0);
        CHECK_EQ(world_area_set(&w, "High", 3, 3, 8, 8, &e), 0);
        undo_end(&w.undo);
        int lo = map_area_find(live, "Low"), hi = map_area_find(live, "High");
        CHECK(world_floor_off(&w, lo, &e) < 0 && e.cls == CTL_E_STATE && !strcmp(e.text, "Low is not a floor"));
        CHECK(world_floor_set(&w, lo, 100, &e) < 0 && e.cls == CTL_E_USAGE && !strcmp(e.text, "a level is -99 to 99"));
        undo_begin(&w.undo);
        CHECK_EQ(world_floor_set(&w, lo, -1, &e), 0);
        undo_end(&w.undo);
        CHECK(live->areas[lo].floor && live->areas[lo].level == -1);
        CHECK(world_floor_set(&w, hi, 1, &e) < 0 && e.cls == CTL_E_CONFLICT && !strncmp(e.text, "High cannot be a floor: ", 24));
        undo_begin(&w.undo);
        CHECK_EQ(world_floor_off(&w, lo, &e), 0);
        undo_end(&w.undo);
        CHECK(!live->areas[lo].floor);
    }

    CASE("environments on a bare World: a card named, a bad name refused, taken off, and none to take off");
    {
        WorldErr e;
        int lo = map_area_find(live, "Low");
        CHECK(world_area_env(&w, lo, "bad name", &e) < 0 && e.cls == CTL_E_USAGE &&
              !strcmp(e.text, "bad name: a card's name is letters, digits, - and _"));
        CHECK(world_area_env(&w, lo, "", &e) < 0 && e.cls == CTL_E_STATE && !strcmp(e.text, "Low has no environment"));
        undo_begin(&w.undo);
        CHECK_EQ(world_area_env(&w, lo, "damp-cellar", &e), 0);
        undo_end(&w.undo);
        CHECK(!strcmp(live->areas[lo].env, "damp-cellar"));
        undo_begin(&w.undo);
        CHECK_EQ(world_area_env(&w, lo, "", &e), 0);
        undo_end(&w.undo);
        CHECK(!live->areas[lo].env[0]);
    }

    CASE("stamps on a bare World: put down as one step; off the map a conflict, a map full of notes a limit");
    {
        WorldErr e;
        Map *st = map_new(2, 1, "Pool");
        map_set_tile(st, 0, 0, TILE_WATER);
        map_set_tile(st, 1, 0, TILE_WATER);
        CHECK(world_stamp_place(&w, st, 19, 0, &e) < 0 && e.cls == CTL_E_CONFLICT && strstr(e.text, "runs off the map"));
        int d = w.undo.depth;
        CHECK_EQ(world_stamp_place(&w, st, 0, 0, &e), 0);
        CHECK(map_tile(live, 1, 0) == TILE_WATER && w.undo.depth == d + 1);
        undo_undo(&w.undo, live);
        map_note_set(st, 0, 0, "the stamp's");
        int had = live->nnotes;
        for (int i = 0; live->nnotes < MAP_NOTES_MAX; i++) map_note_set(live, i % 20, 2 + i / 20, "full");
        CHECK(world_stamp_place(&w, st, 0, 0, &e) < 0 && e.cls == CTL_E_LIMIT &&
              !strcmp(e.text, "no room: a map holds 64 notes on squares"));
        while (live->nnotes > had) live->nnotes--;
        map_free(st);
    }

    CASE("scenes on a bare World: saved, found, put back as one step; a bad name, a missing one refused");
    {
        WorldErr e;
        CHECK(world_scene_find(&w, "Ambush", &e) < 0 && e.cls == CTL_E_MISSING && !strcmp(e.text, "no scene called Ambush"));
        CHECK(world_scene_save(&w, "", NULL, &e) < 0 && e.cls == CTL_E_USAGE);
        int i = world_scene_save(&w, "Ambush", NULL, &e);
        CHECK(i >= 0 && world_scene_find(&w, "ambush", &e) == i);
        int d = w.undo.depth;
        CHECK(world_scene_restore(&w, i, &e) >= 0 && w.undo.depth == d + 1);   /* the creatures there: it records */
        Token troll;
        memset(&troll, 0, sizeof troll);
        troll.x = 14; troll.y = 9; troll.size = 2; troll.kind = TOKEN_ENEMY;
        str_lcpy(troll.label, "Troll", sizeof troll.label);
        int ti = undo_add_token(&w.undo, live, troll);
        int box[4] = { 15, 10, 15, 10 };                      /* meets the troll's corner only */
        int c = world_scene_save(&w, "Corner", box, &e);
        undo_move_token(&w.undo, live, ti, 0, 10);            /* the troll leaves */
        Token cara = troll;
        cara.size = 1;
        str_lcpy(cara.label, "Cara", sizeof cara.label);
        undo_add_token(&w.undo, live, cara);                  /* Cara stands in its old corner, outside the box */
        CHECK(c >= 0 && world_scene_restore(&w, c, &e) < 0 && e.cls == CTL_E_CONFLICT &&
              strstr(e.text, "Troll would come back onto Cara"));
    }

    CASE("the jobs' change sets are bounded together: past JOB_CELLS_MAX squares an agent's proposal is a limit");
    {
        world_jobs_clear(&w);
        int k = world_job_new(&w, JOB_FROM_AGENT, "a held set", NULL, 0);
        CHECK(k >= 0);
        w.jobs[k].has_cs = 1;
        w.jobs[k].cs.ncells = (int)(JOB_CELLS_MAX - 5);          /* its cells are counted, never read */
        t = ask(&w, "tile A1:J1 water");                           /* ten squares: past it */
        CHECK(t && !strncmp(t, "error limit: ", 13) && strstr(t, "would pass the 4194304 they may hold"));
        free(t);
        CHECK_EQ(world_jobs_cells(&w, -1), JOB_CELLS_MAX - 5);     /* nothing came in */
        w.jobs[k].cs.ncells = (int)(JOB_CELLS_MAX - 10);
        t = ask(&w, "tile A1:J1 water");                           /* ten: exactly to it */
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        w.jobs[k].cs.ncells = 0;                                   /* its own, for the clear */
        w.jobs[k].has_cs = 0;
    }

    CASE("a job's number is a whole number from 1 to JOB_NUM_MAX: request_int, the one check");
    {
        static const char *const BAD[] = { "0", "100", "1x", "-1", "99999999999999999999999" };
        for (size_t i = 0; i < sizeof BAD / sizeof *BAD; i++) {
            char req[64];
            snprintf(req, sizeof req, "job %s take", BAD[i]);
            t = ask(&w, req);
            CHECK(t && !strncmp(t, "error usage: ", 13) && strstr(t, "a job's number, from jobs"));
            free(t);
        }
        t = ask(&w, "job 99 take");                                /* a number, but no such job */
        CHECK(t && !strncmp(t, "error missing: ", 15));
        free(t);
    }

    CASE("at the cap with every slot a finished job's, a proposal fits: the job it evicts takes its cells along");
    {
        world_jobs_clear(&w);
        for (int i = 0; i < JOB_MAX; i++) {
            int s = world_job_new(&w, JOB_FROM_AGENT, "finished", NULL, 0);
            CHECK(s == i);
            w.jobs[s].state  = JOB_ACCEPTED;
            w.jobs[s].has_cs = 1;
            w.jobs[s].cs.ncells = (int)(JOB_CELLS_MAX / JOB_MAX);   /* counted, never read */
        }
        CHECK_EQ(world_jobs_cells(&w, -1), JOB_CELLS_MAX);
        int evict = world_job_slot_for_new(&w);
        CHECK(evict >= 0);
        t = ask(&w, "tile A1:J1 water");
        CHECK(t && !strncmp(t, "ok\n", 3));
        free(t);
        CHECK(w.jobs[evict].state == JOB_READY && w.jobs[evict].cs.ncells == 10);   /* the slot the cap skipped */
        for (int i = 0; i < JOB_MAX; i++)
            if (i != evict) { w.jobs[i].cs.ncells = 0; w.jobs[i].has_cs = 0; }
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
