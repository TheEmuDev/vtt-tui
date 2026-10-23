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

#include "apiagent.h"
#include "apikey.h"
#include "app_priv.h"
#include "proc.h"
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
            fprintf(out, "  %s: %s\n", world_job_who_name(j->thread[t].who), j->thread[t].text);
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
    request_edit_verbs(out);            /* from the channel's own table: it cannot fall behind it */
    fputs(".\n    ", out);
    sh_quote(out, vtt); fprintf(out, "%s 'help token'\n", call);
    fputs("    how one of them is written; help alone lists every request.\n", out);
    sh_quote(out, vtt); fputs(" --agent-guide\n", out);
    fputs("    the whole guide for agents: rooms, corridors, creatures, links, the file format.\n"
          "Propose once, read it back, then stop. The verdict is the GM's; feedback starts you again with this job.\n", out);
}

static AgentRun *run_of(App *a, int job)
{
    for (int i = 0; i < a->agents.nrun; i++)
        if (a->agents.run[i].job == job) return &a->agents.run[i];
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
    if (!a->agents.nrun || sscanf(who, AGENT_NAME_FMT, &num) != 1 || num < 1) return;
    AgentRun *r = run_of(a, num);
    if (r && !r->stopping && r->heard_ms) r->heard_ms = a->world.now_ms ? a->world.now_ms : 1;
}

