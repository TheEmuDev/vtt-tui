/* The app's side of the control channel (docs/CONTROL.md): the language and
 * its handlers are request.c's, on a World; here is what they ask of the GM's
 * screen -- the world's host (world.h) -- and the GM's own `:agent`. Every
 * message is the GM's alone. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_priv.h"
#include "floor.h"

/* ----------------------------------------------------------------- state */

const char *app_ctl_screen_name(Screen s)
{
    switch (s) {
    case SCREEN_MENU:    return "menu";
    case SCREEN_BROWSER: return "file browser";
    case SCREEN_EDITOR:  return "build";
    case SCREEN_PLAY:    return "play";
    case SCREEN_HELP:    return "help";
    }
    return "?";
}

const char *app_ctl_mode_name(EdMode m)
{
    switch (m) {
    case ED_NORMAL:  return "normal";
    case ED_WALL:    return "wall";
    case ED_VISUAL:  return "visual";
    case ED_COMMAND: return "command line";
    case ED_STAMP:   return "stamp";
    case ED_REVIEW:  return "review";
    }
    return "?";
}

const char *app_ctl_busy(const App *a)
{
    if (!a->world.map)                         return "no map is open";
    if (a->screen == SCREEN_PLAY)        return "the GM is in play mode - edits are build mode's";
    if (a->screen != SCREEN_EDITOR)      return "the GM is not in build mode";
    if (a->modal == MODAL_PROMPT)        return "the GM is answering a prompt";
    if (a->modal == MODAL_PICKER)        return "the GM is choosing from a list";
    if (a->modal != MODAL_NONE)          return "a question is open on the GM's screen";
    if (a->ed.mode == ED_COMMAND)        return "the GM is typing a : command";
    if (a->pending || a->ed.pending_g)   return "the GM is part way through a key";
    if (a->world.undo.open)                    return "the GM is laying wall";
    return NULL;
}

/* A refusal goes to the session log too, or the log cannot say why an agent
 * stopped. The same one again is not written: a caller that keeps asking
 * what was refused would otherwise write the log as fast as it can ask. */
