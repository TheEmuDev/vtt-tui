/* Jobs and the review (docs/CONFLICTS.md): :ask, :ask!, :jobs, :review, the
 * review mode's keys, the tints and labels, and feedback. The jobs themselves
 * are job.h's and their core world.c's (a new one, a proposal coming in, a
 * landing); what a proposal is, and how it is drawn in place, accepted and
 * checked, is changeset.c's. Every message is the GM's alone. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "draw.h"
#include "prof.h"

/* ------------------------------------------------------------- review */

static int ready_after(const App *a, int from, int dir)
{
    for (int k = 1; k <= JOB_MAX; k++) {
        int i = ((from + dir * k) % JOB_MAX + JOB_MAX) % JOB_MAX;
        if (a->world.jobs[i].used && a->world.jobs[i].state == JOB_READY && a->world.jobs[i].has_cs) return i;
    }
    return -1;
}

static void review_status(App *a)
{
    Job *j = &a->world.jobs[a->review.slot];
    int conflicts = cs_check(&j->cs, a->world.map);
    char msg[256];
    snprintf(msg, sizeof msg, "#%d %.70s: %.110s%s%s", j->num, j->text[0] ? j->text : "an agent's change",
             j->summary, conflicts ? " - in red: " : "", conflicts ? "would overwrite changes since" : "");
    app_set_status_gm(a, msg);
}

static void review_open(App *a, int slot)
{
    a->review.slot = slot;
    a->review.box = 0;
    a->ed.mode = ED_REVIEW;
    review_status(a);
}

/* The one way out of a review, by any road: F1/F2, :play, :stamp, a job
 * taken away. ED_REVIEW holds exactly when `review` names a job. */
void app_review_leave(App *a)
{
    if (a->ed.mode == ED_REVIEW) a->ed.mode = ED_NORMAL;
    a->review.box = 0;
    a->review.slot = -1;
}

static void review_close(App *a, const char *why)
{
    app_review_leave(a);
    app_set_status_gm(a, why);
}

/* After the reviewed job is done with: the next ready one, else out. */
static void review_next_or_close(App *a)
{
    int next = ready_after(a, a->review.slot, 1);
    if (next >= 0 && next != a->review.slot) review_open(a, next);
    else app_review_leave(a);
}

static CsBox rv_box_now(const App *a)
{
    CsBox b = { imin(a->review.ax, a->ed.cx), imin(a->review.ay, a->ed.cy), imax(a->review.ax, a->ed.cx), imax(a->review.ay, a->ed.cy) };
    return b;
}

