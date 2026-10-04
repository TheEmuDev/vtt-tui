#ifndef VTT_APP_H
#define VTT_APP_H

#include "clock.h"
#include "ctl.h"
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

typedef enum { PCAM_FOLLOW, PCAM_PARTY, PCAM_HOLD } PlayersCamera;

typedef enum {
    MODAL_NONE,
    MODAL_PROMPT,
    MODAL_MESSAGE,     /* dismissed by any key */
    MODAL_CONFIRM_QUIT,
    MODAL_CONFIRM_DISCARD,
    MODAL_CONFIRM_DELETE,
    MODAL_CLEAR_STATUS,   /* which of a token's markers to take off */
    MODAL_CONFIRM_RECOVER,   /* an autosave newer than the map: take it? */
    MODAL_PICKER,            /* a list filtered as the GM types: App.picker */
    MODAL_CARD,              /* :card -- a creature's card whole, scrolled by card_top */
} ModalKind;

/* A handout's body: 2 KB of text, and its NUL. */
#define HANDOUT_BODY_MAX 2049

/* App.pending after i t: not a key of its own, so no key can collide. */
#define PENDING_IT 0x110000u

/* What the picker is choosing from. */
typedef enum { PICK_CHARACTER, PICK_STAMP, PICK_SCENE, PICK_HANDOUT } PickWhat;

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

/* A ping: a ring round a block of squares, until a moment passes. */
#define PING_SHOW_MS 2000
#define PING_GM      0u                     /* phones are 1 and up */
#define PING_MAX     (NET_MAX_CLIENTS + 1)
typedef struct {
    uint32_t who;
    int      x0, y0, x1, y1;
    uint64_t until_ms;
} Ping;

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

    /* Stretches of the status message drawn in a color of their own -- the
     * two duality dice. Byte offsets into status; cleared with every new
     * message, so a span can never outlive the text it was measured on. */
    struct { int at, len; uint32_t fg; } status_span[2];
    int nstatus_span;
    /* The status message is the GM's alone -- a counter's value, say -- and
     * the players' frame leaves it out. Cleared with every new message. */
    int status_gm;
    /* Something was hidden when the key being handled began. */
    int key_saw_hidden;

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

    /* The players' floor (docs/FLOORS.md), by name like Editor.floor: the
     * floor their screens show, a pin the GM set, the floor each side last
     * moved on (the tie-breaker's third step), the creature that had the
     * turn at the last key (a change is a turn starting), and the camera
     * their frame is drawn through when their floor is not the GM's. */
    char     pfloor[AREA_NAME_MAX];
    char     ppin[AREA_NAME_MAX];
    char     side_floor[2][AREA_NAME_MAX];    /* [0] players, [1] enemies */
    int      last_acting;
    GridView pview;
    char     pview_floor[AREA_NAME_MAX];      /* the floor pview was centered for */
    int      psplit;                          /* the players' frame is being drawn through pview */
    /* The players' camera (docs/CAMERA.md): the GM's (follow), framing the
     * party, or held where the GM left it. Party's framing is worked out
     * again only when what it was worked out from changes. */
    PlayersCamera pcam;
    struct { const Map *map; unsigned gen; int floor, zoom; Rect view; } pcam_for;

    /* One log for the whole session: token moves in play mode undo through
     * the same history as wall edits in build mode. */
    Undo    undo;

    /* The session log, off until :log. */
    SessionLog slog;

    /* The remote view's server, off until :serve. main polls it. */
    Net net;

    /* The control channel, off until :agent on (docs/CONTROL.md). main
     * polls it; app_tick runs what it has read. */
    Ctl ctl;

    /* The recovery autosave: a copy of the map written beside its file once
     * the changes have been quiet for a moment, removed by a save or a
     * deliberate discard, and offered back the next time the map is opened
     * if it is still there -- which it only is after a crash or a lost
     * terminal. Off for headless runs, which would litter. */
    int      autosave_on;
    unsigned autosave_gen;   /* the map generation the autosave holds */
    unsigned seen_gen;       /* the last generation app_tick saw */
    uint64_t change_ms;      /* when seen_gen last moved */
    uint64_t now_ms;         /* the time app_tick was last given */

    /* Pings: rings on the table for PING_SHOW_MS, one per source -- the
     * GM (PING_GM) or a phone, by its connection id -- a newer one from the
     * same source replacing it. Not the map's: nothing here is saved. */
    Ping     pings[PING_MAX];
    int      npings;
    /* The last ping from each source, kept after its ring comes down, for
     * the control channel's `marked`: "the spot I pinged a minute ago".
     * until_ms here is when it was made. Cleared with the map. */
    Ping     pinged[PING_MAX];
    int      npinged;
    /* Round what the control channel's last edit changed, for the same two
     * seconds; the GM's screen alone (never the players' frame), and only
     * while until_ms is set. */
    Ping     agent_ring;
    /* The undo log's stamp just after the channel's last edit, and whether
     * that edit is still there to take back: the agent's `undo` works only
     * while the log is exactly as the edit left it. */
    unsigned ctl_stamp;
    unsigned ctl_gen;           /* Map.gen then, for changes outside the log */
    int      ctl_undoable;

    TextPrompt prompt;
    PromptWhat prompt_what;

    /* The handout (docs/HANDOUTS.md): the last one shown, kept so
     * :handout on puts it back, and whether it is up on the players'
     * screens. The server holds its own copy for phones that join later. */
    char       handout_title[64];
    char       handout_body[HANDOUT_BODY_MAX];
    int        handout_up;

    /* The picker (MODAL_PICKER): the character templates or the stamps, and
     * for a character the side it goes down on, -1 for the side it was
     * saved on. */
    UiPicker   picker;
    PickWhat   pick_what;
    int        pick_kind;
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

    /* The stamp in hand: what y copied or :stamp picked, ready for p. Kept
     * across maps, so a piece of one map can go down on another. `turns`
     * and `mirrored` are only for the status line: the stamp itself is
     * turned each time. */
    Map     *stamp;
    char     stamp_name[MAP_NAME_MAX];   /* "" for a copy not yet saved */
    int      stamp_turns, stamp_mirrored;

    /* The ? page: which screen to go back to, which key map to lead with, and
     * how far down it is scrolled. */
    Screen   help_from;
    KeyMapId help_id;
    int      help_top;
    int      help_lines;   /* what the last draw measured, for clamping */

    /* Cards (docs/CARDS.md): the box beside the map is on unless :card off
     * says otherwise; :card's view is scrolled to card_top, and names whose
     * card it shows (card_token), the last draw measuring card_lines. */
    int      card_box_off;
    int      card_top;
    int      card_token;
    int      card_lines;
    /* The map and generation the phones' names were last offered from
     * (app_whisper.c): rebuilt only when one changes. */
    const Map *offer_map;
    unsigned   offer_gen;

    /* A prefix key waiting for the one that completes it -- i for placing, s
     * for markers, PENDING_IT for i t. 0 when nothing is pending. */
    uint32_t pending;

    ModalKind modal;
    char      modal_title[64];
    char      modal_body[192];
} App;

