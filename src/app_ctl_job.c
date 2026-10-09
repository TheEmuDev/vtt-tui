/* The channel's requests about jobs (docs/CONFLICTS.md, docs/CONTROL.md):
 * `jobs`, and `job N take|area|say|drop|dump|check|describe`. `job N propose`
 * and `propose` are app_ctl.c's, because the edits after them run there.
 * Every message to the GM is the GM's alone. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "json.h"
#include "maptools.h"

static void box_name(const CsBox *b, char *buf, size_t sz)
{
    map_region_name(b->x0, b->y0, b->x1, b->y1, buf, sz);
}

static const char *who_name(char who)
{
    return who == 'G' ? "gm" : who == 'A' ? "agent" : "-";
}

/* One job as text: its line, its thread, its proposal. */
static void job_text(App *a, Job *j, FILE *out)
{
    char where[2 * MAP_COORD_MAX + 16] = "";
    if (j->has_box)  { strcpy(where, " in ");   box_name(&j->box, where + 4, sizeof where - 4); }
    else if (j->has_area) { strcpy(where, " area "); box_name(&j->area, where + 6, sizeof where - 6); }
    static const char *const FROM[] = { "asked by the GM", "an agent's own", "from --apply", "the file on disk" };
    fprintf(out, "#%d %s%s, %s%s%s%s\n", j->num, app_job_state_name(j->state), where, FROM[j->from],
            j->at_once ? ", to land at once" : "", j->text[0] ? ": " : "", j->text);
    for (int t = 0; t < j->nthread; t++)
        fprintf(out, "  %s: %s\n", who_name(j->thread[t].who), j->thread[t].text);
    if (j->has_cs && j->state == JOB_READY) {
        int c = cs_check(&j->cs, a->map);
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
    box_name(b, buf, sizeof buf);
    json_str(js, buf);
}

static void job_json(App *a, Job *j, Json *js)
{
    json_open(js, '{');
    json_kint(js, "num", j->num);
    json_kstr(js, "state", app_job_state_name(j->state));
    json_kstr(js, "from", app_job_from_name(j->from));
    json_key(js, "at_once"); json_bool(js, j->at_once);
    json_kstr(js, "text", j->text);
    json_box(js, "box", j->has_box, &j->box);
    json_box(js, "area", j->has_area, &j->area);
    json_key(js, "thread");
    json_open(js, '[');
    for (int t = 0; t < j->nthread; t++) {
        json_open(js, '{');
        json_kstr(js, "who", who_name(j->thread[t].who));
        json_kstr(js, "text", j->thread[t].text);
        json_close(js, '}');
    }
    json_close(js, ']');
    int ready = j->has_cs && j->state == JOB_READY;
    json_key(js, "proposal");
    if (ready) json_str(js, j->summary); else json_null(js);
    json_kint(js, "conflicts", ready ? cs_check(&j->cs, a->map) : 0);
    json_close(js, '}');
}

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
            Job *j = &a->jobs[i];
            if (!j->used) continue;
            if (json) job_json(a, j, &js); else job_text(a, j, out);
            any = 1;
        }
        if (json) { json_close(&js, ']'); fputc('\n', out); }
        else if (!any) fputs("no jobs\n", out);
        return 0;
    }

    /* job N VERB ... */
    if (n < 3) { snprintf(err, errsz, "job N take, area REGION, say \"...\", propose, drop, dump, check or describe"); return -1; }
    int num = 0;
    char *end;
    long v = strtol(w[1], &end, 10);
    if (*end || v < 1 || v > JOB_NUM_MAX) { snprintf(err, errsz, "%.20s: a job's number, from jobs", w[1]); return -1; }
    num = (int)v;
    int slot = app_job_find(a, num);
    if (slot < 0) { snprintf(err, errsz, "no job #%d - jobs lists them", num); return -1; }
    Job *j = &a->jobs[slot];
    const char *verb = w[2];
    int open = j->state == JOB_ASKED || j->state == JOB_WORKING || j->state == JOB_READY;
    char msg[256];

    if (!strcmp(verb, "dump") || !strcmp(verb, "check") || !strcmp(verb, "describe")) {
        /* The map as accepting the proposal would make it, now. */
        /* A ready one: an accepted set applied again would double what it
         * adds. */
        if (j->state != JOB_READY || !j->has_cs || cs_empty(&j->cs)) {
            snprintf(err, errsz, "#%d has no proposal waiting", num);
            return -1;
        }
        Map *m = app_job_result(a, slot);
        if (!m) { snprintf(err, errsz, "#%d was made before the map was resized - propose it again", num); return -1; }
        if (!strcmp(verb, "dump")) {
            int x0 = 0, y0 = 0, x1 = m->w - 1, y1 = m->h - 1;
            if (n > 4) { snprintf(err, errsz, "job N dump takes one region, like B2:K12"); return -1; }
            if (n == 4 && !app_ctl_region(m, w[3], &x0, &y0, &x1, &y1, err, errsz)) return -1;
            maptools_dump(out, m, x0, y0, x1, y1);
            return 0;
        }
        int json = 0;
        if (n == 4 && !strcmp(w[3], "json")) json = 1;
        else if (n > 3) { snprintf(err, errsz, "job N %s takes nothing but json after it", verb); return -1; }
        if (verb[0] == 'c') maptools_check_map(out, m, json);
        else maptools_describe(out, m, json);
        return 0;
    }

    if (!open) {
        snprintf(err, errsz, "#%d was %s%s", num, app_job_state_name(j->state),
                 j->state == JOB_SCRAPPED ? " - :review N is the GM's way to bring it back" : "");
        return -1;
    }

    if (!strcmp(verb, "take")) {
        if (n != 3) { snprintf(err, errsz, "job N take takes nothing after it"); return -1; }
        if (j->state == JOB_ASKED) {
            j->state = JOB_WORKING;
            snprintf(msg, sizeof msg, "#%d taken by an agent", num);
            app_note_gm(a, msg);
            a->dirty = 1;
        }
        job_text(a, j, out);
        return 0;
    }
    if (!strcmp(verb, "area")) {
        if (n != 4) { snprintf(err, errsz, "job N area REGION, like B2:K12 or an area's name"); return -1; }
        if (j->state == JOB_READY) { snprintf(err, errsz, "#%d has a proposal waiting - its squares are what the GM sees", num); return -1; }
        int x0, y0, x1, y1;
        if (!app_ctl_region(a->map, w[3], &x0, &y0, &x1, &y1, err, errsz)) return -1;
        j->state = JOB_WORKING;
        j->has_area = 1;
        j->area = (CsBox){ x0, y0, x1, y1 };
        char where[2 * MAP_COORD_MAX + 2];
        box_name(&j->area, where, sizeof where);
        snprintf(msg, sizeof msg, "#%d: the agent is working in %s", num, where);
        app_note_gm(a, msg);
        fprintf(out, "#%d area %s\n", num, where);
        a->dirty = 1;
        return 0;
    }
    if (!strcmp(verb, "say")) {
        if (n != 4) { snprintf(err, errsz, "job N say \"a line for the GM\""); return -1; }
        app_job_thread_add(j, 'A', w[3]);
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
            app_job_thread_add(j, '-', "withdrawn by the agent");
            snprintf(msg, sizeof msg, "#%d withdrawn by the agent - :review %d brings it back", num, num);
            fprintf(out, "#%d withdrawn\n", num);
        } else {
            if (j->has_cs) { cs_free(&j->cs); j->has_cs = 0; }
            j->state = JOB_ASKED;
            j->has_area = 0;
            j->summary[0] = '\0';
            app_job_thread_add(j, '-', "given back");
            snprintf(msg, sizeof msg, "#%d given back by the agent - it waits as asked", num);
            fprintf(out, "#%d given back, as asked\n", num);
        }
        app_note_gm(a, msg);
        a->dirty = 1;
        return 0;
    }
    snprintf(err, errsz, "job N %.20s: take, area, say, propose, drop, dump, check or describe", verb);
    return -1;
}
