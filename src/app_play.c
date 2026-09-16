#include "app_priv.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------- play mode */

/* Esc is a cancel, so the walk is taken back out of the history rather than
 * answered with a step back to where it began: undo it and there is nothing
 * there, which is what a cancel means. Only the moves are unwound, so a
 * marker added part way through the walk survives it.
 *
 * The square it set out from is by definition free -- it is the one square
 * nothing else can have moved onto, since the held creature is the only one
 * moving -- so a cancel works from on top of an ally, where a drop does not. */
static void play_cancel_move(App *a)
{
    Play *pl = &a->play;

    int back = undo_rewind_moves(&a->undo, a->map, pl->grab_depth,
                                 pl->group, pl->ngroup);
    play_trail_sync(pl, a->map);

    pl->grabbed = 0;
    pl->ntrail  = 0;
    app_follow_selection(a);

    char at[MAP_COORD_MAX];
    map_coord_name(pl->origin_x, pl->origin_y, at, sizeof at);

    char msg[96];
    if (back) snprintf(msg, sizeof msg, "cancelled - back to %s", at);
    else      snprintf(msg, sizeof msg, "put down at %s", at);
    app_set_status(a, msg);
}

/* Putting a creature down is the strict half of the rule: it steps through
 * its own side on the way past, but two of them cannot come to rest on the
 * same square. Returns 1 when it was put down. */
static int play_put_down(App *a, const char *how)
{
    Play  *pl = &a->play;
    Map   *m  = a->map;

    /* Every creature in hand has to have somewhere to land, and they are
     * transparent to each other: a formation is put down as it stands. */
    if (pl->enforce_walls) {
        for (int i = 0; i < pl->ngroup; i++) {
            int idx = pl->group[i];
            if (idx < 0 || idx >= m->tokens.n) continue;

            const Token *t = &m->tokens.v[idx];
            int on = tokens_overlapping_set(&m->tokens, t->x, t->y, t->size,
                                            pl->group, pl->ngroup,
                                            TOKEN_ANY_KIND);
            if (on >= 0) {
                const Token *u = &m->tokens.v[on];
                char msg[112];
                snprintf(msg, sizeof msg, "%.24s is on this square - move off to put down",
                         u->label[0] ? u->label : token_kind_name(u->kind));
                app_set_status(a, msg);
                return 0;
            }
        }
    }

    pl->grabbed = 0;
    pl->ntrail  = 0;
    app_set_status(a, how);
    return 1;
}

/* Which creature the cursor is currently offering, when it covers more than
 * one. Says how to take it and how to see the next, since the choice is only
 * discoverable once you know enter does not drop while it is being made. */
static void report_choice(App *a)
{
    Play *pl = &a->play;
    if (pl->sel < 0 || pl->sel >= a->map->tokens.n) return;

    const Token *t = &a->map->tokens.v[pl->sel];
    char at[MAP_COORD_MAX];
    map_coord_name(t->x, t->y, at, sizeof at);

    char msg[128];
    snprintf(msg, sizeof msg, "%.24s at %s - enter for the next, move to take it",
             t->label[0] ? t->label : token_kind_name(t->kind), at);
    app_set_status(a, msg);
}

/* One of the three cycle keys. `kind` picks the track and the shifted key
 * runs it backwards. */
static void cycle_track(App *a, int kind, int delta)
{
    if (!play_cycle(&a->play, a->map, delta, kind)) {
        const char *what = kind == TOKEN_PLAYER ? "players"
                         : kind == TOKEN_ENEMY  ? "enemies"
                                                : "tokens";
        char msg[64];
        snprintf(msg, sizeof msg, "no %s on the map", what);
        app_set_status(a, msg);
        return;
    }
    app_follow_selection(a);
    app_report_selection(a);
}

/* What y and d act on: the box while one is open, the selection when there is
 * one, and otherwise whatever the cursor is over. Returns how many there are
 * even when that exceeds `max`, so "too many" can be told from "full". */
