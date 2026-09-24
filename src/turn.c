#include "turn.h"

#include "counter.h"
#include "fog.h"

#include <stdio.h>
#include <string.h>

#include "prof.h"
#include "ruler.h"
#include "util.h"

/* Where a creature comes in the walk. The order is never stored or sorted:
 * a key per token and "the smallest key greater than this one" is all that
 * next-turn, the status line and the cycle keys ever ask, and it is a single
 * pass over the tokens with nothing allocated.
 *
 * In the order: highest init first, then list position. Out of it: after
 * everyone who is in, by list position. */
static int64_t walk_key(const Token *t, int idx)
{
    if (t->turn & TURN_IN) return ((int64_t)(INT16_MAX - t->init) << 32) | (int64_t)idx;
    return ((int64_t)1 << 48) | (int64_t)idx;
}

/* Set for the length of a call drawing words for the players' frame over
 * fog: a creature the party cannot see is named "?", so the title bar and
 * the panel say someone is acting in the dark without saying who. */
static const Map *g_mask;

static const char *name_of(const Token *t)
{
    if (g_mask && fog_token_hidden(g_mask, t)) return "?";
    return t->label[0] ? t->label : token_kind_name(t->kind);
}

/* A row in its creature's side colour -- but a creature the players' frame
 * names "?" is not coloured by side either, or the colour would say what
 * the name does not. */
static Style side_style(const Token *t, const Theme *th)
{
    if (g_mask && fog_token_hidden(g_mask, t)) return style(th->dim, th->bg, 0);
    return style(t->kind == TOKEN_ENEMY ? th->enemy : th->player, th->bg, 0);
}

/* The neighbour of `from` in key order among tokens that pass the filter,
 * without wrapping: -1 when `from` is already the last (or first). A `from`
 * of -1 gives the first (dir > 0) or the last. */
static int neighbour(const Map *m, int from, int dir, int kind, int only_in)
{
    int     have = from >= 0 && from < m->tokens.n;
    int64_t at   = have ? walk_key(&m->tokens.v[from], from) : 0;

    int     best = -1;
    int64_t bkey = 0;
    for (int i = 0; i < m->tokens.n; i++) {
        const Token *t = &m->tokens.v[i];
        if (only_in && !(t->turn & TURN_IN)) continue;
        if (kind != TOKEN_ANY_KIND && t->kind != kind) continue;

        int64_t k = walk_key(t, i);
        if (have && (dir > 0 ? k <= at : k >= at)) continue;
        if (best < 0 || (dir > 0 ? k < bkey : k > bkey)) { best = i; bkey = k; }
    }
    return best;
}

int turn_count(const Map *m)
{
    int n = 0;
    for (int i = 0; i < m->tokens.n; i++)
        if (m->tokens.v[i].turn & TURN_IN) n++;
    return n;
}

int turn_acting(const Map *m)
{
    for (int i = 0; i < m->tokens.n; i++)
        if (m->tokens.v[i].turn & TURN_ACTING) return i;
    return -1;
}

int turn_walk(const Map *m, int from, int dir, int kind)
{
    if (dir == 0) dir = 1;
    int next = neighbour(m, from, dir, kind, 0);
    if (next < 0) next = neighbour(m, -1, dir, kind, 0);    /* wrap */
    return next;
}

static void set_flags(Map *m, Undo *u, int idx, uint8_t set, uint8_t clear)
{
    if (idx < 0 || idx >= m->tokens.n) return;
    Token t = m->tokens.v[idx];
    t.turn = (uint8_t)((t.turn | set) & ~clear);
    undo_edit_token(u, m, idx, t);
}

/* Hands the turn from whoever has it to `to` (-1 for nobody). */
static void pass_turn(Map *m, Undo *u, int to)
{
    int cur = turn_acting(m);
    if (cur == to) return;
    set_flags(m, u, cur, 0, TURN_ACTING);
    set_flags(m, u, to, TURN_ACTING, 0);
}

