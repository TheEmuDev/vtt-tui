#include "app_priv.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "counter.h"
#include "floor.h"
#include "fog.h"
#include "prof.h"


const char *const APP_MENU_ITEMS[APP_MENU_COUNT] = { "Open Map", "New Map", "Quit" };

/* ------------------------------------------------------------ lifecycle */

void app_init(App *a, Term *t, Renderer *r)
{
    memset(a, 0, sizeof *a);
    a->term    = t;
    a->rnd     = r;
    a->th      = &THEME_DARK;
    a->screen  = SCREEN_MENU;
    a->running = 1;
    a->dirty   = 1;
    a->pending_token = -1;
    a->last_acting   = -1;
    undo_init(&a->undo);
    slog_init(&a->slog);
    net_init(&a->net);
    ctl_init(&a->ctl);

    /* The frame clear paints the theme background, so no screen-sized fill
     * is needed at the top of any draw. */
    rnd_set_clear(r, a->th->fg, a->th->bg);
}

void app_free(App *a)
{
    map_free(a->map);
    a->map = NULL;
    free(a->entries);
    a->entries = NULL;
    undo_free(&a->undo);
    slog_close(&a->slog);
    net_stop(&a->net);
    ctl_stop(&a->ctl);
    map_free(a->stamp);
    a->stamp = NULL;
    ui_picker_free(&a->picker);
}

void app_set_status(App *a, const char *msg)
{
    str_lcpy(a->status, msg, sizeof a->status);
    a->nstatus_span = 0;
    /* A message made while anything was hidden is the GM's, even when the
     * key that made it took the last hidden creature away: "removed
     * Zorkmid" would name it. */
    a->status_gm    = a->key_saw_hidden || (a->map && tokens_any_hidden(&a->map->tokens));
    a->dirty = 1;
}

void app_status_span(App *a, int at, int len, uint32_t fg)
{
    int max = (int)(sizeof a->status_span / sizeof *a->status_span);
    if (a->nstatus_span >= max || at < 0 || len <= 0) return;
    if (at + len > (int)strlen(a->status)) return;
    a->status_span[a->nstatus_span].at  = at;
    a->status_span[a->nstatus_span].len = len;
    a->status_span[a->nstatus_span].fg  = fg;
    a->nstatus_span++;
}

/* For the things that happened, as opposed to the things the app has to
 * say: the status line shows it and the session log, when on, keeps it. */
void app_note(App *a, const char *msg)
{
    app_set_status(a, msg);
    slog_write(&a->slog, msg);
}

void app_set_status_gm(App *a, const char *msg)
{
    app_set_status(a, msg);
    a->status_gm = 1;
}

void app_note_gm(App *a, const char *msg)
{
    app_note(a, msg);
    a->status_gm = 1;
}

/* Adds a clause to the status line instead of replacing it. Closing a map
 * can be two pieces of news at once -- "wrote crypt.vtt", and the remote
 * view going down with it -- and the second must not quietly eat the
 * first. Spans are byte offsets into what is already there, so they
 * survive. */
static void app_note_more(App *a, const char *msg)
{
    size_t n = strlen(a->status);
    if (n && n + 4 < sizeof a->status)
        snprintf(a->status + n, sizeof a->status - n, " - %s", msg);
    else
        str_lcpy(a->status, msg, sizeof a->status);
    slog_write(&a->slog, msg);
    a->dirty = 1;
}

void app_show_message(App *a, const char *title, const char *body)
{
    a->modal = MODAL_MESSAGE;
    str_lcpy(a->modal_title, title, sizeof a->modal_title);
    str_lcpy(a->modal_body, body, sizeof a->modal_body);
    a->dirty = 1;
}

/* --------------------------------------------------------------- maps */

static void offer_recovery(App *a);

int app_open_map(App *a, const char *path)
{
    char err[MAPIO_ERR_MAX] = { 0 };
    Map *m = mapio_load(path, err, sizeof err);
    if (!m) {
        app_show_message(a, "Cannot open map", err);
        return -1;
    }

    slog_close(&a->slog);
    map_free(a->map);
    a->map = m;
    undo_clear(&a->undo);          /* history does not survive a new map */
    play_init(&a->play);
    ed_init(&a->ed, m);
    app_floor_reset(a);
    ed_layout(&a->ed, m, a->rnd->w, a->rnd->h);
    grid_center_on(&a->ed.view, m, a->ed.cx, a->ed.cy);

    a->screen = SCREEN_EDITOR;
    a->autosave_gen = a->seen_gen = m->gen;
    a->npings = 0;                 /* squares of the old map mean nothing here */
    a->npinged = 0;
    a->agent_ring.until_ms = 0;
    fog_recompute(m);

    char msg[192];
    snprintf(msg, sizeof msg, "opened %s (%dx%d, %d token%s)",
             m->name, m->w, m->h, m->tokens.n, m->tokens.n == 1 ? "" : "s");
    app_set_status(a, msg);
    offer_recovery(a);
    return 0;
}

static void drop_autosave(const App *a);

/* A trip's half of opening a map (docs/MAPLINKS.md): the map in hand, already
 * saved, is put down and `m` taken up in play mode. Unlike :e the server,
 * the session log and the phones carry on -- the table walked through a
 * door, it did not close the book. The play settings carry on too; what
 * points into the old map (the selection, the range, the ruler, pings) goes.
 * A handout is the encounter's and comes down. */
void app_travel_to(App *a, Map *m)
{
    if (a->handout_up) net_set_handout(&a->net, "", 0, a->now_ms);
    a->handout_up = 0;
    a->handout_title[0] = a->handout_body[0] = '\0';
    drop_autosave(a);
    map_free(a->map);
    a->map = m;
    undo_clear(&a->undo);
    play_focus(&a->play, -1);
    a->play.visual = 0;
    a->play.clock  = -1;
    range_clear(&a->play.range);
    a->ruler.active = 0;
    ed_init(&a->ed, m);
    app_floor_reset(a);
    ed_layout(&a->ed, m, a->rnd->w, a->rnd->h);
    a->screen = SCREEN_PLAY;
    a->autosave_gen = a->seen_gen = m->gen;
    a->npings = a->npinged = 0;
    a->agent_ring.until_ms = 0;
    a->dirty = 1;
}

/* Recovery is asked in the way a shell asks about a core file: the map is
 * open as it was saved, and this offers the newer copy over it. Saying no
 * throws the copy away, so the question is asked once. */
static void offer_recovery(App *a)
{
    char autosave[MAP_PATH_MAX + 16];
    mapio_autosave_path(a->map, autosave, sizeof autosave);
    long when = 0;
    if (!mapio_autosave_newer(a->map->path, autosave, &when)) return;

    char stamp[32] = "";
    struct tm tmv;
    time_t t = (time_t)when;
    if (localtime_r(&t, &tmv)) strftime(stamp, sizeof stamp, "%H:%M on %d %b", &tmv);

    str_lcpy(a->pending_file, autosave, sizeof a->pending_file);
    a->modal = MODAL_CONFIRM_RECOVER;
    str_lcpy(a->modal_title, "Unsaved work found", sizeof a->modal_title);
    snprintf(a->modal_body, sizeof a->modal_body,
             "%.40s was still being edited at %s when it was last open, and those changes were never saved. Recover them?",
             a->map->name, stamp);
}

