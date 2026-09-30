#include "keys.h"

#include <stddef.h>

/* Bars are capped at six hints. The cap is the point: the bar teaches the
 * shape of a mode, and ? tells the whole story. Ordered by how often a GM
 * reaches for each, since ui_keybar drops from the right on a narrow
 * terminal. */

#define GROUP(title) { NULL, (title), NULL, NULL }
#define KEY(k, w)    { (k), (w), NULL, NULL }

/* ------------------------------------------------------------------ play */

static const KeyDoc PLAY[] = {
    GROUP("Move"),
    { "h j k l",  "move the cursor, or the creature in hand", NULL, NULL },
    KEY("arrows", "the same"),
    KEY("3j",     "any motion takes a count"),
    KEY(":d6",    "jump to a square by its label"),
    KEY(":area Crypt", "jump to a named area   :areas lists them"),
    KEY("[  ]",   "the floor below / above   :floor all shows the whole map"),
    KEY("#",      "column letters and row numbers, on or off"),
    KEY("z",      "center the view on the cursor"),
    KEY("+ -",    "zoom in and out"),

    GROUP("Creatures"),
    { "i p",      "place a player",                    "i",     "place" },
    KEY("i e",    "place an enemy"),
    KEY("i t e  i t p", "place a saved character as an enemy / a player: a list to pick from"),
    KEY(":character save", "save the creature under the cursor as a character   :character NAME picks one"),
    KEY(":scene save Ambush", "keep every creature as it stands (a v box: those in it)   :scene Ambush puts them back"),
    KEY(":handout Tomb", "put Tomb.txt up on the players' screens   :handout say TEXT, :handout off"),
    KEY("b  B",   "the cursor's size, cycled; resizes the selected"),
    KEY("2b",     "name the size outright -- 1, 2 or 3"),
    { "enter",    "pick up or put down; a big cursor walks what it covers",
                                                             NULL,    "grab" },
    { "d",        "remove it, keeping it to paste  (x does too)", "d y p", "edit" },
    KEY("y",      "yank -- copy it"),
    KEY("p",      "put what was yanked or removed here"),
    { "v",        "select several: a box from here to the cursor", NULL, "select" },
    KEY("c",      "change its label"),

    GROUP("Find a creature"),
    { "t  T",     "next / previous token -- in turn order once there is one", "t/f/e", "cycle" },
    KEY("f  F",   "next / previous friendly"),
    KEY("e  E",   "next / previous enemy"),
    KEY("tab",    "the same as t, shift-tab as T"),
    KEY("/",      "find a token by part of its label"),
    /* Off the bar to make room for v: the bar holds six, and a new gesture
     * nobody has met yet earns its place over a search that t/f/e mostly
     * stands in for. ? still lists it. */
    KEY("n  N",   "next / previous match"),

    GROUP("Turn order"),
    KEY("a  A",   "next / previous turn; 3a moves three on; a lap is a new round"),
    KEY("a (spotlight)", "under a spotlight ruleset: the spotlight crosses to the other side"),
    KEY("s i",    "initiative: a number joins the order, blank leaves it"),
    KEY("s t",    "hand the turn to this creature, in the order or not"),
    KEY(":turns", "the whole order on one line    :turns end ends the fight"),
    KEY(":panel", "the side panel, on or off; it appears when there is a fight or a clock"),
    GROUP("Clocks"),
    KEY(":clock Dragon 6", "start a clock of six segments; d6 rolls the start; 'down' or 'up' says which way"),
    KEY(":tick",  "a step on the clock in hand   :tick Dragon 2, :tick Dragon -1, :tick Dragon =3, reset"),
    KEY(":clock", "list them   :clock Dragon remove drops one"),

    GROUP("Remote view"),
    KEY(":serve",  "let players watch from a browser; the status line shows the URL"),
    KEY(":serve 7777", "serve on a fixed port; on the port already serving, shows the URL"),
    KEY(":serve --stay-alive", "keep that server, and its join code, when the map closes"),
    KEY(":serve --no-pings", "ignore taps from the phones; --pings takes them again"),
    KEY("g p",    "ping: ring the cursor's squares (or the box) on every screen for two seconds"),
    KEY(":mirror", "a second window mirroring play mode, to drag to a TV"),
    KEY(":serve off", "close the remote view and drop everyone"),
    KEY(":player preview", "see the players' frame on your own screen; q returns"),

    GROUP("Status markers"),
    KEY("s a",    "add a marker: a color and a word"),
    KEY("s c",    "color the next marker will use"),
    KEY("s d",    "drop a marker, asking which when there are several"),
    GROUP("Counters"),
    KEY("s v",    "the creature's counters: hp 6, hp -2, stress 0/6, -hp"),
    KEY("<  >",   "one off / one on its current counter; 3< takes three"),
    GROUP("Fog"),
    KEY("g r  g h", "light / darken the fog under the cursor or the box"),
    KEY("g R  g H", "the whole fog patch under the cursor"),
    KEY(":fog",     "the patches and the switch  (:fog Crypt clear, :fog off ...)"),
    KEY(":fog --soft-edge", "show the players the rim of the dark: dim walls, silhouettes"),
    GROUP("Notes"),
    KEY("s n",    "a note on this creature, or on the square; the prompt reads and writes it"),
    KEY(":notes", "where the notes are"),
    GROUP("Hidden creatures"),
    KEY("s h",    "hide it from the players, dimmed on yours; again shows it   :hidden lists them"),

    GROUP("Tools"),
    KEY("m",      "measure (the ruler)"),
    KEY("r",      "range: cycle the bands, or grow a square a press (20r)"),
    KEY("R",      "its shape: circle, cone, line, square (2R names one); the cursor aims"),
    KEY("g e",    "group effect: a burst at the cursor, following it; Very Close under Daggerheart, 3ge Close"),
    KEY("o  O",   "open or close a door / a secret door"),
    KEY("g o",    "take the link here: everyone on that end goes through, as one u"),
    KEY(":link 3", "jump to link 3's end, again for the other   :links lists them"),
    KEY("ctrl-w", "let creatures through walls and each other, or stop them"),

    GROUP("Dice and the log"),
    KEY(":roll 2d6+3", "roll dice, shown die by die"),
    KEY(":roll +2",    "the ruleset's action roll -- Daggerheart's Hope and Fear d12s"),
    KEY(":roll attack = 2d12+3", "save a roll under a name; :roll attack rolls it, :rolls lists them"),
    KEY(":roll attack remove", "remove a saved roll"),
    KEY(":log",        "the session log, on or off  (:log on, :log off, :log file)"),

    GROUP("Undo and elsewhere"),
    KEY("u",      "undo    ctrl-r redo"),
    KEY("esc",    "put down, then range off, then deselect"),
    KEY(":",      "command line -- :w :q :scale :metric :ruleset ..."),
    KEY("F1",     "build mode    F2 back here    F12 profiler"),
    KEY("q",      "leave the map"),
    { "?",        "this page",                         NULL,    "keys" },
};

