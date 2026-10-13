/* :agent command (docs/CONFLICTS.md, decision 1b): a command vtt starts for
 * each job the GM asks for (:ask) and each one sent back with feedback, so
 * asking works with no agent already waiting. The command gets the job and
 * its thread on stdin, with how to answer; it reports back through the
 * channel like any agent (docs/AGENTS.md). It runs cut from the terminal, in
 * a session of its own, with its output in the session log when one is on;
 * vtt is its parent (docs/V2.md, D8): the tick reaps it, says how it ended
 * when it left its job undone, and stops one that has gone silent.
 * Every message is the GM's alone. */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app_priv.h"
#include "prof.h"

/* A word for a shell, whatever is in it: in single quotes, each of its own
 * written as '\''. The prompt's lines are run as printed. */
static void sh_quote(FILE *out, const char *s)
{
    fputc('\'', out);
    for (; *s; s++) {
        if (*s == '\'') fputs("'\\''", out);
        else fputc(*s, out);
    }
    fputc('\'', out);
}

/* What the command reads: the job, where, the thread, and the few requests
 * it needs. Whole in itself: the command may know nothing of vtt. */
void app_agent_prompt(const App *a, const Job *j, FILE *out)
{
    long pid = (long)getpid();
    const char *vtt = app_self_path ? app_self_path : "vtt";
    char where[2 * MAP_COORD_MAX + 2] = "";
    if (j->has_box) cs_box_name(&j->box, where, sizeof where);

    fputs("You are an agent for the GM of a tabletop map open in vtt, a virtual tabletop in a terminal.\n"
          "The GM asked for a change to the map. Make it as a proposal: the GM reviews it and accepts it or not.\n\n", out);
    fprintf(out, "Job #%d: %s\n", j->num, j->text);
    if (where[0]) fprintf(out, "Where: %s (the box the GM drew)\n", where);
    else          fputs("Where: the GM drew no box - `marked` says what they are pointing at\n", out);
    if (a->world.map)   fprintf(out, "Map: %s, %dx%d squares (A1 is the top left)\n", a->world.map->name, a->world.map->w, a->world.map->h);
    if (j->nthread > 1) {
        fputs("\nThe job so far (the last gm: line is what to do now):\n", out);
        for (int t = 0; t < j->nthread; t++)
            fprintf(out, "  %s: %s\n", app_job_who_name(j->thread[t].who), j->thread[t].text);
    }

    /* Each line runs as printed: the path quoted, nothing indented that a
     * shell would mind (a heredoc's end is at the margin). */
    char call[64];
    snprintf(call, sizeof call, " --ctl-pid %ld --ctl", pid);
    fputs("\nTalk to vtt with one request a call:\n\n", out);
    sh_quote(out, vtt); fprintf(out, "%s 'job %d take'\n", call, j->num);
    fputs("    the job is yours. If it answers \"no job #", out);
    fprintf(out, "%d\", the GM withdrew it or closed the map: stop.\n", j->num);
    sh_quote(out, vtt); fprintf(out, "%s 'dump%s%s'\n", call, where[0] ? " " : "", where);
    fputs("    the map as text. Also: 'describe' (rooms and what is in them), 'check', 'marked' (what the GM points at).\n", out);
    if (where[0]) {
        sh_quote(out, vtt); fprintf(out, "%s 'job %d area %s'\n", call, j->num, where);
        fputs("    where you will work, shown to the GM (any region, like B2:F6).\n", out);
    }
    sh_quote(out, vtt); fprintf(out, "%s <<'EOF'\n", call);
    fprintf(out, "job %d propose \"one line for the GM\"\n"
                 "tile C3 water\n"
                 "token add enemy D4 \"Ghoul\"\n"
                 "EOF\n", j->num);
    fputs("    several lines on stdin are one request, and its edits are your proposal. These two edit lines are\n"
          "    only the shape: write your own, for the squares of this job.\n", out);
    sh_quote(out, vtt); fprintf(out, "%s 'job %d dump'\n", call, j->num);
    fputs("    your proposal read back, as the map would be with it.\n\n", out);
    fputs("A request is all or nothing: if a line fails, nothing changed and the answer names the line and why.\n"
          "The lines that change a map: ", out);
    app_ctl_edit_verbs(out);            /* from the channel's own table: it cannot fall behind it */
    fputs(".\n    ", out);
    sh_quote(out, vtt); fprintf(out, "%s 'help token'\n", call);
    fputs("    how one of them is written; help alone lists every request.\n"
          "Propose once, read it back, then stop. The verdict is the GM's; feedback starts you again with this job.\n", out);
}