void app_review_key(App *a, Key k)
{
    PROF_ZONE("job.review");
    Editor *e = &a->ed;
    Map    *m = a->world.map;
    if (a->review.slot < 0 || !a->world.jobs[a->review.slot].used || a->world.jobs[a->review.slot].state != JOB_READY) {
        review_close(a, "");
        return;
    }
    Job *j = &a->world.jobs[a->review.slot];

    if (k.kind == KEY_ESC) {
        e->count = 0;
        if (a->review.box) { a->review.box = 0; review_status(a); return; }    /* one layer at a time */
        char msg[96];
        snprintf(msg, sizeof msg, "#%d stays ready - :review %d", j->num, j->num);
        review_close(a, msg);
        return;
    }
    if (k.kind == KEY_ENTER) {
        e->count = 0;
        CsBox b = rv_box_now(a);
        world_job_land(&a->world, j, a->review.box ? &b : NULL);
        a->review.box = 0;
        if (j->state != JOB_READY) review_next_or_close(a);
        else review_status(a);
        return;
    }
    if (k.kind == KEY_LEFT)  { ed_move(e, m, -1, 0, take_count(e)); return; }
    if (k.kind == KEY_RIGHT) { ed_move(e, m,  1, 0, take_count(e)); return; }
    if (k.kind == KEY_UP)    { ed_move(e, m,  0, -1, take_count(e)); return; }
    if (k.kind == KEY_DOWN)  { ed_move(e, m,  0,  1, take_count(e)); return; }
    if (k.kind == KEY_CHAR && (k.mods & MOD_CTRL)) {
        if (k.ch == 'r' && undo_redo(&a->world.undo, m)) app_set_status_gm(a, "redo");
        return;
    }
    if (k.kind != KEY_CHAR || k.mods) return;
    if (k.ch >= '1' && k.ch <= '9') { count_digit(e, k.ch); return; }
    if (k.ch == '0' && e->count)    { e->count *= 10; return; }

    char msg[200];
    if (k.ch > 127 || !strchr("hjkl", (int)k.ch)) e->count = 0;   /* a count is for moving */
    switch (k.ch) {
    case 'h': ed_move(e, m, -1,  0, take_count(e)); break;
    case 'l': ed_move(e, m,  1,  0, take_count(e)); break;
    case 'k': ed_move(e, m,  0, -1, take_count(e)); break;
    case 'j': ed_move(e, m,  0,  1, take_count(e)); break;
    case 'v':
        a->review.box = !a->review.box;
        a->review.ax = e->cx;
        a->review.ay = e->cy;
        app_set_status_gm(a, a->review.box ? "box: move to its far corner, enter accepts what it holds, esc drops it"
                                       : "box dropped");
        break;
    case 'd':
        j->state = JOB_SCRAPPED;
        world_job_thread_add(j, '-', "scrapped");
        world_event(&a->world, CTL_EV_SCRAPPED, j->num, NULL);
        snprintf(msg, sizeof msg, "#%d scrapped - :review %d brings it back", j->num, j->num);
        app_note_gm(a, msg);
        review_next_or_close(a);
        break;
    case 'c':
        if (j->from == JOB_FROM_DISK) {
            app_set_status_gm(a, "a file cannot be asked for something else - enter takes it, d leaves it out");
            break;
        }
        a->prompt_what = PROMPT_JOB_FEEDBACK;
        a->modal = MODAL_PROMPT;
        snprintf(msg, sizeof msg, "What should #%d do instead?", j->num);
        ui_prompt_open(&a->prompt, msg, "enter sends it back to the agent, esc keeps the review", "");
        a->prompt.max = JOB_TEXT_MAX - 1;
        break;
    case 'n': case 'N': {
        int next = ready_after(a, a->review.slot, k.ch == 'n' ? 1 : -1);
        if (next >= 0 && next != a->review.slot) review_open(a, next);
        else app_set_status_gm(a, "no other change is waiting");
        break;
    }
    case 'u':
        app_set_status_gm(a, undo_undo(&a->world.undo, m) ? "undo" : "nothing to undo");
        break;
    case ':':
        e->mode = ED_COMMAND;
        e->cmd_from_review = 1;
        ui_prompt_open(&e->cmd, "", "", "");
        break;
    default:
        app_set_status_gm(a, "enter accepts, v boxes a part, d scraps, c sends it back, n next, esc leaves");
        break;
    }
}

/* PROMPT_JOB_FEEDBACK's answer: the change goes back to the agent with it. */
void app_job_feedback(App *a, const char *text)
{
    if (a->review.slot < 0) return;
    Job *j = &a->world.jobs[a->review.slot];
    if (!text[0]) { review_status(a); return; }
    world_job_thread_add(j, 'G', text);
    if (j->has_cs) { cs_free(&j->cs); j->has_cs = 0; }
    j->state = JOB_WORKING;
    j->summary[0] = '\0';
    char msg[220];
    world_event(&a->world, CTL_EV_FEEDBACK, j->num, "%s", text);
    /* A job an agent of the GM's own has (or nobody: `job-N` is the name vtt
     * gives the one it starts) is started again with the thread, feedback
     * last. One another name took is that name's to answer: it hears the
     * event, and a second agent started for it would only be refused. */
    char own[CTL_NAME_MAX];
    snprintf(own, sizeof own, AGENT_NAME_FMT, j->num);
    int theirs = j->taker[0] && strcmp(j->taker, own) != 0;
    snprintf(msg, sizeof msg, "#%d back to %s: %.150s", j->num, theirs ? j->taker : "the agent", text);
    app_note_gm(a, msg);
    if (!theirs) app_agent_start(a, j);
    review_next_or_close(a);
}

/* ------------------------------------------------------------ commands */

/* :ask TEXT, :ask! TEXT -- over the v box when there is one -- and
 * :ask N remove. */