/* Whether the copy is whole: an autosave ends with an `end` line, and a
 * power cut before it reached the disk can leave any start of it -- one that
 * falls on a line's end reads as a smaller map and nothing else would say. */
static int autosave_whole(const char *path)
{
    char tail[6] = { 0 };
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = fseek(f, -5, SEEK_END) == 0 && fread(tail, 1, 5, f) == 5 && !strcmp(tail, "\nend\n");
    fclose(f);
    return ok;
}

static void recover_autosave(App *a)
{
    char err[MAPIO_ERR_MAX] = { 0 };
    if (!autosave_whole(a->pending_file)) {
        /* Set aside rather than deleted, and so never offered again. */
        char aside[sizeof a->pending_file + 16];
        snprintf(aside, sizeof aside, "%s.damaged", a->pending_file);
        int kept = rename(a->pending_file, aside) == 0;
        const char *shown = kept ? aside : a->pending_file;
        const char *base = strrchr(shown, '/');
        char body[192];
        snprintf(body, sizeof body, "Kept as %.80s. The map is open as last saved.%s",
                 base ? base + 1 : shown, kept ? "" : " n at the next open discards it.");
        app_show_message(a, "The autosave is incomplete", body);
        return;
    }
    Map *m = mapio_load(a->pending_file, err, sizeof err);
    if (!m) { app_show_message(a, "Cannot read the autosave", err); return; }

    /* It stands in for the map, under the map's own path, and counts as
     * unsaved: the file on disk is still the older one until :w. */
    str_lcpy(m->path, a->map->path, sizeof m->path);
    map_free(a->map);
    a->map = m;
    undo_clear(&a->undo);
    play_init(&a->play);
    ed_init(&a->ed, m);
    app_floor_reset(a);
    ed_layout(&a->ed, m, a->rnd->w, a->rnd->h);
    grid_center_on(&a->ed.view, m, a->ed.cx, a->ed.cy);
    m->modified = 1;
    a->autosave_gen = a->seen_gen = m->gen;
    fog_recompute(m);
    app_set_status(a, "recovered - :w keeps it, :q! lets it go");
}

/* The autosave lives while the work is unsaved and goes with the first
 * save or the decision to discard; only a crash leaves it behind. */
static void drop_autosave(const App *a)
{
    if (!a->map) return;
    char autosave[MAP_PATH_MAX + 16];
    mapio_autosave_path(a->map, autosave, sizeof autosave);
    unlink(autosave);
}

/* The slot for `who` in a set of PING_MAX: its own, else a free one, else
 * the one with the earliest until_ms. */
static Ping *ping_slot(Ping *v, int *n, uint32_t who)
{
    int k = 0;
    while (k < *n && v[k].who != who) k++;
    if (k == *n) {
        if (*n < PING_MAX) (*n)++;
        else
            for (int i = k = 0; i < *n; i++)
                if (v[i].until_ms < v[k].until_ms) k = i;
    }
    return &v[k];
}

void app_ping(App *a, uint32_t who, int x0, int y0, int x1, int y1)
{
    if (!a->map) return;
    Map *m = a->map;
    x0 = iclamp(x0, 0, m->w - 1); x1 = iclamp(x1, x0, m->w - 1);
    y0 = iclamp(y0, 0, m->h - 1); y1 = iclamp(y1, y0, m->h - 1);

    /* The same source's ring moves; a new source takes a free slot, or the
     * one closest to going -- only reachable after phones reconnect. */
    Ping *p = ping_slot(a->pings, &a->npings, who);
    p->who = who;
    p->x0 = x0; p->y0 = y0; p->x1 = x1; p->y1 = y1;
    p->until_ms = a->now_ms + PING_SHOW_MS;

    /* And the record `marked` reads, kept after the ring comes down. */
    Ping *rec = ping_slot(a->pinged, &a->npinged, who);
    *rec = *p;
    rec->until_ms = a->now_ms;

    /* On the status line, not in the log: a gesture, not something that
     * happened to the encounter. Over fog the players' frame shows no
     * message at all, so this reaches the table only where fog is off. */
    char at[MAP_COORD_MAX], to[MAP_COORD_MAX], msg[64];
    map_coord_name(x0, y0, at, sizeof at);
    if (x1 == x0 && y1 == y0) snprintf(msg, sizeof msg, "ping at %s", at);
    else { map_coord_name(x1, y1, to, sizeof to); snprintf(msg, sizeof msg, "ping at %s-%s", at, to); }
    app_set_status(a, msg);
    a->dirty = 1;
}

int app_ping_cell(App *a, uint32_t who, int sx, int sy)
{
    /* The phones are shown play mode, laid out by the same view the GM's
     * frame is: out of play, or on the gutter, the bars or the panel, a tap
     * names nothing. A question box on the GM's screen is the GM's alone;
     * the table's board under it still takes a tap. */
    if (!a->map || a->screen != SCREEN_PLAY) return 0;
    /* Through the camera their frame was drawn with: their own, when they
     * are on a floor the GM is not showing. */
    const GridView *g = app_players_split(a) ? &a->pview : &a->ed.view;
    if (!rect_contains(g->view, sx, sy)) return 0;
    int tx, ty;
    if (!grid_screen_to_tile(g, a->map, sx, sy, &tx, &ty)) return 0;
    app_ping(a, who, tx, ty, tx, ty);
    int f = floor_at(a->map, tx, ty);
    if (who != PING_GM && f >= 0 && f != app_floor_shown(a)) {
        char at[MAP_COORD_MAX], msg[96];
        map_coord_name(tx, ty, at, sizeof at);
        snprintf(msg, sizeof msg, "ping on %s at %s", a->map->areas[f].name, at);
        app_set_status_gm(a, msg);
    }
    return 1;
}

int app_ping_due(const App *a, uint64_t now_ms)
{
    uint64_t at = a->agent_ring.until_ms;
    for (int i = 0; i < a->npings; i++)
        if (!at || a->pings[i].until_ms < at) at = a->pings[i].until_ms;
    if (!at) return -1;
    return now_ms >= at ? 0 : (int)(at - now_ms);
}