static AgentRun *run_of(App *a, int job)
{
    for (int i = 0; i < a->nagents; i++)
        if (a->agents[i].job == job) return &a->agents[i];
    return NULL;
}

void app_agent_forget(App *a, int num)
{
    AgentRun *r = run_of(a, num);
    if (r) { r->job = 0; r->again = 0; }
}

void app_agent_heard(App *a, const char *who)
{
    int num = 0;
    if (!a->nagents || sscanf(who, AGENT_NAME_FMT, &num) != 1 || num < 1) return;
    AgentRun *r = run_of(a, num);
    if (r && !r->stopping && r->heard_ms) r->heard_ms = a->world.now_ms ? a->world.now_ms : 1;
}

int app_agents_due(const App *a, uint64_t now_ms)
{
    int due = -1;
    for (int i = 0; i < a->nagents; i++) {
        const AgentRun *r = &a->agents[i];
        if (!r->heard_ms) return 0;                           /* not yet seen by a tick */
        /* Told to stop and then made to: nothing more to do at any time. Its
         * end wakes the loop (SIGCHLD); one that cannot die must not spin it. */
        if (r->stopping == 2) continue;
        uint64_t at = r->heard_ms + (uint64_t)(r->stopping ? a->agent_grace_ms : a->agent_deadline_ms);
        int      d  = now_ms >= at ? 0 : (int)(at - now_ms);
        if (due < 0 || d < due) due = d;
    }
    return due;
}

/* How a run ended, as the GM reads it. */
static void ended_how(const AgentRun *r, int status, int deadline_ms, char *buf, size_t sz)
{
    if (r->stopping) snprintf(buf, sz, "stopped by vtt after %d minutes without a word", deadline_ms / 60000);
    else if (WIFSIGNALED(status)) snprintf(buf, sz, "killed by signal %d", WTERMSIG(status));
    else if (WEXITSTATUS(status) == 127) snprintf(buf, sz, "exit 127: the command could not be run");
    else if (WEXITSTATUS(status) == 126) snprintf(buf, sz, "exit 126: the command is not a program that can be run");
    else snprintf(buf, sz, "exit %d", WEXITSTATUS(status));
}

/* A run is over. A job it left undone -- asked or working, and nobody
 * else's -- waits as asked again, said to the GM and to the agents. */
static void run_ended(App *a, AgentRun r, int status)
{
    int slot = r.job ? app_job_find(a, r.job) : -1;
    if (slot < 0) return;
    Job *j = &a->world.jobs[slot];
    /* A job done meanwhile (the old run proposed late and the GM took it,
     * or scrapped it) wants nobody. */
    if (j->state != JOB_ASKED && j->state != JOB_WORKING) return;
    if (r.again) { app_agent_start(a, j); return; }         /* the feedback that came while it ran */
    char own[CTL_NAME_MAX], how[80], msg[320];
    snprintf(own, sizeof own, AGENT_NAME_FMT, j->num);
    if (j->taker[0] && strcmp(j->taker, own) != 0) return;
    ended_how(&r, status, a->agent_deadline_ms, how, sizeof how);
    j->state    = JOB_ASKED;
    j->taker[0] = '\0';
    j->has_area = 0;
    snprintf(msg, sizeof msg, "its agent stopped (%s)", how);
    app_job_thread_add(j, '-', msg);
    app_event(a, CTL_EV_ASKED, j->num, "its agent stopped (%s): %.120s", how, j->text);
    snprintf(msg, sizeof msg, "#%d: its agent stopped with nothing proposed (%s) - the job waits as asked%s", j->num, how,
             slog_on(&a->slog) ? "; the session log has what it printed" : "; :log on shows what the next one prints");
    app_note_gm(a, msg);
    a->dirty = 1;
}

