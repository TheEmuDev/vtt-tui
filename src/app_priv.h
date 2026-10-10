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

/* app.c: a message in a dialog; quitting, asking first about unsaved work. */
void app_show_message(App *a, const char *title, const char *body);
void app_request_quit(App *a);

/* app_browser.c: the menu and the map browser, and the file operations they
 * and the prompts ask for. */
void app_menu_key(App *a, Key k);
void app_browser_key(App *a, Key k);
void app_refresh_entries(App *a);
void app_rescan_keeping_place(App *a);
void app_rename_map(App *a, const char *from, const char *typed);
void app_duplicate_map(App *a, const char *from, const char *typed);
void app_delete_map(App *a, const char *path);

/* app_build.c: build mode's keys (wall mode's among them). */
void app_editor_key(App *a, Key k);

/* app.c: the creature a key acts on -- the selected one, else the one under
 * the cursor's footprint (docs/KEYS.md, README "The selected creature") --
 * or -1; and just the one under the cursor. */
int app_target_token(const App *a);
int app_target_token_under(const App *a);

/* app_card.c: s k -- the creature's card in the GM's editor. */
void app_card_edit(App *a);

/* app_whisper.c: :whisper, :players; and each tick, the names a phone is
 * offered kept in step and the phones that arrived said. */
void app_whisper_command(App *a, const char *rest);
void app_players_command(App *a);
void app_whisper_tick(App *a);

/* app_damage.c: :dmg. */
void app_damage_command(App *a, const char *rest);

/* app_ctl_marked.c: the channel's `marked` read, as text or JSON; and
 * app_ctl.c's words for a screen and a build mode, as the channel says them. */
void app_ctl_marked(App *a, FILE *out, int json);
const char *app_ctl_screen_name(Screen s);
const char *app_ctl_mode_name(EdMode m);

/* app_picker.c: the picker (characters, stamps, scenes, handouts). kind is
 * the side a character goes down on, -1 for its own. */
void app_pick_open(App *a, PickWhat what, int kind, const char *initial);
void app_pick_key(App *a, Key k);

/* app_character.c: :character, and a named character put down at the
 * cursor. */
void app_character_command(App *a, const char *rest);
void app_character_place(App *a, const char *name, int kind);

/* app.c: a trip through a link to another map puts this map down (saved)
 * and takes `m` up, keeping the server, the log and the play settings. */
void app_travel_to(App *a, Map *m);

/* app_handout.c: :handout; app_handout_show puts one up by name. */
void app_handout_command(App *a, const char *rest);
void app_handout_show(App *a, const char *name);
UiPickItem *app_handout_items(int *count);

/* app_scene.c: :scene and :scenes; app_scene_restore puts one back by name. */
void app_scene_command(App *a, const char *verb, const char *rest);
void app_scene_restore(App *a, const char *name);

/* app_stamp.c: y, p, the stamp mode's keys, :stamp. */
void app_stamp_yank(App *a);
void app_stamp_lift(App *a);
void app_stamp_key(App *a, Key k);

/* app.c: the creatures were renumbered (a scene put back, a change set
 * landed): the selection, group and range held indices that are gone, and
 * whoever holds the turn now started no turn. */
void app_creatures_renumbered(App *a);

/* Events for agents (app_event.c). */
void app_event(App *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int  app_events_after(const App *a, unsigned after);
/* "seq N", then every event after `after`, one a line. */
void app_events_write(const App *a, FILE *out, unsigned after);
/* app_events_flush and app_events_due are the main loop's too: app.h. */
/* Round a change that is not the GM's (an accept, the agent's undo): the
 * GM's changes so far become their event first, and the checkpoint starts
 * again after, so the change is not told twice. */
void app_events_map_flush(App *a);
void app_events_map_restart(App *a);

/* Jobs and the review (app_job.c). */
void app_jobs_clear(App *a);
int  app_job_new(App *a, int from, const char *text, const CsBox *box, int at_once);
void app_job_set_proposal(App *a, int slot, ChangeSet *cs, const char *line);
void app_jobs_land_waiting(App *a);
void app_review_key(App *a, Key k);
void app_review_resume(App *a);
void app_review_leave(App *a);
/* For the channel's job requests (app_ctl_job.c). */
int  app_job_find(const App *a, int num);
void app_job_thread_add(Job *j, char who, const char *text);
const char *app_job_state_name(int state);
const char *app_job_from_name(int from);

/* The v box a : command was typed over (build mode), into x0..y1, and back to
 * normal mode; 0 when there was none. :area, :scene save and :ask read it. */
int  app_cmd_vbox(App *a, int *x0, int *y0, int *x1, int *y1);
void app_job_feedback(App *a, const char *text);
void app_ask_command(App *a, const char *verb, const char *rest);
void app_jobs_command(App *a, const char *rest);
void app_review_command(App *a, const char *rest);
int  app_jobs_shown(const App *a);
void app_jobs_prepare(App *a);
int  app_review_show(App *a);
void app_review_unshow(App *a);
void app_jobs_labels(App *a);
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
/* :player camera follow|party|hold (docs/CAMERA.md). */
void app_players_camera_command(App *a, const char *rest);

/* app_ctl.c: :agent on, :agent off, :agent accept auto|review, :agent to ask. */
void app_agent_command(App *a, const char *rest);

/* The channel's words (app_ctl.c splits a line into them), and what its
 * other files share: a region by name (an area's name, or B2:K12), refused
 * off the map; the requests about jobs (app_ctl_job.c): 0 done, -1 with why
 * in err, 1 not one of them. */
#define CTL_WORDS    12
#define CTL_WORD_MAX 256
int  app_ctl_region(const Map *m, const char *w, int *x0, int *y0, int *x1, int *y1,
                    char *err, size_t errsz);
int  app_ctl_job(App *a, char w[][CTL_WORD_MAX], int n, FILE *out, char *err, size_t errsz);
/* The scratch map (App.ctl_scratch) as a fresh copy of the live map, its log
 * (ctl_sundo) cleared: a proposal is made in it, and a job's result read. */
Map *app_scratch_copy(App *a);
/* The map a job's proposal would make, accepted now: the scratch map, valid
 * until the next request. NULL when the job has none, or one made for
 * another size of map. */
Map *app_job_result(App *a, int slot);

/* app_play.c */
void app_play_key(App *a, Key k);

#endif /* VTT_APP_PRIV_H */