static const KeyDoc PLAY_VISUAL[] = {
    GROUP("Selecting several"),
    { "h j k l",  "stretch the box; it lights what it catches", "hjkl", "extend" },
    { "enter",    "carry everything in it -- they move as one", NULL, "carry" },
    { "y",        "yank them all",                     NULL,    "yank" },
    { "d",        "remove them all, keeping them to paste  (x too)", NULL, "remove" },
    { "v  esc",   "drop the box",                      "esc",   "cancel" },
    { "?",        "this page",                         NULL,    "keys" },
};

static const KeyDoc PLAY_GRABBED[] = {
    GROUP("Carrying a creature"),
    { "h j k l",  "walk it; a group walks together, or not at all", "hjkl", "move" },
    { "enter",    "put it down here",                  NULL, "drop" },
    { "esc",      "cancel: back to where it set out from", NULL, "cancel" },
    { "u",        "take back a step",                  NULL, "undo" },
    KEY("ctrl-w", "let it through walls and creatures, or stop it"),
    { "?",        "this page",                         NULL, "keys" },
};

/* ----------------------------------------------------------------- build */

static const KeyDoc BUILD[] = {
    GROUP("Move"),
    { "h j k l",  "move the cursor",                   "hjkl", "move" },
    KEY("0  $",   "first / last column"),
    KEY("gg  G",  "first / last row"),
    KEY("3j",     "any motion takes a count"),
    KEY(":d6",    "jump to a square by its label  (:6 for a row)"),
    KEY("#",      "column letters and row numbers, on or off"),
    KEY("ctrl-d", "half a page down    ctrl-u up"),
    KEY("z",      "center the view    + - zoom"),

    GROUP("Walls and doors"),
    { "H J K L",  "wall on the west / south / north / east face", "HJKL", "wall" },
    { "w",        "trace mode: walk the cursor and leave wall behind", NULL, "trace" },
    { "t",        "cycle which boundary H J K L and the pen lay", "t", "kind" },
    KEY("o  O",   "open or close a door / a secret door"),

    GROUP("Floors"),
    KEY(":floor Upper 1", "make the named area Upper a floor, level 1   :floors lists them"),
    KEY("[  ]",   "show the floor below / above; :floor Upper shows one, :floor all the map"),

    GROUP("Links"),
    KEY("g l",    "make a link: g l on one end, g l on the other; the brush is its size"),
    KEY(":link ladder", "what g l makes: stairs, ladder, trapdoor or portal"),
    KEY(":link 3 oneway", "change it: a kind, oneway, twoway, reverse, secret, seen, remove"),
    KEY(":link to crypt Entrance", "a link from here to an area or square in crypt.vtt; g o takes it in play mode"),

    GROUP("Fog"),
    KEY(":fog Crypt", "make a patch, or pick one, for g f to paint"),
    KEY(":fog --soft-edge", "show the rim of the dark: dim walls, silhouettes"),
    KEY("g f  g c", "paint the patch over the brush or the box / scrub fog off"),
    GROUP("Ground"),
    { "b  B",     "brush size, cycled -- 2b names it; keys act on it all", NULL, NULL },
    { "space",    "floor here, or clear it back to void", NULL, NULL },
    { "f",        "paint the selected terrain",        NULL, "paint" },
    KEY("x",      "clear to void"),
    KEY("T",      "cycle which terrain f paints"),
    { "v  V",     "select a box / a circle, to paint many at once", NULL, NULL },

    GROUP("Stamps"),
    KEY("y  p",   "copy the brush's squares or the box / show the copy on the cursor to place"),
    KEY(":stamp", "pick from your stamps   :stamp Table takes one   :stamp save Table keeps the copy"),
    KEY(":area Crypt", "name the v box Crypt, or jump to it   :areas lists them"),

    GROUP("Undo and elsewhere"),
    KEY("u",      "undo    ctrl-r redo"),
    KEY("m",      "measure (the ruler)"),
    KEY(":roll 2d6+3", "roll dice    :log keeps a record of the session"),
    KEY(":agent on", "let an AI agent read and edit this map (vtt --ctl); u takes back each change"),
    KEY(":",      "command line -- :w :q :resize :scale :metric ..."),
    KEY("F2",     "play mode    F1 back here    F12 profiler"),
    KEY("q",      "leave the map"),
    { "?",        "this page",                         NULL, "keys" },
};