void app_agents_tick(App *a, uint64_t now_ms)
{
    if (!a->nagents) return;
    PROF_ZONE("agent.reap");
    for (int i = 0; i < a->nagents; ) {
        AgentRun *r = &a->agents[i];
        int   status = 0;
        pid_t got = waitpid((pid_t)r->pid, &status, WNOHANG);
        if (got == (pid_t)r->pid || (got < 0 && errno == ECHILD)) {
            AgentRun done = *r;
            *r = a->agents[--a->nagents];
            run_ended(a, done, got < 0 ? 0 : status);        /* may start another: the slot is free first */
            continue;
        }
        if (!r->heard_ms) r->heard_ms = now_ms ? now_ms : 1;
        /* One holding a wait is listening, not silent. */
        char     own[CTL_NAME_MAX];
        unsigned seq;
        snprintf(own, sizeof own, AGENT_NAME_FMT, r->job);
        if (!r->stopping && r->job && ctl_waiter_of(&a->ctl, own, &seq) >= 0) r->heard_ms = now_ms;
        /* Its group: the shell and whatever it started that did not leave it. */
        if (!r->stopping && now_ms - r->heard_ms >= (uint64_t)a->agent_deadline_ms) {
            kill((pid_t)-r->pid, SIGTERM);
            r->stopping = 1;
            r->heard_ms = now_ms;
        } else if (r->stopping == 1 && now_ms - r->heard_ms >= (uint64_t)a->agent_grace_ms) {
            kill((pid_t)-r->pid, SIGKILL);
            r->stopping = 2;
        }
        i++;
    }
}

