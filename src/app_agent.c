/* :agent command (docs/CONFLICTS.md, decision 1b): a command vtt starts for
 * each job the GM asks for (:ask) and each one sent back with feedback, so
 * asking works with no agent already waiting. The command gets the job and
 * its thread on stdin, with how to answer; it reports back through the
 * channel like any agent (docs/AGENTS.md). It runs detached -- nobody's
 * child, no terminal -- with its output in the session log when one is on.
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
    if (j->has_box) map_region_name(j->box.x0, j->box.y0, j->box.x1, j->box.y1, where, sizeof where);

    fputs("You are an agent for the GM of a tabletop map open in vtt, a virtual tabletop in a terminal.\n"
          "The GM asked for a change to the map. Make it as a proposal: the GM reviews it and accepts it or not.\n\n", out);
    fprintf(out, "Job #%d: %s\n", j->num, j->text);
    if (where[0]) fprintf(out, "Where: %s (the box the GM drew)\n", where);
    else          fputs("Where: the GM drew no box - `marked` says what they are pointing at\n", out);
    if (a->map)   fprintf(out, "Map: %s, %dx%d squares (A1 is the top left)\n", a->map->name, a->map->w, a->map->h);
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
          "The lines that change a map: room, tile, wall, edge, door, corridor, area, token add|move|del|set, note,\n"
          "fog paint, stamp, link, floor, scene. docs/AGENTS.md in vtt's source has all of them.\n"
          "Propose once, read it back, then stop. The verdict is the GM's; feedback starts you again with this job.\n", out);
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
    /* The command answers over the channel: on, for it -- and said. */
    if (!a->agent_on) {
        if (ctl_start(&a->ctl, err, sizeof err) < 0) {
            snprintf(msg, sizeof msg, "#%d: the agent was not started - %.150s", j->num, err);
            app_note_gm(a, msg);
            return -1;
        }
        a->agent_on = 1;
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
        /* Twice, so the agent is nobody's child (no zombie to reap) and our
         * terminal is not its controlling one. */
        setsid();
        if (fork() != 0) _exit(0);
        close(in[1]);
        dup2(in[0], 0);
        int out = slog_on(&a->slog) ? open(a->slog.path, O_WRONLY | O_APPEND) : -1;
        if (out < 0) out = open("/dev/null", O_WRONLY);
        if (out >= 0) { dup2(out, 1); dup2(out, 2); }
        for (int fd = 3; fd < 256; fd++) close(fd);        /* the terminal, the sockets, the log */
        signal(SIGPIPE, SIG_DFL);       /* ignored here (term.c): a pipeline in the command must end quietly */
        setenv("VTT_JOB", job, 1);
        setenv("VTT_PID", pidv, 1);
        if (app_self_path) setenv("VTT", app_self_path, 1);
        execl("/bin/sh", "sh", "-c", a->agent_cmd, (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) { }
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
