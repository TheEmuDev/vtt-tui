#ifndef VTT_APP_H
#define VTT_APP_H

#include "clock.h"
#include "editor.h"
#include "input.h"
#include "map.h"
#include "mapio.h"
#include "net.h"
#include "play.h"
#include "ruler.h"
#include "slog.h"
#include "render.h"
#include "term.h"
#include "theme.h"
#include "turn.h"
#include "ui.h"
#include "undo.h"

typedef enum {
    SCREEN_MENU,
    SCREEN_BROWSER,
    SCREEN_EDITOR,     /* build mode */
    SCREEN_PLAY,
    SCREEN_HELP,       /* the ? reference, over whatever called it */
} Screen;

/* Whose eyes a frame is drawn for. The GM's terminal is VIEW_GM; the
 * players' frame -- what the phones and the watcher receive, and what
 * :player preview shows the GM -- is VIEW_PLAYERS, which draws no modal, no
 * prompt, no profiler overlay and no hint that a note exists. */
typedef enum { VIEW_GM, VIEW_PLAYERS } View;

typedef enum {
    MODAL_NONE,
    MODAL_PROMPT,
    MODAL_MESSAGE,     /* dismissed by any key */
    MODAL_CONFIRM_QUIT,
    MODAL_CONFIRM_DISCARD,
    MODAL_CONFIRM_DELETE,
    MODAL_CLEAR_STATUS,   /* which of a token's markers to take off */
    MODAL_CONFIRM_RECOVER,   /* an autosave newer than the map: take it? */
} ModalKind;

typedef enum {
    PROMPT_NONE,
    PROMPT_NEW_NAME,
    PROMPT_NEW_SIZE,
    PROMPT_SAVE_AS,
    PROMPT_TOKEN_LABEL,
    PROMPT_RELABEL,
    PROMPT_RENAME_MAP,
    PROMPT_DUPLICATE_MAP,
    PROMPT_STATUS_LABEL,
    PROMPT_INITIATIVE,
    PROMPT_TOKEN_SEARCH,
    PROMPT_NOTE,
    PROMPT_COUNTERS,
} PromptWhat;

typedef struct {
    Term        *term;
    Renderer    *rnd;
    const Theme *th;

    Screen screen;
    int    running;
    int    dirty;      /* a redraw is owed */
    int    ascii;
    View   view;       /* what app_draw_view is drawing right now */
    int    preview;    /* :player preview -- the GM's terminal shows VIEW_PLAYERS */

    char status[160];

    /* Stretches of the status message drawn in a colour of their own -- the
     * two duality dice. Byte offsets into status; cleared with every new
     * message, so a span can never outlive the text it was measured on. */
    struct { int at, len; uint32_t fg; } status_span[2];
    int nstatus_span;
    /* The status message is the GM's alone -- a counter's value, say -- and
     * the players' frame leaves it out. Cleared with every new message. */
    int status_gm;

    ListState menu;

    MapEntry *entries;
    int       nentries;
    ListState browser;

    Map    *map;
    Editor  ed;
    Play    play;

    /* Measuring is available in both build and play, so it lives beside the
     * editor rather than inside either mode's state. */
    Ruler   ruler;

    /* One log for the whole session: token moves in play mode undo through
     * the same history as wall edits in build mode. */
    Undo    undo;

    /* The session log, off until :log. */
    SessionLog slog;

    /* The remote view's server, off until :serve. main polls it. */
    Net net;

    /* The recovery autosave: a copy of the map written beside its file once
     * the changes have been quiet for a moment, removed by a save or a
     * deliberate discard, and offered back the next time the map is opened
     * if it is still there -- which it only is after a crash or a lost
     * terminal. Off for headless runs, which would litter. */
    int      autosave_on;
    unsigned autosave_gen;   /* the map generation the autosave holds */
    unsigned seen_gen;       /* the last generation app_tick saw */
    uint64_t change_ms;      /* when seen_gen last moved */

    TextPrompt prompt;
    PromptWhat prompt_what;
    char       pending_name[MAP_NAME_MAX];

    /* The file a pending delete or rename acts on, held in full so the
     * question and the action cannot disagree about which one is meant even
     * if the list changes underneath. */
    char       pending_file[MAP_PATH_MAX];

    /* Where and what a pending token placement will become once the label
     * prompt is answered. */
    uint8_t pending_kind;
    uint8_t pending_size;
    int     pending_tx, pending_ty;
    int     pending_token;   /* token a pending status marker hangs on */

    /* The ? page: which screen to go back to, which key map to lead with, and
     * how far down it is scrolled. */
    Screen   help_from;
    KeyMapId help_id;
    int      help_top;
    int      help_lines;   /* what the last draw measured, for clamping */

    /* A prefix key waiting for the one that completes it -- i for placing, s
     * for markers. 0 when nothing is pending. */
    uint32_t pending;

    ModalKind modal;
    char      modal_title[64];
    char      modal_body[192];
} App;

void app_init(App *a, Term *t, Renderer *r);
void app_free(App *a);
void app_key(App *a, Key k);
/* Works fog's sight out again if anything on the map changed since last
 * time -- a creature moved, a door opened, a patch was painted, an undo.
 * app_key calls it after every key, so it is once a keystroke at most. */
void app_fog_sync(App *a);
/* Draws into a->rnd: the GM's view, or the players' under :player preview. */
void app_draw(App *a);
void app_draw_view(App *a, View view);

/* One whole frame: the GM's view to the terminal (NULL for headless), then,
 * with clients attached and play mode live, the players' frame to them --
 * drawn when the two views could differ, copied when they cannot. Every
 * frame the app shows goes through here, so main, the bench and the tests
 * cannot disagree about the sequence. */
void app_frame(App *a, Term *t, uint64_t now_ms);

/* Could the players' frame differ from the GM's right now? Conservative:
 * true unless nothing GM-only is on screen. This is a privacy boundary. */
int  app_view_differs(const App *a);
void app_set_status(App *a, const char *msg);
void app_note(App *a, const char *msg);     /* status line + session log */
void app_note_gm(App *a, const char *msg);  /* the same, kept off the players' frame */
void app_set_status_gm(App *a, const char *msg);  /* a hint or error the table must not see */
void app_status_span(App *a, int at, int len, uint32_t fg);   /* colour part of it */

/* Whether the remote view should be streaming this frame: play mode is what
 * the players may see. GM-only things are not in the players' frame at all,
 * so nothing else has to freeze it. */
static inline int app_remote_live(const App *a)
{
    return a->screen == SCREEN_PLAY;
}

/* How to run this binary again, for :mirror's second window; main sets it
 * from /proc/self/exe or argv[0]. */
extern const char *app_self_path;

/* Opens a map by path, replacing whatever is loaded. Returns 0 on success
 * and leaves a message modal up on failure -- or, on success, the offer of
 * a newer autosave, which the next key answers. */
int  app_open_map(App *a, const char *path);

/* The autosave's clock. app_tick is called once round the event loop with
 * the time; app_autosave_due says how many ms until it will want to write,
 * -1 for never, so the loop can sleep exactly that long. */
#define AUTOSAVE_QUIET_MS 1500
void app_tick(App *a, uint64_t now_ms);
int  app_autosave_due(const App *a, uint64_t now_ms);
int  app_autosave(App *a);      /* writes it now; 0 on success */

#endif /* VTT_APP_H */