int turn_advance(Map *m, Undo *u, int delta)
{
    PROF_ZONE("turn.advance");
    if (turn_count(m) == 0) return TURN_NO_ORDER;
    if (delta == 0) return turn_acting(m);

    int dir   = delta > 0 ? 1 : -1;
    int steps = delta > 0 ? delta : -delta;
    int cur   = turn_acting(m);
    int round = m->round;

    /* Worked out in full before anything is touched, so a step back that
     * runs into the start of the fight changes nothing at all. */
    for (int s = 0; s < steps; s++) {
        int in = cur >= 0 && (m->tokens.v[cur].turn & TURN_IN);
        if (dir > 0) {
            int next = in ? neighbour(m, cur, 1, TOKEN_ANY_KIND, 1) : -1;
            if (next < 0) {
                /* Off the end, or nobody in the order had the turn: the top
                 * of the order, and a new round if this was a lap. */
                next  = neighbour(m, -1, 1, TOKEN_ANY_KIND, 1);
                round = in ? round + 1 : imax(round, 1);
            }
            cur = next;
        } else {
            if (!in) return TURN_AT_START;
            int prev = neighbour(m, cur, -1, TOKEN_ANY_KIND, 1);
            if (prev < 0) {
                if (round <= 1) return TURN_AT_START;
                prev = neighbour(m, -1, -1, TOKEN_ANY_KIND, 1);
                round--;
            }
            cur = prev;
        }
    }

    undo_begin(u);
    pass_turn(m, u, cur);
    undo_set_round(u, m, imax(round, 1));
    undo_end(u);
    return cur;
}

int turn_spotlight_ruleset(const Map *m)
{
    const Ruleset *rs = ruleset_by_name(m->ruleset);
    return rs && rs->spotlight;
}

void turn_flip_spotlight(Map *m, Undo *u)
{
    undo_begin(u);
    pass_turn(m, u, -1);
    undo_set_spotlight(u, m, m->spotlight == SPOTLIGHT_GM ? SPOTLIGHT_PLAYERS : SPOTLIGHT_GM);
    undo_end(u);
}

void turn_join(Map *m, Undo *u, int idx, int init)
{
    if (idx < 0 || idx >= m->tokens.n) return;
    Token t = m->tokens.v[idx];
    t.turn |= TURN_IN;
    t.init  = (int16_t)iclamp(init, -999, 999);

    undo_begin(u);
    undo_edit_token(u, m, idx, t);
    /* Joining while holding the turn puts the fight on the clock. */
    if ((t.turn & TURN_ACTING) && m->round == 0) undo_set_round(u, m, 1);
    undo_end(u);
}

void turn_leave(Map *m, Undo *u, int idx)
{
    if (idx < 0 || idx >= m->tokens.n) return;
    if (!(m->tokens.v[idx].turn & TURN_IN)) return;

    undo_begin(u);
    turn_before_remove(m, u, idx);
    set_flags(m, u, idx, 0, TURN_IN | TURN_ACTING);
    turn_settle(m, u);
    undo_end(u);
}

void turn_take(Map *m, Undo *u, int idx)
{
    if (idx < 0 || idx >= m->tokens.n) return;

    undo_begin(u);
    pass_turn(m, u, idx);
    if ((m->tokens.v[idx].turn & TURN_IN) && m->round == 0) undo_set_round(u, m, 1);
    /* The side follows the creature: an enemy's turn is the GM's spotlight. */
    undo_set_spotlight(u, m, m->tokens.v[idx].kind == TOKEN_ENEMY ? SPOTLIGHT_GM : SPOTLIGHT_PLAYERS);
    undo_end(u);
}

int turn_clear(Map *m, Undo *u)
{
    int had = turn_count(m);

    undo_begin(u);
    for (int i = 0; i < m->tokens.n; i++)
        if (m->tokens.v[i].turn) set_flags(m, u, i, 0, TURN_IN | TURN_ACTING);
    undo_set_round(u, m, 0);
    undo_set_spotlight(u, m, SPOTLIGHT_PLAYERS);
    undo_end(u);
    return had;
}

void turn_before_remove(Map *m, Undo *u, int idx)
{
    if (idx < 0 || idx >= m->tokens.n) return;
    const Token *t = &m->tokens.v[idx];
    if (!(t->turn & TURN_ACTING)) return;

    /* The turn goes to whoever is next. Alone in the order, or holding the
     * turn from outside it, there is nobody to pass it to. */
    if ((t->turn & TURN_IN) && turn_count(m) > 1) turn_advance(m, u, 1);
    else                                          set_flags(m, u, idx, 0, TURN_ACTING);
}

void turn_settle(Map *m, Undo *u)
{
    if (m->round != 0 && turn_count(m) == 0) undo_set_round(u, m, 0);
}

void turn_sanitize(Map *m)
{
    int seen = 0;
    for (int i = 0; i < m->tokens.n; i++) {
        Token *t = &m->tokens.v[i];
        t->turn &= (uint8_t)(TURN_IN | TURN_ACTING);
        if (!(t->turn & TURN_ACTING)) continue;
        if (seen) t->turn &= (uint8_t)~TURN_ACTING;
        seen = 1;
    }
    if (m->round < 0) m->round = 0;
    if (turn_count(m) == 0) m->round = 0;
    m->spotlight = m->spotlight ? SPOTLIGHT_GM : SPOTLIGHT_PLAYERS;
}

