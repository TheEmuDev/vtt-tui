/* Stamps in build mode: y copies, p shows the copy on the cursor and p
 * again puts it down, :stamp keeps and fetches named ones. The stamp
 * itself -- copying, turning, placing, files -- is stamp.c's. */

#include <stdio.h>
#include <string.h>

#include "app_priv.h"
#include "stamp.h"

/* The stamp in hand, replaced; the old one freed. */
static void hold(App *a, Map *s, const char *name)
{
    map_free(a->stamp);
    a->stamp = s;
    str_lcpy(a->stamp_name, name ? name : "", sizeof a->stamp_name);
    a->stamp_turns = a->stamp_mirrored = 0;
}

/* "Table 3x2, turned 90, mirrored" -- or "the copy" before it has a name. */
static void describe(const App *a, char *buf, size_t sz)
{
    char turn[24] = "";
    if (a->stamp_turns) snprintf(turn, sizeof turn, ", turned %d", a->stamp_turns * 90);
    snprintf(buf, sz, "%s %dx%d%s%s", a->stamp_name[0] ? a->stamp_name : "the copy",
             a->stamp->w, a->stamp->h, turn, a->stamp_mirrored ? ", mirrored" : "");
}

void app_stamp_yank(App *a)
{
    Editor *e = &a->ed;
    int x0 = e->cx, y0 = e->cy, x1 = e->cx + e->brush - 1, y1 = e->cy + e->brush - 1;
    if (e->mode == ED_VISUAL) {
        /* A circle's box: see-through placing makes the corners harmless
         * only where they are void, so say so rather than pretend. */
        EdShape sh = ed_shape(e->shape, e->anchor_x, e->anchor_y, e->cx, e->cy, 0);
        x0 = sh.x0; y0 = sh.y0; x1 = sh.x1; y1 = sh.y1;
        e->mode = ED_NORMAL;
    }
    Map *s = stamp_copy(a->map, x0, y0, x1, y1);
    if (!s) { app_set_status(a, "nothing to copy there"); return; }
    hold(a, s, NULL);
    char msg[96];
    snprintf(msg, sizeof msg, "copied %dx%d%s - p shows it on the cursor", s->w, s->h,
             s->tokens.n ? " with its creatures" : "");
    app_set_status(a, msg);
}

void app_stamp_lift(App *a)
{
    if (!a->stamp) { app_set_status(a, "nothing to paste - y copies, :stamp picks a saved one"); return; }
    a->ed.mode = ED_STAMP;
    char what[96], msg[160];
    describe(a, what, sizeof what);
    snprintf(msg, sizeof msg, "%s - move it, r turns, | mirrors, p puts it down, esc cancels", what);
    app_set_status(a, msg);
}

/* Puts the stamp in hand down at the cursor. */
static void place(App *a)
{
    char err[160], what[96], at[MAP_COORD_MAX], msg[200];
    if (!stamp_place(a->map, &a->undo, a->stamp, a->ed.cx, a->ed.cy, err, sizeof err)) {
        app_set_status(a, err);
        return;
    }
    a->ed.mode = ED_NORMAL;
    describe(a, what, sizeof what);
    map_coord_name(a->ed.cx, a->ed.cy, at, sizeof at);
    snprintf(msg, sizeof msg, "stamped %s at %s - u takes it back", what, at);
    app_note(a, msg);
}

static void turn(App *a, int quarters)
{
    Map *t = quarters ? stamp_turned(a->stamp, quarters) : stamp_mirrored(a->stamp);
    map_free(a->stamp);
    a->stamp = t;
    if (quarters) a->stamp_turns = ((a->stamp_turns + quarters) % 4 + 4) % 4;
    else {
        /* A mirror after a turn is the mirror before the opposite turn:
         * keep the readout as "turned, then mirrored". */
        a->stamp_mirrored = !a->stamp_mirrored;
        a->stamp_turns = (4 - a->stamp_turns) % 4;
    }
    char what[96];
    describe(a, what, sizeof what);
    app_set_status(a, what);
}

