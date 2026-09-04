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
    KEY(":d6",    "jump to a square by its label"),
    KEY("#",      "column letters and row numbers, on or off"),
    KEY("z",      "centre the view on the cursor"),
    KEY("+ -",    "zoom in and out"),

    GROUP("Creatures"),
    { "i p",      "place a player",                    "i",     "place" },
    KEY("i e",    "place an enemy"),
    KEY("1 2 3",  "the cursor's size; resizes the selected creature too"),
    { "enter",    "pick up or put down; a big cursor walks what it covers",
                                                             NULL,    "grab" },
    { "d",        "remove it, keeping it to paste  (x does too)", "d y p", "edit" },
    KEY("y",      "yank -- copy it"),
    KEY("p",      "put the yanked or removed one here"),
    KEY("c",      "change its label"),

    GROUP("Find a creature"),
    { "t  T",     "next / previous token, any kind",   "t/f/e", "cycle" },
    KEY("f  F",   "next / previous friendly"),
    KEY("e  E",   "next / previous enemy"),
    KEY("tab",    "the same as t, shift-tab as T"),
    KEY("/",      "find a token by part of its label"),
    { "n  N",     "next / previous match",             "/",     "find" },

    GROUP("Status markers"),
    KEY("s a",    "add a marker: a colour and a word"),
    KEY("s c",    "colour the next marker will use"),
    KEY("s d",    "drop a marker, asking which when there are several"),

    GROUP("Tools"),
    KEY("m",      "measure (the ruler)"),
    KEY("r",      "cycle the range-band highlight"),
    KEY("o  O",   "open or close a door / a secret door"),
    KEY("ctrl-w", "let creatures through walls and each other, or stop them"),

    GROUP("Undo and elsewhere"),
    KEY("u",      "undo    ctrl-r redo"),
    KEY("esc",    "put down, then range off, then deselect"),
    KEY(":",      "command line -- :w :q :scale :ruleset ..."),
    KEY("F1",     "build mode    F2 back here    F12 profiler"),
    KEY("q",      "leave the map"),
    { "?",        "this page",                         NULL,    "keys" },
};

static const KeyDoc PLAY_GRABBED[] = {
    GROUP("Carrying a creature"),
    { "h j k l",  "walk it; the ribbon shows the route, the label how far", "hjkl", "move" },
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
    KEY("z",      "centre the view    + - zoom"),

    GROUP("Walls and doors"),
    { "H J K L",  "wall on the west / south / north / east face", "HJKL", "wall" },
    { "w",        "trace mode: walk the cursor and leave wall behind", NULL, "trace" },
    { "t",        "cycle which boundary H J K L and the pen lay", "t/T", "kind" },
    KEY("o  O",   "open or close a door / a secret door"),

    GROUP("Ground"),
    { "space",    "floor here, or clear it back to void", NULL, NULL },
    { "f",        "paint the selected terrain",        NULL, "paint" },
    KEY("x",      "clear to void"),
    KEY("T",      "cycle which terrain f paints"),
    { "v  V",     "select a box / a circle, to paint many at once", NULL, NULL },

    GROUP("Undo and elsewhere"),
    KEY("u",      "undo    ctrl-r redo"),
    KEY("m",      "measure (the ruler)"),
    KEY(":",      "command line -- :w :q :resize :scale ..."),
    KEY("F2",     "play mode    F1 back here    F12 profiler"),
    KEY("q",      "leave the map"),
    { "?",        "this page",                         NULL, "keys" },
};

static const KeyDoc VISUAL[] = {
    GROUP("Visual select"),
    { "h j k l",  "stretch the selection",             "hjkl", "extend" },
    { "v  V",     "box or circle; a circle is centred where you started", "v/V", "shape" },
    { "f",        "paint the selected terrain over it", NULL, "floor" },
    { "x",        "clear it to void",                  NULL, "clear" },
    { "esc",      "drop the selection",                NULL, "cancel" },
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
    KEY("z",      "centre the view    + - zoom"),
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
    KEY("z",      "centre the view    + - zoom"),
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
    MAP(KEYS_PLAY_GRABBED, "Play mode, carrying", PLAY_GRABBED),
    MAP(KEYS_BUILD,        "Build mode",          BUILD),
    MAP(KEYS_VISUAL,       "Build mode, visual",  VISUAL),
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
