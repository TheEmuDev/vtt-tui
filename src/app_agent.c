/* :agent command (docs/CONFLICTS.md, decision 1b): a command vtt starts for
 * each job the GM asks for (:ask) and each one sent back with feedback, so
 * asking works with no agent already waiting. The command gets the job and
 * its thread on stdin, with how to answer; it reports back through the
 * channel like any agent (docs/AGENTS.md). It runs detached -- nobody's
 * child, no terminal -- with its output in the session log when one is on.
 * Every message is the GM's alone. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app_priv.h"
#include "prof.h"

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
            fprintf(out, "  %s: %s\n", j->thread[t].who == 'G' ? "gm" : j->thread[t].who == 'A' ? "agent" : "-",
                    j->thread[t].text);
    }
    fprintf(out, "\nTalk to vtt with one request a call (or several lines on stdin):\n"
                 "  %s --ctl-pid %ld --ctl 'job %d take'            say the job is yours\n"
                 "  %s --ctl-pid %ld --ctl 'dump%s%s'               the map as text (also: describe, check, marked)\n"
                 "  %s --ctl-pid %ld --ctl 'job %d area %s'         where you will work, shown to the GM\n"
                 "  %s --ctl-pid %ld --ctl <<'EOF'\n"
                 "  job %d propose \"one line for the GM\"\n"
                 "  room Crypt B2:K12\n"
                 "  tile C3:J10 water\n"
                 "  token add enemy D4 \"Ghoul\"\n"
                 "  EOF\n"
                 "  %s --ctl-pid %ld --ctl 'job %d dump'            read your proposal back\n\n",
            vtt, pid, j->num, vtt, pid, where[0] ? " " : "", where, vtt, pid, j->num, where[0] ? where : "B2:K12",
            vtt, pid, j->num, vtt, pid, j->num);
    fputs("A request is all or nothing: if a line fails, nothing changed and the answer names the line and why.\n"
          "The lines that change a map: room, tile, wall, edge, door, corridor, area, token add|move|del|set, note,\n"
          "fog paint, stamp, link, floor, scene. docs/AGENTS.md in vtt's source has all of them.\n"
          "Propose once, read it back, then stop. The verdict is the GM's; feedback starts you again with this job.\n", out);
}

int app_agent_start(App *a, const Job *j)
{
    if (!a->agent_cmd[0]) return 0;
    PROF_ZONE("agent.spawn");
    char msg[AGENT_CMD_MAX + 96], err[CTL_PATH_MAX + 64];
    /* The command answers over the channel: on, for it. */
    if (!a->agent_on) {
        if (ctl_start(&a->ctl, err, sizeof err) < 0) {
            snprintf(msg, sizeof msg, "#%d: the agent was not started - %.150s", j->num, err);
            app_note_gm(a, msg);
            return -1;
        }
        a->agent_on = 1;
    }

    char  *prompt = NULL;
    size_t plen = 0;
    FILE  *pf = open_memstream(&prompt, &plen);
    if (!pf) return -1;
    app_agent_prompt(a, j, pf);
    fclose(pf);

    int in[2];
    if (pipe(in) < 0) { free(prompt); return -1; }
    char job[16], pidv[24];
    snprintf(job, sizeof job, "%d", j->num);
    snprintf(pidv, sizeof pidv, "%ld", (long)getpid());

    fflush(NULL);                       /* nothing of ours buffered goes out twice */
    pid_t pid = fork();
    if (pid < 0) { close(in[0]); close(in[1]); free(prompt); return -1; }
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
        setenv("VTT_JOB", job, 1);
        setenv("VTT_PID", pidv, 1);
        if (app_self_path) setenv("VTT", app_self_path, 1);
        execl("/bin/sh", "sh", "-c", a->agent_cmd, (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) { }
    /* The prompt is a few KB: it fits the pipe, so this never waits on the
     * agent reading it. */
    for (size_t off = 0; off < plen; ) {
        ssize_t w = write(in[1], prompt + off, plen - off);
        if (w > 0) off += (size_t)w;
        else if (w < 0 && errno == EINTR) continue;
        else break;
    }
    close(in[1]);
    free(prompt);
    snprintf(msg, sizeof msg, "#%d: started %.200s", j->num, a->agent_cmd);
    slog_write(&a->slog, msg);
    return 1;
}

void app_agent_command_cmd(App *a, const char *rest)
{
    char msg[AGENT_CMD_MAX + 96];
    while (*rest == ' ') rest++;
    if (!*rest) {
        if (a->agent_cmd[0]) snprintf(msg, sizeof msg, "each :ask starts: %s - :agent command off stops that", a->agent_cmd);
        else snprintf(msg, sizeof msg, ":agent command CMD starts CMD for each :ask (the job on its stdin); none is set");
        app_set_status_gm(a, msg);
        return;
    }
    if (!strcmp(rest, "off")) {                              /* KEYS.md rule 9 */
        if (!a->agent_cmd[0]) { app_set_status_gm(a, "no agent command is set"); return; }
        a->agent_cmd[0] = '\0';
        app_note_gm(a, "agent command off - :ask waits for an agent that comes for work");
        return;
    }
    if (strlen(rest) >= sizeof a->agent_cmd) {
        snprintf(msg, sizeof msg, "the command is over %d characters - put it in a script", AGENT_CMD_MAX - 1);
        app_set_status_gm(a, msg);
        return;
    }
    str_lcpy(a->agent_cmd, rest, sizeof a->agent_cmd);
    snprintf(msg, sizeof msg, "each :ask now starts: %s", a->agent_cmd);
    app_note_gm(a, msg);
}