static int play_action_group(App *a, int *out, int max)
{
    Play *pl = &a->play;

    if (pl->visual)
        return play_box_tokens(a->map, pl->anchor_x, pl->anchor_y,
                               a->ed.cx, a->ed.cy, out, max);

    if (pl->ngroup > 0) {
        int n = pl->ngroup < max ? pl->ngroup : max;
        for (int i = 0; i < n; i++) out[i] = pl->group[i];
        return pl->ngroup;
    }

    int idx = app_token_under_cursor(a);
    if (idx < 0) return 0;
    if (max > 0) out[0] = idx;
    return 1;
}

/* Fills the yank buffer. Positions are kept as they stand; paste reads the
 * offsets back off the group's own bounding box, so a formation is stamped
 * out in the shape it was copied in. */
static void yank_group(App *a, const int *idx, int n)
{
    Play *pl = &a->play;
    if (n > PLAY_GROUP_MAX) n = PLAY_GROUP_MAX;

    for (int i = 0; i < n; i++) pl->yank[i] = a->map->tokens.v[idx[i]];
    pl->nyank = n;
}

/* Names a set for the status line: the one label when there is one creature,
 * a count when there are more. */
static void group_name(const Map *m, const int *idx, int n, char *buf, size_t bufsz)
{
    if (n == 1) {
        const Token *t = &m->tokens.v[idx[0]];
        snprintf(buf, bufsz, "%.30s", t->label[0] ? t->label
                                                  : token_kind_name(t->kind));
        return;
    }
    snprintf(buf, bufsz, "%d creatures", n);
}

/* The creature a command acts on: the selection when there is one, otherwise
 * whatever the cursor is over. */
static int play_target_token(App *a)
{
    Play *pl = &a->play;
    if (pl->sel >= 0 && pl->sel < a->map->tokens.n) return pl->sel;
    return app_token_under_cursor(a);
}

static void place_token(App *a, uint8_t kind)
{
    Editor *e = &a->ed;
    Play   *pl = &a->play;

    if (!play_can_place(a->map, e->cx, e->cy, pl->next_size, -1)) {
        char msg[96];
        snprintf(msg, sizeof msg, "no room for a %dx%d here%s",
                 pl->next_size, pl->next_size,
                 tokens_overlapping(&a->map->tokens, e->cx, e->cy,
                                    pl->next_size, -1, TOKEN_ANY_KIND) >= 0
                     ? " - something is on it" : "");
        app_set_status(a, msg);
        return;
    }

    a->pending_kind = kind;
    a->pending_size = pl->next_size;
    a->pending_tx   = e->cx;
    a->pending_ty   = e->cy;

    char title[64];
    snprintf(title, sizeof title, "Label for %s token (%dx%d)",
             token_kind_name(kind), pl->next_size, pl->next_size);
    app_open_prompt(a, PROMPT_TOKEN_LABEL, title, "a blank label is fine", "");
}

/* ------------------------------------------------------- s, status markers */

static void status_add(App *a)
{
    Play *pl = &a->play;
    int idx = play_target_token(a);
    if (idx < 0) { app_set_status(a, "no token here to mark"); return; }

    const Token *t = &a->map->tokens.v[idx];
    a->pending_token = idx;

    char title[64];
    snprintf(title, sizeof title, "%s marker on %.20s",
             status_color_name(pl->status_color),
             t->label[0] ? t->label : token_kind_name(t->kind));
    app_open_prompt(a, PROMPT_STATUS_LABEL, title, "s c changes the colour", "");
}

static void status_colour(App *a)
{
    Play *pl = &a->play;
    pl->status_color = (uint8_t)((pl->status_color + 1) % STATUS_COLOR_COUNT);

    char msg[64];
    snprintf(msg, sizeof msg, "next marker: %s", status_color_name(pl->status_color));
    app_set_status(a, msg);
}

