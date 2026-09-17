#include "app_priv.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "draw.h"
#include "prof.h"

static int menu_visible_rows(const App *a) { return imax(1, a->rnd->h - 10); }

static const char *MENU_ITEMS[] = { "Open Map", "New Map", "Quit" };
#define MENU_COUNT ((int)(sizeof MENU_ITEMS / sizeof *MENU_ITEMS))

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
    undo_init(&a->undo);
    slog_init(&a->slog);

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
}

void app_set_status(App *a, const char *msg)
{
    str_lcpy(a->status, msg, sizeof a->status);
    a->nstatus_span = 0;
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

/* The status message, then its coloured spans over the top. A span is drawn
 * only when all of it survived the ellipsis; half a number in gold would be
 * a different number. */
static void draw_status_msg(App *a, int x, int y, int maxw)
{
    Renderer    *r  = a->rnd;
    const Theme *th = a->th;

    draw_text_ellipsis(r, x, y, a->status, maxw, style(th->dim, th->bg, 0));

    int cut = text_width(a->status) > maxw;
    for (int i = 0; i < a->nstatus_span; i++) {
        char pre[sizeof a->status];
        int  at = a->status_span[i].at, len = a->status_span[i].len;
        memcpy(pre, a->status, (size_t)at);
        pre[at] = '\0';
        int col = text_width(pre);
        if (col + len > maxw - (cut ? 1 : 0)) continue;

        char word[16];
        if (len >= (int)sizeof word) continue;
        memcpy(word, a->status + at, (size_t)len);
        word[len] = '\0';
        draw_text(r, x + col, y, word, len, style(a->status_span[i].fg, th->bg, 0));
    }
}

/* For the things that happened, as opposed to the things the app has to
 * say: the status line shows it and the session log, when on, keeps it. */
void app_note(App *a, const char *msg)
{
    app_set_status(a, msg);
    slog_write(&a->slog, msg);
}

static void show_message(App *a, const char *title, const char *body)
{
    a->modal = MODAL_MESSAGE;
    str_lcpy(a->modal_title, title, sizeof a->modal_title);
    str_lcpy(a->modal_body, body, sizeof a->modal_body);
    a->dirty = 1;
}

/* --------------------------------------------------------------- maps */

int app_open_map(App *a, const char *path)
{
    char err[MAPIO_ERR_MAX] = { 0 };
    Map *m = mapio_load(path, err, sizeof err);
    if (!m) {
        show_message(a, "Cannot open map", err);
        return -1;
    }

    slog_close(&a->slog);
    map_free(a->map);
    a->map = m;
    undo_clear(&a->undo);          /* history does not survive a new map */
    play_init(&a->play);
    ed_init(&a->ed, m);
    ed_layout(&a->ed, m, a->rnd->w, a->rnd->h);
    grid_center_on(&a->ed.view, m, a->ed.cx, a->ed.cy);

    a->screen = SCREEN_EDITOR;

    char msg[192];
    snprintf(msg, sizeof msg, "opened %s (%dx%d, %d token%s)",
             m->name, m->w, m->h, m->tokens.n, m->tokens.n == 1 ? "" : "s");
    app_set_status(a, msg);
    return 0;
}

/* A fresh map is a floored rectangle with a wall around it: the common case
 * is a room, and starting from an empty void gives the user nothing to see
 * or move around in. */
static void app_new_map(App *a, const char *name, int w, int h)
{
    Map *m = map_new(w, h, name);
    map_fill_tiles(m, 0, 0, w - 1, h - 1, TILE_FLOOR);
    map_rect_walls(m, 0, 0, w - 1, h - 1, EDGE_WALL);
    m->modified = 1;

    char path[MAP_PATH_MAX];
    mapio_resolve_path(name, path, sizeof path);
    str_lcpy(m->path, path, sizeof m->path);

    slog_close(&a->slog);
    map_free(a->map);
    a->map = m;
    undo_clear(&a->undo);
    play_init(&a->play);
    ed_init(&a->ed, m);
    ed_layout(&a->ed, m, a->rnd->w, a->rnd->h);
    grid_center_on(&a->ed.view, m, a->ed.cx, a->ed.cy);

    a->screen = SCREEN_EDITOR;

    char msg[192];
    /* Bounded conversions: a long path should shorten the message, not
     * silently overrun the intent of it. */
    snprintf(msg, sizeof msg, "new map %.40s (%dx%d) - :w saves to %.100s",
             name, w, h, path);
    app_set_status(a, msg);
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
        char partial[MAP_PATH_MAX];
        size_t n = str_lcpy(partial, dir, sizeof partial);
        for (size_t i = 1; i < n; i++) {
            if (partial[i] != '/') continue;
            partial[i] = '\0';
            mkdir(partial, 0755);
            partial[i] = '/';
        }
        mkdir(dir, 0755);
    }

    char err[MAPIO_ERR_MAX] = { 0 };
    if (mapio_save(a->map, path, err, sizeof err) != 0) {
        show_message(a, "Cannot save map", err);
        return -1;
    }

    char msg[192];
    snprintf(msg, sizeof msg, "wrote %.170s", path);
    app_set_status(a, msg);
    return 0;
}

static void app_refresh_entries(App *a)
{
    free(a->entries);
    a->entries  = NULL;
    a->nentries = mapio_scan(&a->entries);
    a->browser.sel = 0;
    a->browser.top = 0;
}

/* Rescans without losing your place, so deleting several in a row does not
 * send the caret back to the top each time. */
static void app_rescan_keeping_place(App *a)
{
    int sel = a->browser.sel;
    app_refresh_entries(a);
    a->browser.sel = iclamp(sel, 0, imax(0, a->nentries - 1));
    ui_list_move(&a->browser, a->nentries, 0, menu_visible_rows(a));
}

/* Copies bytes, refusing to write over anything. The "x" mode is C11's
 * exclusive create, so the check and the create are one operation rather than
 * a test that another process could slip past. */