void app_tick(App *a, uint64_t now_ms)
{
    a->now_ms = now_ms;
    if (net_active(&a->net)) {
        /* A phone that just joined was sent the last players' frame; draw
         * a current one. */
        if (a->net.joined) { a->net.joined = 0; a->dirty = 1; }
        NetPing in[NET_MAX_CLIENTS];
        int n = net_take_pings(&a->net, in, NET_MAX_CLIENTS);
        for (int i = 0; i < n; i++) app_ping_cell(a, in[i].who, in[i].sx, in[i].sy);
        app_whisper_tick(a);
    }
    if (ctl_active(&a->ctl)) {
        const char *req;
        size_t      len;
        int         i;
        while ((i = ctl_next(&a->ctl, &req, &len)) >= 0) {
            char *ans = app_ctl_exec(a, req, &len);
            ctl_answer(&a->ctl, i, ans, ans ? len : 0);     /* none: out of memory, just close */
        }
    }
    for (int i = 0; i < a->npings; ) {
        if (a->pings[i].until_ms <= now_ms) { a->pings[i] = a->pings[--a->npings]; a->dirty = 1; }
        else i++;
    }
    if (a->agent_ring.until_ms && a->agent_ring.until_ms <= now_ms) {
        a->agent_ring.until_ms = 0;
        a->dirty = 1;
    }
    if (!a->map) return;
    if (a->map->gen != a->seen_gen) {
        a->seen_gen  = a->map->gen;
        a->change_ms = now_ms;
    }
    if (app_autosave_due(a, now_ms) == 0) app_autosave(a);
}

int app_autosave_due(const App *a, uint64_t now_ms)
{
    if (!a->autosave_on || !a->map || !a->map->modified) return -1;
    if (a->map->gen == a->autosave_gen) return -1;
    uint64_t at = a->change_ms + AUTOSAVE_QUIET_MS;
    return now_ms >= at ? 0 : (int)(at - now_ms);
}

int app_autosave(App *a)
{
    if (!a->map) return -1;
    PROF_ZONE("autosave");
    char autosave[MAP_PATH_MAX + 16], err[MAPIO_ERR_MAX];
    mapio_autosave_path(a->map, autosave, sizeof autosave);
    /* The directory may not exist yet for a map that was never saved; one
     * failure is not worth a message, the next save will say. Either way
     * this generation counts as attempted: a failure that stayed "owed"
     * would be retried on every turn of the loop, with poll told not to
     * wait, which is a spinning process for as long as the map is unsaved. */
    int rc = mapio_write_unflushed(a->map, autosave, err, sizeof err);
    a->autosave_gen = a->map->gen;
    return rc == 0 ? 0 : -1;
}

/* A fresh map is a floored rectangle with a wall around it: the common case
 * is a room, and starting from an empty void gives the user nothing to see
 * or move around in. */
static void app_new_map(App *a, const char *name, int w, int h)
{
    Map *m = map_new(w, h, name);
    map_fill_tiles(m, 0, 0, w - 1, h - 1, TILE_FLOOR);
    map_rect_walls(m, 0, 0, w - 1, h - 1, EDGE_WALL);
    map_touch(m);

    char path[MAP_PATH_MAX];
    mapio_resolve_path(name, path, sizeof path);
    str_lcpy(m->path, path, sizeof m->path);

    slog_close(&a->slog);
    map_free(a->map);
    a->map = m;
    undo_clear(&a->undo);
    play_init(&a->play);
    ed_init(&a->ed, m);
    app_floor_reset(a);
    ed_layout(&a->ed, m, a->rnd->w, a->rnd->h);
    grid_center_on(&a->ed.view, m, a->ed.cx, a->ed.cy);

    a->screen = SCREEN_EDITOR;

    char msg[192];
    /* Bounded conversions: a long path should shorten the message, not
     * silently overrun the intent of it. */
    snprintf(msg, sizeof msg, "new map %.40s (%dx%d) - :w saves to %.100s",
             name, w, h, path);
    app_set_status(a, msg);
    a->autosave_gen = a->seen_gen = m->gen;
    offer_recovery(a);
}

int app_save_map(App *a, const char *path)
{
    if (!a->map) return -1;

    /* Create the map directory on demand rather than making the user do it. */
    char dir[MAP_PATH_MAX];
    str_lcpy(dir, path, sizeof dir);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        dir_make(dir);
    }

    char err[MAPIO_ERR_MAX] = { 0 };
    /* The autosave is named from the path the map had; find it before the
     * save moves the map to a new one. */
    char autosave[MAP_PATH_MAX + 16];
    mapio_autosave_path(a->map, autosave, sizeof autosave);
    if (mapio_save(a->map, path, err, sizeof err) != 0) {
        app_show_message(a, "Cannot save map", err);
        return -1;
    }
    unlink(autosave);
    a->autosave_gen = a->map->gen;

    char msg[192];
    snprintf(msg, sizeof msg, "wrote %.170s", path);
    app_set_status(a, msg);
    return 0;
}

/* -------------------------------------------------------------- prompts */

void app_open_prompt(App *a, PromptWhat what, const char *title,
                        const char *hint, const char *initial)
{
    a->prompt_what = what;
    a->modal       = MODAL_PROMPT;
    ui_prompt_open(&a->prompt, title, hint, initial);
    a->dirty = 1;
}

/* The prompt is the reader as well as the writer: it opens holding what is
 * there, enter keeps or changes it, ctrl-u then enter takes it away. */
void app_note_prompt(App *a, int idx, int x, int y)
{
    char title[64];
    const char *had;
    if (idx >= 0 && idx < a->map->tokens.n) {
        const Token *t = &a->map->tokens.v[idx];
        a->pending_token = idx;
        snprintf(title, sizeof title, "note on %.20s", token_name(t));
        had = t->note;
    } else {
        a->pending_token = -1;
        a->pending_tx    = x;
        a->pending_ty    = y;
        char at[MAP_COORD_MAX];
        map_coord_name(x, y, at, sizeof at);
        snprintf(title, sizeof title, "note on %s", at);
        had = map_note_at(a->map, x, y);
    }
    app_open_prompt(a, PROMPT_NOTE, title, "enter keeps it, ctrl-u then enter takes it away", had ? had : "");
    a->prompt.max = TOKEN_NOTE_MAX;   /* the field is the limit, so nothing typed is lost */
}

/* The cursor goes to whatever is now selected, and the view goes with it.
 * A selection scrolled off screen is no use for finding a creature, which is
 * the whole point of cycling and searching. */
void app_follow_selection(App *a)
{
    Play *pl = &a->play;
    if (pl->sel < 0 || pl->sel >= a->map->tokens.n) return;

    a->ed.cx = a->map->tokens.v[pl->sel].x;
    a->ed.cy = a->map->tokens.v[pl->sel].y;
    grid_ensure_visible(&a->ed.view, a->map, a->ed.cx, a->ed.cy, ED_SCROLLOFF);
}

/* Says which creature the selection landed on, since cycling and searching
 * move it somewhere the eye has not followed yet. */
void app_report_selection(App *a)
{
    Play *pl = &a->play;
    if (pl->sel < 0 || pl->sel >= a->map->tokens.n) return;

    const Token *t = &a->map->tokens.v[pl->sel];
    char msg[128];
    char at[MAP_COORD_MAX];
    map_coord_name(t->x, t->y, at, sizeof at);
    snprintf(msg, sizeof msg, "%.30s (%s) at %s",
             t->label[0] ? t->label : "unlabeled",
             token_kind_name(t->kind), at);
    app_set_status(a, msg);
}