static const char *side_name(const Map *m)
{
    return m->spotlight == SPOTLIGHT_GM ? "GM spotlight" : "Players' spotlight";
}

void turn_status(const Map *m, char *buf, size_t bufsz)
{
    turn_status_view(m, 0, buf, bufsz);
}

static void turn_status_body(const Map *m, char *buf, size_t bufsz);

void turn_status_view(const Map *m, int players, char *buf, size_t bufsz)
{
    g_mask = players && fog_any(m) ? m : NULL;
    turn_status_body(m, buf, bufsz);
    g_mask = NULL;
}

static void turn_status_body(const Map *m, char *buf, size_t bufsz)
{
    PROF_ZONE("turn.status");
    buf[0] = '\0';

    int cur = turn_acting(m);
    int n   = turn_count(m);

    /* No initiative in this game: the turn is a side, and a creature only
     * when one was handed it by name. */
    if (n == 0 && turn_spotlight_ruleset(m)) {
        if (cur >= 0) snprintf(buf, bufsz, "%s - %.20s", side_name(m), name_of(&m->tokens.v[cur]));
        else          snprintf(buf, bufsz, "%s", side_name(m));
        return;
    }
    if (cur < 0 && n == 0) return;

    int off = 0;
    if (m->round > 0) off = snprintf(buf, bufsz, "Round %d - ", m->round);
    if (cur < 0) {
        snprintf(buf + off, bufsz - (size_t)off, "%d in the order, a starts", n);
        return;
    }
    off += snprintf(buf + off, bufsz - (size_t)off, "%.20s's turn", name_of(&m->tokens.v[cur]));

    /* Who is up next: two names is what fits, and what a GM calls out. */
    if (!(m->tokens.v[cur].turn & TURN_IN) || n < 2) return;
    int next = cur;
    for (int i = 0; i < 2 && i < n - 1 && (size_t)off + 24 < bufsz; i++) {
        int after = neighbour(m, next, 1, TOKEN_ANY_KIND, 1);
        if (after < 0) after = neighbour(m, -1, 1, TOKEN_ANY_KIND, 1);
        if (after < 0 || after == cur) break;
        next = after;
        off += snprintf(buf + off, bufsz - (size_t)off, "%s%.16s",
                        i ? ", " : ", then ", name_of(&m->tokens.v[next]));
    }
}

void turn_list(const Map *m, char *buf, size_t bufsz)
{
    int n = turn_count(m);
    if (n == 0) {
        int cur = turn_acting(m);
        if (cur >= 0) snprintf(buf, bufsz, "no turn order - %.20s holds the turn", name_of(&m->tokens.v[cur]));
        else          snprintf(buf, bufsz, "no turn order - s i gives the selected creature a place in it");
        return;
    }

    int off = m->round > 0 ? snprintf(buf, bufsz, "Round %d: ", m->round)
                           : snprintf(buf, bufsz, "Order: ");
    int at = -1;
    for (int i = 0; i < n && (size_t)off + 28 < bufsz; i++) {
        at = neighbour(m, at, 1, TOKEN_ANY_KIND, 1);
        if (at < 0) break;
        const Token *t = &m->tokens.v[at];
        off += snprintf(buf + off, bufsz - (size_t)off, "%s%.16s %d%s",
                        i ? ", " : "", name_of(t), t->init,
                        (t->turn & TURN_ACTING) ? "*" : "");
    }
    if (at >= 0 && neighbour(m, at, 1, TOKEN_ANY_KIND, 1) >= 0 && (size_t)off + 5 < bufsz)
        snprintf(buf + off, bufsz - (size_t)off, ", ...");
}

/* ------------------------------------------------------------ the panel */

int turn_panel_wanted(const Map *m)
{
    return turn_count(m) > 0 || turn_acting(m) >= 0 || turn_spotlight_ruleset(m);
}

/* The actor's counter, right-aligned on its row: "4/6", or nothing. Returns
 * the columns it took, so the name can give them up. */
static int draw_actor_counter(Renderer *r, const Token *t, const char *counter,
                              int x, int y, int w, Style s)
{
    if (!counter) return 0;
    int i = counter_find(t, counter);
    if (i < 0) return 0;
    char num[16];
    int  n = snprintf(num, sizeof num, "%d/%d", t->counters[i].value, t->counters[i].max);
    if (n <= 0 || n + 6 > w) return 0;
    draw_text(r, x + w - n, y, num, n, s);
    return n + 1;
}