static int copy_file(const char *from, const char *to, char *err, size_t errsz)
{
    FILE *in = fopen(from, "rb");
    if (!in) {
        snprintf(err, errsz, "%.60s", strerror(errno));
        return -1;
    }

    FILE *out = fopen(to, "wbx");
    if (!out) {
        snprintf(err, errsz, errno == EEXIST ? "already exists" : "%.60s",
                 strerror(errno));
        fclose(in);
        return -1;
    }

    char   buf[8192];
    size_t n;
    int    ok = 1;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    if (ferror(in)) ok = 0;

    fclose(in);
    if (fclose(out) != 0) ok = 0;

    if (!ok) {
        snprintf(err, errsz, "%.60s", strerror(errno));
        unlink(to);          /* never leave half a map behind */
        return -1;
    }
    return 0;
}

/* Splits a map path into the directory it lives in and its name without the
 * extension, which is what both renaming and duplicating start from. */
static void split_map_path(const char *path, char *dir, size_t dirsz,
                           char *base, size_t basesz)
{
    str_lcpy(dir, path, dirsz);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    else       str_lcpy(dir, ".", dirsz);

    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    str_lcpy(base, name, basesz);

    size_t n = strlen(base);
    if (n > 4 && strcmp(base + n - 4, ".vtt") == 0) base[n - 4] = '\0';
}

/* Strips a trailing " copy" or " copy 3" so that duplicating a duplicate
 * counts up from the original rather than stacking the word: a copy of
 * "goblin copy" should be offered "goblin copy 2", not "goblin copy copy". */
static void strip_copy_suffix(char *base)
{
    size_t n = strlen(base);

    /* Walk back over a trailing number, if there is one. */
    size_t end = n;
    while (end > 0 && base[end - 1] >= '0' && base[end - 1] <= '9') end--;
    if (end < n && end > 0 && base[end - 1] == ' ') end--;
    else if (end < n) return;                  /* digits with no space before */

    const size_t clen = 5;                     /* " copy" */
    if (end >= clen && strncmp(base + end - clen, " copy", clen) == 0)
        base[end - clen] = '\0';
}

/* "goblin ambush" -> "goblin ambush copy", then "copy 2" and so on, so the
 * offered name is one you can accept without thinking. */
static void suggest_copy_name(const char *dir, const char *base,
                              char *out, size_t outsz)
{
    char root[MAP_NAME_MAX];
    str_lcpy(root, base, sizeof root);
    strip_copy_suffix(root);
    if (!root[0]) str_lcpy(root, base, sizeof root);

    for (int i = 1; i < 100; i++) {
        char cand[MAP_NAME_MAX];
        if (i == 1) snprintf(cand, sizeof cand, "%.40s copy", root);
        else        snprintf(cand, sizeof cand, "%.40s copy %d", root, i);

        char path[MAP_PATH_MAX];
        snprintf(path, sizeof path, "%.400s/%.80s.vtt", dir, cand);
        if (access(path, F_OK) != 0) { str_lcpy(out, cand, outsz); return; }
    }
    str_lcpy(out, base, outsz);
}

/* Validates a name the user typed and builds the path it names, beside the
 * file it came from. Reports the reason and returns -1 when it will not do. */
static int build_dest_path(App *a, const char *from, const char *typed,
                           char *base, size_t basesz, char *to, size_t tosz)
{
    str_lcpy(base, typed, basesz);

    /* Trim an extension the user typed, so "x.vtt" does not become
     * "x.vtt.vtt". */
    size_t n = strlen(base);
    if (n > 4 && strcmp(base + n - 4, ".vtt") == 0) base[n - 4] = '\0';

    if (!base[0]) { app_set_status(a, "cancelled: a map needs a name"); return -1; }
    if (strchr(base, '/')) {
        app_set_status(a, "a name cannot contain '/': this names a map, not a path");
        return -1;
    }

    char dir[MAP_PATH_MAX], unused[MAP_NAME_MAX];
    split_map_path(from, dir, sizeof dir, unused, sizeof unused);
    snprintf(to, tosz, "%.400s/%.80s.vtt", dir, base);
    return 0;
}

/* Sets the title inside a saved map. Best effort: a map too damaged to load
 * keeps whatever title it had. */
static int retitle_map(const char *path, const char *title)
{
    char err[MAPIO_ERR_MAX] = { 0 };
    Map *m = mapio_load(path, err, sizeof err);
    if (!m) return 0;

    str_lcpy(m->name, title, sizeof m->name);
    int ok = (mapio_save(m, path, err, sizeof err) == 0);
    map_free(m);
    return ok;
}

/* Rescans, then puts the caret on a particular file rather than leaving it on
 * whatever now sits at the old index. */
static void select_path(App *a, const char *path)
{
    app_rescan_keeping_place(a);
    for (int i = 0; i < a->nentries; i++) {
        if (strcmp(a->entries[i].path, path) == 0) {
            a->browser.sel = i;
            ui_list_move(&a->browser, a->nentries, 0, menu_visible_rows(a));
            return;
        }
    }
}

/* Renames the file, and then its title to match if the map will parse.
 *
 * The file move comes first and on its own: it preserves the contents exactly
 * and works even on a map too damaged to load, which is when you most want to
 * be able to move it out of the way. Updating the title is best-effort on top
 * of an already-completed rename, so a failure there costs nothing. */