static void prompt_accept(App *a)
{
    const char *text = a->prompt.buf;
    PromptWhat  what = a->prompt_what;

    a->modal       = MODAL_NONE;
    a->prompt_what = PROMPT_NONE;

    switch (what) {
    case PROMPT_NEW_NAME: {
        if (!text[0]) { app_set_status(a, "canceled: a map needs a name"); return; }
        str_lcpy(a->pending_name, text, sizeof a->pending_name);
        app_open_prompt(a, PROMPT_NEW_SIZE, "Map size", "width x height, in tiles", "40x25");
        return;
    }
    case PROMPT_NEW_SIZE: {
        int w = 0, h = 0;
        if (sscanf(text, "%dx%d", &w, &h) != 2 && sscanf(text, "%d %d", &w, &h) != 2) {
            app_show_message(a, "Bad size", "expected something like 40x25");
            return;
        }
        if (w < MAP_MIN_DIM || h < MAP_MIN_DIM || w > MAP_MAX_DIM || h > MAP_MAX_DIM) {
            char body[128];
            snprintf(body, sizeof body, "size must be between %dx%d and %dx%d",
                     MAP_MIN_DIM, MAP_MIN_DIM, MAP_MAX_DIM, MAP_MAX_DIM);
            app_show_message(a, "Bad size", body);
            return;
        }
        app_new_map(a, a->pending_name, w, h);
        return;
    }
    case PROMPT_TOKEN_LABEL: {
        Token t;
        memset(&t, 0, sizeof t);
        t.x    = (int16_t)a->pending_tx;
        t.y    = (int16_t)a->pending_ty;
        t.kind = a->pending_kind;
        t.size = a->pending_size;
        str_lcpy(t.label, text, sizeof t.label);

        undo_begin(&a->undo);
        int placed = undo_add_token(&a->undo, a->map, t);
        undo_end(&a->undo);
        play_focus(&a->play, placed);

        char msg[160];
        char at[MAP_COORD_MAX];
        map_coord_name(t.x, t.y, at, sizeof at);
        snprintf(msg, sizeof msg, "placed %s %.30s (%dx%d) at %s",
                 token_kind_name(t.kind), t.label[0] ? t.label : "unlabeled",
                 t.size, t.size, at);
        app_note(a, msg);
        return;
    }
    case PROMPT_RELABEL: {
        Play *pl = &a->play;
        if (pl->sel < 0 || pl->sel >= a->map->tokens.n) return;

        /* Through the undo log, so a mistyped name is one u away. */
        Token t = a->map->tokens.v[pl->sel];
        char was[TOKEN_LABEL_MAX];
        str_lcpy(was, t.label, sizeof was);
        str_lcpy(t.label, text, sizeof t.label);
        undo_begin(&a->undo);
        undo_edit_token(&a->undo, a->map, pl->sel, t);
        undo_end(&a->undo);
        char msg[96];
        snprintf(msg, sizeof msg, "relabeled %.30s to %.30s",
                 was[0] ? was : token_kind_name(t.kind), t.label);
        app_note(a, msg);
        return;
    }
    case PROMPT_INITIATIVE: {
        int idx = a->pending_token;
        a->pending_token = -1;
        if (idx < 0 || idx >= a->map->tokens.n) return;

        const Token *t   = &a->map->tokens.v[idx];
        const char  *who = token_name(t);
        char msg[96];

        /* A number joins the order, or moves within it; a blank leaves. */
        const char *p = text;
        while (*p == ' ') p++;
        if (!*p) {
            if (!(t->turn & TURN_IN)) { app_set_status(a, "canceled: it was not in the turn order"); return; }
            snprintf(msg, sizeof msg, "%.30s leaves the turn order", who);
            turn_leave(a->map, &a->undo, idx);
            app_note(a, msg);
            return;
        }
        char *end;
        long  v = strtol(p, &end, 10);
        while (*end == ' ') end++;
        if (end == p || *end || v < -999 || v > 999) {
            app_set_status(a, "initiative is a number, -999 to 999");
            return;
        }
        snprintf(msg, sizeof msg, "%.30s %s the turn order at %ld", who,
                 (t->turn & TURN_IN) ? "moves in" : "joins", v);
        turn_join(a->map, &a->undo, idx, (int)v);
        app_note(a, msg);
        return;
    }
    case PROMPT_STATUS_LABEL: {
        int idx = a->pending_token;
        a->pending_token = -1;
        if (idx < 0 || idx >= a->map->tokens.n) return;
        if (!text[0]) { app_set_status(a, "canceled: a marker needs a word"); return; }

        Token t = a->map->tokens.v[idx];
        if (!token_add_status(&t, a->play.status_color, text)) {
            char msg[64];
            snprintf(msg, sizeof msg, "a token holds at most %d markers", TOKEN_STATUS_MAX);
            app_set_status(a, msg);
            return;
        }

        undo_begin(&a->undo);
        undo_edit_token(&a->undo, a->map, idx, t);
        undo_end(&a->undo);

        char msg[96];
        snprintf(msg, sizeof msg, "%s marker on %.24s: %.30s",
                 status_color_name(a->play.status_color),
                 token_name(&t), text);
        app_note(a, msg);
        return;
    }
    case PROMPT_COUNTERS: {
        int idx = a->pending_token;
        a->pending_token = -1;
        if (idx < 0 || idx >= a->map->tokens.n) return;

        const Ruleset *rs = ruleset_by_name(a->map->ruleset);
        Token t = a->map->tokens.v[idx];
        char  cur[COUNTER_NAME_MAX], msg[160], out[200];
        app_current_counter(a, cur, sizeof cur);
        /* Every message here is about a creature's numbers, the ones that
         * say nothing changed and the complaints that echo what was typed
         * included, so none of them may reach the players' frame. */
        if (counter_apply(&t, text, rs ? rs->counters : NULL, cur, sizeof cur, msg, sizeof msg) != 0) {
            app_set_status_gm(a, msg);
            return;
        }
        str_lcpy(a->play.counter, cur, sizeof a->play.counter);
        const Token *was = &a->map->tokens.v[idx];
        if (token_equal(was, &t)) { app_set_status_gm(a, msg); return; }
        undo_begin(&a->undo);
        undo_edit_token(&a->undo, a->map, idx, t);
        undo_end(&a->undo);
        snprintf(out, sizeof out, "%.24s: %s", token_name(&t), msg);
        app_note_gm(a, out);
        return;
    }
    case PROMPT_NOTE: {
        /* The text itself stays off the status line and out of the log: the
         * line is in the frame the players see, and the log is for what
         * happened, which is that a note was made. */
        int idx = a->pending_token;
        a->pending_token = -1;
        while (*text == ' ') text++;
        char msg[96];
        if (idx >= 0) {
            if (idx >= a->map->tokens.n) return;
            Token t = a->map->tokens.v[idx];
            const char *who = token_name(&t);
            int had = t.note[0] != '\0';
            if (!*text && !had) { app_set_status(a, "nothing noted"); return; }
            str_lcpy(t.note, text, sizeof t.note);
            undo_begin(&a->undo);
            undo_edit_token(&a->undo, a->map, idx, t);
            undo_end(&a->undo);
            snprintf(msg, sizeof msg, *text ? "noted on %.30s" : "note taken off %.30s", who);
            app_note(a, msg);
            return;
        }
        char at[MAP_COORD_MAX];
        map_coord_name(a->pending_tx, a->pending_ty, at, sizeof at);
        int had = map_note_at(a->map, a->pending_tx, a->pending_ty) != NULL;
        if (!*text && !had) { app_set_status(a, "nothing noted"); return; }
        /* Through the log, like a creature's note: u takes it back. */
        undo_begin(&a->undo);
        int noted = undo_set_note(&a->undo, a->map, a->pending_tx, a->pending_ty, text);
        undo_end(&a->undo);
        if (!noted) {
            snprintf(msg, sizeof msg, "no room: a map holds %d notes on squares", MAP_NOTES_MAX);
            app_set_status(a, msg);
            return;
        }
        snprintf(msg, sizeof msg, *text ? "noted on %s" : "note taken off %s", at);
        app_note(a, msg);
        return;
    }
    case PROMPT_TOKEN_SEARCH: {
        Play *pl = &a->play;
        /* An empty line repeats the last search, the way : and / do in vim. */
        if (!play_find(pl, a->map, text, 1)) {
            char msg[96];
            if (!pl->search[0]) app_set_status(a, "nothing to search for");
            else {
                snprintf(msg, sizeof msg, "no token matching \"%.30s\"", pl->search);
                app_set_status(a, msg);
            }
            return;
        }
        app_follow_selection(a);
        app_report_selection(a);
        return;
    }
    case PROMPT_RENAME_MAP: {
        char from[MAP_PATH_MAX];
        str_lcpy(from, a->pending_file, sizeof from);
        a->pending_file[0] = '\0';
        app_rename_map(a, from, text);
        return;
    }
    case PROMPT_DUPLICATE_MAP: {
        char from[MAP_PATH_MAX];
        str_lcpy(from, a->pending_file, sizeof from);
        a->pending_file[0] = '\0';
        app_duplicate_map(a, from, text);
        return;
    }
    case PROMPT_SAVE_AS: {
        if (!text[0]) return;
        char path[MAP_PATH_MAX];
        mapio_resolve_path(text, path, sizeof path);
        app_save_map(a, path);
        return;
    }
    case PROMPT_NONE:
    default:
        return;
    }
}