static const KeyDoc VISUAL[] = {
    GROUP("Visual select"),
    { "h j k l",  "stretch the selection",             "hjkl", "extend" },
    { "v  V",     "box or circle; a circle is centered where you started", "v/V", "shape" },
    { "f",        "paint the selected terrain over it", NULL, "floor" },
    { "x",        "clear it to void",                  NULL, "clear" },
    { "y",        "copy it: ground, walls, creatures, notes", NULL, NULL },
    { "esc",      "drop the selection",                NULL, "cancel" },
    { "?",        "this page",                         NULL, "keys" },
};

static const KeyDoc STAMP[] = {
    GROUP("Placing a stamp"),
    { "h j k l",  "move it: the cursor is its top-left square", "hjkl", "move" },
    { "r  R",     "turn it a quarter clockwise / back", "r/R", "turn" },
    KEY("|",      "mirror it left to right"),
    { "p  enter", "put it down here; one u takes it back", "p", "place" },
    { "esc",      "put it away without placing",       NULL, "cancel" },
    KEY(":stamp save Table", "keep what you copied as a stamp   :stamp Table -f places it at once"),
    { "?",        "this page",                         NULL, "keys" },
};

static const KeyDoc WALL[] = {
    GROUP("Wall trace"),
    { "h j k l",  "walk the corner, laying wall when the pen is down", "hjkl", "trace" },
    { "space",    "pen up or down",                    NULL, "pen" },
    { "d",        "erase instead of lay",              NULL, "erase" },
    KEY("t",      "cycle which boundary the pen lays"),
    KEY("v  V",   "anchor a rectangle / a circle, then enter"),
    { "enter",    "wall around the anchored shape",     NULL, "rect" },
    KEY("u",      "undo    ctrl-r redo"),
    KEY("z",      "center the view    + - zoom"),
    { "esc",      "drop the anchor, or leave trace mode", NULL, "back" },
    { "?",        "this page",                         NULL, "keys" },
};