static void app_rename_map(App *a, const char *from, const char *typed)
{
    char base[MAP_NAME_MAX], to[MAP_PATH_MAX];
    if (build_dest_path(a, from, typed, base, sizeof base, to, sizeof to) != 0) return;

    if (strcmp(from, to) == 0) { app_set_status(a, "name unchanged"); return; }

    /* link() fails if the destination exists, which makes this refuse to
     * clobber another map rather than racing an access() check. Filesystems
     * that will not hard-link fall back to a checked rename. */
    if (link(from, to) == 0) {
        if (unlink(from) != 0) {
            char body[MAP_PATH_MAX + 96];
            snprintf(body, sizeof body,
                     "renamed, but the old file is still there: %.60s", strerror(errno));
            show_message(a, "Partly renamed", body);
        }
    } else if (errno == EEXIST) {
        char body[MAP_PATH_MAX + 64];
        snprintf(body, sizeof body, "%.200s already exists", to);
        show_message(a, "Cannot rename", body);
        return;
    } else {
        if (access(to, F_OK) == 0) {
            char body[MAP_PATH_MAX + 64];
            snprintf(body, sizeof body, "%.200s already exists", to);
            show_message(a, "Cannot rename", body);
            return;
        }
        if (rename(from, to) != 0) {
            char body[MAP_PATH_MAX + 96];
            snprintf(body, sizeof body, "%.200s: %.60s", to, strerror(errno));
            show_message(a, "Cannot rename", body);
            return;
        }
    }

    int titled = retitle_map(to, base);
    select_path(a, to);

    char msg[MAP_PATH_MAX + 64];
    snprintf(msg, sizeof msg, "renamed to %.80s.vtt%s", base,
             titled ? "" : "  (title unchanged: the map would not load)");
    app_set_status(a, msg);
}

/* Copies the file, then retitles the copy. Byte-for-byte rather than load and
 * re-save, so the duplicate is exactly the original -- including a map the
 * loader would choke on. */
static void app_duplicate_map(App *a, const char *from, const char *typed)
{
    char base[MAP_NAME_MAX], to[MAP_PATH_MAX];
    if (build_dest_path(a, from, typed, base, sizeof base, to, sizeof to) != 0) return;

    if (strcmp(from, to) == 0) {
        app_set_status(a, "a copy needs a name of its own");
        return;
    }

    char err[96] = { 0 };
    if (copy_file(from, to, err, sizeof err) != 0) {
        char body[MAP_PATH_MAX + 128];
        snprintf(body, sizeof body, "%.200s: %.60s", to, err);
        show_message(a, "Cannot duplicate", body);
        return;
    }

    int titled = retitle_map(to, base);
    select_path(a, to);

    char msg[MAP_PATH_MAX + 64];
    snprintf(msg, sizeof msg, "copied to %.80s.vtt%s", base,
             titled ? "" : "  (title unchanged: the map would not load)");
    app_set_status(a, msg);
}