void app_ask_command(App *a, const char *verb, const char *rest)
{
    char msg[256];
    if (!*rest) { app_set_status_gm(a, ":ask TEXT asks an agent for a change in the v box; :ask! lands it at once"); return; }

    char word[16];
    int num, used = 0;
    if (sscanf(rest, "%d %15s%n", &num, word, &used) == 2 && !rest[used]) {
        if (!strcmp(word, "remove")) {
            int i = world_job_find(&a->world, num);
            if (i < 0) { snprintf(msg, sizeof msg, "no job #%d", num); app_set_status_gm(a, msg); return; }
            if (a->review.slot == i) review_close(a, "");
            world_job_clear(&a->world, &a->world.jobs[i]);
            world_event(&a->world, CTL_EV_REMOVED, num, NULL);
            snprintf(msg, sizeof msg, "#%d removed", num);
            app_note_gm(a, msg);
            return;
        }
        if (!strcmp(word, "off")) {                      /* KEYS.md rule 9 */
            snprintf(msg, sizeof msg, ":ask %d remove takes it away", num);
            app_set_status_gm(a, msg);
            return;
        }
    }

    CsBox box, *bp = NULL;
    if (app_cmd_vbox(a, &box.x0, &box.y0, &box.x1, &box.y1)) bp = &box;
    int at_once = verb[strlen(verb) - 1] == '!';
    int slot = world_job_new(&a->world, JOB_FROM_GM, rest, bp, at_once);
    if (slot < 0) { snprintf(msg, sizeof msg, "%d jobs are open - :jobs, :ask N remove", JOB_MAX); app_set_status_gm(a, msg); return; }
    char where[2 * MAP_COORD_MAX + 8] = "";
    if (bp) { strcpy(where, " in "); cs_box_name(bp, where + 4, sizeof where - 4); }
    world_event(&a->world, CTL_EV_ASKED, a->world.jobs[slot].num, "%s%s%s%s", bp ? where + 1 : "", at_once ? (bp ? ", to land at once" : "to land at once") : "",
              bp || at_once ? ": " : "", rest);
    snprintf(msg, sizeof msg, "#%d asked%s%s: %.150s", a->world.jobs[slot].num, where, at_once ? ", to land at once" : "", rest);
    app_agent_ask(a, &a->world.jobs[slot], msg);   /* the line, then someone to do it, or why not */
}

/* :jobs lists them on the status line; :jobs N shows one's history. */
void app_jobs_command(App *a, const char *rest)
{
    char msg[512];
    if (*rest) {
        int i = world_job_find(&a->world, atoi(rest));
        if (i < 0) { snprintf(msg, sizeof msg, "no job #%.10s", rest); app_set_status_gm(a, msg); return; }
        const Job *j = &a->world.jobs[i];
        int off = 0;
        for (int t = 0; t < j->nthread && off < (int)sizeof msg - 8; t++)
            off += snprintf(msg + off, sizeof msg - (size_t)off, "%s%s%s", t ? "\n" : "",
                            j->thread[t].who == 'G' ? "you: " : j->thread[t].who == 'A' ? "agent: " : "",
                            j->thread[t].text);
        char title[96];
        snprintf(title, sizeof title, "Job #%d, %s%s%s", j->num, world_job_state_name(j->state), j->taker[0] ? ", taken by " : "", j->taker);
        app_show_message(a, title, j->nthread ? msg : "(nothing yet)");
        return;
    }
    int off = 0, any = 0;
    for (int i = 0; i < JOB_MAX && off < (int)sizeof msg - 40; i++) {
        const Job *j = &a->world.jobs[i];
        if (!j->used) continue;
        char where[2 * MAP_COORD_MAX + 4] = "";
        if (j->has_box) { where[0] = ' '; cs_box_name(&j->box, where + 1, sizeof where - 1); }
        off += snprintf(msg + off, sizeof msg - (size_t)off, "%s#%d %s%s %.40s", any ? "; " : "jobs: ",
                        j->num, world_job_state_name(j->state), where, j->text[0] ? j->text : j->summary);
        any = 1;
    }
    app_set_status_gm(a, any ? msg : "no jobs - :ask TEXT over a v box asks an agent for a change");
}

/* :review N, or :review for the oldest ready one. A scrapped one comes back. */
void app_review_command(App *a, const char *rest)
{
    char msg[160];
    if (a->screen != SCREEN_EDITOR) { app_set_status_gm(a, "reviewing is build mode's - F1 first"); return; }
    int slot;
    if (*rest) {
        slot = world_job_find(&a->world, atoi(rest));
        if (slot < 0) { snprintf(msg, sizeof msg, "no job #%.10s", rest); app_set_status_gm(a, msg); return; }
        Job *j = &a->world.jobs[slot];
        /* Scrapped, or accepted and then taken back with u: its set is still
         * here. (A part accepted by box left the set; undone, it is gone.) */
        if ((j->state == JOB_SCRAPPED || j->state == JOB_ACCEPTED) && j->has_cs && !cs_empty(&j->cs)) {
            j->state = JOB_READY;
            world_job_thread_add(j, '-', "brought back");
            world_event(&a->world, CTL_EV_BACK, j->num, "for review");
        }
        if (j->state != JOB_READY) {
            snprintf(msg, sizeof msg, "#%d is %s, with nothing to review", j->num, world_job_state_name(j->state));
            app_set_status_gm(a, msg);
            return;
        }
    } else {
        slot = ready_after(a, -1, 1);
        if (slot < 0) { app_set_status_gm(a, "nothing is waiting for review - :jobs lists the jobs"); return; }
    }
    review_open(a, slot);
}