int app_agent_start(App *a, const Job *j)
{
    /* For what the GM asked for, and nothing else (decision 1b: "for each
     * :ask"): an agent's own idea, an --apply or the file's change has its
     * maker already, and a second agent on its job would only collide. */
    if (!a->agent_cmd[0] || a->agent_cmd_off || j->from != JOB_FROM_GM) return 0;
    PROF_ZONE("agent.spawn");
    char msg[AGENT_CMD_MAX + 160], err[CTL_PATH_MAX + 64];
    int  turned_on = 0;
    /* One agent a job: a second would collide with the first on the same
     * squares. It is started when the one running stops. */
    AgentRun *running = run_of(a, j->num);
    if (running) {
        running->again = 1;
        snprintf(msg, sizeof msg, "#%d: its agent is still running - it is started again when that one stops", j->num);
        app_note_gm(a, msg);
        return 0;
    }
    /* Full: runs whose job is gone and which have stopped make room here,
     * since nothing of theirs is left to say (a vtt that never ticks -- the
     * bench's key path -- would otherwise fill up on jobs asked and removed).
     * Never reached from inside the tick: it frees a slot before it starts. */
    for (int i = 0; a->nagents == JOB_MAX && i < a->nagents; ) {
        if (!a->agents[i].job && waitpid((pid_t)a->agents[i].pid, NULL, WNOHANG) != 0) a->agents[i] = a->agents[--a->nagents];
        else i++;
    }
    if (a->nagents == JOB_MAX) {
        snprintf(msg, sizeof msg, "#%d: %d agents are running already - none started; :ask again when one stops", j->num, JOB_MAX);
        app_note_gm(a, msg);
        return -1;
    }
    /* The command answers over the channel: on, for it -- and said. */
    if (!a->world.agent_on) {
        if (ctl_start(&a->ctl, err, sizeof err) < 0) {
            snprintf(msg, sizeof msg, "#%d: the agent was not started - %.150s", j->num, err);
            app_note_gm(a, msg);
            return -1;
        }
        a->world.agent_on = 1;
        turned_on = 1;
    }

    char  *prompt = NULL;
    size_t plen = 0;
    FILE  *pf = open_memstream(&prompt, &plen);
    int    in[2] = { -1, -1 };
    if (pf) { app_agent_prompt(a, j, pf); fclose(pf); }
    if (!pf || !prompt || pipe(in) < 0) {
        snprintf(msg, sizeof msg, "#%d: the agent was not started - %.100s", j->num, strerror(errno));
        app_note_gm(a, msg);
        free(prompt);
        return -1;
    }
    char job[16], pidv[24];
    snprintf(job, sizeof job, "%d", j->num);
    snprintf(pidv, sizeof pidv, "%ld", (long)getpid());

    fflush(NULL);                       /* nothing of ours buffered goes out twice */
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(msg, sizeof msg, "#%d: the agent was not started - %.100s", j->num, strerror(errno));
        app_note_gm(a, msg);
        close(in[0]); close(in[1]); free(prompt);
        return -1;
    }
    if (pid == 0) {
        /* A session of its own: our terminal is not its controlling one,
         * and it leads a group that can be stopped whole. */
        setsid();
        close(in[1]);
        dup2(in[0], 0);
        int out = slog_on(&a->slog) ? open(a->slog.path, O_WRONLY | O_APPEND) : -1;
        if (out < 0) out = open("/dev/null", O_WRONLY);
        if (out >= 0) { dup2(out, 1); dup2(out, 2); }
        for (int fd = 3; fd < 256; fd++) close(fd);        /* the terminal, the sockets, the log */
        signal(SIGPIPE, SIG_DFL);       /* ignored here (term.c): a pipeline in the command must end quietly */
        setenv("VTT_JOB", job, 1);
        /* Its name on the channel (docs/V2.md, D8): the job's own, so two
         * agents started for two jobs cannot take each other's. */
        char who[CTL_NAME_MAX];
        snprintf(who, sizeof who, AGENT_NAME_FMT, j->num);
        setenv("VTT_AGENT", who, 1);
        setenv("VTT_PID", pidv, 1);
        if (app_self_path) setenv("VTT", app_self_path, 1);
        execl("/bin/sh", "sh", "-c", a->agent_cmd, (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    AgentRun *run = &a->agents[a->nagents++];
    memset(run, 0, sizeof *run);
    run->pid      = (long)pid;
    run->job      = j->num;
    /* heard_ms stays 0 until the tick sees it: the deadline is counted on
     * the tick's clock, from the tick's own first look. */
    /* The prompt is a few KB: it fits the pipe, so this never waits on the
     * agent reading it. A command that exits without reading it must not
     * take vtt with it: SIGPIPE is ignored for the write. */
    void (*pipe_was)(int) = signal(SIGPIPE, SIG_IGN);
    for (size_t off = 0; off < plen; ) {
        ssize_t w = write(in[1], prompt + off, plen - off);
        if (w > 0) off += (size_t)w;
        else if (w < 0 && errno == EINTR) continue;
        else break;
    }
    signal(SIGPIPE, pipe_was);
    close(in[1]);
    free(prompt);
    /* On the GM's line: what was started, where its output is (a command
     * that cannot run says so only there), and the channel if it came on. */
    snprintf(msg, sizeof msg, "#%d asked - started %.150s%s%s", j->num, a->agent_cmd,
             slog_on(&a->slog) ? "" : " (:log on shows what it prints)",
             turned_on ? " - agent channel on for it" : "");
    app_note_gm(a, msg);
    return 1;
}

void app_agent_command_cmd(App *a, const char *rest)
{
    char msg[AGENT_CMD_MAX + 96];
    while (*rest == ' ') rest++;
    if (!*rest) {
        if (a->agent_cmd[0] && !a->agent_cmd_off)
            snprintf(msg, sizeof msg, "each :ask starts: %s - :agent command off stops that", a->agent_cmd);
        else if (a->agent_cmd[0])
            snprintf(msg, sizeof msg, "the agent command is off (:agent command on): %s", a->agent_cmd);
        else snprintf(msg, sizeof msg, ":agent command CMD starts CMD for each :ask (the job on its stdin); none is set");
        app_set_status_gm(a, msg);
        return;
    }
    /* off and on: the command is kept, and on brings it back as it was
     * (KEYS.md rule 9). */
    if (!strcmp(rest, "off")) {
        if (!a->agent_cmd[0] || a->agent_cmd_off) { app_set_status_gm(a, "no agent command is set"); return; }
        a->agent_cmd_off = 1;
        app_note_gm(a, "agent command off - :ask waits for an agent that comes for work; :agent command on brings it back");
        return;
    }
    if (!strcmp(rest, "on")) {
        if (!a->agent_cmd[0]) { app_set_status_gm(a, "no agent command to switch on - :agent command CMD sets one"); return; }
        a->agent_cmd_off = 0;
        snprintf(msg, sizeof msg, "each :ask now starts: %s", a->agent_cmd);
        app_note_gm(a, msg);
        return;
    }
    if (strlen(rest) >= sizeof a->agent_cmd) {
        snprintf(msg, sizeof msg, "the command is over %d characters - put it in a script", AGENT_CMD_MAX - 1);
        app_set_status_gm(a, msg);
        return;
    }
    str_lcpy(a->agent_cmd, rest, sizeof a->agent_cmd);
    a->agent_cmd_off = 0;
    snprintf(msg, sizeof msg, "each :ask now starts: %s", a->agent_cmd);
    app_note_gm(a, msg);
}