static void app_delete_map(App *a, const char *path)
{
    char shown[MAP_PATH_MAX];
    str_lcpy(shown, path, sizeof shown);

    if (unlink(path) != 0) {
        char body[MAP_PATH_MAX + 64];
        snprintf(body, sizeof body, "%.200s: %.60s", shown, strerror(errno));
        show_message(a, "Could not delete", body);
        /* Rescan anyway: whatever went wrong, the list on screen may no
         * longer match the disk. */
        app_rescan_keeping_place(a);
        return;
    }

    app_rescan_keeping_place(a);

    char msg[MAP_PATH_MAX + 32];
    snprintf(msg, sizeof msg, "deleted %.180s", shown);
    app_set_status(a, msg);
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
             t->label[0] ? t->label : "unlabelled",
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
        if (!text[0]) { app_set_status(a, "cancelled: a map needs a name"); return; }
        str_lcpy(a->pending_name, text, sizeof a->pending_name);
        app_open_prompt(a, PROMPT_NEW_SIZE, "Map size", "width x height, in tiles", "40x25");
        return;
    }
    case PROMPT_NEW_SIZE: {
        int w = 0, h = 0;
        if (sscanf(text, "%dx%d", &w, &h) != 2 && sscanf(text, "%d %d", &w, &h) != 2) {
            show_message(a, "Bad size", "expected something like 40x25");
            return;
        }
        if (w < MAP_MIN_DIM || h < MAP_MIN_DIM || w > MAP_MAX_DIM || h > MAP_MAX_DIM) {
            char body[128];
            snprintf(body, sizeof body, "size must be between %dx%d and %dx%d",
                     MAP_MIN_DIM, MAP_MIN_DIM, MAP_MAX_DIM, MAP_MAX_DIM);
            show_message(a, "Bad size", body);
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
                 token_kind_name(t.kind), t.label[0] ? t.label : "unlabelled",
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
        snprintf(msg, sizeof msg, "relabelled %.30s to %.30s",
                 was[0] ? was : token_kind_name(t.kind), t.label);
        app_note(a, msg);
        return;
    }
    case PROMPT_STATUS_LABEL: {
        int idx = a->pending_token;
        a->pending_token = -1;
        if (idx < 0 || idx >= a->map->tokens.n) return;
        if (!text[0]) { app_set_status(a, "cancelled: a marker needs a word"); return; }

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
                 t.label[0] ? t.label : token_kind_name(t.kind), text);
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
static const Token *clear_status_target(const App *a)
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

/* ------------------------------------------------------------- key page */

/* Which map describes the keys that work right now. Measuring and carrying
 * are flags rather than screens, but they change enough of the keyboard to be
 * worth their own page. */
static KeyMapId app_keymap_id(const App *a)
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
static int help_order(const App *a, const KeyMap *out[KEYS_COUNT])
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

static void draw_help(App *a)
{
    const KeyMap *maps[KEYS_COUNT];
    int n = help_order(a, maps);

    a->help_lines = ui_keypage(a->rnd, a->th, maps, n, &a->help_top,
                               a->ascii ? &BOX_ASCII : &BOX_ROUND);
}

/* ---------------------------------------------------------------- input */

static int modal_key(App *a, Key k)
{
    switch (a->modal) {
    case MODAL_NONE:
        return 0;

    case MODAL_PROMPT: {
        int r = ui_prompt_key(&a->prompt, k);
        if (r == 1) prompt_accept(a);
        else if (r == -1) {
            a->modal       = MODAL_NONE;
            a->prompt_what = PROMPT_NONE;
            app_set_status(a, "cancelled");
        }
        return 1;
    }

    case MODAL_MESSAGE:
        a->modal = MODAL_NONE;
        return 1;

    case MODAL_CLEAR_STATUS: {
        const Token *t = clear_status_target(a);
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
            app_set_status(a, "cancelled");
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

    case MODAL_CONFIRM_QUIT:
    case MODAL_CONFIRM_DISCARD: {
        int discard = (a->modal == MODAL_CONFIRM_DISCARD);
        if (k.kind == KEY_CHAR && (k.ch == 'y' || k.ch == 'Y')) {
            a->modal = MODAL_NONE;
            if (discard) {
                app_close_map(a);
                app_set_status(a, "discarded unsaved changes");
            } else {
                a->running = 0;
            }
        } else if (k.kind == KEY_CHAR && (k.ch == 'n' || k.ch == 'N')) {
            a->modal = MODAL_NONE;
        } else if (k.kind == KEY_ESC) {
            a->modal = MODAL_NONE;
        }
        return 1;
    }
    }
    return 0;
}

/* Leaving a map with unsaved work must ask first. */
/* The one way a map is put down: the log is a session with one map, so it
 * closes here and nowhere else. */
void app_close_map(App *a)
{
    slog_close(&a->slog);
    map_free(a->map);
    a->map = NULL;
    undo_clear(&a->undo);
    a->screen = SCREEN_MENU;
}

void app_leave_map(App *a)
{
    if (a->map && a->map->modified) {
        a->modal = MODAL_CONFIRM_DISCARD;
        str_lcpy(a->modal_title, "Unsaved changes", sizeof a->modal_title);
        snprintf(a->modal_body, sizeof a->modal_body,
                 "%s has unsaved changes. Discard them?", a->map->name);
        return;
    }
    app_close_map(a);
}

static void app_request_quit(App *a)
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

static void menu_key(App *a, Key k)
{
    if (k.kind == KEY_CHAR && k.mods == 0) {
        switch (k.ch) {
        case 'j': ui_list_move(&a->menu, MENU_COUNT, 1, menu_visible_rows(a)); return;
        case 'k': ui_list_move(&a->menu, MENU_COUNT, -1, menu_visible_rows(a)); return;
        case 'q': app_request_quit(a); return;
        default: break;
        }
    }
    if (k.kind == KEY_DOWN) { ui_list_move(&a->menu, MENU_COUNT, 1, menu_visible_rows(a)); return; }
    if (k.kind == KEY_UP)   { ui_list_move(&a->menu, MENU_COUNT, -1, menu_visible_rows(a)); return; }

    if (k.kind == KEY_ENTER) {
        switch (a->menu.sel) {
        case 0:
            app_refresh_entries(a);
            a->screen = SCREEN_BROWSER;
            break;
        case 1:
            app_open_prompt(a, PROMPT_NEW_NAME, "New map", "saved as <name>.vtt", "");
            break;
        default:
            app_request_quit(a);
            break;
        }
    }
}

static void browser_key(App *a, Key k)
{
    int rows = menu_visible_rows(a);

    if (k.kind == KEY_ESC) { a->screen = SCREEN_MENU; return; }
    if (k.kind == KEY_DOWN) { ui_list_move(&a->browser, a->nentries, 1, rows); return; }
    if (k.kind == KEY_UP)   { ui_list_move(&a->browser, a->nentries, -1, rows); return; }

    if (k.kind == KEY_CHAR && k.mods == 0) {
        switch (k.ch) {
        case 'j': ui_list_move(&a->browser, a->nentries, 1, rows); return;
        case 'k': ui_list_move(&a->browser, a->nentries, -1, rows); return;
        case 'g': ui_list_move(&a->browser, a->nentries, -a->nentries, rows); return;
        case 'G': ui_list_move(&a->browser, a->nentries, a->nentries, rows); return;
        case 'r': app_refresh_entries(a); app_set_status(a, "rescanned"); return;

        case 'R': {
            if (a->nentries <= 0) { app_set_status(a, "nothing to rename"); return; }

            str_lcpy(a->pending_file, a->entries[a->browser.sel].path,
                     sizeof a->pending_file);

            /* Pre-fill with the current name so a small correction is a small
             * edit, and drop the extension since the prompt adds it back. */
            char base[MAP_NAME_MAX];
            str_lcpy(base, a->entries[a->browser.sel].name, sizeof base);
            size_t bn = strlen(base);
            if (bn > 4 && strcmp(base + bn - 4, ".vtt") == 0) base[bn - 4] = '\0';

            app_open_prompt(a, PROMPT_RENAME_MAP, "Rename map",
                        "renames the file and its title", base);
            return;
        }

        case 'c': {
            if (a->nentries <= 0) { app_set_status(a, "nothing to duplicate"); return; }

            const char *src = a->entries[a->browser.sel].path;
            str_lcpy(a->pending_file, src, sizeof a->pending_file);

            /* Offer a name that is already free, so accepting it is enough. */
            char dir[MAP_PATH_MAX], base[MAP_NAME_MAX], suggested[MAP_NAME_MAX];
            split_map_path(src, dir, sizeof dir, base, sizeof base);
            suggest_copy_name(dir, base, suggested, sizeof suggested);

            app_open_prompt(a, PROMPT_DUPLICATE_MAP, "Duplicate map",
                        "copies the file and titles the copy", suggested);
            return;
        }

        case 'd': {
            if (a->nentries <= 0) { app_set_status(a, "nothing to delete"); return; }

            /* The path is captured now rather than read back from the index
             * when the answer comes in, so the confirmation and the deletion
             * can never disagree about which file is meant. */
            str_lcpy(a->pending_file, a->entries[a->browser.sel].path,
                     sizeof a->pending_file);
            a->modal = MODAL_CONFIRM_DELETE;
            str_lcpy(a->modal_title, "Delete map?", sizeof a->modal_title);
            str_lcpy(a->modal_body, a->pending_file, sizeof a->modal_body);
            return;
        }

        case 'q': a->screen = SCREEN_MENU; return;
        default: break;
        }
    }

    if (k.kind == KEY_ENTER && a->nentries > 0)
        app_open_map(a, a->entries[a->browser.sel].path);
}

/* -------------------------------------------------------------- wall mode */

static void wall_key(App *a, Key k)
{
    Editor *e = &a->ed;
    Map    *m = a->map;

    if (k.kind == KEY_ESC) {
        if (e->has_anchor) { e->has_anchor = 0; app_set_status(a, "anchor cleared"); return; }
        undo_end(&a->undo);            /* close any stroke still in progress */
        e->mode = ED_NORMAL;
        e->pen = e->erase = 0;
        e->cx = iclamp(e->wx, 0, m->w - 1);
        e->cy = iclamp(e->wy, 0, m->h - 1);
        app_set_status(a, "");
        return;
    }

    if (k.kind == KEY_ENTER) {
        if (!e->has_anchor) { app_set_status(a, "set an anchor with v or V first"); return; }

        EdShape s = ed_shape(e->shape, e->ax, e->ay, e->wx, e->wy, 1);
        undo_end(&a->undo);            /* the shape is its own step */
        ed_wall_shape(m, &a->undo, &s, e->erase ? EDGE_NONE : EDGE_WALL);
        e->has_anchor = 0;

        const char *what = (e->shape == ED_SHAPE_CIRCLE) ? "circle" : "rectangle";
        char msg[64];
        snprintf(msg, sizeof msg, "%s %s", e->erase ? "cleared" : "laid", what);
        app_set_status(a, msg);
        return;
    }

    if (k.kind == KEY_LEFT)  { ed_wall_step(e, m, &a->undo, -1, 0, take_count(e)); return; }
    if (k.kind == KEY_RIGHT) { ed_wall_step(e, m, &a->undo,  1, 0, take_count(e)); return; }
    if (k.kind == KEY_UP)    { ed_wall_step(e, m, &a->undo,  0, -1, take_count(e)); return; }
    if (k.kind == KEY_DOWN)  { ed_wall_step(e, m, &a->undo,  0,  1, take_count(e)); return; }

    if (k.kind == KEY_CHAR && (k.mods & MOD_CTRL) && k.ch == 'r') {
        if (undo_redo(&a->undo, m)) app_set_status(a, "redo");
        return;
    }
    if (k.kind != KEY_CHAR || k.mods != 0) return;

    if (k.ch >= '1' && k.ch <= '9') { count_digit(e, k.ch); return; }

    switch (k.ch) {
    case 'h': ed_wall_step(e, m, &a->undo, -1,  0, take_count(e)); break;
    case 'l': ed_wall_step(e, m, &a->undo,  1,  0, take_count(e)); break;
    case 'k': ed_wall_step(e, m, &a->undo,  0, -1, take_count(e)); break;
    case 'j': ed_wall_step(e, m, &a->undo,  0,  1, take_count(e)); break;

    case ' ':
        e->pen = !e->pen;
        /* Lifting the pen ends the stroke, which is what makes the whole run
         * a single undo step. */
        if (!e->pen) undo_end(&a->undo);
        app_set_status(a, e->pen ? "pen down - movement lays wall" : "pen up");
        break;

    case 'd':
        /* Erasing is the same tool with the sign flipped, so the pen comes
         * down with it rather than making the user press two keys. Switching
         * direction starts a new stroke. */
        undo_end(&a->undo);
        e->erase = !e->erase;
        if (e->erase) e->pen = 1;
        app_set_status(a, e->erase ? "erasing - movement clears wall" : "laying wall");
        break;

    case 'v': case 'V': {
        uint8_t want = (k.ch == 'V') ? ED_SHAPE_CIRCLE : ED_SHAPE_RECT;

        /* The same key twice clears the anchor; the other one changes the
         * shape and keeps it, the way v and V swap between vim's two visual
         * modes rather than cancelling each other. */
        if (e->has_anchor && e->shape == want) {
            e->has_anchor = 0;
            app_set_status(a, "anchor cleared");
            break;
        }

        if (!e->has_anchor) { e->ax = e->wx; e->ay = e->wy; }
        e->has_anchor = 1;
        e->shape      = want;
        app_set_status(a, want == ED_SHAPE_CIRCLE
                          ? "circle anchor - move out for the radius, enter to lay"
                          : "anchor set - move and press enter");
        break;
    }

    case 't': {
        /* Changing what the pen lays starts a new stroke. */
        undo_end(&a->undo);
        ed_cycle_material(e);
        char msg[64];
        snprintf(msg, sizeof msg, "pen lays: %s", edge_name(e->material));
        app_set_status(a, msg);
        break;
    }

    case 'u': if (undo_undo(&a->undo, m)) app_set_status(a, "undo"); break;

    case '+': case '=': ed_set_zoom(e, m, e->view.zoom + 1); break;
    case '-': case '_': ed_set_zoom(e, m, e->view.zoom - 1); break;
    case 'z': grid_center_on(&e->view, m, iclamp(e->wx, 0, m->w - 1),
                             iclamp(e->wy, 0, m->h - 1)); break;
    default: break;
    }
}

/* ------------------------------------------------------------ ruler mode */

/* The creature the cursor is over, reading the whole footprint rather than
 * the square in its corner. A 3x3 cursor covers nine squares and a creature
 * standing on any of them is under it, which is the only reading that matches
 * what the cursor draws. */
int app_token_under_cursor(App *a)
{
    return tokens_covered_next(&a->map->tokens, a->ed.cx, a->ed.cy,
                               play_cursor_size(&a->play, a->map), -1);
}

/* Starts measuring at the cursor. In play mode the anchor snaps to a token
 * under the cursor, so "how far is the ogre from Aria" is two keystrokes. */
void app_ruler_begin(App *a)
{
    Editor *e = &a->ed;
    int     x = e->cx, y = e->cy;

    if (a->screen == SCREEN_PLAY) {
        int idx = app_token_under_cursor(a);
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
        m->modified = 1;
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

/* ------------------------------------------------------------ build mode */

static void editor_key(App *a, Key k)
{
    Editor *e = &a->ed;
    Map    *m = a->map;
    if (!m) { a->screen = SCREEN_MENU; return; }

    if (e->mode == ED_COMMAND) { app_command_key(a, k); return; }
    if (app_ruler_key(a, k))       { return; }
    if (e->mode == ED_WALL)    { wall_key(a, k); return; }

    if (k.kind == KEY_ESC) {
        if (e->mode == ED_VISUAL) { e->mode = ED_NORMAL; app_set_status(a, ""); }
        e->count = 0;
        e->pending_g = 0;
        return;
    }

    /* Arrows mirror hjkl so the editor is usable before the keys are learnt. */
    if (k.kind == KEY_LEFT)  { ed_move(e, m, -1, 0, take_count(e)); return; }
    if (k.kind == KEY_RIGHT) { ed_move(e, m,  1, 0, take_count(e)); return; }
    if (k.kind == KEY_UP)    { ed_move(e, m,  0, -1, take_count(e)); return; }
    if (k.kind == KEY_DOWN)  { ed_move(e, m,  0,  1, take_count(e)); return; }

    if (k.kind == KEY_CHAR && (k.mods & MOD_CTRL)) {
        int page = imax(1, e->view.view.h / zoom_ph(e->view.zoom) / 2);
        if (k.ch == 'd') { ed_move(e, m, 0,  1, page); return; }
        if (k.ch == 'u') { ed_move(e, m, 0, -1, page); return; }
        if (k.ch == 'r') {
            if (undo_redo(&a->undo, m)) app_set_status(a, "redo");
            return;
        }
        return;
    }

    if (k.kind != KEY_CHAR || k.mods != 0) return;

    if (e->pending_g) {
        e->pending_g = 0;
        if (k.ch == 'g') { e->cy = 0; grid_ensure_visible(&e->view, m, e->cx, e->cy, ED_SCROLLOFF); }
        return;
    }

    if (k.ch >= '1' && k.ch <= '9') { count_digit(e, k.ch); return; }
    if (k.ch == '0' && e->count)    { e->count *= 10; return; }

    switch (k.ch) {
    case 'h': ed_move(e, m, -1,  0, take_count(e)); break;
    case 'l': ed_move(e, m,  1,  0, take_count(e)); break;
    case 'k': ed_move(e, m,  0, -1, take_count(e)); break;
    case 'j': ed_move(e, m,  0,  1, take_count(e)); break;

    /* Shift-HJKL toggles the wall on that face of the cursor tile: the fast
     * way to close a single gap without entering the tracing mode. */
    case 'H': ed_toggle_edge(e, m, &a->undo, -1,  0); break;
    case 'L': ed_toggle_edge(e, m, &a->undo,  1,  0); break;
    case 'K': ed_toggle_edge(e, m, &a->undo,  0, -1); break;
    case 'J': ed_toggle_edge(e, m, &a->undo,  0,  1); break;

    case 'b': case 'B': {
        e->brush = size_key(take_count_raw(e), e->brush, k.ch == 'b' ? 1 : -1);
        char msg[32];
        snprintf(msg, sizeof msg, "brush %dx%d", e->brush, e->brush);
        app_set_status(a, msg);
        break;
    }

    case '0': e->cx = 0;        grid_ensure_visible(&e->view, m, e->cx, e->cy, ED_SCROLLOFF); break;
    case '$': e->cx = m->w - 1; grid_ensure_visible(&e->view, m, e->cx, e->cy, ED_SCROLLOFF); break;
    case 'g': e->pending_g = 1; break;
    case 'G': e->cy = m->h - 1; grid_ensure_visible(&e->view, m, e->cx, e->cy, ED_SCROLLOFF); break;

    case 'v': case 'V': {
        uint8_t want = (k.ch == 'V') ? ED_SHAPE_CIRCLE : ED_SHAPE_RECT;

        /* The same key twice leaves visual mode; the other one changes the
         * shape and keeps the anchor, the way v and V swap between vim's two
         * visual modes rather than cancelling each other. */
        if (e->mode == ED_VISUAL && e->shape == want) {
            e->mode = ED_NORMAL;
            app_set_status(a, "");
            break;
        }

        if (e->mode != ED_VISUAL) { e->anchor_x = e->cx; e->anchor_y = e->cy; }
        e->mode  = ED_VISUAL;
        e->shape = want;
        app_set_status(a, want == ED_SHAPE_CIRCLE
                          ? "VISUAL circle - move out for the radius, f paints, x clears"
                          : "VISUAL - f floor, x clear, esc cancel");
        break;
    }

    case 'f': {
        ed_apply_tiles(e, m, &a->undo, e->terrain);
        char msg[64];
        snprintf(msg, sizeof msg, "painted %s", tile_name(e->terrain));
        if (e->mode == ED_VISUAL) e->mode = ED_NORMAL;
        app_set_status(a, msg);
        break;
    }

    case 't': {
        ed_cycle_material(e);
        char msg[64];
        snprintf(msg, sizeof msg, "boundary: %s", edge_name(e->material));
        app_set_status(a, msg);
        break;
    }

    case 'T': {
        ed_cycle_terrain(e);
        char msg[64];
        snprintf(msg, sizeof msg, "terrain: %s", tile_name(e->terrain));
        app_set_status(a, msg);
        break;
    }

    case 'o': case 'O': {
        int secret = (k.ch == 'O');
        int n = ed_toggle_doors(e, m, &a->undo, secret);
        char msg[80];
        if (n) snprintf(msg, sizeof msg, "toggled %d %s%s", n,
                        secret ? "secret door" : "door", n == 1 ? "" : "s");
        else   snprintf(msg, sizeof msg, "no %s on this tile",
                        secret ? "secret doors" : "doors");
        app_set_status(a, msg);
        break;
    }

    case 'x':
        ed_apply_tiles(e, m, &a->undo, TILE_VOID);
        if (e->mode == ED_VISUAL) { e->mode = ED_NORMAL; app_set_status(a, "cleared to void"); }
        break;

    case ' ': ed_toggle_tile(e, m, &a->undo); break;

    case 'u':
        if (undo_undo(&a->undo, m)) app_set_status(a, "undo");
        else                        app_set_status(a, "nothing to undo");
        break;

    case 'm': app_ruler_begin(a); break;

    case 'w':
        e->mode  = ED_WALL;
        e->wx    = e->cx;
        e->wy    = e->cy;
        e->pen   = 0;
        e->erase = 0;
        e->has_anchor = 0;
        app_set_status(a, "WALL - space pen, d erase, v anchor, esc back");
        break;

    case ':':
        e->mode = ED_COMMAND;
        ui_prompt_open(&e->cmd, "", "", "");
        break;

    case '+': case '=': ed_set_zoom(e, m, e->view.zoom + 1); break;
    case '-': case '_': ed_set_zoom(e, m, e->view.zoom - 1); break;

    case 'z': grid_center_on(&e->view, m, e->cx, e->cy); break;
    case 'q': app_leave_map(a); break;
    default: break;
    }
}

void app_key(App *a, Key k)
{
    PROF_ZONE("input.key");

    a->dirty = 1;

    /* F12 is global and must work even with a modal up, so the profiler can
     * be consulted whenever something feels slow. */
    if (k.kind == KEY_F12) { prof_overlay_toggle(); return; }

    if (modal_key(a, k)) return;

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

    /* F1/F2 switch between building the map and running the fight on it. */
    if (a->map && (k.kind == KEY_F1 || k.kind == KEY_F2)) {
        int to_play = (k.kind == KEY_F2);
        a->screen   = to_play ? SCREEN_PLAY : SCREEN_EDITOR;
        a->ed.mode  = ED_NORMAL;
        /* Neither a half-typed prefix nor a half-typed count means anything
         * on the other side; play movement reads the count, so a stray one
         * would arrive as a multiplier nobody asked for. */
        a->pending  = 0;
        a->ed.count = 0;
        app_set_status(a, to_play ? "play mode - i places, enter grabs, ? for keys"
                                  : "build mode - ? for keys");
        return;
    }

    if (k.kind == KEY_CHAR && (k.mods & MOD_CTRL) && k.ch == 'c') {
        app_request_quit(a);
        return;
    }

    switch (a->screen) {
    case SCREEN_MENU:    menu_key(a, k); break;
    case SCREEN_BROWSER: browser_key(a, k); break;
    case SCREEN_EDITOR:  editor_key(a, k); break;
    case SCREEN_PLAY:    app_play_key(a, k); break;
    case SCREEN_HELP:    break;               /* handled above */
    }
}

/* ----------------------------------------------------------------- draw */

static void menu_row(void *ctx, int i, char *buf, size_t bufsz)
{
    (void)ctx;
    str_lcpy(buf, MENU_ITEMS[i], bufsz);
}

static void browser_row(void *ctx, int i, char *buf, size_t bufsz)
{
    const App *a = ctx;
    snprintf(buf, bufsz, "%-32s  %s", a->entries[i].name, a->entries[i].path);
}

/* Every screen puts its transient message on the row above the keybinding
 * bar. Without this the browser silently swallowed its own confirmations --
 * a delete would report nothing at all. */
static void draw_status_line(App *a)
{
    if (!a->status[0]) return;

    Renderer    *r  = a->rnd;
    const Theme *th = a->th;
    int          y  = r->h - 2;

    draw_fill(r, rect(0, y, r->w, 1), ' ', style(th->bar_fg, th->bg, 0));
    draw_status_msg(a, 1, y, r->w - 2);
}

static void draw_menu(App *a)
{
    Renderer    *r  = a->rnd;
    const Theme *th = a->th;

    ui_titlebar(r, th, "vtt", "F12 profiler");

    Rect panel = rect_center(rect(0, 1, r->w, r->h - 2), imin(52, r->w - 4), 12);

    Style dim = style(th->dim, th->bg, 0);
    Style acc = style(th->accent, th->bg, ATTR_BOLD);

    draw_text(r, panel.x, panel.y, "virtual tabletop", -1, acc);
    draw_text(r, panel.x, panel.y + 1, "rules-agnostic battle maps for the terminal", -1, dim);

    Rect list = rect(panel.x, panel.y + 3, panel.w, MENU_COUNT);
    ui_list_draw(r, th, list, &a->menu, MENU_COUNT, menu_row, a);

    draw_status_line(a);

    ui_keybar(r, th, keys_map(KEYS_MENU));
}

static void draw_browser(App *a)
{
    Renderer    *r  = a->rnd;
    const Theme *th = a->th;

    char right[64];
    snprintf(right, sizeof right, "%d map%s", a->nentries, a->nentries == 1 ? "" : "s");
    ui_titlebar(r, th, "open map", right);

    Style dim = style(th->dim, th->bg, 0);

    if (a->nentries == 0) {
        char dir[MAP_PATH_MAX], body[MAP_PATH_MAX + 64];
        mapio_default_dir(dir, sizeof dir);
        snprintf(body, sizeof body, "no .vtt files in . or %s", dir);
        Rect c = rect_center(rect(0, 1, r->w, r->h - 2), imin(70, r->w - 4), 2);
        draw_text_ellipsis(r, c.x, c.y, body, c.w, dim);
        draw_text(r, c.x, c.y + 1, "esc  back to the menu, where you can make one", c.w, dim);
    } else {
        /* One row shorter than the screen allows, leaving the status its
         * place above the keybinding bar. */
        Rect list = rect(2, 2, r->w - 4, imax(1, r->h - 5));
        ui_list_draw(r, th, list, &a->browser, a->nentries, browser_row, a);
    }

    draw_status_line(a);

    ui_keybar(r, th, keys_map(KEYS_BROWSER));
}

static void draw_editor(App *a)
{
    Renderer    *r  = a->rnd;
    const Theme *th = a->th;
    Map         *m  = a->map;

    ed_layout(&a->ed, m, r->w, r->h);

    char left[192];
    snprintf(left, sizeof left, "%s%s", m->name, m->modified ? " [+]" : "");
    ui_titlebar(r, th, left, a->screen == SCREEN_PLAY ? "PLAY" : "BUILD");

    int playing = (a->screen == SCREEN_PLAY);

    if (playing) play_draw(r, m, &a->ed, &a->play, th, a->ascii);
    else         ed_draw(r, m, &a->ed, th, a->ascii);

    if (a->ruler.active) {
        ClipRect saved = rnd_clip_push(r, a->ed.view.view.x, a->ed.view.view.y,
                                       a->ed.view.view.w, a->ed.view.view.h);
        ruler_draw(r, m, &a->ed.view, &a->ruler, th, 1);
        rnd_clip_restore(r, saved);
    }

    /* Status line sits directly above the keybinding bar. */
    char status[192];
    if (a->ruler.active)            ruler_status(&a->ruler, m, status, sizeof status);
    else if (playing && a->play.range.active)
                                    range_status(&a->play.range, m, status, sizeof status);
    else if (playing)               play_status(&a->play, m, &a->ed, status, sizeof status);
    else                            ed_status(&a->ed, m, status, sizeof status);

    int   sy  = r->h - 2;
    Style sbg = style(th->bar_fg, th->bg, 0);
    draw_fill(r, rect(0, sy, r->w, 1), ' ', sbg);

    /* The transient message takes what it needs from the right; the status
     * gets everything left over, rather than a fixed half that truncates it
     * on a wide terminal for no reason. */
    int msg_w = 0;
    if (a->status[0]) msg_w = imin(text_width(a->status), imax(0, r->w * 2 / 3));

    draw_text_ellipsis(r, 1, sy, status, imax(0, r->w - msg_w - 3),
                       style(th->fg, th->bg, 0));
    if (msg_w > 0) draw_status_msg(a, r->w - msg_w - 1, sy, msg_w);

    if (a->ruler.active && a->ed.mode != ED_COMMAND) {
        ui_keybar(r, th, keys_map(KEYS_RULER));
        return;
    }

    if (playing) {
        /* Through app_keymap_id rather than deciding again here: the bar and
         * the ? page have to name the same mode, and this branch had already
         * drifted -- it did not know about the box. */
        if (a->ed.mode != ED_COMMAND)
            ui_keybar(r, th, keys_map(app_keymap_id(a)));
        if (a->ed.mode == ED_COMMAND)
            ui_cmdline_draw(r, th, &a->ed.cmd, r->h - 1, ':');
        return;
    }

    switch (a->ed.mode) {
    case ED_WALL: {
        /* The bar names the shape enter would lay, since v and V chose it a
         * while ago and the anchor on screen does not spell it out. */
        ui_keybar_ex(r, th, keys_map(KEYS_WALL), "enter",
                     a->ed.shape == ED_SHAPE_CIRCLE ? "circle" : "rect");
        break;
    }
    case ED_VISUAL: {
        ui_keybar(r, th, keys_map(KEYS_VISUAL));
        break;
    }
    case ED_COMMAND:
        break;
    case ED_NORMAL:
    default: {
        ui_keybar(r, th, keys_map(KEYS_BUILD));
        break;
    }
    }

    /* The command line replaces the keybinding bar while it is open, the way
     * vim's does. */
    if (a->ed.mode == ED_COMMAND)
        ui_cmdline_draw(r, th, &a->ed.cmd, r->h - 1, ':');
}

void app_draw(App *a)
{
    PROF_ZONE("app.draw");

    switch (a->screen) {
    case SCREEN_HELP:    draw_help(a); prof_overlay_draw(a->rnd); return;
    case SCREEN_MENU:    draw_menu(a); break;
    case SCREEN_BROWSER: draw_browser(a); break;
    case SCREEN_EDITOR:
    case SCREEN_PLAY:
        if (a->map) draw_editor(a);
        else        draw_menu(a);
        break;
    }

    const BoxGlyphs *frame = a->ascii ? &BOX_ASCII : &BOX_ROUND;

    switch (a->modal) {
    case MODAL_PROMPT:  ui_prompt_draw(a->rnd, a->th, &a->prompt, frame); break;
    case MODAL_MESSAGE: ui_modal(a->rnd, a->th, a->modal_title, a->modal_body,
                                 "press any key", frame); break;
    case MODAL_CONFIRM_QUIT:
    case MODAL_CONFIRM_DISCARD:
        ui_confirm(a->rnd, a->th, a->modal_title, a->modal_body, frame);
        break;

    case MODAL_CONFIRM_DELETE:
        /* Its own footer: this one removes a file from disk, and the word
         * "yes" does not say that. */
        ui_modal(a->rnd, a->th, a->modal_title, a->modal_body,
                 "y  delete from disk, permanently      n / esc  keep", frame);
        break;
    case MODAL_CLEAR_STATUS: {
        const Token *t = clear_status_target(a);
        if (!t) break;

        UiChoice items[TOKEN_STATUS_MAX];
        for (int i = 0; i < t->nstatus; i++) {
            snprintf(items[i].text, sizeof items[i].text, "%s  %.20s",
                     status_color_name(t->status[i].color), t->status[i].label);
            items[i].color = a->th->status[t->status[i].color % STATUS_COLOR_COUNT];
        }

        char footer[64];
        snprintf(footer, sizeof footer, "1-%d  clear one      a  all      esc  cancel",
                 t->nstatus);
        ui_choice(a->rnd, a->th, a->modal_title, items, t->nstatus, footer, frame);
        break;
    }

    case MODAL_NONE: break;
    }

    prof_overlay_draw(a->rnd);
}