/* After a : command typed over a review, back to it if it is still there. */
void app_review_resume(App *a)
{
    if (a->review.slot >= 0 && a->world.jobs[a->review.slot].used && a->world.jobs[a->review.slot].state == JOB_READY &&
        a->screen == SCREEN_EDITOR && a->ed.mode == ED_NORMAL)
        a->ed.mode = ED_REVIEW;
    else if (a->ed.mode != ED_REVIEW)               /* the command went elsewhere: :play, :stamp */
        app_review_leave(a);
}

/* ------------------------------------------------------------ drawing */

/* Anything to tint: a job asked over a box or declared somewhere, or a
 * change waiting. The GM's alone (app_view_differs). */
int app_jobs_shown(const App *a)
{
    for (int i = 0; i < JOB_MAX; i++) {
        const Job *j = &a->world.jobs[i];
        if (!j->used) continue;
        if ((j->state == JOB_ASKED || j->state == JOB_WORKING) && (j->has_box || j->has_area)) return 1;
        if (j->state == JOB_READY && j->has_cs) return 1;
    }
    return 0;
}

static void tint_box(Renderer *r, const GridView *g, const CsBox *b, int vx0, int vy0, int vx1, int vy1, uint32_t bg)
{
    int x0 = imax(b->x0, vx0), y0 = imax(b->y0, vy0), x1 = imin(b->x1, vx1), y1 = imin(b->y1, vy1);
    if (x0 <= x1 && y0 <= y1) grid_tint_tiles(r, g, x0, y0, x1, y1, bg);
}

static void tint_sq(Renderer *r, const GridView *g, int x, int y, int vx0, int vy0, int vx1, int vy1, uint32_t bg)
{
    if (x >= vx0 && x <= vx1 && y >= vy0 && y <= vy1) grid_draw_tile_cursor(r, g, x, y, bg);
}

/* The squares a change set touches, in the window: ready, or in red where it
 * would overwrite something changed since. The cells are walked by the
 * blocks meeting the window only, so the cost follows the screen. */
static void tint_set(Renderer *r, const GridView *g, const ChangeSet *cs, const Theme *th,
                     int vx0, int vy0, int vx1, int vy1)
{
    for (int pass = 0; pass < 2; pass++) {                /* conflicts last: they win */
        uint32_t bg = pass ? th->job_conflict_bg : th->job_ready_bg;
        if (cs->ncells && cs->block_start)
            CS_FOR_CELLS_IN(cs, vx0, vy0, vx1, vy1, i) {
                const CsCell *c = &cs->cells[i];
                if (c->conflict != pass) continue;
                tint_sq(r, g, imin(c->x, cs->w - 1), imin(c->y, cs->h - 1), vx0, vy0, vx1, vy1, bg);
            }
        for (int i = 0; i < cs->ntoks; i++) {
            const CsToken *c = &cs->toks[i];
            if (c->conflict != pass) continue;
            for (int k = 0; k < 2; k++) {
                if (!(k ? c->has : c->had)) continue;
                const Token *t = k ? &c->after : &c->before;
                CsBox b = { t->x, t->y, t->x + t->size - 1, t->y + t->size - 1 };
                tint_box(r, g, &b, vx0, vy0, vx1, vy1, bg);
            }
        }
        for (int i = 0; i < cs->nnotes; i++)
            if (cs->notes[i].conflict == pass) tint_sq(r, g, cs->notes[i].x, cs->notes[i].y, vx0, vy0, vx1, vy1, bg);
        for (int i = 0; i < cs->nlinks; i++) {
            const CsLink *c = &cs->links[i];
            if (c->conflict != pass) continue;
            const Link *l = c->has ? &c->after : &c->before;
            for (int e = 0; e < link_ends(l); e++) {
                CsBox b = { l->x[e], l->y[e], l->x[e] + l->size - 1, l->y[e] + l->size - 1 };
                tint_box(r, g, &b, vx0, vy0, vx1, vy1, bg);
            }
        }
    }
}

