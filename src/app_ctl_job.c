/* The channel's requests about jobs (docs/CONFLICTS.md, docs/CONTROL.md):
 * `jobs`, and `job N take|area|say|drop|undo|conflicts|dump|check|describe`. `job N propose`
 * and `propose` are app_ctl.c's, because the edits after them run there.
 * Every message to the GM is the GM's alone. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "json.h"
#include "maptools.h"


/* One job as text: its line, its thread, its proposal. */
static void job_text(App *a, Job *j, FILE *out)
{
    char where[2 * MAP_COORD_MAX + 16] = "";
    if (j->has_box)  { strcpy(where, " in ");   cs_box_name(&j->box, where + 4, sizeof where - 4); }
    else if (j->has_area) { strcpy(where, " area "); cs_box_name(&j->area, where + 6, sizeof where - 6); }
    static const char *const FROM[] = { "asked by the GM", "an agent's own", "from --apply", "the file on disk" };
    fprintf(out, "#%d %s%s, %s%s%s%s%s%s\n", j->num, world_job_state_name(j->state), where, FROM[j->from],
            j->taker[0] ? ", taken by " : "", j->taker,
            j->at_once ? ", to land at once" : "", j->text[0] ? ": " : "", j->text);
    for (int t = 0; t < j->nthread; t++)
        fprintf(out, "  %s: %s\n", world_job_who_name(j->thread[t].who), j->thread[t].text);
    if (j->has_cs && j->state == JOB_READY) {
        int c = cs_check(&j->cs, a->world.map);
        fprintf(out, "  proposal: %s", j->summary);
        if (c) fprintf(out, " (%d conflict%s: changed since)", c, c == 1 ? "" : "s");
        fputc('\n', out);
    }
}

static void json_box(Json *js, const char *key, int has, const CsBox *b)
{
    json_key(js, key);
    if (!has) { json_null(js); return; }
    char buf[2 * MAP_COORD_MAX + 2];
    cs_box_name(b, buf, sizeof buf);
    json_str(js, buf);
}

static void job_json(App *a, Job *j, Json *js)
{
    json_open(js, '{');
    json_kint(js, "num", j->num);
    json_kstr(js, "state", world_job_state_name(j->state));
    json_kstr(js, "from", world_job_from_name(j->from));
    json_key(js, "taker"); if (j->taker[0]) json_str(js, j->taker); else json_null(js);
    json_key(js, "at_once"); json_bool(js, j->at_once);
    json_kstr(js, "text", j->text);
    json_box(js, "box", j->has_box, &j->box);
    json_box(js, "area", j->has_area, &j->area);
    json_key(js, "thread");
    json_open(js, '[');
    for (int t = 0; t < j->nthread; t++) {
        json_open(js, '{');
        json_kstr(js, "who", world_job_who_name(j->thread[t].who));
        json_kstr(js, "text", j->thread[t].text);
        json_close(js, '}');
    }
    json_close(js, ']');
    int ready = j->has_cs && j->state == JOB_READY;
    json_key(js, "proposal");
    if (ready) json_str(js, j->summary); else json_null(js);
    json_kint(js, "conflicts", ready ? cs_check(&j->cs, a->world.map) : 0);
    json_close(js, '}');
}

int app_ctl_job_mine(App *a, const Job *j, char *err, size_t errsz)
{
    if (!j->taker[0] || !strcmp(j->taker, a->world.ctl_who)) return 1;
    app_ctl_class(CTL_E_STATE);
    snprintf(err, errsz, "#%d is %s's, and you are %s - jobs says who has what", j->num, j->taker, a->world.ctl_who);
    return 0;
}

int app_ctl_job_claim(App *a, Job *j)
{
    if (j->taker[0]) return 0;
    str_lcpy(j->taker, a->world.ctl_who, sizeof j->taker);
    return 1;
}

/* A job taken is news to the other agents: they would learn it from a
 * refusal otherwise. */
static void say_taken(App *a, const Job *j)
{
    world_event(&a->world, CTL_EV_TAKEN, j->num, "by %s", a->world.ctl_who);
    world_event_by(&a->world, a->world.ctl_who);
}

/* job N undo: the job's landing taken back, while it is the last thing done
 * to the map. A step under later ones is not reached: taking it out from
 * under them is a new change to the map as it is, and that is a proposal. */
static int job_undo(App *a, Job *j, FILE *out, char *err, size_t errsz)
{
    /* The caller's own steps: a job given back and taken by another name
     * has landed for both, and each takes back what it landed. */
    int under, run = undo_tag_run(&a->world.undo, j->num, a->world.ctl_who, &under);
    const UndoTag *top = undo_tag_top(&a->world.undo);
    if (!run) {
        app_ctl_class(CTL_E_STATE);
        if (top && top->job == j->num)
            snprintf(err, errsz, "what #%d landed last is %s's, and you are %s - undo takes back your own", j->num, top->who, a->world.ctl_who);
        else if (under) snprintf(err, errsz, "#%d landed, and something has changed the map since - only the GM's u reaches it now; "
                                        "propose what would put it right", j->num);
        else snprintf(err, errsz, "#%d has nothing landed to take back", j->num);
        return -1;
    }
    if (!app_ctl_undo(a, run, j->num, err, errsz)) return -2;
    char msg[160];
    snprintf(msg, sizeof msg, "#%d taken back by %s - ctrl-r puts it back", j->num,
             strcmp(a->world.ctl_who, CTL_NAME_NONE) ? a->world.ctl_who : "the agent");
    app_note_gm(a, msg);
    fprintf(out, "took back #%d%s\n", j->num, run > 1 ? ", every part of it" : "");
    return 0;
}