/* The token a pending marker-clearing question is about, or NULL if it has
 * gone away underneath the modal. Both the drawing and the answer go through
 * here so neither can act on a token the other did not see. */
const Token *app_clear_status_target(const App *a)
{
    if (!a->map) return NULL;
    if (a->pending_token < 0 || a->pending_token >= a->map->tokens.n) return NULL;

    const Token *t = &a->map->tokens.v[a->pending_token];
    return t->nstatus ? t : NULL;
}

/* Takes one marker off a token, or all of them when `which` is -1. Goes
 * through the undo log, so a marker cleared in error is one u away. */
void app_clear_token_status(App *a, int idx, int which)
{
    Map *m = a->map;
    if (idx < 0 || idx >= m->tokens.n) return;

    Token t = m->tokens.v[idx];
    if (which >= t.nstatus) return;

    char msg[96];
    if (which < 0) {
        int had = t.nstatus;
        if (!had) { app_set_status(a, "no markers to clear"); return; }
        token_clear_status(&t);
        snprintf(msg, sizeof msg, "cleared %d marker%s", had, had == 1 ? "" : "s");
    } else {
        /* Name the one that went: the map only ever showed its initial. */
        snprintf(msg, sizeof msg, "cleared %s %.30s",
                 status_color_name(t.status[which].color), t.status[which].label);
        token_remove_status(&t, which);
    }

    undo_begin(&a->undo);
    undo_edit_token(&a->undo, m, idx, t);
    undo_end(&a->undo);
    app_note(a, msg);
}

/* ---------------------------------------------------------------- mirror */

/* Terminals differ in how a command is handed to them. The user's $TERMINAL
 * is tried first, then the usual suspects. */
typedef struct { const char *name; const char *style; } TermKind;
static const TermKind TERMINALS[] = {
    { "alacritty", "-e" }, { "kitty", "" },   { "foot", "" },       { "ghostty", "-e" },
    { "wezterm", "start" }, { "gnome-terminal", "--" }, { "konsole", "-e" },
    { "xfce4-terminal", "-x" }, { "xterm", "-e" }, { "urxvt", "-e" }, { "st", "-e" },
};

static const char *term_style(const char *name)
{
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    for (size_t i = 0; i < sizeof TERMINALS / sizeof *TERMINALS; i++)
        if (!strcmp(TERMINALS[i].name, base)) return TERMINALS[i].style;
    return "-e";
}

const char *app_self_path = "vtt";

int app_spawn_mirror(App *a, char *msg, size_t msgsz)
{
    char target[64];
    snprintf(target, sizeof target, "127.0.0.1:%u", (unsigned)a->net.port);

    /* The candidates, $TERMINAL first. */
    const char *names[16];
    int         nn = 0;
    const char *env = getenv("TERMINAL");
    if (env && env[0]) names[nn++] = env;
    for (size_t i = 0; i < sizeof TERMINALS / sizeof *TERMINALS && nn < 16; i++)
        names[nn++] = TERMINALS[i].name;

    /* A pipe the grandchild writes a byte to only if every exec failed;
     * exec closes it (CLOEXEC), so silence within a moment means success. */
    int pfd[2];
    if (pipe(pfd) < 0) { snprintf(msg, msgsz, "cannot open a window: %s", strerror(errno)); return -1; }
    fcntl(pfd[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) { snprintf(msg, msgsz, "cannot open a window: %s", strerror(errno)); close(pfd[0]); close(pfd[1]); return -1; }
    if (pid == 0) {
        /* Twice, so the window is nobody's child and our terminal is not
         * its controlling one. */
        setsid();
        pid_t g = fork();
        if (g != 0) _exit(0);
        close(pfd[0]);
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); }
        for (int i = 0; i < nn; i++) {
            const char *style = term_style(names[i]);
            const char *argv[8];
            int k = 0;
            argv[k++] = names[i];
            if (style[0]) argv[k++] = style;
            if (!strcmp(style, "start")) argv[k++] = "--";
            argv[k++] = app_self_path;
            argv[k++] = "--watch";
            argv[k++] = target;
            argv[k]   = NULL;
            execvp(names[i], (char *const *)argv);
        }
        char no = 1;
        (void)!write(pfd[1], &no, 1);
        _exit(127);
    }
    close(pfd[1]);
    waitpid(pid, NULL, 0);

    struct pollfd p = { pfd[0], POLLIN, 0 };
    int failed = 0;
    if (poll(&p, 1, 300) > 0) {
        char no;
        failed = read(pfd[0], &no, 1) == 1;
    }
    close(pfd[0]);
    if (failed) {
        snprintf(msg, msgsz, "no terminal found - set $TERMINAL, or run: vtt --watch %s", target);
        return -1;
    }
    snprintf(msg, msgsz, "mirror opened - vtt --watch %s (q closes it)", target);
    return 0;
}