static void turn_draw_panel_body(Renderer *r, const Map *m, const Theme *th, Rect rc,
                                 int ascii, const char *counter);

void turn_draw_panel(Renderer *r, const Map *m, const Theme *th, Rect rc, int ascii,
                     const char *counter)
{
    /* No counter means the players' frame, which never names the unseen. */
    g_mask = !counter && fog_any(m) ? m : NULL;
    turn_draw_panel_body(r, m, th, rc, ascii, counter);
    g_mask = NULL;
}

static void turn_draw_panel_body(Renderer *r, const Map *m, const Theme *th, Rect rc,
                                 int ascii, const char *counter)
{
    PROF_ZONE("panel.draw");
    if (rc.w < 8 || rc.h < 3) return;

    Style plain  = style(th->fg, th->bg, 0);
    Style dim    = style(th->dim, th->bg, 0);
    Style head   = style(th->accent, th->bg, ATTR_BOLD);
    Style lit    = style(th->turn, th->bg, ATTR_BOLD);

    draw_fill(r, rc, ' ', plain);
    for (int y = rc.y; y < rc.y + rc.h; y++)
        draw_text(r, rc.x, y, ascii ? "|" : "\u2502", 1, dim);

    int x = rc.x + 2, w = rc.w - 3, y = rc.y;
    int cur = turn_acting(m);
    int n   = turn_count(m);
    const char *mark = ascii ? ">" : "\u25b6";

    if (n == 0 && turn_spotlight_ruleset(m)) {
        draw_text(r, x, y++, "Spotlight", w, head);
        y++;
        for (int side = 0; side < 2 && y < rc.y + rc.h; side++) {
            int on = (m->spotlight == SPOTLIGHT_GM) == (side == 1);
            char line[32];
            snprintf(line, sizeof line, "%s %s", on ? mark : " ", side ? "GM" : "Players");
            draw_text(r, x, y++, line, w, on ? lit : plain);
            /* The creature holding it sits under its side. */
            if (on && cur >= 0 && y < rc.y + rc.h) {
                const Token *ct = &m->tokens.v[cur];
                Style ws = side_style(ct, th);
                int   took = draw_actor_counter(r, ct, counter, x, y, w, ws);
                char who[40];
                snprintf(who, sizeof who, "    %.24s", name_of(ct));
                draw_text(r, x, y++, who, w - took, ws);
            }
        }
        return;
    }

    draw_text(r, x, y++, "Turn order", w, head);
    if (m->round > 0) {
        char round[24];
        snprintf(round, sizeof round, "Round %d", m->round);
        draw_text(r, x, y++, round, w, dim);
    }
    y++;

    /* Highest first, each with its number; the actor lit. One row is kept
     * for the count of creatures not in the fight. */
    int at    = -1;
    int shown = 0;
    while (shown < n && y < rc.y + rc.h - 2) {
        at = neighbour(m, at, 1, TOKEN_ANY_KIND, 1);
        if (at < 0) break;
        const Token *t = &m->tokens.v[at];
        int on = at == cur;

        char line[48];
        Style rs   = on ? lit : side_style(t, th);
        int   took = on ? draw_actor_counter(r, t, counter, x, y, w, rs) : 0;
        snprintf(line, sizeof line, "%s %3d  %.*s", on ? mark : " ", t->init,
                 imax(1, w - 7 - took), name_of(t));
        draw_text(r, x, y++, line, w - took, rs);
        shown++;
    }
    if (shown < n) {
        char more[24];
        snprintf(more, sizeof more, "  +%d more", n - shown);
        draw_text(r, x, y++, more, w, dim);
    }

    /* A creature holding the turn from outside the order, and the rest. */
    if (cur >= 0 && !(m->tokens.v[cur].turn & TURN_IN) && y < rc.y + rc.h) {
        char who[48];
        snprintf(who, sizeof who, "%s  %.*s", mark, imax(1, w - 3), name_of(&m->tokens.v[cur]));
        draw_text(r, x, y++, who, w, lit);
    }
    /* Over fog, a creature the party cannot see is not counted either:
     * "1 not in the fight" would announce it. */
    int out = 0;
    for (int i = 0; i < m->tokens.n; i++)
        if (!(m->tokens.v[i].turn & TURN_IN) && !(g_mask && fog_token_hidden(g_mask, &m->tokens.v[i])))
            out++;
    if (out > 0 && y < rc.y + rc.h) {
        char rest[32];
        snprintf(rest, sizeof rest, "%d not in the fight", out);
        draw_text(r, x, rc.y + rc.h - 1, rest, w, dim);
    }
}
