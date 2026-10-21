/* A request's edits as a proposal (docs/CONFLICTS.md): the `propose` and
 * `job N propose` lines, the scratch map the edits run on, and the change set
 * they become a job's. request.c runs the edits; this is what is around them. */

#include <stdio.h>
#include <string.h>

#include "request.h"
#include "util.h"
#include "prof.h"

/* A proposal's edits run on a copy of the live map with a log of their
 * own: the live map, its log (the GM's redo tail too), `modified` and
 * Map.gen never move for a change nobody has accepted (docs/CONFLICTS.md,
 * "Why not run it live and roll it back"). */
void request_proposal_begin(World *world, Edits *ed)
{
    world_scratch_copy(world);
    ed->proposal  = 1;
    ed->live      = world->map;
    ed->live_undo = world->undo;
    world->undo = world->ctl_sundo;
    world->map  = world->ctl_scratch;
    undo_begin(&world->undo);
    ed->ops0 = world->undo.stamp;
}

/* A read about the GM's side (status, marked) in the middle of a proposal:
 * the live map and log in for its length, and out again (call it twice). */
void request_live_swap(World *world, Edits *ed)
{
    if (!ed->proposal) return;
    Map *m = world->map;   world->map  = ed->live;      ed->live      = m;
    Undo u = world->undo;  world->undo = ed->live_undo; ed->live_undo = u;
}

/* What a request that landed says, and the ring round it on the GM's screen. */
void request_say_changed(World *world, const Edits *ed, FILE *out)
{
    char area[2 * MAP_COORD_MAX + 2] = "";
    if (ed->x1 >= ed->x0) map_region_name(ed->x0, ed->y0, ed->x1, ed->y1, area, sizeof area);
    fprintf(out, "changed %s: %d line%s, one undo step\n", area, ed->lines, ed->lines == 1 ? "" : "s");
    if (ed->x1 < ed->x0) return;
    world->host->ring(world->gm, ed->x0, ed->y0, ed->x1, ed->y1);
}

/* The live map back, the scratch log kept beside it for the diff. */
void request_proposal_end(World *world, Edits *ed)
{
    undo_end(&world->undo);
    world->ctl_sundo = world->undo;
    world->undo = ed->live_undo;
    world->map  = ed->live;
}

/* The request's edits are in, on the scratch map: their change set becomes a
 * job's proposal, landing at once under :agent accept auto. -1 with why in
 * err when there is no room for it. */
int request_finish_proposal(World *world, Edits *ed, FILE *out, char *err, size_t errsz)
{
    ChangeSet cs;
    cs_init(&cs);
    int slot = ed->job;
    {
        PROF_ZONE("job.propose");
        if (!cs_diff(&cs, world->map, world->ctl_scratch, &world->ctl_sundo)) {
            cs_free(&cs);
            fputs("no change: the map already looked like that\n", out);
            return 0;
        }
        /* What will be held once this is in: a new job's slot may be a finished
         * job's that world_job_new clears, and its cells go with it. */
        long held = world_jobs_cells(world, slot >= 0 ? slot : world_job_slot_for_new(world));
        if (held + cs.ncells > JOB_CELLS_MAX) {
            int mine = cs.ncells;
            cs_free(&cs);
            request_class(CTL_E_LIMIT), snprintf(err, errsz, "the changes kept for the GM hold %ld squares, and this one's %d would pass the %ld they may hold - jobs lists them; nothing more until some are removed",
                                           held, mine, JOB_CELLS_MAX);
            return -1;
        }
        /* At once is :agent on's (a request not from the socket is not asked). */
        int at_once = world->ctl_auto && (world->agent_on || !world->ctl_can_hold);
        if (slot < 0) slot = world_job_new(world, ed->from, ed->say, NULL, at_once);
        if (slot < 0) {
            cs_free(&cs);
            request_class(CTL_E_LIMIT), snprintf(err, errsz, "%d changes are waiting for the GM - nothing more until some are reviewed", JOB_MAX);
            return -1;
        }
        Job *j = &world->jobs[slot];
        if (j->from != JOB_FROM_APPLY) request_job_claim(world, j);    /* --apply's is nobody's: the GM's alone to decide */
        if (at_once) j->at_once = 1;               /* everything that comes in */
        world_job_set_proposal(world, slot, &cs, ed->job >= 0 ? ed->say : NULL);
    }
    Job *j = &world->jobs[slot];
    fprintf(out, "proposal #%d: %s\n", j->num, j->summary);
    if (j->state == JOB_ACCEPTED) request_say_changed(world, ed, out);
    else if (j->at_once) fprintf(out, "lands when the GM is back: %s\n", world->host->busy(world->gm));
    else fprintf(out, "waiting for the GM's review - :review %d\n", j->num);
    return 0;
}

int request_propose(World *world, char w[][CTL_WORD_MAX], int n, Edits *ed, char *err, size_t errsz)
{
    const char *v = w[0];
    /* propose ["a line for the GM"], or job N propose [...]: what the
     * request's edits are. Without one, they are an agent's own idea. */
    int at = v[0] == 'p' ? 1 : 3, slot = -1, num;
    /* propose apply "plan.txt": what `vtt map --apply` sends to the vtt
     * that has the map open. */
    int from_apply = at == 1 && n == 3 && !strcmp(w[1], "apply");
    if (from_apply) at = 2;
    if (n > at + 1) { snprintf(err, errsz, "%s takes one line for the GM after it, in quotes", at == 1 ? "propose" : "job N propose"); return -1; }
    if (ed->header || ed->lines) { snprintf(err, errsz, "propose comes once, before the request's edits"); return -1; }
    if (world->ctl_direct) { request_class(CTL_E_STATE), snprintf(err, errsz, "there is no GM to propose to: --apply changes the file"); return -1; }
    if (at == 3) {
        if (!request_int(w[1], 1, JOB_NUM_MAX, &num) || (slot = world_job_find(world, num)) < 0) {
            request_class(CTL_E_MISSING), snprintf(err, errsz, "no job #%.10s - jobs lists them", w[1]);
            return -1;
        }
        const Job *j = &world->jobs[slot];
        if (j->state == JOB_ACCEPTED || j->state == JOB_SCRAPPED) {
            request_class(CTL_E_STATE), snprintf(err, errsz, "#%d was %s - propose alone offers a new change", j->num, world_job_state_name(j->state));
            return -1;
        }
        if (world_job_gm_only(j)) {
            request_class(CTL_E_STATE), snprintf(err, errsz, "#%d is %s: the GM's to decide - propose alone offers a change of your own", j->num,
                     j->from == JOB_FROM_DISK ? "what another program wrote to the file" : "an --apply's");
            return -1;
        }
        if (!request_job_mine(world, j, err, errsz)) return -1;
    }
    ed->header = 1;
    ed->job = slot;
    if (from_apply) {
        ed->from = JOB_FROM_APPLY;
        snprintf(ed->say, sizeof ed->say, "--apply %.140s", w[at]);
    }
    else str_lcpy(ed->say, n > at ? w[at] : "", sizeof ed->say);
    return 0;
}
