#ifndef VTT_APP_PRIV_H
#define VTT_APP_PRIV_H

/* Shared between app.c and the files split out of it: app_cmd.c holds the
 * : command line, app_play.c the play-mode key handler, app_draw.c the
 * frame and app_ctl.c the control channel's requests. Nothing here is
 * for main.c or the tests, which see app.h alone. */

#include "app.h"

/* Digits build a count prefix, exactly as in vim: 10j moves ten tiles. The
 * cap keeps a held key from wrapping the int, and nothing takes a count
 * that large anyway. */
#define COUNT_MAX 9999
static inline void count_digit(Editor *e, uint32_t ch)
{
    e->count = imin(e->count * 10 + (int)(ch - '0'), COUNT_MAX);
}

/* The size key, shared by both modes so it cannot drift: b cycles up, B
 * cycles back, and a count names the size outright -- 2b is 2x2 without
 * cycling past it. */
static inline uint8_t size_key(int count, uint8_t cur, int delta)
{
    if (count) return (uint8_t)iclamp(count, 1, 3);
    if (delta > 0) return (uint8_t)(cur % 3 + 1);
    return (uint8_t)(cur == 1 ? 3 : cur - 1);
}

static inline int take_count(Editor *e)
{
    int c = e->count ? e->count : 1;
    e->count = 0;
    return c;
}

/* Like take_count, but 0 when no count was typed: a motion with no count
 * means once, while a size key with no count means cycle -- the two callers
 * disagree about what silence means. */
static inline int take_count_raw(Editor *e)
{
    int c = e->count;
    e->count = 0;
    return c;
}

/* app.c */
/* The main menu, in order. */
#define APP_MENU_COUNT 3
extern const char *const APP_MENU_ITEMS[APP_MENU_COUNT];
static inline int app_menu_visible_rows(const App *a) { return imax(1, a->rnd->h - 10); }
/* Which key map describes where the GM is now: the bar and the ? page agree. */
KeyMapId app_keymap_id(const App *a);
/* The ? page's maps, the one for where it was opened first. */
int  app_help_order(const App *a, const KeyMap *out[KEYS_COUNT]);
/* The creature a pending "clear which marker" question is about, or NULL. */
const Token *app_clear_status_target(const App *a);

void app_open_prompt(App *a, PromptWhat what, const char *title,
                     const char *hint, const char *initial);
void app_clear_token_status(App *a, int idx, int which);
/* The counter < and > step, whether named yet or not. */
void app_current_counter(const App *a, char *buf, size_t bufsz);

/* Opens the note on creature idx, or on square (x,y) when idx is -1. */
void app_note_prompt(App *a, int idx, int x, int y);
void app_follow_selection(App *a);
void app_report_selection(App *a);
void app_ruler_begin(App *a);
int  app_ruler_key(App *a, Key k);
int  app_token_under_cursor(App *a);
void app_leave_map(App *a);
void app_leave_map_for(App *a, const char *next);   /* :e -- ask, then open */
void app_close_map(App *a);
int  app_save_map(App *a, const char *path);

/* app.c: opens a terminal window running `vtt --watch` against our server.
 * Returns 0 with what was opened in msg, or -1 with why not. */
int  app_spawn_mirror(App *a, char *msg, size_t msgsz);

/* app_cmd.c */
void app_exec_command(App *a, const char *line);
void app_command_key(App *a, Key k);

/* app_draw.c: the status message and its colored spans, as the bars draw it. */
void app_draw_status_msg(App *a, int x, int y, int maxw);

/* app_stamp.c: y, p, the stamp mode's keys, :stamp. */
void app_stamp_yank(App *a);
void app_stamp_lift(App *a);
void app_stamp_key(App *a, Key k);
void app_stamp_command(App *a, const char *rest);

/* app_link.c: g l in build mode (its first end, then its second), g o's
 * trip from end `end` of link `li` (the shift it made, 0 when refused),
 * :link. */
void app_link_mark(App *a);
void app_link_cancel(App *a);
void app_link_go(App *a, int li, int end, int enforce, int *moved_dx, int *moved_dy);
void app_link_command(App *a, const char *rest);

/* app_floor.c: the floor the GM's screen shows (area index, -1 for the whole
 * map); after every key and before every draw, _sync puts it into the view
 * and follows the cursor onto another floor; _show changes it, keeping the
 * cursor's place in the box; [ ] and :floor. */
void app_floor_sync(App *a);
void app_floor_show(App *a, int f);
void app_floor_step(App *a, int dir);
void app_floor_command(App *a, const char *verb, const char *rest);
/* The players' side: reset with each map; a creature of a side moved on a
 * floor (the tie-breaker's memory); their camera, set before their frame is
 * drawn; the spotlight crossed (the GM's view goes by floor_pick); and
 * :player floor NAME|auto. */
void app_floor_reset(App *a);
void app_floor_note_move(App *a, const Token *t);
void app_players_camera(App *a);
void app_floor_spotlight(App *a);
void app_players_pin(App *a, const char *rest);

/* app_ctl.c: :agent on, :agent off, :agent to ask. */
void app_agent_command(App *a, const char *rest);

/* app_play.c */
void app_play_key(App *a, Key k);

#endif /* VTT_APP_PRIV_H */