void app_init(App *a, Term *t, Renderer *r);
void app_free(App *a);
/* The floor the GM's screen shows, as an area index; -1 for the whole map. */
int  app_floor_shown(const App *a);
/* The players' floor, worked out afresh (a pin, the turn, the spotlight,
 * else the party); -1 for the whole map. */
int  app_players_floor(App *a);
/* The players are on a floor the GM is not showing: their frame is drawn
 * through its own camera. */
int  app_players_split(const App *a);
/* Their frame is drawn through their own camera, pview: on a split floor,
 * or with :player camera party or hold. Pings and the frame ask this. */
int  app_players_own_camera(const App *a);
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
/* The creature whose card the box beside the map shows -- the selected
 * one, else the one under the cursor, in play mode with the box on -- or -1. */
int  app_card_shown(const App *a);
/* A Horde's changed attack while half or more of its HP is marked, for the
 * card box's title: 1 with it in buf (app_damage.c). */
int  app_horde_note(const App *a, int idx, char *buf, size_t sz);
int  app_view_differs(const App *a);

/* The control channel (app_ctl.c). Runs one request against the app and
 * returns the answer, verdict line first, malloc'd, its length in *len.
 * app_tick hands it every request the socket has read. */
char *app_ctl_exec(App *a, const char *req, size_t *len);
/* Why an agent's edit would be refused right now, or NULL when it would
 * be taken. */
const char *app_ctl_busy(const App *a);
void app_set_status(App *a, const char *msg);
void app_note(App *a, const char *msg);     /* status line + session log */
void app_note_gm(App *a, const char *msg);  /* the same, kept off the players' frame */
void app_set_status_gm(App *a, const char *msg);  /* a hint or error the table must not see */
void app_status_span(App *a, int at, int len, uint32_t fg);   /* color part of it */

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

/* Pings. app_ping rings the block x0..x1, y0..y1 for `who` and says so on
 * the status line; app_ping_cell is a phone's tap, a screen cell of the
 * frame it was shown, turned into the square under it -- or nothing, off
 * the map or out of play. app_tick drains the server's taps and takes
 * rings down; app_ping_due is ms until the next goes, -1 with none. */
void app_ping(App *a, uint32_t who, int x0, int y0, int x1, int y1);
int  app_ping_cell(App *a, uint32_t who, int sx, int sy);
int  app_ping_due(const App *a, uint64_t now_ms);
int  app_autosave(App *a);      /* writes it now; 0 on success */

#endif /* VTT_APP_H */