static void status_drop(App *a)
{
    Map *m = a->map;
    int idx = play_target_token(a);
    if (idx < 0) { app_set_status(a, "no token here"); return; }

    const Token *t = &m->tokens.v[idx];
    if (!t->nstatus) { app_set_status(a, "no markers to clear"); return; }

    /* One marker needs no question -- a chooser with a single row is a
     * keystroke that asks nothing. Two or more, and the GM has to be able to
     * say which condition ended. */
    if (t->nstatus == 1) { app_clear_token_status(a, idx, 0); return; }

    a->pending_token = idx;
    snprintf(a->modal_title, sizeof a->modal_title, "Clear marker on %.24s",
             t->label[0] ? t->label : token_kind_name(t->kind));
    a->modal = MODAL_CLEAR_STATUS;
}

/* ------------------------------------------------------------- prefixes */

/* A prefix swallows whatever comes next: a half-typed command must never turn
 * into a different whole one. Both prefixes announce their options in the
 * status line, which is how they stay discoverable without the bar growing. */
static int pending_key(App *a, Key k)
{
    uint32_t pre = a->pending;
    if (!pre) return 0;
    a->pending = 0;

    if (k.kind != KEY_CHAR || k.mods != 0) {
        app_set_status(a, k.kind == KEY_ESC ? "cancelled" : "");
        return 1;
    }

    if (pre == 'i') {
        if (k.ch == 'p') { place_token(a, TOKEN_PLAYER); return 1; }
        if (k.ch == 'e') { place_token(a, TOKEN_ENEMY);  return 1; }
        app_set_status(a, "i wants p for a player or e for an enemy");
        return 1;
    }

    if (pre == 's') {
        if (k.ch == 'a') { status_add(a);    return 1; }
        if (k.ch == 'c') { status_colour(a); return 1; }
        if (k.ch == 'd') { status_drop(a);   return 1; }
        app_set_status(a, "s wants a to add, c for colour, d to drop");
        return 1;
    }
    return 1;
}

/* The keys the vim-shaped scheme retired. Each is unbound now, so a week of
 * muscle memory can fail loudly instead of silently. */
static const char *retired_key(uint32_t ch)
{
    switch (ch) {
    case 'a': case 'A': return "a is gone - t and T walk every token";
    case 'V':           return "V is now v - select several creatures";
    case 'P':           return "P is now p - paste";
    case 'R':           return "R is now r - the range highlight";
    case 'S':           return "S is now s c - marker colour";
    default:            return NULL;
    }
}