/* ------------------------------------------------------------- key page */

/* Which map describes the keys that work right now. Measuring and carrying
 * are flags rather than screens, but they change enough of the keyboard to be
 * worth their own page. */
KeyMapId app_keymap_id(const App *a)
{
    if (a->ruler.active && a->ed.mode != ED_COMMAND) return KEYS_RULER;

    switch (a->screen) {
    case SCREEN_MENU:    return KEYS_MENU;
    case SCREEN_BROWSER: return KEYS_BROWSER;
    case SCREEN_PLAY:
        if (a->play.visual)  return KEYS_PLAY_VISUAL;
        return a->play.grabbed ? KEYS_PLAY_GRABBED : KEYS_PLAY;
    case SCREEN_EDITOR:
        if (a->ed.mode == ED_WALL)   return KEYS_WALL;
        if (a->ed.mode == ED_VISUAL) return KEYS_VISUAL;
        if (a->ed.mode == ED_STAMP)  return KEYS_STAMP;
        return KEYS_BUILD;
    default:             return KEYS_PLAY;
    }
}

static void help_open(App *a)
{
    a->help_from = a->screen;
    a->help_id   = app_keymap_id(a);
    a->help_top  = 0;
    a->screen    = SCREEN_HELP;
}

/* The map you came from leads; the rest follow in their own order, so the
 * page answers "what can I press" first and "what else is there" after. */
int app_help_order(const App *a, const KeyMap *out[KEYS_COUNT])
{
    int n = 0;
    out[n++] = keys_map(a->help_id);
    for (int i = 0; i < KEYS_COUNT; i++)
        if (i != (int)a->help_id) out[n++] = keys_map((KeyMapId)i);
    return n;
}

/* Scrolling only asks; the draw decides. The page knows its own length and
 * clamps as it lays out, so G is "further than there is" rather than a number
 * this side has to work out -- and it is right on the first keypress, before
 * any frame has been drawn. */
#define HELP_END (1 << 24)

static void help_key(App *a, Key k)
{
    int page = imax(1, a->rnd->h - 6);

    if (k.kind == KEY_CHAR && (k.mods & MOD_CTRL)) {
        if (k.ch == 'd') a->help_top += page;
        if (k.ch == 'u') a->help_top -= page;
    } else if (k.kind == KEY_DOWN)  a->help_top += 1;
    else if (k.kind == KEY_UP)      a->help_top -= 1;
    else if (k.kind == KEY_ESC)     a->screen = a->help_from;
    else if (k.kind == KEY_CHAR && k.mods == 0) {
        switch (k.ch) {
        case 'j': a->help_top += 1; break;
        case 'k': a->help_top -= 1; break;
        case 'g': a->help_top  = 0; break;
        case 'G': a->help_top  = HELP_END; break;
        case 'q': case '?': a->screen = a->help_from; break;
        default: break;
        }
    }

    if (a->help_top < 0) a->help_top = 0;
}

/* ---------------------------------------------------------------- input */

static int modal_key(App *a, Key k)
{
    switch (a->modal) {
    case MODAL_NONE:
        return 0;

    case MODAL_CARD: {
        /* A card read whole: j k and the arrows a line, ctrl-d ctrl-u half
         * a screen, g G the ends; esc, q or enter put it away. The draw
         * clamps the top to what the card has. */
        int half = imax(1, a->rnd->h / 2);
        if (k.kind == KEY_ESC || k.kind == KEY_ENTER || (k.kind == KEY_CHAR && k.ch == 'q' && !k.mods)) {
            a->modal = MODAL_NONE;
        } else if (k.kind == KEY_DOWN || (k.kind == KEY_CHAR && k.ch == 'j' && !k.mods)) a->card_top++;
        else if (k.kind == KEY_UP || (k.kind == KEY_CHAR && k.ch == 'k' && !k.mods))     a->card_top--;
        else if (k.kind == KEY_CHAR && (k.mods & MOD_CTRL) && k.ch == 'd')                a->card_top += half;
        else if (k.kind == KEY_CHAR && (k.mods & MOD_CTRL) && k.ch == 'u')                a->card_top -= half;
        else if (k.kind == KEY_CHAR && k.ch == 'g' && !k.mods)                            a->card_top = 0;
        else if (k.kind == KEY_CHAR && k.ch == 'G' && !k.mods)                            a->card_top = a->card_lines;
        if (a->card_top < 0) a->card_top = 0;
        a->dirty = 1;
        return 1;
    }

    case MODAL_PROMPT: {
        int r = ui_prompt_key(&a->prompt, k);
        if (r == 1) prompt_accept(a);
        else if (r == -1) {
            a->modal       = MODAL_NONE;
            a->prompt_what = PROMPT_NONE;
            app_set_status(a, "canceled");
        }
        return 1;
    }

    case MODAL_MESSAGE:
        a->modal = MODAL_NONE;
        return 1;

    case MODAL_PICKER:
        app_pick_key(a, k);
        return 1;

    case MODAL_CLEAR_STATUS: {
        const Token *t = app_clear_status_target(a);
        if (!t) { a->modal = MODAL_NONE; a->pending_token = -1; return 1; }

        if (k.kind == KEY_CHAR && k.ch >= '1' && k.ch < '1' + (uint32_t)t->nstatus) {
            int idx = a->pending_token;
            a->modal = MODAL_NONE;
            a->pending_token = -1;
            app_clear_token_status(a, idx, (int)(k.ch - '1'));
        } else if (k.kind == KEY_CHAR && (k.ch == 'a' || k.ch == 'A')) {
            int idx = a->pending_token;
            a->modal = MODAL_NONE;
            a->pending_token = -1;
            app_clear_token_status(a, idx, -1);
        } else if (k.kind == KEY_ESC ||
                   (k.kind == KEY_CHAR && (k.ch == 'q' || k.ch == 'n'))) {
            a->modal = MODAL_NONE;
            a->pending_token = -1;
            app_set_status(a, "canceled");
        }
        return 1;
    }

    case MODAL_CONFIRM_DELETE: {
        if (k.kind == KEY_CHAR && (k.ch == 'y' || k.ch == 'Y')) {
            a->modal = MODAL_NONE;
            app_delete_map(a, a->pending_file);
        } else if (k.kind == KEY_ESC ||
                   (k.kind == KEY_CHAR && (k.ch == 'n' || k.ch == 'N'))) {
            a->modal = MODAL_NONE;
            app_set_status(a, "kept");
        }
        if (a->modal == MODAL_NONE) a->pending_file[0] = '\0';
        return 1;
    }

    case MODAL_CONFIRM_RECOVER: {
        if (k.kind == KEY_CHAR && (k.ch == 'y' || k.ch == 'Y')) {
            a->modal = MODAL_NONE;
            recover_autosave(a);                  /* may put up a message */
            a->pending_file[0] = '\0';
        } else if (k.kind == KEY_ESC ||
                   (k.kind == KEY_CHAR && (k.ch == 'n' || k.ch == 'N'))) {
            a->modal = MODAL_NONE;
            unlink(a->pending_file);
            app_set_status(a, "the unsaved work was let go");
            a->pending_file[0] = '\0';
        }
        return 1;
    }

    case MODAL_CONFIRM_QUIT:
    case MODAL_CONFIRM_DISCARD: {
        int discard = (a->modal == MODAL_CONFIRM_DISCARD);
        if (k.kind == KEY_CHAR && (k.ch == 'y' || k.ch == 'Y')) {
            a->modal = MODAL_NONE;
            if (discard) {
                app_set_status(a, "discarded unsaved changes");
                if (a->pending_file[0]) {
                    char next[MAP_PATH_MAX];
                    str_lcpy(next, a->pending_file, sizeof next);
                    a->pending_file[0] = '\0';
                    drop_autosave(a);
                    app_open_map(a, next);
                } else {
                    app_close_map(a);
                }
            } else {
                /* Quitting is the other way work is let go on purpose, and
                 * the copy must not offer it back next time. */
                drop_autosave(a);
                a->running = 0;
            }
        } else if (k.kind == KEY_CHAR && (k.ch == 'n' || k.ch == 'N')) {
            a->modal = MODAL_NONE;
            a->pending_file[0] = '\0';
        } else if (k.kind == KEY_ESC) {
            a->modal = MODAL_NONE;
            a->pending_file[0] = '\0';
        }
        return 1;
    }
    }
    return 0;
}