int app_agents_due(const App *a, uint64_t now_ms)
{
    int due = -1;
    for (int i = 0; i < a->agents.nrun; i++) {
        const AgentRun *r = &a->agents.run[i];
        if (!r->heard_ms) return 0;                           /* not yet seen by a tick */
        /* Told to stop and then made to: nothing more to do at any time. Its
         * end wakes the loop (SIGCHLD); one that cannot die must not spin it. */
        if (r->stopping == 2) continue;
        uint64_t at = r->heard_ms + (uint64_t)(r->stopping ? a->agents.grace_ms : a->agents.deadline_ms);
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
    int slot = r.job ? world_job_find(&a->world, r.job) : -1;
    if (slot < 0) return;
    Job *j = &a->world.jobs[slot];
    /* A job done meanwhile (the old run proposed late and the GM took it,
     * or scrapped it) wants nobody. */
    if (j->state != JOB_ASKED && j->state != JOB_WORKING) return;
    if (r.again) { app_agent_start(a, j); return; }         /* the feedback that came while it ran */
    char own[CTL_NAME_MAX], how[80], msg[480], said[JOB_TEXT_MAX + 8] = "";
    snprintf(own, sizeof own, AGENT_NAME_FMT, j->num);
    if (j->taker[0] && strcmp(j->taker, own) != 0) return;
    ended_how(&r, status, a->agents.deadline_ms, how, sizeof how);
    /* An agent that says why it stops (job N say, as the API agent does on a
     * refused key) is heard: its last line stays on the GM's. */
    if (j->nthread && j->thread[j->nthread - 1].who == 'A') snprintf(said, sizeof said, ": %.150s", j->thread[j->nthread - 1].text);
    j->state    = JOB_ASKED;
    j->taker[0] = '\0';
    j->has_area = 0;
    snprintf(msg, sizeof msg, "its agent stopped (%s)", how);
    world_job_thread_add(j, '-', msg);
    world_event(&a->world, CTL_EV_ASKED, j->num, "its agent stopped (%s): %.120s", how, j->text);
    snprintf(msg, sizeof msg, "#%d: its agent stopped with nothing proposed (%s)%s - the job waits as asked%s", j->num, how, said,
             slog_on(&a->slog) ? "; the session log has what it printed" : "; :log on shows what the next one prints");
    app_note_gm(a, msg);
    a->dirty = 1;
}

void app_agents_tick(App *a, uint64_t now_ms)
{
    if (!a->agents.nrun) return;
    PROF_ZONE("agent.reap");
    for (int i = 0; i < a->agents.nrun; ) {
        AgentRun *r = &a->agents.run[i];
        int   status = 0;
        pid_t got = waitpid((pid_t)r->pid, &status, WNOHANG);
        if (got == (pid_t)r->pid || (got < 0 && errno == ECHILD)) {
            AgentRun done = *r;
            *r = a->agents.run[--a->agents.nrun];
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
        if (!r->stopping && now_ms - r->heard_ms >= (uint64_t)a->agents.deadline_ms) {
            kill((pid_t)-r->pid, SIGTERM);
            r->stopping = 1;
            r->heard_ms = now_ms;
        } else if (r->stopping == 1 && now_ms - r->heard_ms >= (uint64_t)a->agents.grace_ms) {
            kill((pid_t)-r->pid, SIGKILL);
            r->stopping = 2;
        }
        i++;
    }
}

/* What an :ask starts, as the GM reads it: the Claude agent by its model,
 * a command as it was typed. */
static void agent_shown(const App *a, char *buf, size_t sz)
{
    if (!a->agents.from_env && a->settings.agent == AGENT_KIND_CLAUDE)
        snprintf(buf, sz, "the Claude agent (%s, %s effort)", a->settings.model, a->settings.effort);
    else snprintf(buf, sz, "%.250s", a->agents.cmd);
}

int app_agent_start(App *a, const Job *j)
{
    /* For what the GM asked for, and nothing else (decision 1b: "for each
     * :ask"): an agent's own idea, an --apply or the file's change has its
     * maker already, and a second agent on its job would only collide. */
    if (!a->agents.cmd[0] || a->agents.cmd_off || j->from != JOB_FROM_GM) return 0;
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
    for (int i = 0; a->agents.nrun == JOB_MAX && i < a->agents.nrun; ) {
        if (!a->agents.run[i].job && waitpid((pid_t)a->agents.run[i].pid, NULL, WNOHANG) != 0) a->agents.run[i] = a->agents.run[--a->agents.nrun];
        else i++;
    }
    if (a->agents.nrun == JOB_MAX) {
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
    /* The key vtt holds (this session's, or the one it took from its
     * environment) goes to the Claude agent down a pipe, written now: it is
     * a few hundred bytes, which a pipe holds whole. A keyring key is not
     * sent: the agent looks it up itself, a call at a time. */
    int claude = !a->agents.from_env && a->settings.agent == AGENT_KIND_CLAUDE;
    int kp[2] = { -1, -1 };
    {
        char k[APIKEY_MAX];
        if (claude && apikey_held(k, sizeof k) != APIKEY_NONE && pipe(kp) == 0) {
            size_t kn = strlen(k);
            if (write(kp[1], k, kn) != (ssize_t)kn) { close(kp[0]); kp[0] = -1; }
            close(kp[1]);
            kp[1] = -1;
        }
        apikey_wipe(k, sizeof k);
    }

    fflush(NULL);                       /* nothing of ours buffered goes out twice */
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(msg, sizeof msg, "#%d: the agent was not started - %.100s", j->num, strerror(errno));
        app_note_gm(a, msg);
        close(in[0]); close(in[1]); free(prompt);
        if (kp[0] >= 0) close(kp[0]);
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
        for (int fd = 3; fd < 256; fd++) if (fd != kp[0]) close(fd);   /* the terminal, the sockets, the log */
        if (kp[0] >= 0) {
            if (kp[0] != 3) { dup2(kp[0], 3); close(kp[0]); }
            setenv("VTT_KEY_FD", "3", 1);
        }
        signal(SIGPIPE, SIG_DFL);       /* ignored here (term.c): a pipeline in the command must end quietly */
        /* No key in the environment (docs/APIAGENT.md, *The key, made safer*):
         * vtt took ANTHROPIC_API_KEY out of its own at start; an agent
         * command gets the exported one back only when the GM said so. */
        unsetenv("ANTHROPIC_API_KEY");
        if (!claude && a->settings.pass_key) {
            char k[APIKEY_MAX];
            if (apikey_held(k, sizeof k) == APIKEY_ENV) setenv("ANTHROPIC_API_KEY", k, 1);
            apikey_wipe(k, sizeof k);
        }
        setenv("VTT_JOB", job, 1);
        /* Its name on the channel (docs/V2.md, D8): the job's own, so two
         * agents started for two jobs cannot take each other's. */
        char who[CTL_NAME_MAX];
        snprintf(who, sizeof who, AGENT_NAME_FMT, j->num);
        setenv("VTT_AGENT", who, 1);
        setenv("VTT_PID", pidv, 1);
        if (app_self_path) setenv("VTT", app_self_path, 1);
        execl("/bin/sh", "sh", "-c", a->agents.cmd, (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    if (kp[0] >= 0) close(kp[0]);
    AgentRun *run = &a->agents.run[a->agents.nrun++];
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
    char shown[300];
    agent_shown(a, shown, sizeof shown);
    snprintf(msg, sizeof msg, "#%d asked - started %s%s%s", j->num, shown,
             slog_on(&a->slog) ? "" : " (:log on shows what it prints)",
             turned_on ? " - agent channel on for it" : "");
    app_note_gm(a, msg);
    return 1;
}

/* ------------------------------------------------------------ the settings */

void app_agent_apply(App *a)
{
    const Settings *st = &a->settings;
    a->agents.cmd_off = st->paused;
    if (a->agents.from_env) return;                   /* VTT_AGENT_COMMAND, for this session */
    if (st->agent == AGENT_KIND_COMMAND) { str_lcpy(a->agents.cmd, st->command, sizeof a->agents.cmd); return; }
    if (st->agent != AGENT_KIND_CLAUDE) { a->agents.cmd[0] = '\0'; return; }
    /* The Claude agent is this vtt again, told what the settings say: it
     * never reads them itself, and the session log shows what a run used. */
    char  *line;
    size_t len;
    FILE  *f = xmemstream(&line, &len);
    sh_quote(f, app_self_path ? app_self_path : "vtt");
    const char *model = apiagent_model_id(st->model);
    fprintf(f, " --claude-agent --model %s --effort %s --cap %.2f --calls %d --fallbacks %s",
            model ? model : "claude-opus-5-5", st->effort, st->cap, st->calls, st->fallbacks ? "on" : "off");
    fclose(f);
    if (len < sizeof a->agents.cmd) str_lcpy(a->agents.cmd, line, sizeof a->agents.cmd);
    else a->agents.cmd[0] = '\0';
    free(line);
}

void app_settings_changed(App *a)
{
    app_agent_apply(a);
    if (!a->settings_file[0]) return;
    char err[1200];
    if (settings_save(&a->settings, a->settings_file, err, sizeof err) < 0) app_set_status_gm(a, err);
}

void app_agent_describe(const App *a, char *buf, size_t sz)
{
    const Settings *st = &a->settings;
    const char *paused = st->paused ? " - paused, :agent command on starts it again" : "";
    if (a->agents.from_env) snprintf(buf, sz, "agent: %.150s (VTT_AGENT_COMMAND, this session)%s", a->agents.cmd, paused);
    else if (st->agent == AGENT_KIND_CLAUDE) {
        const char *id = apiagent_model_id(st->model);
        char cap[32];
        if (st->cap > 0) snprintf(cap, sizeof cap, "$%.2f and ", st->cap);
        else cap[0] = '\0';
        snprintf(buf, sz, "agent claude: %s%s%s%s, %s effort, %s%d calls a job, fallbacks %s%s", st->model,
                 id && strcmp(id, st->model) ? " (" : "", id && strcmp(id, st->model) ? id : "", id && strcmp(id, st->model) ? ")" : "",
                 st->effort, cap, st->calls, st->fallbacks ? "on" : "off", paused);
    }
    else if (st->agent == AGENT_KIND_COMMAND) snprintf(buf, sz, "agent command: %.150s%s", st->command, paused);
    else if (st->agent == AGENT_KIND_OFF) snprintf(buf, sz, "agent off: an :ask waits for an agent that comes to the channel");
    else snprintf(buf, sz, "no agent chosen: :agent claude, :agent command CMD, or the Options screen");
}

/* Whether the Claude agent could run now: a key, and curl. The key's copy
 * is wiped; only where it is from is kept. */
static int claude_ready(App *a, char *why, size_t whysz)
{
    char last4[8], err[600];
    if (apikey_where(last4, sizeof last4, err, sizeof err) == APIKEY_NONE) { snprintf(why, whysz, "%s", err); return 0; }
    const char *curl = getenv("VTT_CLAUDE_CURL");
    if (!proc_found(curl && *curl ? curl : "curl")) { snprintf(why, whysz, "the Claude agent needs curl, and it is not on PATH"); return 0; }
    return 1;
}

/* ------------------------------------------------- an :ask, and who answers it */

/* What the channel knows of an agent listening: a wait held now (by name),
 * or one in the last ten minutes. Nothing guessed. */
static int listening(const App *a, char *who, size_t sz)
{
    unsigned seq;
    uint64_t until;
    const char *name;
    for (int i = 0; i < ctl_waiters(&a->ctl); i++)
        if (ctl_waiter(&a->ctl, i, &seq, &until, &name)) { snprintf(who, sz, "%s is waiting for work", name && *name ? name : "an agent"); return 1; }
    uint64_t now = a->world.now_ms;
    if (a->world.agent_seen_ms && now >= a->world.agent_seen_ms && now - a->world.agent_seen_ms < (uint64_t)CTL_WAIT_MAX_S * 1000) {
        snprintf(who, sz, "an agent waited for work %d min ago", (int)((now - a->world.agent_seen_ms) / 60000));
        return 1;
    }
    return 0;
}

void app_agent_ask(App *a, const Job *j, const char *asked)
{
    char msg[1100], why[600], who[96];
    const Settings *st = &a->settings;
    if (a->agents.cmd_off && a->agents.cmd[0]) {
        snprintf(msg, sizeof msg, "%s - the agent is paused (:agent command on starts it); the job waits", asked);
        app_note_gm(a, msg);
        return;
    }
    if (!a->agents.from_env && st->agent == AGENT_KIND_CLAUDE && !claude_ready(a, why, sizeof why)) {
        snprintf(msg, sizeof msg, "%s - the Claude agent cannot start: %s; the job waits", asked, why);
        app_note_gm(a, msg);
        return;
    }
    if (a->agents.cmd[0]) { app_note_gm(a, asked); app_agent_start(a, j); return; }
    if (!a->agents.from_env && st->agent == AGENT_KIND_UNSET) {
        app_note_gm(a, asked);
        a->agents.choice_job = j->num;
        /* Read once, here: the box is drawn every frame, and a lookup is a
         * keyring round trip (or its unlock window). */
        a->options.key = app_agent_key_where(a->options.key_last4, sizeof a->options.key_last4);
        snprintf(a->modal_title, sizeof a->modal_title, "How should agents answer :ask?");
        a->modal = MODAL_CHOOSE_AGENT;
        a->dirty = 1;
        return;
    }
    if (listening(a, who, sizeof who))
        snprintf(msg, sizeof msg, "%s - for the agent on the channel (%s)", asked, who);
    else
        snprintf(msg, sizeof msg, "%s - no agent is set to start and none is listening: :agent claude, :agent command CMD, or Options%s",
                 asked, a->world.agent_on ? "" : "; :agent on lets one come by itself");
    app_note_gm(a, msg);
}

int app_agent_choice_rows(const App *a, UiChoice *items, int max)
{
    if (max < 3) return 0;
    int src = a->options.key;                        /* read when the box opened: never a lookup a frame */
    if (src == APIKEY_NONE) snprintf(items[0].text, sizeof items[0].text, "c  the Claude API agent  (no key yet - c asks for it)");
    else snprintf(items[0].text, sizeof items[0].text, "c  the Claude API agent  (key: %s ...%s)", apikey_source_name(src), a->options.key_last4);
    snprintf(items[1].text, sizeof items[1].text, "m  a command  (Claude Code, a script...)");
    snprintf(items[2].text, sizeof items[2].text, "w  wait for an agent that comes to the channel");
    for (int i = 0; i < 3; i++) items[i].color = a->th->fg;
    return 3;
}

void app_agent_choice_continue(App *a)
{
    int num = a->agents.choice_job;
    a->agents.choice_job = 0;
    int slot = num ? world_job_find(&a->world, num) : -1;
    if (slot < 0) return;
    Job *j = &a->world.jobs[slot];
    if (j->state == JOB_ASKED && a->agents.cmd[0] && !a->agents.cmd_off) app_agent_start(a, j);
}

void app_agent_choice_key(App *a, Key k)
{
    if (k.kind == KEY_ESC || (k.kind == KEY_CHAR && k.ch == 'q')) {
        a->modal = MODAL_NONE;
        char msg[160];
        snprintf(msg, sizeof msg, "#%d waits - no agent chosen yet; the next :ask asks again (or Options, :agent)", a->agents.choice_job);
        a->agents.choice_job = 0;
        app_set_status_gm(a, msg);
        return;
    }
    if (k.kind != KEY_CHAR || k.mods) return;
    uint32_t c = k.ch;
    if (c == '1') c = 'c';
    else if (c == '2') c = 'm';
    else if (c == '3') c = 'w';
    if (c == 'c') {
        a->modal = MODAL_NONE;
        app_agent_setting(a, "claude");          /* asks for a key first when there is none */
        if (a->settings.agent == AGENT_KIND_CLAUDE) app_agent_choice_continue(a);
        else if (a->modal != MODAL_PROMPT) a->agents.choice_job = 0;   /* refused (no curl): said, nothing waits on it */
        return;
    }
    if (c == 'm') {
        a->modal = MODAL_NONE;
        a->options.prompt = OPT_COMMAND;
        app_open_prompt(a, PROMPT_SETTING, "Agent command", "started for each :ask, the job on its stdin", "");
        a->prompt.max = SETTINGS_COMMAND_MAX;
        return;
    }
    if (c == 'w') {
        a->modal = MODAL_NONE;
        a->settings.agent = AGENT_KIND_OFF;
        app_settings_changed(a);
        int num = a->agents.choice_job;
        a->agents.choice_job = 0;
        if (!a->world.agent_on) app_agent_command(a, "on");
        char msg[200];
        snprintf(msg, sizeof msg, "#%d waits for an agent that comes to the channel - saved; :agent claude or :agent command change it", num);
        app_note_gm(a, msg);
    }
}

/* ------------------------------------------------------------- the API key */

int app_agent_key_where(char *last4, size_t sz)
{
    char err[600];
    return apikey_where(last4, sz, err, sizeof err);
}

void app_agent_key_prompt(App *a, int then, const char *why)
{
    char hint[96], last4[8];
    int  src = app_agent_key_where(last4, sizeof last4);
    if (why && *why) snprintf(hint, sizeof hint, "%.90s", why);
    else if (src == APIKEY_NONE) snprintf(hint, sizeof hint, "none yet - shown as *; then the keyring, or this session only");
    else snprintf(hint, sizeof hint, "now: the %s's, ending ...%s - a new one replaces it", apikey_source_name(src), last4);
    app_open_prompt(a, PROMPT_API_KEY, "Anthropic API key", hint, "");
    a->prompt.secret = 1;
    a->prompt.max = APIKEY_MAX < UI_PROMPT_MAX ? APIKEY_MAX : UI_PROMPT_MAX;
    a->agents.key_then = then;
    apikey_typing(1);                   /* not dumpable while it is typed */
}

/* After a key is kept or held: what the prompt was opened for. */
static void key_then(App *a, int then)
{
    app_options_key_changed(a);
    if (then == KEY_THEN_CLAUDE) {
        app_agent_setting(a, "claude");
        if (a->settings.agent == AGENT_KIND_CLAUDE) app_agent_choice_continue(a);   /* the job the box was for */
        else a->agents.choice_job = 0;            /* refused (no curl), and said: nothing waits on the box now */
    }
}

/* Held for this session: in vtt's memory only, gone when it quits. */
static void key_held(App *a, int then, const char *last4)
{
    char msg[300];
    int was_env = apikey_held_src() == APIKEY_ENV;
    apikey_pending_session();
    snprintf(msg, sizeof msg, "API key ...%s held for this session only - gone when vtt quits%s", last4,
             was_env ? " (in place of ANTHROPIC_API_KEY's)" : "");
    app_note_gm(a, msg);
    key_then(a, then);
}

void app_agent_key_typed(App *a, const char *key)
{
    int  then = a->agents.key_then;
    /* Pasted keys bring white space at the ends; inside, it is not a key.
     * Still "typing" (not dumpable) until the key is in its pending slot. */
    while (*key == ' ' || *key == '\t') key++;
    char k[APIKEY_MAX];
    str_lcpy(k, key, sizeof k);
    size_t n = strlen(k);
    while (n && (k[n - 1] == ' ' || k[n - 1] == '\t' || k[n - 1] == '\r' || k[n - 1] == '\n')) k[--n] = '\0';
    if (!*k) {
        apikey_wipe(k, sizeof k);
        apikey_typing(0);
        a->agents.key_then = KEY_THEN_NOTHING;
        app_set_status_gm(a, "canceled - no key stored");
        return;
    }
    if (!apikey_ok(k)) {
        apikey_wipe(k, sizeof k);
        app_agent_key_prompt(a, then, "not a key: letters, digits, - and _ only - again, or esc");
        return;
    }
    apikey_pending_set(k);
    char last4[8];
    snprintf(last4, sizeof last4, "%s", k + n - 4);
    apikey_wipe(k, sizeof k);
    apikey_typing(0);                   /* the pending slot keeps it not dumpable */
    /* Where it goes is the GM's word; with no keyring there is one answer. */
    if (!apikey_keyring_here()) {
        a->agents.key_then = KEY_THEN_NOTHING;
        key_held(a, then, last4);
        return;
    }
    snprintf(a->modal_title, sizeof a->modal_title, "Keep the API key ending ...%s?", last4);
    snprintf(a->modal_body, sizeof a->modal_body,
             "r: in your keyring, encrypted and unlocked when you log in, for every session.\n"
             "s: for this session only, in vtt's memory, gone when vtt quits.");
    a->modal = MODAL_KEY_WHERE;
    a->dirty = 1;
}

void app_agent_key_where_chosen(App *a, int ch)
{
    int then = a->agents.key_then;
    a->agents.key_then = KEY_THEN_NOTHING;
    a->modal = MODAL_NONE;
    char last4[8], err[300], msg[400];
    apikey_pending_last4(last4, sizeof last4);
    if (ch == 's') { key_held(a, then, last4); return; }
    if (ch != 'r') { apikey_pending_drop(); app_set_status_gm(a, "canceled - no key stored"); return; }
    if (apikey_pending_keep(err, sizeof err) < 0) { app_set_status_gm(a, err); return; }
    ApiKeySource h = apikey_held_src();
    if (h == APIKEY_SESSION) apikey_drop();   /* the session's own gives way to the one kept */
    snprintf(msg, sizeof msg, "API key ...%s stored in your keyring%s", last4,
             h == APIKEY_ENV ? " - but ANTHROPIC_API_KEY was set when vtt started, and it is the one used" : "");
    app_note_gm(a, msg);
    key_then(a, then);
}

void app_agent_key_remove(App *a)
{
    char err[600], last4[8], msg[200];
    ApiKeySource h = apikey_held_src();
    if (h == APIKEY_SESSION) {
        apikey_drop();
        app_options_key_changed(a);
        if (apikey_where_stored(last4, sizeof last4, err, sizeof err) == APIKEY_KEYRING)
            snprintf(msg, sizeof msg, "the session's API key is let go - the keyring's, ending ...%s, is used now", last4);
        else snprintf(msg, sizeof msg, "the session's API key is let go");
        app_note_gm(a, msg);
        return;
    }
    if (apikey_remove(err, sizeof err) < 0) { app_set_status_gm(a, err); return; }
    app_options_key_changed(a);
    if (h == APIKEY_ENV) app_note_gm(a, "the stored key is removed - ANTHROPIC_API_KEY, from the environment vtt started with, is used until vtt quits");
    else app_note_gm(a, "the stored API key is removed");
}

static void key_remove_ask(App *a)
{
    char err[600], last4[8] = "", kept4[8] = "";
    ApiKeySource h = apikey_held_src();
    /* This session's key is the one to let go; else what is stored, whatever
     * the environment held: a typed key behind the variable is still the GM's
     * to take away. */
    ApiKeySource stored = apikey_where_stored(kept4, sizeof kept4, err, sizeof err);
    ApiKeySource src = h == APIKEY_SESSION ? apikey_where(last4, sizeof last4, err, sizeof err) : stored;
    if (src == APIKEY_KEYRING) str_lcpy(last4, kept4, sizeof last4);
    int env = h == APIKEY_ENV;
    if (src == APIKEY_NONE) {
        app_set_status_gm(a, env ? "no API key is stored - the one in use is ANTHROPIC_API_KEY, from the environment vtt started with"
                                 : "no API key is stored");
        return;
    }
    snprintf(a->modal_title, sizeof a->modal_title, "Remove the API key?");
    if (src == APIKEY_SESSION && stored == APIKEY_KEYRING)
        snprintf(a->modal_body, sizeof a->modal_body, "The key ending ...%s, held for this session.\nThe keyring's, ending ...%s, is used then.", last4, kept4);
    else if (src == APIKEY_SESSION)
        snprintf(a->modal_body, sizeof a->modal_body, "The key ending ...%s, held for this session.\nThe Claude agent stops until a key is given again.", last4);
    else snprintf(a->modal_body, sizeof a->modal_body, "The key ending ...%s, from your %s%s", last4, apikey_source_name(src),
                  env ? ".\nANTHROPIC_API_KEY is used until vtt quits." : ".\nThe Claude agent stops until a key is given again.");
    a->modal = MODAL_CONFIRM_KEY;
    a->dirty = 1;
}

static int word_is(const char *rest, const char *w, const char **arg)
{
    size_t n = strlen(w);
    if (strncmp(rest, w, n) || (rest[n] && rest[n] != ' ')) return 0;
    *arg = rest + n;
    while (**arg == ' ') (*arg)++;
    return 1;
}

int app_agent_setting(App *a, const char *rest)
{
    static const struct { const char *word; SettingId id; } ONE[] = {
        { "model", SET_MODEL }, { "effort", SET_EFFORT }, { "cap", SET_CAP }, { "calls", SET_CALLS },
        { "fallbacks", SET_FALLBACKS },
    };
    char msg[700], err[600];
    const char *arg;
    if (word_is(rest, "key", &arg)) {
        if (!strcmp(arg, "remove")) key_remove_ask(a);
        else if (!strcmp(arg, "off")) app_set_status_gm(a, ":agent key remove takes the stored key away");   /* KEYS.md rule 9 */
        else if (*arg) app_set_status_gm(a, ":agent key types a key in (hidden); :agent key remove takes it away");
        else app_agent_key_prompt(a, KEY_THEN_NOTHING, NULL);
        return 1;
    }
    if (word_is(rest, "passkey", &arg)) {
        char v[8];
        if (!*arg) {
            settings_value(&a->settings, SET_PASSKEY, v, sizeof v);
            snprintf(msg, sizeof msg, "passkey is %s - :agent passkey on gives agent commands an exported ANTHROPIC_API_KEY", v);
            app_set_status_gm(a, msg);
            return 1;
        }
        if (settings_set(&a->settings, SET_PASSKEY, arg, err, sizeof err) < 0) { app_set_status_gm(a, err); return 1; }
        app_settings_changed(a);
        app_note_gm(a, a->settings.pass_key ? "passkey on - agent commands get ANTHROPIC_API_KEY when vtt was started with it"
                                            : "passkey off - an exported ANTHROPIC_API_KEY stays with vtt and the Claude agent");
        return 1;
    }
    if (word_is(rest, "claude", &arg)) {
        Settings was = a->settings;
        if (*arg && settings_set(&a->settings, SET_MODEL, arg, err, sizeof err) < 0) { app_set_status_gm(a, err); return 1; }
        if (!claude_ready(a, err, sizeof err)) {
            /* No key: asked for one, and the agent is set once it is stored
             * (the choice box's c does the same). No curl: nothing to ask. */
            char last4[8];
            if (app_agent_key_where(last4, sizeof last4) == APIKEY_NONE) {
                app_settings_changed(a);                 /* the model, if one was named */
                app_agent_key_prompt(a, KEY_THEN_CLAUDE, err);
                return 1;
            }
            a->settings = was;
            snprintf(msg, sizeof msg, "%s", err);
            app_set_status_gm(a, msg);
            return 1;
        }
        a->settings.agent = AGENT_KIND_CLAUDE;
        a->settings.paused = 0;
        a->agents.from_env = 0;
        app_settings_changed(a);
        char d[400];
        app_agent_describe(a, d, sizeof d);
        snprintf(msg, sizeof msg, "each :ask now starts the Claude agent - %s", d);
        app_note_gm(a, msg);
        return 1;
    }
    for (size_t i = 0; i < sizeof ONE / sizeof *ONE; i++) {
        if (!word_is(rest, ONE[i].word, &arg)) continue;
        char v[80];
        if (!*arg) {
            settings_value(&a->settings, ONE[i].id, v, sizeof v);
            snprintf(msg, sizeof msg, "%s is %s - :agent %s VALUE changes it", ONE[i].word, v, ONE[i].word);
            app_set_status_gm(a, msg);
            return 1;
        }
        if (settings_set(&a->settings, ONE[i].id, arg, err, sizeof err) < 0) { app_set_status_gm(a, err); return 1; }
        app_settings_changed(a);
        settings_value(&a->settings, ONE[i].id, v, sizeof v);
        snprintf(msg, sizeof msg, "%s %s%s", ONE[i].word, v,
                 a->settings.agent == AGENT_KIND_CLAUDE ? " - from the next :ask" : " - for the Claude agent (:agent claude)");
        app_note_gm(a, msg);
        return 1;
    }
    return 0;
}

void app_agent_command_cmd(App *a, const char *rest)
{
    char msg[AGENT_CMD_MAX + 96];
    while (*rest == ' ') rest++;
    if (!*rest) {
        char shown[300];
        agent_shown(a, shown, sizeof shown);
        if (a->agents.cmd[0] && !a->agents.cmd_off)
            snprintf(msg, sizeof msg, "each :ask starts: %s - :agent command off stops that", shown);
        else if (a->agents.cmd[0])
            snprintf(msg, sizeof msg, "the agent command is off (:agent command on): %s", shown);
        else snprintf(msg, sizeof msg, ":agent command CMD starts CMD for each :ask (the job on its stdin); none is set");
        app_set_status_gm(a, msg);
        return;
    }
    /* off and on: the command is kept, and on brings it back as it was
     * (KEYS.md rule 9). */
    if (!strcmp(rest, "off")) {
        if (!a->agents.cmd[0] || a->agents.cmd_off) { app_set_status_gm(a, "no agent command is set"); return; }
        a->settings.paused = 1;
        app_settings_changed(a);
        app_note_gm(a, "agent command off - :ask waits for an agent that comes for work; :agent command on brings it back");
        return;
    }
    if (!strcmp(rest, "on")) {
        if (!a->agents.cmd[0]) { app_set_status_gm(a, "no agent command to switch on - :agent command CMD sets one"); return; }
        a->settings.paused = 0;
        app_settings_changed(a);
        char shown[300];
        agent_shown(a, shown, sizeof shown);
        snprintf(msg, sizeof msg, "each :ask now starts: %s", shown);
        app_note_gm(a, msg);
        return;
    }
    char err[160];
    if (settings_set(&a->settings, SET_COMMAND, rest, err, sizeof err) < 0) { app_set_status_gm(a, err); return; }
    a->settings.agent  = AGENT_KIND_COMMAND;
    a->settings.paused = 0;
    a->agents.from_env  = 0;
    app_settings_changed(a);
    snprintf(msg, sizeof msg, "each :ask now starts: %.250s", a->agents.cmd);
    app_note_gm(a, msg);
}
