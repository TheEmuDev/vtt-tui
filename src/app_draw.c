/* Drawing the frame: every screen, the modal over it, the players' frame
 * beside the GM's, and the question of whether the two could differ. Split
 * out of app.c, which keeps the state and the keys; app_priv.h is what they
 * share. */

#include "app_priv.h"

#include <stdio.h>
#include <string.h>

#include "counter.h"
#include "draw.h"
#include "fog.h"
#include "link.h"
#include "prof.h"
#include "stamp.h"

/* The status message, then its colored spans over the top. A span is drawn
 * only when all of it survived the ellipsis; half a number in gold would be
 * a different number. */
void app_draw_status_msg(App *a, int x, int y, int maxw)
{
    Renderer    *r  = a->rnd;
    const Theme *th = a->th;
    /* The players' frame carries no GM-only message, and over fog no message
     * at all: most of what the app says names a creature or a square, and
     * one placed in the dark would be announced to the table. */
    if (a->view == VIEW_PLAYERS && (a->status_gm || (a->map && fog_any(a->map)))) return;

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

static void draw_help(App *a)
{
    const KeyMap *maps[KEYS_COUNT];
    int n = app_help_order(a, maps);

    a->help_lines = ui_keypage(a->rnd, a->th, maps, n, &a->help_top,
                               a->ascii ? &BOX_ASCII : &BOX_ROUND);
}

/* ----------------------------------------------------------------- draw */

static void menu_row(void *ctx, int i, char *buf, size_t bufsz)
{
    (void)ctx;
    str_lcpy(buf, APP_MENU_ITEMS[i], bufsz);
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
    app_draw_status_msg(a, 1, y, r->w - 2);
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

    Rect list = rect(panel.x, panel.y + 3, panel.w, APP_MENU_COUNT);
    ui_list_draw(r, th, list, &a->menu, APP_MENU_COUNT, menu_row, a);

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

static int ping_visible(const void *ctx, int tx, int ty)
{
    return !fog_ground_hidden((const Map *)ctx, tx, ty);
}

static void draw_editor(App *a)
{
    Renderer    *r  = a->rnd;
    const Theme *th = a->th;
    Map         *m  = a->map;

    /* The turn-order panel takes its columns off the map view, and only
     * when there is a fight to show and room to show it; the bars keep the
     * whole width either way. */
    int clocks = a->screen == SCREEN_PLAY ? clock_panel_rows(m) : 0;
    int turns  = a->screen == SCREEN_PLAY && turn_panel_wanted(m);
    int panel  = a->play.panel && r->w >= 80 && (turns || clocks);
    ed_layout(&a->ed, m, r->w - (panel ? TURN_PANEL_W : 0), r->h);

    /* The fight rides in the title bar: it is true for the whole table, not
     * for whatever happens to be selected, so it does not belong on the
     * status line that describes the selection. */
    char left[192], fight[128] = "";
    if (a->screen == SCREEN_PLAY) turn_status_view(m, a->view == VIEW_PLAYERS, fight, sizeof fight);
    snprintf(left, sizeof left, "%.63s%s%s%.120s", m->name, m->modified ? " [+]" : "",
             fight[0] ? "    " : "", fight);
    ui_titlebar(r, th, left, a->screen == SCREEN_PLAY ? "PLAY" : "BUILD");

    int playing = (a->screen == SCREEN_PLAY);

    /* A stamp on the cursor is drawn as if it were down: swapped into the
     * map for this one draw and straight back out, with a ring round the
     * ground it would cover. */
    StampShow sv;
    int stamping = !playing && a->ed.mode == ED_STAMP && a->stamp;
    if (stamping) { PROF_ZONE("stamp.show"); stamp_show(m, a->stamp, a->ed.cx, a->ed.cy, &sv); }

    if (playing) play_draw(r, m, &a->ed, &a->play, th, a->ascii, a->view == VIEW_PLAYERS);
    else         ed_draw(r, m, &a->ed, th, a->ascii);

    if (stamping) {
        stamp_unshow(m, &sv);
        ClipRect saved = rnd_clip_push(r, a->ed.view.view.x, a->ed.view.view.y,
                                       a->ed.view.view.w, a->ed.view.view.h);
        grid_draw_tile_ring(r, &a->ed.view, m, a->ed.cx, a->ed.cy,
                            imin(a->ed.cx + a->stamp->w - 1, m->w - 1),
                            imin(a->ed.cy + a->stamp->h - 1, m->h - 1), th->ping_bg, NULL, NULL);
        rnd_clip_restore(r, saved);
    }

    /* The ruler is the GM's instrument; over fog its line would cross, and
     * its numbers measure, ground the players cannot see. */
    int fog_players = a->view == VIEW_PLAYERS && fog_any(m);
    if (a->ruler.active && !fog_players) {
        ClipRect saved = rnd_clip_push(r, a->ed.view.view.x, a->ed.view.view.y,
                                       a->ed.view.view.w, a->ed.view.view.h);
        ruler_draw(r, m, &a->ed.view, &a->ruler, th, 1);
        rnd_clip_restore(r, saved);
    }

    /* Pings, over everything on the map: they are what is being looked at
     * right now. In the players' frame over fog, only round what the
     * players can see. */
    if (playing && a->npings) {
        PROF_ZONE("ping.draw");
        ClipRect saved = rnd_clip_push(r, a->ed.view.view.x, a->ed.view.view.y,
                                       a->ed.view.view.w, a->ed.view.view.h);
        for (int i = 0; i < a->npings; i++) {
            const Ping *p = &a->pings[i];
            grid_draw_tile_ring(r, &a->ed.view, m, p->x0, p->y0, p->x1, p->y1, th->ping_bg,
                                fog_players ? ping_visible : NULL, m);
        }
        rnd_clip_restore(r, saved);
    }
    /* And the agent's last change, for the GM alone. */
    if (a->agent_ring.until_ms && a->view == VIEW_GM) {
        const Ping *p = &a->agent_ring;
        ClipRect saved = rnd_clip_push(r, a->ed.view.view.x, a->ed.view.view.y,
                                       a->ed.view.view.w, a->ed.view.view.h);
        grid_draw_tile_ring(r, &a->ed.view, m, p->x0, p->y0, p->x1, p->y1, th->ping_bg, NULL, NULL);
        rnd_clip_restore(r, saved);
    }

    /* The panel is the turn order with the clocks under it; each draws its
     * own rows, so whichever is absent leaves no gap. */
    if (panel) {
        Rect pr = rect(r->w - TURN_PANEL_W, 1, TURN_PANEL_W, r->h - 3);
        int  ch = imin(clocks, pr.h);
        char cn[COUNTER_NAME_MAX];
        app_current_counter(a, cn, sizeof cn);
        if (turns) turn_draw_panel(r, m, th, rect(pr.x, pr.y, pr.w, pr.h - ch), a->ascii,
                                   a->view == VIEW_GM ? cn : NULL);
        else {
            draw_fill(r, rect(pr.x, pr.y, pr.w, pr.h - ch), ' ', style(th->fg, th->bg, 0));
            for (int y = pr.y; y < pr.y + pr.h - ch; y++)
                draw_text(r, pr.x, y, a->ascii ? "|" : "\u2502", 1, style(th->dim, th->bg, 0));
        }
        if (ch) clock_draw_panel(r, m, th, rect(pr.x, pr.y + pr.h - ch, pr.w, ch), a->ascii);
    }

    /* Status line sits directly above the keybinding bar. */
    char status[192];
    if (fog_players && playing)     play_status(&a->play, m, &a->ed, 0, status, sizeof status);
    else if (a->ruler.active)       ruler_status(&a->ruler, m, status, sizeof status);
    else if (playing && a->play.range.active)
                                    range_status(&a->play.range, m, status, sizeof status);
    else if (playing)               play_status(&a->play, m, &a->ed, a->view == VIEW_GM, status, sizeof status);
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
    if (msg_w > 0) app_draw_status_msg(a, r->w - msg_w - 1, sy, msg_w);

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
    case ED_STAMP:
        ui_keybar(r, th, keys_map(KEYS_STAMP));
        break;
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
    app_draw_view(a, a->preview && a->screen == SCREEN_PLAY ? VIEW_PLAYERS : VIEW_GM);
}

void app_draw_view(App *a, View view)
{
    PROF_ZONE("app.draw");
    a->view = view;

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

    /* Modals, prompts and the profiler are the GM's: they ask the GM
     * questions, and one of them holds a note's text. The players' frame
     * ends here. */
    if (view == VIEW_PLAYERS) return;

    const BoxGlyphs *frame = a->ascii ? &BOX_ASCII : &BOX_ROUND;

    switch (a->modal) {
    case MODAL_PROMPT:  ui_prompt_draw(a->rnd, a->th, &a->prompt, frame); break;
    case MODAL_MESSAGE: ui_modal(a->rnd, a->th, a->modal_title, a->modal_body,
                                 "press any key", frame); break;
    case MODAL_CONFIRM_QUIT:
    case MODAL_CONFIRM_DISCARD:
        ui_confirm(a->rnd, a->th, a->modal_title, a->modal_body, frame);
        break;

    case MODAL_CONFIRM_RECOVER:
        ui_modal(a->rnd, a->th, a->modal_title, a->modal_body,
                 "y  recover them      n / esc  let them go", frame);
        break;

    case MODAL_CONFIRM_DELETE:
        /* Its own footer: this one removes a file from disk, and the word
         * "yes" does not say that. */
        ui_modal(a->rnd, a->th, a->modal_title, a->modal_body,
                 "y  delete from disk, permanently      n / esc  keep", frame);
        break;
    case MODAL_CLEAR_STATUS: {
        const Token *t = app_clear_status_target(a);
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

void app_current_counter(const App *a, char *buf, size_t bufsz)
{
    if (a->play.counter[0]) { str_lcpy(buf, a->play.counter, bufsz); return; }
    const Ruleset *rs = a->map ? ruleset_by_name(a->map->ruleset) : NULL;
    counter_default(rs ? rs->counters : NULL, buf, bufsz);
}

int app_view_differs(const App *a)
{
    if (a->preview) return 0;                 /* the GM is already looking at it */
    if (a->modal != MODAL_NONE) return 1;
    if (a->status_gm && a->status[0]) return 1;
    if (a->screen == SCREEN_PLAY && a->map && fog_any(a->map)) return 1;
    if (prof_overlay_visible()) return 1;
    if (a->agent_ring.until_ms) return 1;       /* the GM's alone */
    if (a->screen == SCREEN_PLAY && a->map) {
        const Map  *m  = a->map;
        const Play *pl = &a->play;
        if (pl->sel >= 0 && pl->sel < m->tokens.n) {
            const Token *t = &m->tokens.v[pl->sel];
            if (t->note[0] || t->ncounters) return 1;
        }
        int cur = turn_acting(m);                 /* the panel shows the actor's */
        if (cur >= 0 && m->tokens.v[cur].ncounters) return 1;
        if (map_note_at(m, a->ed.cx, a->ed.cy)) return 1;
        /* The GM's status line names a secret link under the cursor. */
        int li = link_at(m, a->ed.cx, a->ed.cy, NULL);
        if (li >= 0 && m->links[li].secret) return 1;
    }
    return 0;
}

void app_frame(App *a, Term *t, uint64_t now_ms)
{
    Renderer *r = a->rnd;
    rnd_begin(r);
    app_draw(a);

    Net *net = &a->net;
    net_set_live(net, app_remote_live(a));
    int wanted = net_active(net) && net_clients(net) > 0 && net_is_live(net);

    /* The players' frame: drawn when it could differ from the GM's, copied
     * when it cannot -- and copied before the GM's flush, which swaps its
     * buffers. Either way the players' renderer diffs against what the
     * clients are showing, which is the only thing that makes a diff
     * stream, and a FULL on resync, correct. */
    Renderer *pr = wanted ? net_players_renderer(net, r) : NULL;
    if (pr) {
        if (app_view_differs(a)) {
            PROF_ZONE("net.players_frame");
            rnd_begin(pr);
            a->rnd = pr;
            app_draw_view(a, VIEW_PLAYERS);
            a->rnd = r;
        } else {
            rnd_copy_back(pr, r);
        }
    }

    rnd_flush(r, t);
    if (!pr) return;
    net_frame_begin(net);
    rnd_flush(pr, NULL);
    net_frame_end(net, now_ms);
}
