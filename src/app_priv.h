#ifndef VTT_APP_PRIV_H
#define VTT_APP_PRIV_H

/* Shared between app.c and the files split out of it: app_cmd.c holds the
 * : command line and app_play.c the play-mode key handler. Nothing here is
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
void app_open_prompt(App *a, PromptWhat what, const char *title,
                     const char *hint, const char *initial);
void app_clear_token_status(App *a, int idx, int which);
void app_follow_selection(App *a);
void app_report_selection(App *a);
void app_ruler_begin(App *a);
int  app_ruler_key(App *a, Key k);
int  app_token_under_cursor(App *a);
void app_leave_map(App *a);
void app_close_map(App *a);
int  app_save_map(App *a, const char *path);

/* app_cmd.c */
void app_exec_command(App *a, const char *line);
void app_command_key(App *a, Key k);

/* app_play.c */
void app_play_key(App *a, Key k);

#endif /* VTT_APP_PRIV_H */