static void app_jobs_overlay(void *ctx, Renderer *r, const Map *m, const GridView *g)
{
    PROF_ZONE("job.highlight");
    App *a = ctx;
    const Theme *th = a->th;
    int vx0, vy0, vx1, vy1;
    grid_visible_tiles(g, m, &vx0, &vy0, &vx1, &vy1);
    for (int i = 0; i < JOB_MAX; i++) {
        const Job *j = &a->world.jobs[i];
        if (!j->used) continue;
        if (j->state == JOB_ASKED || j->state == JOB_WORKING) {
            if (j->has_box)  tint_box(r, g, &j->box, vx0, vy0, vx1, vy1, th->job_work_bg);
            if (j->has_area) tint_box(r, g, &j->area, vx0, vy0, vx1, vy1, th->job_work_bg);
        } else if (j->state == JOB_READY && j->has_cs && j->cs.w == m->w && j->cs.h == m->h)
            tint_set(r, g, &j->cs, th, vx0, vy0, vx1, vy1);
    }
    if (a->ed.mode == ED_REVIEW && a->review.box) {
        CsBox b = rv_box_now(a);
        tint_box(r, g, &b, vx0, vy0, vx1, vy1, th->sel_bg);
    }
}

/* Where a job's #N goes: its box's corner, else the corner of what it touches. */
static int job_corner(const Job *j, int *x, int *y)
{
    if ((j->state == JOB_ASKED || j->state == JOB_WORKING) && (j->has_box || j->has_area)) {
        const CsBox *b = j->has_box ? &j->box : &j->area;
        *x = b->x0; *y = b->y0;
        return 1;
    }
    if (j->state != JOB_READY || !j->has_cs || j->cx < 0) return 0;
    *x = j->cx; *y = j->cy;
    return 1;
}

/* Before the GM's draw: every waiting change checked against the map as it
 * is (cs_check is gated on Map.gen, so this is nothing between edits), and
 * the overlay set. Never inside a preview's show and unshow. */
void app_jobs_prepare(App *a)
{
    int any = a->view == VIEW_GM && a->world.map && app_jobs_shown(a);
    a->ed.overlay     = any ? app_jobs_overlay : NULL;
    a->ed.overlay_ctx = a;
    if (!any) return;
    for (int i = 0; i < JOB_MAX; i++)
        if (a->world.jobs[i].used && a->world.jobs[i].state == JOB_READY && a->world.jobs[i].has_cs)
            cs_check(&a->world.jobs[i].cs, a->world.map);
}

/* The review's preview, swapped in around the grid's draw (counter-
 * suggestion B): only the squares meeting the window. */
int app_review_show(App *a)
{
    if (a->ed.mode != ED_REVIEW || a->review.slot < 0) return 0;
    Job *j = &a->world.jobs[a->review.slot];
    if (!j->has_cs || j->cs.w != a->world.map->w || j->cs.h != a->world.map->h) return 0;
    int vx0, vy0, vx1, vy1;
    grid_visible_tiles(&a->ed.view, a->world.map, &vx0, &vy0, &vx1, &vy1);
    cs_show(&j->cs, a->world.map, vx0, vy0, vx1, vy1);
    return 1;
}

void app_review_unshow(App *a)
{
    cs_unshow(&a->world.jobs[a->review.slot].cs, a->world.map);
}

/* The #N labels, on top of everything on the map. */
void app_jobs_labels(App *a)
{
    if (!a->ed.overlay) return;
    Renderer *r = a->rnd;
    const GridView *g = &a->ed.view;
    int vx0, vy0, vx1, vy1;
    grid_visible_tiles(g, a->world.map, &vx0, &vy0, &vx1, &vy1);
    ClipRect saved = grid_clip_push(r, g, a->world.map);
    for (int i = 0; i < JOB_MAX; i++) {
        const Job *j = &a->world.jobs[i];
        int x, y;
        if (!j->used || !job_corner(j, &x, &y)) continue;
        if (x < vx0 || x > vx1 || y < vy0 || y > vy1) continue;
        int sx, sy;
        grid_tile_interior(g, x, y, &sx, &sy);
        char lab[8];
        snprintf(lab, sizeof lab, "#%d", j->num);
        uint32_t bg = j->state == JOB_READY ? a->th->job_ready_bg : a->th->job_work_bg;
        draw_text(r, sx, sy, lab, 3, style(a->th->fg, bg, ATTR_BOLD));
    }
    rnd_clip_restore(r, saved);
}
