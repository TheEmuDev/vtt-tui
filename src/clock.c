#include "clock.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "prof.h"
#include "util.h"

int clock_count(const Map *m)
{
    int n = 0;
    for (int i = 0; i < CLOCK_MAX; i++) n += m->clocks[i].name[0] != '\0';
    return n;
}

static int prefix_ci(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++)
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
    return 1;
}

int clock_find(const Map *m, const char *prefix)
{
    if (!prefix || !*prefix) return CLOCK_NONE;
    int found = CLOCK_NONE, hits = 0;
    for (int i = 0; i < CLOCK_MAX; i++) {
        const char *name = m->clocks[i].name;
        if (!name[0] || !prefix_ci(name, prefix)) continue;
        if (strlen(name) == strlen(prefix)) return i;     /* exact beats longer */
        found = i;
        hits++;
    }
    return hits > 1 ? CLOCK_AMBIGUOUS : found;
}

int clock_start(Map *m, const char *name, int size, int down)
{
    size = iclamp(size, 1, CLOCK_SIZE_MAX);

    char clean[CLOCK_NAME_MAX];
    int  n = 0;
    for (const char *p = name; *p && n + 1 < CLOCK_NAME_MAX; p++)
        if (!isspace((unsigned char)*p)) clean[n++] = *p;
    clean[n] = '\0';
    /* A name starting like a number would be read as an amount by :tick. */
    if (!n || !isalpha((unsigned char)clean[0])) return -1;

    int idx = CLOCK_NONE;
    for (int i = 0; i < CLOCK_MAX && idx < 0; i++)
        if (m->clocks[i].name[0] && !strcmp(m->clocks[i].name, clean)) idx = i;
    if (idx < 0)
        for (int i = 0; i < CLOCK_MAX && idx < 0; i++)
            if (!m->clocks[i].name[0]) idx = i;
    if (idx < 0) return -1;

    Clock *c = &m->clocks[idx];
    int fresh = !c->name[0] || (c->down != 0) != (down != 0);
    str_lcpy(c->name, clean, sizeof c->name);
    c->size = (uint8_t)size;
    c->down = (uint8_t)(down != 0);
    if (fresh)                   c->value = (uint8_t)clock_start_value(c);
    else if (c->value > c->size) c->value = c->size;
    map_touch(m);
    return idx;
}

void clock_drop(Map *m, int idx)
{
    if (idx < 0 || idx >= CLOCK_MAX || !m->clocks[idx].name[0]) return;
    memset(&m->clocks[idx], 0, sizeof m->clocks[idx]);
    map_touch(m);
}

int clock_set(Map *m, Undo *u, int idx, int value)
{
    if (idx < 0 || idx >= CLOCK_MAX || !m->clocks[idx].name[0]) return 0;
    undo_begin(u);
    undo_set_clock(u, m, idx, value);
    undo_end(u);
    return m->clocks[idx].value;
}

int clock_tick(Map *m, Undo *u, int idx, int delta, int *want)
{
    if (idx < 0 || idx >= CLOCK_MAX || !m->clocks[idx].name[0]) return 0;
    const Clock *c = &m->clocks[idx];
    int v = c->down ? c->value - delta : c->value + delta;
    if (want) *want = v;
    if (v < 0 || v > c->size) return c->value;
    return clock_set(m, u, idx, v);
}

void clock_format(const Clock *c, char *buf, size_t bufsz)
{
    snprintf(buf, bufsz, "%s %d/%d", c->name, c->value, c->size);
}

/* ------------------------------------------------------------ the panel */

int clock_panel_rows(const Map *m)
{
    int n = clock_count(m);
    return n ? n + 2 : 0;      /* a heading, a blank, a row each */
}

void clock_draw_panel(Renderer *r, const Map *m, const Theme *th, Rect rc, int ascii)
{
    PROF_ZONE("clock.draw");
    if (rc.w < 8 || rc.h < 3) return;

    Style plain = style(th->fg, th->bg, 0);
    Style dim   = style(th->dim, th->bg, 0);
    Style head  = style(th->accent, th->bg, ATTR_BOLD);
    Style full  = style(th->turn, th->bg, ATTR_BOLD);

    draw_fill(r, rc, ' ', plain);
    for (int y = rc.y; y < rc.y + rc.h; y++)
        draw_text(r, rc.x, y, ascii ? "|" : "│", 1, dim);

    int x = rc.x + 2, w = rc.w - 3, y = rc.y;
    draw_text(r, x, y++, "Clocks", w, head);
    y++;

    /* Names in one column, the longest setting it, then the segments as
     * dots when they fit the row and as a fraction when they do not: a
     * filled clock is the news, so it is lit. */
    int col = 4;
    for (int i = 0; i < CLOCK_MAX; i++)
        col = imax(col, (int)strlen(m->clocks[i].name));
    col = imin(col, w - 5);

    for (int i = 0; i < CLOCK_MAX && y < rc.y + rc.h; i++) {
        const Clock *c = &m->clocks[i];
        if (!c->name[0]) continue;

        int  dots = c->size <= w - col - 1;
        char line[96];
        int  off = snprintf(line, sizeof line, "%-*.*s ", col, col, c->name);
        if (dots) {
            for (int s = 0; s < c->size && off + 4 < (int)sizeof line; s++)
                off += snprintf(line + off, sizeof line - (size_t)off, "%s",
                                s < c->value ? (ascii ? "#" : "●") : (ascii ? "." : "○"));
        } else {
            snprintf(line + off, sizeof line - (size_t)off, "%d/%d", c->value, c->size);
        }
        draw_text(r, x, y++, line, w, clock_done(c) ? full : plain);
    }
}