/* ----------------------------------------------------------------- ruler */

static const KeyDoc RULER[] = {
    GROUP("Measuring"),
    { "h j k l",  "move the far end",                  "hjkl", "measure" },
    { "enter",    "pin a corner and carry on",         "enter", "leg" },
    KEY("bksp",   "drop the last corner  (u does too)"),
    KEY("m",      "start again from here"),
    { "M",        "cycle the distance metric",         NULL, "metric" },
    KEY("z",      "center the view    + - zoom"),
    { "esc",      "done measuring",                    NULL, "done" },
    { "?",        "this page",                         NULL, "keys" },
};

/* ------------------------------------------------------- menu and browser */

static const KeyDoc BROWSER[] = {
    GROUP("Your maps"),
    { "j  k",     "move the selection",                "j/k", "move" },
    KEY("g  G",   "first / last"),
    { "enter",    "open it",                           NULL, "open" },
    { "d",        "delete it, asking first",           NULL, "delete" },
    KEY("R",      "rename it"),
    KEY("c",      "duplicate it"),
    KEY("r",      "rescan the map directory"),
    KEY("esc",    "back to the menu  (q does too)"),
    { "?",        "this page",                         NULL, "keys" },
};

static const KeyDoc MENU[] = {
    GROUP("Menu"),
    { "j  k",     "move the selection",                "j/k", "move" },
    { "enter",    "choose",                            NULL, "select" },
    { "q",        "quit vtt",                          NULL, "quit" },
    KEY("F12",    "profiler overlay, from anywhere"),
    { "?",        "this page",                         NULL, "keys" },
};

/* ------------------------------------------------------------------------ */

#define MAP(id, title, table) [id] = { (title), (table), (int)(sizeof (table) / sizeof *(table)) }

static const KeyMap MAPS[KEYS_COUNT] = {
    MAP(KEYS_PLAY,         "Play mode",           PLAY),
    MAP(KEYS_PLAY_VISUAL,  "Play mode, selecting", PLAY_VISUAL),
    MAP(KEYS_PLAY_GRABBED, "Play mode, carrying", PLAY_GRABBED),
    MAP(KEYS_BUILD,        "Build mode",          BUILD),
    MAP(KEYS_VISUAL,       "Build mode, visual",  VISUAL),
    MAP(KEYS_STAMP,        "Build mode, stamp",   STAMP),
    MAP(KEYS_WALL,         "Build mode, tracing", WALL),
    MAP(KEYS_RULER,        "Ruler",               RULER),
    MAP(KEYS_BROWSER,      "Open a map",          BROWSER),
    MAP(KEYS_MENU,         "Menu",                MENU),
};

const KeyMap *keys_map(KeyMapId id)
{
    if (id < 0 || id >= KEYS_COUNT) id = KEYS_PLAY;
    return &MAPS[id];
}