static void host_refused(void *gm, const char *verdict)
{
    App *a = gm;
    if (!slog_on(&a->slog)) return;
    unsigned h = 2166136261u;
    for (const char *p = verdict; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    if (h == a->ctl_refused) return;
    a->ctl_refused = h;
    char msg[360];
    snprintf(msg, sizeof msg, "agent refused: %s", verdict);
    slog_write(&a->slog, msg);
}

/* The GM's side of the world (world.h): what a job or a request asks of the
 * screen, the editor and the agent command. */
static void host_note(void *gm, const char *text, int logged)
{
    if (logged) app_note_gm(gm, text);
    else        app_set_status_gm(gm, text);
}
static const char *host_busy(void *gm) { return app_ctl_busy(gm); }
static void host_landed(void *gm, int creatures)
{
    App *a = gm;
    /* What the channel's edits did to the live App when they landed
     * straight: the overlay and the selection name creatures by index. */
    if (creatures) app_creatures_renumbered(a);
    else a->last_acting = turn_acting(a->world.map);     /* an accept starts no turn */
    app_fog_sync(a);
    a->dirty = 1;
}
static int  host_reviewing(void *gm) { return ((App *)gm)->review; }
static void host_review_leave(void *gm) { app_review_leave(gm); }
static void host_job_gone(void *gm, int num) { app_agent_forget(gm, num); }
static int  host_deaf(void *gm) { App *a = gm; return ctl_active(&a->ctl) && !a->world.agent_on; }

static void host_redraw(void *gm) { ((App *)gm)->dirty = 1; }
static void host_changed(void *gm, int what, int i, int x, int y)
{
    App *a = gm;
    switch (what) {
    case WORLD_TOKEN_GONE:
        range_token_removed(&a->play.range, i, x, y);
        play_focus(&a->play, -1);
        a->play.visual = 0;
        return;
    case WORLD_ROLLED_BACK:
        /* The overlay followed the deletion; the creature is back. */
        range_clear(&a->play.range);
        return;
    case WORLD_RENUMBERED:
        app_creatures_renumbered(a);
        return;
    case WORLD_UNDONE:
        range_history_changed(&a->play.range, a->world.map);
        break;
    default:
        break;
    }
    app_fog_sync(a);
    a->dirty = 1;
}
static void host_ring(void *gm, int x0, int y0, int x1, int y1)
{
    App  *a = gm;
    Ping *r = &a->agent_ring;
    r->who = PING_GM;
    r->x0 = x0; r->y0 = y0; r->x1 = x1; r->y1 = y1;
    r->until_ms = a->world.now_ms + PING_SHOW_MS;
    if (!r->until_ms) r->until_ms = 1;
}
static void host_screen_lines(void *gm, FILE *out)
{
    App       *a = gm;
    const Map *m = a->world.map;
    if (a->screen == SCREEN_EDITOR) fprintf(out, "screen build, %s mode\n", app_ctl_mode_name(a->ed.mode));
    else                            fprintf(out, "screen %s\n", app_ctl_screen_name(a->screen));
    if (m && (a->screen == SCREEN_EDITOR || a->screen == SCREEN_PLAY)) {
        int order[MAP_AREAS_MAX];
        if (floor_order(m, order))
            fprintf(out, "floor %s\n", floor_name(m, app_floor_shown(a)));
    }
}
static void host_marked(void *gm, FILE *out, int json) { app_ctl_marked(gm, out, json); }
static int  host_floor_shown(void *gm) { return app_floor_shown(gm); }
static void host_heard(void *gm, const char *who) { app_agent_heard(gm, who); }

const WorldHost APP_WORLD_HOST = {
    host_note, host_busy, host_landed, host_reviewing, host_review_leave, host_job_gone, host_deaf,
    host_redraw, host_changed, host_ring, host_screen_lines, host_marked, host_floor_shown, host_refused, host_heard,
};

/* ------------------------------------------------------------- :agent */

void app_agent_command(App *a, const char *rest)
{
    Ctl *c = &a->ctl;
    char msg[CTL_PATH_MAX + 96];
    if (!*rest) {
        if (!a->world.agent_on) app_set_status_gm(a, "the agent channel is off - :agent on opens it; until then every request is refused");
        else {
            snprintf(msg, sizeof msg, "agent channel on at %s - %u request%s so far; changes %s", c->path,
                     c->requests, c->requests == 1 ? "" : "s", a->world.ctl_auto ? "land at once" : "wait for :review");
            app_set_status_gm(a, msg);
        }
        return;
    }
    if (!strncmp(rest, "accept", 6) && (!rest[6] || rest[6] == ' ')) {
        const char *how = rest[6] ? rest + 7 : "";
        if (!strcmp(how, "auto"))        a->world.ctl_auto = 1;
        else if (!strcmp(how, "review")) a->world.ctl_auto = 0;
        else { app_set_status_gm(a, ":agent accept auto lands an agent's changes at once; :agent accept review waits for you"); return; }
        app_note_gm(a, a->world.ctl_auto ? "an agent's changes land at once - u takes each back"
                                   : "an agent's changes wait for :review");
        return;
    }
    if (!strncmp(rest, "command", 7) && (!rest[7] || rest[7] == ' ')) {
        app_agent_command_cmd(a, rest + 7);
        return;
    }
    if (!strcmp(rest, "off")) {
        if (!a->world.agent_on) { app_set_status_gm(a, "the agent channel is already off"); return; }
        /* The socket stays: --apply must still find this map open, to be
         * refused rather than write the file. Every other request is refused. */
        a->world.agent_on = 0;
        a->world.agent_seen_ms = 0;                /* nobody to tell of the map: the checkpoint may stop */
        app_note_gm(a, "agent channel off - requests are refused until :agent on");
        return;
    }
    if (strcmp(rest, "on") != 0) { app_set_status_gm(a, ":agent on, :agent off, :agent accept auto|review, :agent command CMD, or :agent to ask"); return; }
    if (a->world.agent_on) {
        snprintf(msg, sizeof msg, "the agent channel is already on at %s", c->path);
        app_set_status_gm(a, msg);
        return;
    }
    char err[CTL_PATH_MAX + 64];
    if (ctl_start(c, err, sizeof err) < 0) { app_set_status_gm(a, err); return; }
    a->world.agent_on = 1;
    snprintf(msg, sizeof msg, "agent channel on - vtt --ctl talks to this map; its changes %s",
             a->world.ctl_auto ? "land at once, u takes each back" : "wait for :review");
    app_note_gm(a, msg);
}