void app_stamp_key(App *a, Key k)
{
    Editor *e = &a->ed;
    Map    *m = a->map;
    if (!a->stamp) { e->mode = ED_NORMAL; return; }

    if (k.kind == KEY_ESC)   { e->mode = ED_NORMAL; e->count = 0; app_set_status(a, "stamp put away"); return; }
    if (k.kind == KEY_ENTER) { place(a); return; }
    if (k.kind == KEY_LEFT)  { ed_move(e, m, -1, 0, take_count(e)); return; }
    if (k.kind == KEY_RIGHT) { ed_move(e, m,  1, 0, take_count(e)); return; }
    if (k.kind == KEY_UP)    { ed_move(e, m,  0, -1, take_count(e)); return; }
    if (k.kind == KEY_DOWN)  { ed_move(e, m,  0,  1, take_count(e)); return; }
    if (k.kind != KEY_CHAR || k.mods) return;

    if (k.ch >= '1' && k.ch <= '9') { count_digit(e, k.ch); return; }
    if (k.ch == '0' && e->count)    { e->count *= 10; return; }
    switch (k.ch) {
    case 'h': ed_move(e, m, -1,  0, take_count(e)); break;
    case 'l': ed_move(e, m,  1,  0, take_count(e)); break;
    case 'k': ed_move(e, m,  0, -1, take_count(e)); break;
    case 'j': ed_move(e, m,  0,  1, take_count(e)); break;
    case 'r': turn(a,  take_count(e)); break;
    case 'R': turn(a, -take_count(e)); break;
    case '|': turn(a, 0); break;
    case 'p': place(a); break;
    default:  app_set_status(a, "p puts the stamp down, r turns it, | mirrors, esc puts it away"); break;
    }
}

/* :stamp                  list them
 * :stamp NAME [-f]        pick one up; -f puts it down at the cursor at once
 * :stamp save NAME        keep the one in hand */
void app_stamp_command(App *a, const char *rest)
{
    char msg[256], err[160];
    if (a->screen != SCREEN_EDITOR) { app_set_status(a, "stamps are build mode's - F1 first"); return; }

    if (!*rest) {
        char names[16][MAP_NAME_MAX];
        int  n = stamp_list(names, 16), off = 0;
        if (!n) { app_set_status(a, "no stamps yet - y copies, :stamp save NAME keeps it"); return; }
        off = snprintf(msg, sizeof msg, "stamps:");
        for (int i = 0; i < n && i < 16 && off < (int)sizeof msg - 40; i++)
            off += snprintf(msg + off, sizeof msg - (size_t)off, " %s", names[i]);
        if (n > 16 && off < (int)sizeof msg - 8) snprintf(msg + off, sizeof msg - (size_t)off, " ...");
        app_set_status(a, msg);
        return;
    }

    char w1[MAP_NAME_MAX + 8] = "", w2[MAP_NAME_MAX + 8] = "", w3[16] = "";
    int  nw = sscanf(rest, "%71s %71s %15s", w1, w2, w3);

    if (!strcmp(w1, "save")) {
        if (nw != 2) { app_set_status(a, ":stamp save NAME keeps the stamp in hand"); return; }
        if (!a->stamp) { app_set_status(a, "nothing in hand to keep - y copies first"); return; }
        if (stamp_save(a->stamp, w2, err, sizeof err) < 0) { app_set_status(a, err); return; }
        str_lcpy(a->stamp_name, w2, sizeof a->stamp_name);
        snprintf(msg, sizeof msg, "stamp %s kept", w2);
        app_note(a, msg);
        return;
    }

    int now = nw == 2 && !strcmp(w2, "-f");
    if (nw > 2 || (nw == 2 && !now)) { app_set_status(a, ":stamp NAME, :stamp NAME -f, :stamp save NAME"); return; }
    Map *s = stamp_load(w1, err, sizeof err);
    if (!s) { app_set_status(a, err); return; }
    hold(a, s, w1);
    if (now) place(a);
    else     app_stamp_lift(a);
}