_Static_assert(sizeof ((Job *)0)->taker == CTL_NAME_MAX, "a job's taker is an agent's name");

int app_ctl_job(App *a, char w[][CTL_WORD_MAX], int n, FILE *out, char *err, size_t errsz)
{
    if (!strcmp(w[0], "jobs")) {
        int json = 0;
        if (n == 2 && !strcmp(w[1], "json")) json = 1;
        else if (n > 1) { snprintf(err, errsz, "jobs takes nothing but json after it"); return -1; }
        Json js;
        if (json) { json_init(&js, out); json_open(&js, '['); }
        int any = 0;
        for (int i = 0; i < JOB_MAX; i++) {
            Job *j = &a->world.jobs[i];
            if (!j->used) continue;
            if (json) job_json(a, j, &js); else job_text(a, j, out);
            any = 1;
        }
        if (json) { json_close(&js, ']'); fputc('\n', out); }
        else if (!any) fputs("no jobs\n", out);
        return 0;
    }

    /* job N VERB ... */
    if (n < 3) { snprintf(err, errsz, "job N take, area REGION, say \"...\", propose, drop, undo, conflicts, dump, check or describe"); return -1; }
    int num = 0;
    char *end;
    long v = strtol(w[1], &end, 10);
    if (*end || v < 1 || v > JOB_NUM_MAX) { snprintf(err, errsz, "%.20s: a job's number, from jobs", w[1]); return -1; }
    num = (int)v;
    int slot = world_job_find(&a->world, num);
    if (slot < 0) { app_ctl_class(CTL_E_MISSING), snprintf(err, errsz, "no job #%d - jobs lists them", num); return -1; }
    Job *j = &a->world.jobs[slot];
    const char *verb = w[2];
    int open = j->state == JOB_ASKED || j->state == JOB_WORKING || j->state == JOB_READY;
    char msg[256];

    if (!strcmp(verb, "dump") || !strcmp(verb, "check") || !strcmp(verb, "describe")) {
        /* The map as accepting the proposal would make it, now. */
        /* A ready one: an accepted set applied again would double what it
         * adds. */
        if (j->state != JOB_READY || !j->has_cs || cs_empty(&j->cs)) {
            app_ctl_class(CTL_E_STATE), snprintf(err, errsz, "#%d has no proposal waiting", num);
            return -1;
        }
        /* The result is the map copied and the set applied: every square, whatever is then read of it. */
        if (!app_ctl_read(a, (long)a->world.map->w * a->world.map->h, err, errsz)) return -1;
        Map *m = world_job_result(&a->world, slot);
        if (!m) { app_ctl_class(CTL_E_STATE), snprintf(err, errsz, "#%d was made before the map was resized - propose it again", num); return -1; }
        if (!strcmp(verb, "dump")) {
            return app_ctl_dump(a, m, w, n, 3, 0, out, err, errsz) ? 0 : -1;
        }
        int json = 0;
        if (n == 4 && !strcmp(w[3], "json")) json = 1;
        else if (n > 3) { snprintf(err, errsz, "job N %s takes nothing but json after it", verb); return -1; }
        if (verb[0] == 'c') maptools_check_map(out, m, json);
        else maptools_describe(out, m, json);
        return 0;
    }

    if (!strcmp(verb, "conflicts")) {
        if (n != 3) { snprintf(err, errsz, "job N conflicts takes nothing after it"); return -1; }
        if (j->state != JOB_READY || !j->has_cs) {
            app_ctl_class(CTL_E_STATE), snprintf(err, errsz, "#%d has no proposal waiting", num);
            return -1;
        }
        /* Every element of the set against the map, once (cs_check is gated on Map.gen). */
        if (!app_ctl_read(a, j->cs.ncells + CTL_FILE_CHARGE, err, errsz)) return -1;
        if (!cs_conflicts_write(&j->cs, a->world.map, out, CTL_CONFLICT_LINES))
            fprintf(out, "#%d has no conflicts: nothing it touches has changed since\n", num);
        return 0;
    }

    /* A landing is taken back by the name whose job it is, whatever the
     * job's state since (accepted, or ready again with a part landed). */
    if (!strcmp(verb, "undo")) {
        if (n != 3) { snprintf(err, errsz, "job N undo takes nothing after it"); return -1; }
        if (world_job_gm_only(j)) {
            app_ctl_class(CTL_E_STATE), snprintf(err, errsz, "#%d is %s: the GM's to take back", num,
                     j->from == JOB_FROM_DISK ? "what another program wrote to the file" : "an --apply's");
            return -1;
        }
        if (!app_ctl_job_mine(a, j, err, errsz)) return -1;
        return job_undo(a, j, out, err, errsz);
    }

    if (!open) {
        app_ctl_class(CTL_E_STATE), snprintf(err, errsz, "#%d was %s%s", num, world_job_state_name(j->state),
                 j->state == JOB_SCRAPPED ? " - :review N is the GM's way to bring it back" : "");
        return -1;
    }

    if (world_job_gm_only(j)) {
        app_ctl_class(CTL_E_STATE), snprintf(err, errsz, "#%d is %s: the GM's to decide - you may dump, check and describe it", num,
                 j->from == JOB_FROM_DISK ? "what another program wrote to the file" : "an --apply's");
        return -1;
    }

    /* From here every word acts on the job: its taker's alone. */
    if (!app_ctl_job_mine(a, j, err, errsz)) return -1;

    if (!strcmp(verb, "take")) {
        if (n != 3) { snprintf(err, errsz, "job N take takes nothing after it"); return -1; }
        int fresh = app_ctl_job_claim(a, j);
        if (fresh || j->state == JOB_ASKED) say_taken(a, j);
        if (j->state == JOB_ASKED) {
            j->state = JOB_WORKING;
            if (strcmp(a->world.ctl_who, CTL_NAME_NONE) != 0) snprintf(msg, sizeof msg, "#%d taken by %s", num, a->world.ctl_who);
            else snprintf(msg, sizeof msg, "#%d taken by an agent", num);
            app_note_gm(a, msg);
            a->dirty = 1;
        }
        job_text(a, j, out);
        return 0;
    }
    if (!strcmp(verb, "area")) {
        if (n != 4) { snprintf(err, errsz, "job N area REGION, like B2:K12 or an area's name"); return -1; }
        if (j->state == JOB_READY) { app_ctl_class(CTL_E_STATE), snprintf(err, errsz, "#%d has a proposal waiting - its squares are what the GM sees", num); return -1; }
        int x0, y0, x1, y1;
        if (!app_ctl_region(a->world.map, w[3], &x0, &y0, &x1, &y1, err, errsz)) return -1;
        if (app_ctl_job_claim(a, j) || j->state == JOB_ASKED) say_taken(a, j);     /* working on it is having it */
        j->state = JOB_WORKING;
        j->has_area = 1;
        j->area = (CsBox){ x0, y0, x1, y1 };
        char where[2 * MAP_COORD_MAX + 2];
        cs_box_name(&j->area, where, sizeof where);
        snprintf(msg, sizeof msg, "#%d: the agent is working in %s", num, where);
        app_note_gm(a, msg);
        fprintf(out, "#%d area %s\n", num, where);
        a->dirty = 1;
        return 0;
    }
    if (!strcmp(verb, "say")) {
        if (n != 4) { snprintf(err, errsz, "job N say \"a line for the GM\""); return -1; }
        /* A line on the GM's status and one written to the session log: counted as a file is. */
        if (!app_ctl_read(a, CTL_FILE_CHARGE, err, errsz)) return -1;
        world_job_thread_add(j, 'A', w[3]);
        snprintf(msg, sizeof msg, "#%d, the agent: %.200s", num, w[3]);
        app_note_gm(a, msg);
        a->dirty = 1;
        return 0;
    }
    if (!strcmp(verb, "drop")) {
        if (n != 3) { snprintf(err, errsz, "job N drop takes nothing after it"); return -1; }
        if (a->review == slot) app_review_leave(a);
        if (j->from != JOB_FROM_GM) {
            /* Nobody asked for it: scrapped, as the GM's d would. Kept, not
             * destroyed (KEYS.md rule 9: only remove destroys), so :review N
             * brings it back; a finished job makes room when the table fills. */
            j->state = JOB_SCRAPPED;
            world_job_thread_add(j, '-', "withdrawn by the agent");
            world_event(&a->world, CTL_EV_SCRAPPED, num, "withdrawn by %s", a->world.ctl_who);
            world_event_by(&a->world, a->world.ctl_who);
            snprintf(msg, sizeof msg, "#%d withdrawn by the agent - :review %d brings it back", num, num);
            fprintf(out, "#%d withdrawn\n", num);
        } else {
            if (j->has_cs) { cs_free(&j->cs); j->has_cs = 0; }
            j->state = JOB_ASKED;
            j->taker[0] = '\0';               /* anyone's to take again */
            j->has_area = 0;
            j->summary[0] = '\0';
            world_job_thread_add(j, '-', "given back");
            /* Asked again: what an agent looking for work waits to hear. */
            world_event(&a->world, CTL_EV_ASKED, num, "given back by %s: %.150s", a->world.ctl_who, j->text);
            world_event_by(&a->world, a->world.ctl_who);
            snprintf(msg, sizeof msg, "#%d given back by the agent - it waits as asked", num);
            fprintf(out, "#%d given back, as asked\n", num);
        }
        app_note_gm(a, msg);
        a->dirty = 1;
        return 0;
    }
    snprintf(err, errsz, "job N %.20s: take, area, say, propose, drop, undo, conflicts, dump, check or describe", verb);
    return -1;
}