/* The remote view belongs to the encounter: the players were watching this
 * map, so closing it takes their view away too, unless :serve was asked to
 * stay. Said out loud rather than done quietly, because the next :serve
 * makes a fresh join code and everyone has to be told the new one.
 *
 * Called before the session log closes, so the log records it. */
static void close_server_with_map(App *a)
{
    if (!net_active(&a->net) || net_stays(&a->net)) return;

    int had = net_clients(&a->net);
    net_stop(&a->net);

    char msg[96];
    if (had) snprintf(msg, sizeof msg, "remote view off - %d client%s dropped",
                      had, had == 1 ? "" : "s");
    else     str_lcpy(msg, "remote view off", sizeof msg);
    app_note_more(a, msg);
}

/* The one way a map is put down: the log is a session with one map, so it
 * closes here and nowhere else. */
void app_close_map(App *a)
{
    a->preview = 0;
    /* A handout is the encounter's: it comes down with the map, before a
     * server kept alive carries on to the next one. */
    if (a->handout_up) net_set_handout(&a->net, "", 0, a->now_ms);
    a->handout_up = 0;
    a->handout_title[0] = a->handout_body[0] = '\0';
    drop_autosave(a);
    close_server_with_map(a);
    slog_close(&a->slog);
    map_free(a->map);
    a->map = NULL;
    a->npings = a->npinged = 0;
    a->agent_ring.until_ms = 0;
    undo_clear(&a->undo);
    a->screen = SCREEN_MENU;
}

/* Leaving a map with unsaved work must ask first. `next` is a map to open
 * once it is discarded, which is how :e asks the same question rather than
 * throwing the work away in silence; NULL just closes. */
void app_leave_map_for(App *a, const char *next)
{
    if (a->map && a->map->modified) {
        a->modal = MODAL_CONFIRM_DISCARD;
        str_lcpy(a->modal_title, "Unsaved changes", sizeof a->modal_title);
        snprintf(a->modal_body, sizeof a->modal_body,
                 next ? "%.40s has unsaved changes. Discard them and open the other map?"
                      : "%.40s has unsaved changes. Discard them?", a->map->name);
        str_lcpy(a->pending_file, next ? next : "", sizeof a->pending_file);
        return;
    }
    if (next) { app_open_map(a, next); return; }
    app_set_status(a, "");
    app_close_map(a);
}

void app_leave_map(App *a) { app_leave_map_for(a, NULL); }

void app_request_quit(App *a)
{
    if (a->map && a->map->modified) {
        a->modal = MODAL_CONFIRM_QUIT;
        str_lcpy(a->modal_title, "Quit without saving?", sizeof a->modal_title);
        snprintf(a->modal_body, sizeof a->modal_body,
                 "%s has unsaved changes.", a->map->name);
        return;
    }
    a->running = 0;
}

/* ------------------------------------------------------------ ruler mode */

/* The creature the cursor is over, reading the whole footprint rather than
 * the square in its corner. A 3x3 cursor covers nine squares and a creature
 * standing on any of them is under it, which is the only reading that matches
 * what the cursor draws. */
int app_target_token_under(const App *a)
{
    return tokens_covered_next(&a->map->tokens, a->ed.cx, a->ed.cy,
                               play_cursor_size(&a->play, a->map), -1);
}

int app_target_token(const App *a)
{
    const Play *pl = &a->play;
    if (pl->sel >= 0 && pl->sel < a->map->tokens.n) return pl->sel;
    return app_target_token_under(a);
}

/* Starts measuring at the cursor. In play mode the anchor snaps to a token
 * under the cursor, so "how far is the ogre from Aria" is two keystrokes. */
void app_ruler_begin(App *a)
{
    Editor *e = &a->ed;
    int     x = e->cx, y = e->cy;

    if (a->screen == SCREEN_PLAY) {
        int idx = app_target_token_under(a);
        if (idx >= 0) {
            x = a->map->tokens.v[idx].x;
            y = a->map->tokens.v[idx].y;
        }
    }
    ruler_start(&a->ruler, x, y);
    app_set_status(a, "RULER - move to measure, enter adds a leg, esc done");
}

/* Returns 1 when the key was consumed. Measuring is a mode: it swallows keys
 * it does not use, so a stray p or d cannot edit the map mid-measurement. */
