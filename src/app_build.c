/* Build mode's keys: the editor's normal mode and wall (trace) mode. The
 * stamp in hand is app_stamp.c's, the ruler app.c's. */

#include "app_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fog.h"

/* -------------------------------------------------------------- wall mode */

static void app_wall_key(App *a, Key k)
{
    Editor *e = &a->ed;
    Map    *m = a->map;

    if (k.kind == KEY_ESC) {
        if (e->has_anchor) { e->has_anchor = 0; app_set_status(a, "anchor cleared"); return; }
        undo_stroke_end(&a->undo);     /* close any stroke still in progress */
        e->mode = ED_NORMAL;
        e->pen = e->erase = 0;
        /* The corner's square, kept on the floor shown: the far edge's
         * corners have their square past it. */
        int x0, y0, x1, y1;
        grid_bounds(&e->view, m, &x0, &y0, &x1, &y1);
        e->cx = iclamp(e->wx, x0, x1);
        e->cy = iclamp(e->wy, y0, y1);
        app_set_status(a, "");
        return;
    }

    if (k.kind == KEY_ENTER) {
        if (!e->has_anchor) { app_set_status(a, "set an anchor with v or V first"); return; }

        EdShape s = ed_shape(e->shape, e->ax, e->ay, e->wx, e->wy, 1);
        undo_stroke_end(&a->undo);     /* the shape is its own step */
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
        if (!e->pen) undo_stroke_end(&a->undo);
        app_set_status(a, e->pen ? "pen down - movement lays wall" : "pen up");
        break;

    case 'd':
        /* Erasing is the same tool with the sign flipped, so the pen comes
         * down with it rather than making the user press two keys. Switching
         * direction starts a new stroke. */
        undo_stroke_end(&a->undo);
        e->erase = !e->erase;
        if (e->erase) e->pen = 1;
        app_set_status(a, e->erase ? "erasing - movement clears wall" : "laying wall");
        break;

    case 'v': case 'V': {
        uint8_t want = (k.ch == 'V') ? ED_SHAPE_CIRCLE : ED_SHAPE_RECT;

        /* The same key twice clears the anchor; the other one changes the
         * shape and keeps it, the way v and V swap between vim's two visual
         * modes rather than canceling each other. */
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
        undo_stroke_end(&a->undo);
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

/* ------------------------------------------------------------ build mode */

void app_editor_key(App *a, Key k)
{
    Editor *e = &a->ed;
    Map    *m = a->map;
    if (!m) { a->screen = SCREEN_MENU; return; }

    if (e->mode == ED_COMMAND) { app_command_key(a, k); return; }
    if (e->mode == ED_STAMP)   { app_stamp_key(a, k); return; }
    if (app_ruler_key(a, k))       { return; }
    if (e->mode == ED_WALL)    { app_wall_key(a, k); return; }

    if (k.kind == KEY_ESC) {
        if (e->mode == ED_VISUAL) { e->mode = ED_NORMAL; app_set_status(a, ""); }
        else if (e->link_on && !e->pending_g) app_link_cancel(a);
        e->count = 0;
        e->pending_g = 0;
        return;
    }

    /* Arrows mirror hjkl so the editor is usable before the keys are learned. */
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

    /* The one member of the s family build mode has: the same key as play
     * mode, on the square, since creatures are play mode's to select. */
    if (a->pending == 's') {
        a->pending = 0;
        if (k.ch == 'n') app_note_prompt(a, -1, e->cx, e->cy);
        else             app_set_status(a, "s wants n for a note on this square");
        return;
    }

    if (e->pending_g) {
        e->pending_g = 0;
        if (k.ch == 'g') { int x0, y0, x1, y1; grid_bounds(&e->view, m, &x0, &y0, &x1, &y1);
                           e->cy = y0; grid_ensure_visible(&e->view, m, e->cx, e->cy, ED_SCROLLOFF); }
        else if (k.ch == 'f' || k.ch == 'c') {
            /* Painting fog is authoring, so it lives here with the terrain
             * brush and takes the same footprint: the brush, or the box. */
            int id = k.ch == 'f' ? e->fog_patch : 0;
            if (k.ch == 'f' && (!id || !m->fog_patches[id - 1].name[0] || m->fog_patches[id - 1].dead)) {
                app_set_status(a, "no fog patch to paint - :fog NAME makes one");
                return;
            }
            int n = ed_apply_fog(e, m, &a->undo, id);
            if (e->mode == ED_VISUAL) e->mode = ED_NORMAL;
            char msg[96];
            if (k.ch == 'f') snprintf(msg, sizeof msg, "fog %s over %d square%s",
                                      m->fog_patches[id - 1].name, n, n == 1 ? "" : "s");
            else             snprintf(msg, sizeof msg, "fog scrubbed from %d square%s", n, n == 1 ? "" : "s");
            app_note(a, msg);
        }
        else if (k.ch == 'l') app_link_mark(a);
        else if (k.ch == 'o') app_set_status(a, "creatures take links in play mode - F2");
        else if (k.ch == 'e') app_set_status(a, "group effects are play mode's - F2, then g e");
        else app_set_status(a, "g wants g for the top, f to paint fog, c to scrub it, l to make a link");
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

    case '0': ed_move(e, m, -1, 0, MAP_MAX_DIM); break;       /* as far as the floor goes */
    case '$': ed_move(e, m,  1, 0, MAP_MAX_DIM); break;
    case 'g': e->pending_g = 1; break;
    case 'G': ed_move(e, m, 0, 1, MAP_MAX_DIM); break;

    case 'v': case 'V': {
        uint8_t want = (k.ch == 'V') ? ED_SHAPE_CIRCLE : ED_SHAPE_RECT;

        /* The same key twice leaves visual mode; the other one changes the
         * shape and keeps the anchor, the way v and V swap between vim's two
         * visual modes rather than canceling each other. */
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

    case 's':
        a->pending = 's';
        app_set_status(a, "s n: a note on this square");
        break;

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

    case 'y': app_stamp_yank(a); break;
    case 'p': app_stamp_lift(a); break;

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
        e->cmd_from_visual = e->mode == ED_VISUAL;
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

