#include "turn.h"

#include <stdio.h>
#include <string.h>

#include "prof.h"
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

static const char *name_of(const Token *t)
{
    return t->label[0] ? t->label : token_kind_name(t->kind);
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
    undo_end(u);
}

int turn_clear(Map *m, Undo *u)
{
    int had = turn_count(m);

    undo_begin(u);
    for (int i = 0; i < m->tokens.n; i++)
        if (m->tokens.v[i].turn) set_flags(m, u, i, 0, TURN_IN | TURN_ACTING);
    undo_set_round(u, m, 0);
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
}

void turn_status(const Map *m, char *buf, size_t bufsz)
{
    PROF_ZONE("turn.status");
    buf[0] = '\0';

    int cur = turn_acting(m);
    int n   = turn_count(m);
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