int app_ruler_key(App *a, Key k)
{
    if (!a->ruler.active) return 0;

    Editor *e = &a->ed;
    Map    *m = a->map;

    if (e->mode == ED_COMMAND) return 0;      /* the command line has priority */

    int dx = 0, dy = 0;
    if (k.kind == KEY_LEFT)       dx = -1;
    else if (k.kind == KEY_RIGHT) dx = 1;
    else if (k.kind == KEY_UP)    dy = -1;
    else if (k.kind == KEY_DOWN)  dy = 1;
    else if (k.kind == KEY_CHAR && k.mods == 0) {
        if (k.ch == 'h') dx = -1;
        else if (k.ch == 'l') dx = 1;
        else if (k.ch == 'k') dy = -1;
        else if (k.ch == 'j') dy = 1;
    }
    if (dx || dy) {
        ed_move(e, m, dx, dy, take_count(e));
        ruler_set_cursor(&a->ruler, e->cx, e->cy);
        /* The opening hint has served its purpose once you start moving, and
         * the status line is more useful showing the whole measurement. */
        a->status[0] = '\0';
        return 1;
    }

    if (k.kind == KEY_ESC) {
        ruler_reset(&a->ruler);
        app_set_status(a, "");
        return 1;
    }
    if (k.kind == KEY_ENTER) {
        if (!ruler_add_waypoint(&a->ruler))
            app_set_status(a, "no room for another leg");
        return 1;
    }
    if (k.kind == KEY_BACKSPACE) {
        if (!ruler_drop_waypoint(&a->ruler)) app_set_status(a, "only the anchor is left");
        return 1;
    }

    if (k.kind != KEY_CHAR || k.mods != 0) return 1;

    if (k.ch >= '1' && k.ch <= '9') { count_digit(e, k.ch); return 1; }

    switch (k.ch) {
    case 'm': app_ruler_begin(a); break;
    case 'u': ruler_drop_waypoint(&a->ruler); break;

    case 'M': {
        /* Cycling in place beats remembering the command name when the number
         * on screen looks wrong. */
        m->metric = (m->metric + 1) % DIST_COUNT;
        map_touch(m);
        char msg[64];
        snprintf(msg, sizeof msg, "metric: %s",
                 dist_metric_name((DistMetric)m->metric));
        app_set_status(a, msg);
        break;
    }

    case ':':
        e->mode = ED_COMMAND;
        ui_prompt_open(&e->cmd, "", "", "");
        break;

    case '+': case '=': ed_set_zoom(e, m, e->view.zoom + 1); break;
    case '-': case '_': ed_set_zoom(e, m, e->view.zoom - 1); break;
    case 'z': grid_center_on(&e->view, m, e->cx, e->cy); break;
    default: break;
    }
    return 1;
}

static void app_key_dispatch(App *a, Key k);

void app_fog_sync(App *a)
{
    if (!a->map) return;
    /* Sight's cache records the generation it was worked out for. */
    if (a->map->sight.valid && a->map->gen == a->map->sight.gen) return;
    fog_recompute(a->map);
}

void app_key(App *a, Key k)
{
    a->key_saw_hidden = a->map && tokens_any_hidden(&a->map->tokens);
    app_key_dispatch(a, k);
    /* The range template and a burst point at the cursor, wherever the key
     * left it -- [ and ], a detour through build mode and :play included. */
    if (a->map && a->screen == SCREEN_PLAY) range_set_aim(&a->play.range, a->ed.cx, a->ed.cy);
    app_floor_sync(a);
    app_fog_sync(a);
}

static void app_key_dispatch(App *a, Key k)
{
    PROF_ZONE("input.key");

    a->dirty = 1;

    /* F12 is global and must work even with a modal up, so the profiler can
     * be consulted whenever something feels slow. */
    if (k.kind == KEY_F12) { prof_overlay_toggle(); return; }

    if (modal_key(a, k)) return;

    /* q leaves :player preview, the way it leaves the ? page, and it is
     * caught here so it cannot reach the q that closes the map. */
    if (a->preview && a->screen == SCREEN_PLAY && k.kind == KEY_CHAR && k.mods == 0 &&
        k.ch == 'q' && a->ed.mode != ED_COMMAND && !a->pending) {
        a->preview = 0;
        app_set_status(a, "back to the GM's view");
        return;
    }

    if (a->screen == SCREEN_HELP) { help_key(a, k); return; }

    /* ? is global rather than repeated in every handler, so no mode can end up
     * without a way to ask what its keys are. The command line keeps it: a
     * question mark is a character you might want to type. */
    if (k.kind == KEY_CHAR && k.mods == 0 && k.ch == '?' &&
        a->ed.mode != ED_COMMAND && !a->pending) {
        help_open(a);
        return;
    }

    /* Global like ?, so no mode can be the one without it. The command line
     * keeps its own # -- it is a character somebody might want to type. */
    if (k.kind == KEY_CHAR && k.mods == 0 && k.ch == '#' && a->map &&
        a->ed.mode != ED_COMMAND && !a->pending) {
        a->ed.labels = !a->ed.labels;
        ed_layout(&a->ed, a->map, a->rnd->w, a->rnd->h);
        app_set_status(a, a->ed.labels ? "labels on" : "labels off");
        return;
    }

    /* [ and ] step between floors, in every mode on the map, the same way. */
    if (k.kind == KEY_CHAR && k.mods == 0 && (k.ch == '[' || k.ch == ']') && a->map &&
        (a->screen == SCREEN_EDITOR || a->screen == SCREEN_PLAY) &&
        a->ed.mode != ED_COMMAND && !a->pending && !a->ed.pending_g) {
        a->ed.count = 0;
        /* Something anchored on this floor would stretch onto the next. */
        if (a->ed.mode == ED_VISUAL || a->play.visual || (a->ed.mode == ED_WALL && a->ed.has_anchor) ||
            a->ruler.active) {
            app_set_status(a, "esc first - the box or the ruler would stretch onto the other floor");
            return;
        }
        app_floor_step(a, k.ch == ']' ? 1 : -1);
        return;
    }

    /* F1/F2 switch between building the map and running the fight on it. */
    if (a->map && (k.kind == KEY_F1 || k.kind == KEY_F2)) {
        int to_play = (k.kind == KEY_F2);
        a->screen   = to_play ? SCREEN_PLAY : SCREEN_EDITOR;
        /* Leaving wall mode lifts the pen, as esc does: the stroke is one
         * step, and the next run is another. */
        undo_stroke_end(&a->undo);
        a->ed.pen   = a->ed.erase = 0;
        a->ed.mode  = ED_NORMAL;
        /* Neither a half-typed prefix nor a half-typed count means anything
         * on the other side; play movement reads the count, so a stray one
         * would arrive as a multiplier nobody asked for. */
        a->pending  = 0;
        a->ed.count = 0;
        a->ed.link_on = 0;                   /* a half-made link is build mode's */
        app_set_status(a, to_play ? "play mode - i places, enter grabs, ? for keys"
                                  : "build mode - ? for keys");
        return;
    }

    if (k.kind == KEY_CHAR && (k.mods & MOD_CTRL) && k.ch == 'c') {
        app_request_quit(a);
        return;
    }

    switch (a->screen) {
    case SCREEN_MENU:    app_menu_key(a, k); break;
    case SCREEN_BROWSER: app_browser_key(a, k); break;
    case SCREEN_EDITOR:  app_editor_key(a, k); break;
    case SCREEN_PLAY:
        app_play_key(a, k);
        break;
    case SCREEN_HELP:    break;               /* handled above */
    }
}