void app_play_key(App *a, Key k)
{
    Editor *e  = &a->ed;
    Play   *pl = &a->play;
    Map    *m  = a->map;
    if (!m) { a->screen = SCREEN_MENU; return; }

    if (e->mode == ED_COMMAND) { app_command_key(a, k); return; }
    if (app_ruler_key(a, k))       { return; }
    if (pending_key(a, k))     { return; }

    /* Movement drives the grabbed token when there is one, and the cursor
     * otherwise. */
    int dx = 0, dy = 0;
    if (k.kind == KEY_LEFT)  dx = -1;
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
        int times = take_count(e);
        if (pl->grabbed) {
            /* A movement key is what settles which creature was meant. The
             * cursor goes to it and takes its size before the first step, so
             * the move is made from where it will actually happen. */
            int settling = pl->choosing;
            pl->choosing = 0;

            /* One keypress is one undo step, count and all. Without the batch
             * the steps were pushed with no mark closing them, so they were
             * not merely un-undoable: the next u reached straight past them
             * to the batch underneath and took the whole token off the map. */
            undo_begin(&a->undo);
            int moved = 0;
            for (int i = 0; i < times; i++) {
                if (!play_step(m, &a->undo, pl, dx, dy)) break;
                moved++;
            }
            undo_end(&a->undo);
            /* Being blocked is the more urgent news, so it wins; otherwise
             * the offer to walk the crowd has to go, since it stopped being
             * true the moment the choice was settled. */
            if (moved < times)
                app_set_status(a, pl->enforce_walls ? "blocked" : "edge of the map");
            else if (settling)
                app_set_status(a, "picked up - enter drops, esc cancels");
            app_follow_selection(a);
        } else {
            ed_move(e, m, dx, dy, times);
        }
        return;
    }

    /* Esc backs out of one thing at a time, innermost first: put the creature
     * down, then take the overlay off, then let go of the creature. */
    if (k.kind == KEY_ESC) {
        /* Outermost thing first, as everywhere else: the box is the most
         * recent thing opened, so it is the first thing esc takes back. */
        if (pl->visual) {
            pl->visual = 0;
            app_set_status(a, "");
            e->count = 0;
            return;
        }
        if (pl->grabbed && pl->choosing) {
            /* Nothing has moved and the cursor never left, so there is no
             * move to cancel -- only a choice to stop making. */
            play_focus(pl, -1);
            app_set_status(a, "");
        } else if (pl->grabbed) {
            play_cancel_move(a);
        } else if (pl->range.active) {
            range_clear(&pl->range);
            app_set_status(a, "range overlay off");
        } else {
            play_focus(pl, -1);
            app_set_status(a, "");
        }
        e->count = 0;
        return;
    }

    if (k.kind == KEY_TAB) {
        cycle_track(a, PLAY_ANY_KIND, (k.mods & MOD_SHIFT) ? -1 : 1);
        return;
    }

    if (k.kind == KEY_ENTER) {
        /* Closing the box picks up everything in it. One creature in the box
         * is an ordinary pickup, so there is no separate case to learn. */
        if (pl->visual) {
            int idx[PLAY_GROUP_MAX];
            int n = play_box_tokens(m, pl->anchor_x, pl->anchor_y, e->cx, e->cy,
                                    idx, PLAY_GROUP_MAX);
            if (n <= 0) { app_set_status(a, "nothing in the box"); return; }
            if (n > PLAY_GROUP_MAX) {
                app_set_status(a, "too many creatures in the box to carry at once");
                return;
            }

            play_focus_group(pl, idx, n);
            play_grab(pl, m, a->undo.depth);
            pl->visual = 0;
            app_follow_selection(a);

            char msg[96];
            snprintf(msg, sizeof msg, n == 1
                        ? "picked up - enter drops, esc cancels"
                        : "carrying %d - they move together, enter drops", n);
            app_set_status(a, msg);
            return;
        }

        int csize = play_cursor_size(pl, m);

        /* Still choosing: enter walks the creatures the cursor covers rather
         * than dropping one, because nothing has been committed to yet. */
        if (pl->grabbed && pl->choosing) {
            int next = tokens_covered_next(&m->tokens, e->cx, e->cy, csize,
                                           pl->sel);
            if (next >= 0 && next != pl->sel) {
                play_focus(pl, next);
                play_grab(pl, m, a->undo.depth);
                pl->choosing = 1;
            }
            report_choice(a);
            return;
        }

        if (pl->grabbed) {
            char msg[96];
            snprintf(msg, sizeof msg, "dropped after %d step%s",
                     pl->steps, pl->steps == 1 ? "" : "s");
            play_put_down(a, msg);
            return;
        }

        int first = tokens_covered_next(&m->tokens, e->cx, e->cy, csize, -1);
        if (first < 0) { app_set_status(a, "no token here"); return; }

        play_focus(pl, first);
        play_grab(pl, m, a->undo.depth);

        /* One candidate is not a choice, so a plain cursor over a plain
         * creature behaves exactly as it always did: picked up, cursor on it.
         * Two or more and the cursor holds still until a movement key says
         * which was meant. */
        if (tokens_covered_next(&m->tokens, e->cx, e->cy, csize, first) != first) {
            pl->choosing = 1;
            report_choice(a);
            return;
        }

        app_follow_selection(a);
        app_set_status(a, "picked up - enter drops, esc cancels");
        return;
    }

    if (k.kind == KEY_CHAR && (k.mods & MOD_CTRL)) {
        if (k.ch == 'r') {
            if (undo_redo(&a->undo, m)) { play_trail_sync(pl, m); app_set_status(a, "redo"); }
            return;
        }
        if (k.ch == 'w') {
            pl->enforce_walls = !pl->enforce_walls;
            app_set_status(a, pl->enforce_walls ? "walls enforced"
                                                : "walls ignored - place freely");
            return;
        }
        return;
    }

    if (k.kind != KEY_CHAR || k.mods != 0) return;

    /* Counts, the same as build mode: 3l walks three squares whichever mode
     * you are in. This is what freed 1 2 3 from being size keys -- the size
     * lives on b now, in both modes, and a count names it (2b). */
    if (k.ch >= '1' && k.ch <= '9') { count_digit(e, k.ch); return; }
    if (k.ch == '0' && e->count)    { e->count *= 10; return; }

    if (k.ch == 'b' || k.ch == 'B') {
        /* One number behind all of it: the size key sets it, the cursor shows
         * it, and a selected creature is resized to it. Being held is not a
         * reason to refuse -- realising a creature is Large is something that
         * happens mid-move as often as not. Cycling starts from the creature
         * being looked at when there is one, so b on a selected creature
         * always means "the next size up from what it is". */
        int idx = (pl->sel >= 0 && pl->sel < m->tokens.n) ? pl->sel : -1;

        uint8_t base = idx >= 0 ? m->tokens.v[idx].size : pl->next_size;
        uint8_t size = size_key(take_count_raw(e), base, k.ch == 'b' ? 1 : -1);

        if (idx >= 0 && m->tokens.v[idx].size != size) {
            Token t = m->tokens.v[idx];
            /* The setting is left alone when the resize is refused, so the
             * cursor never grows past a creature that did not. */
            if (!play_can_place(m, t.x, t.y, size, idx)) {
                app_set_status(a, "not enough room to grow this token");
                return;
            }
            t.size = size;
            undo_begin(&a->undo);
            undo_edit_token(&a->undo, m, idx, t);
            undo_end(&a->undo);
        }

        pl->next_size = size;

        char msg[64];
        if (idx >= 0) snprintf(msg, sizeof msg, "%dx%d", size, size);
        else          snprintf(msg, sizeof msg, "next token will be %dx%d", size, size);
        app_set_status(a, msg);
        return;
    }

    /* Everything the remap left behind, named rather than ignored. */
    {
        const char *moved = retired_key(k.ch);
        if (moved) { app_set_status(a, moved); return; }
    }

    switch (k.ch) {
    /* Three tracks, because a GM running a fight wants the next of their own
     * creatures far more often than the next of anything. Shift reverses. */
    case 't': cycle_track(a, PLAY_ANY_KIND,  1); break;
    case 'T': cycle_track(a, PLAY_ANY_KIND, -1); break;
    case 'f': cycle_track(a, TOKEN_PLAYER,   1); break;
    case 'F': cycle_track(a, TOKEN_PLAYER,  -1); break;
    case 'e': cycle_track(a, TOKEN_ENEMY,    1); break;
    case 'E': cycle_track(a, TOKEN_ENEMY,   -1); break;

    /* i inserts, so p is free to mean what it means everywhere else. */
    case 'i':
        a->pending = 'i';
        app_set_status(a, "i    p player    e enemy");
        break;

    case 's':
        a->pending = 's';
        app_set_status(a, "s    a add marker    c colour    d drop");
        break;

    case '/': {
        /* Opens empty rather than pre-filled with the last search: a prompt
         * you have to clear before you can type is worse than one you have to
         * retype, and a blank line repeats the last search anyway. */
        char hint[80];
        if (pl->search[0])
            snprintf(hint, sizeof hint, "part of a label, any case - blank repeats \"%.20s\"",
                     pl->search);
        else
            str_lcpy(hint, "part of a label, any case", sizeof hint);

        app_open_prompt(a, PROMPT_TOKEN_SEARCH, "Find token", hint, "");
        break;
    }

    case 'n': case 'N': {
        if (!pl->search[0]) { app_set_status(a, "nothing searched for yet - / finds a token"); break; }
        if (!play_find(pl, m, NULL, k.ch == 'n' ? 1 : -1)) {
            char msg[80];
            snprintf(msg, sizeof msg, "no token matching \"%.30s\"", pl->search);
            app_set_status(a, msg);
            break;
        }
        app_follow_selection(a);
        app_report_selection(a);
        break;
    }

    case 'm': app_ruler_begin(a); break;
    case 'v': {
        /* The same key twice closes the box, the way it does in build mode.
         * A creature already selected is not carried into it: the box says
         * what it covers, and inheriting an off-screen selection would make
         * it say something else. */
        if (pl->visual) {
            pl->visual = 0;
            app_set_status(a, "");
            break;
        }
        pl->visual   = 1;
        pl->anchor_x = e->cx;
        pl->anchor_y = e->cy;
        play_focus(pl, -1);
        app_set_status(a, "VISUAL - move to cover creatures, enter carries, y d act");
        break;
    }

    case 'y': {
        int idx[PLAY_GROUP_MAX];
        int n = play_action_group(a, idx, PLAY_GROUP_MAX);
        if (n <= 0) { app_set_status(a, "no token here to yank"); break; }
        if (n > PLAY_GROUP_MAX) {
            app_set_status(a, "too many creatures in the box to yank at once");
            break;
        }

        yank_group(a, idx, n);

        char what[48];
        group_name(m, idx, n, what, sizeof what);
        char msg[80];
        snprintf(msg, sizeof msg, "yanked %s - p pastes", what);

        pl->visual = 0;
        app_set_status(a, msg);
        break;
    }

    case 'p': {
        if (pl->nyank <= 0) {
            app_set_status(a, "nothing yanked yet - y copies a token");
            break;
        }

        /* Offsets from the group's own bounding box, so the cursor lands the
         * formation's top-left corner and the shape survives the trip. */
        int minx = pl->yank[0].x, miny = pl->yank[0].y;
        for (int i = 1; i < pl->nyank; i++) {
            if (pl->yank[i].x < minx) minx = pl->yank[i].x;
            if (pl->yank[i].y < miny) miny = pl->yank[i].y;
        }

        /* Checked to the last creature before a single one lands. A paste
         * that half-arrives leaves the GM reconstructing which half, which is
         * worse than one that refuses and says why. The copies cannot collide
         * with each other, since they did not collide where they came from. */
        for (int i = 0; i < pl->nyank; i++) {
            int tx = e->cx + (pl->yank[i].x - minx);
            int ty = e->cy + (pl->yank[i].y - miny);
            if (play_can_place(m, tx, ty, pl->yank[i].size, -1)) continue;

            int on = tokens_overlapping(&m->tokens, tx, ty, pl->yank[i].size,
                                        -1, TOKEN_ANY_KIND);
            app_set_status(a, pl->nyank == 1
                                ? (on >= 0 ? "something is already here"
                                           : "the copy does not fit here")
                                : (on >= 0 ? "something is already in the way"
                                           : "the whole formation does not fit here"));
            goto paste_done;
        }

        undo_begin(&a->undo);
        int pasted = -1;
        for (int i = 0; i < pl->nyank; i++) {
            Token t = pl->yank[i];
            t.x = (int16_t)(e->cx + (pl->yank[i].x - minx));
            t.y = (int16_t)(e->cy + (pl->yank[i].y - miny));
            tokens_unique_label(&m->tokens, pl->yank[i].label, t.label, sizeof t.label);

            /* A pasted creature arrives fresh. Markers are what is happening
             * to a particular creature right now, not part of what it is, so
             * stamping out five goblins should not give five poisoned ones. */
            token_clear_status(&t);
            pasted = undo_add_token(&a->undo, m, t);
        }
        undo_end(&a->undo);

        /* The copies are the new selection, so a formation can be stamped
         * down and walked straight off without re-boxing it. */
        {
            int idx[PLAY_GROUP_MAX];
            for (int i = 0; i < pl->nyank; i++)
                idx[i] = pasted - (pl->nyank - 1) + i;
            play_focus_group(pl, idx, pl->nyank);
        }

        char at[MAP_COORD_MAX];
        map_coord_name(e->cx, e->cy, at, sizeof at);
        char msg[96];
        if (pl->nyank == 1)
            snprintf(msg, sizeof msg, "pasted %.30s at %s",
                     m->tokens.v[pasted].label[0] ? m->tokens.v[pasted].label
                                                  : token_kind_name(m->tokens.v[pasted].kind),
                     at);
        else
            snprintf(msg, sizeof msg, "pasted %d creatures at %s", pl->nyank, at);
        app_set_status(a, msg);
    paste_done:
        break;
    }

    case 'o': case 'O': {
        int secret = (k.ch == 'O');
        int n = ed_toggle_doors(e, m, &a->undo, secret);
        char msg[80];
        if (n) snprintf(msg, sizeof msg, "%s %d %s%s", secret ? "revealed" : "toggled",
                        n, secret ? "secret door" : "door", n == 1 ? "" : "s");
        else   snprintf(msg, sizeof msg, "no %s on this tile",
                        secret ? "secret doors" : "doors");
        app_set_status(a, msg);
        break;
    }

    case 'r': {
        /* Anchored to the selection when there is one, so the highlight
         * follows that creature as it moves. */
        int anchor = (pl->sel >= 0 && pl->sel < m->tokens.n) ? pl->sel : -1;
        if (anchor < 0) anchor = app_token_under_cursor(a);

        int band = range_cycle(&pl->range, m, anchor, e->cx, e->cy,
                               take_count_raw(e));
        if (band < 0) app_set_status(a, "range overlay off");
        else                               a->status[0] = '\0';
        break;
    }

    case 'd': case 'x': {
        int idx[PLAY_GROUP_MAX];
        int n = play_action_group(a, idx, PLAY_GROUP_MAX);
        if (n <= 0) { app_set_status(a, "no token here"); break; }
        if (n > PLAY_GROUP_MAX) {
            app_set_status(a, "too many creatures in the box to remove at once");
            break;
        }

        /* A delete fills the yank buffer, the way vim's d does, so removing a
         * creature and putting it somewhere else is d then p. Its name comes
         * back with it: the label is free again once the token is gone, so
         * the copy keeps it rather than counting up. */
        char what[48];
        group_name(m, idx, n, what, sizeof what);
        yank_group(a, idx, n);

        /* Highest index first: the list is an array, so removing a low index
         * would shift every one still to go out from under the loop. */
        undo_begin(&a->undo);
        for (int i = n - 1; i >= 0; i--) {
            int rx = m->tokens.v[idx[i]].x, ry = m->tokens.v[idx[i]].y;
            undo_del_token(&a->undo, m, idx[i]);
            range_token_removed(&pl->range, idx[i], rx, ry);
        }
        undo_end(&a->undo);

        play_focus(pl, -1);
        pl->visual = 0;

        char msg[80];
        snprintf(msg, sizeof msg, "removed %s - p puts %s back",
                 what, n == 1 ? "it" : "them");
        app_set_status(a, msg);
        break;
    }

    case 'c': {
        if (pl->sel < 0 || pl->sel >= m->tokens.n) {
            play_select_at(pl, m, e->cx, e->cy, play_cursor_size(pl, m));
            if (pl->sel < 0) { app_set_status(a, "no token here"); break; }
        }
        app_open_prompt(a, PROMPT_RELABEL, "Change label", "",
                    m->tokens.v[pl->sel].label);
        break;
    }

    case 'u':
        if (undo_undo(&a->undo, m)) {
            /* The history can add or remove tokens, which shifts every later
             * index; anything still pointing into the list stops following a
             * particular creature rather than following the wrong one. */
            if (pl->sel >= m->tokens.n) play_focus(pl, -1);
            play_trail_sync(pl, m);
            if (pl->range.token >= 0) {
                int ax, ay, as;
                range_anchor(&pl->range, m, &ax, &ay, &as);
                pl->range.token = -1;
                pl->range.ax = ax;
                pl->range.ay = ay;
            }
            app_set_status(a, "undo");
        } else {
            app_set_status(a, "nothing to undo");
        }
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
