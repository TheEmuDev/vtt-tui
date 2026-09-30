#ifndef VTT_MAP_H
#define VTT_MAP_H

#include <stdint.h>
#include "token.h"

#define MAP_MIN_DIM 1
#define MAP_MAX_DIM 512
#define MAP_NAME_MAX 64
#define MAP_RULESET_MAX 32

/* Feet represented by one tile. Five is the tabletop convention; :scale
 * changes it per map. */
#define MAP_SCALE_DEFAULT 5.0

/* Default distance metric for a new map: DIST_ALT_DIAG, the alternating
 * 5-10-5 diagonal. Spelled as a number because ruler.h includes this header
 * and so cannot be included back; ruler.c carries a static assertion that the
 * two still agree. */
#define MAP_METRIC_DEFAULT 2
#define MAP_PATH_MAX 512

/* Clocks: see clock.h. Fixed slots, an empty one has no name. */
#define CLOCK_MAX      8
#define CLOCK_NAME_MAX 20
typedef struct {
    char    name[CLOCK_NAME_MAX];   /* "" for an empty slot */
    uint8_t value;                  /* segments filled: what is left, for a countdown */
    uint8_t size;
    uint8_t down;                   /* counts down from full to nothing */
    uint8_t gen;                    /* bumped when the slot is dropped, so an undo
                                       op recorded against the old clock can tell
                                       it from a new one in the same slot */
} Clock;

/* Named rolls: ":roll attack" for a stat block's "2d12+3". They belong to
 * the encounter, so they live on the map. A slot with no name is empty. */
#define ROLL_MAX      16
#define ROLL_NAME_MAX 16
#define ROLL_EXPR_MAX 40
typedef struct {
    char name[ROLL_NAME_MAX];
    char expr[ROLL_EXPR_MAX];
} NamedRoll;

/* Notes on squares: "pressure plate", "the altar hides the key". A sparse
 * list, since a map with more than a few dozen is a novel. Not drawn in
 * play mode, where the map is what the players may see. */
#define MAP_NOTES_MAX 64

/* A named area: a box of squares with a name the GM or an agent gave it --
 * "Crypt", "Upper floor". It is a name and nothing else: walls are what
 * make a room, and an area neither draws nor blocks anything. It is what
 * the map tools, `marked` and the control channel's room language call a
 * place by. Names are compared ignoring case. */
#define AREA_NAME_MAX 32
#define MAP_AREAS_MAX 64
typedef struct {
    char    name[AREA_NAME_MAX];
    int16_t x0, y0, x1, y1;       /* inclusive, on the map */
    uint8_t floor;                /* a floor of the building: see floor.h */
    int8_t  level;                /* its elevation, when it is one */
} Area;
/* A link: two blocks of squares joined, so a creature on one can be sent
 * to the other -- stairs, a ladder, a trapdoor, a portal. Nothing crosses
 * by itself; see link.h. The kind only says how the ends are drawn. The
 * number is drawn on both ends and kept for the link's life, so removing
 * one never renumbers the rest. Kept in number order. */
#define MAP_LINKS_MAX 64
#define LINK_NUM_MAX  99
#define LINK_SIZE_MAX 3
typedef enum {
    LINK_STAIRS = 0,
    LINK_LADDER,
    LINK_TRAPDOOR,
    LINK_PORTAL,
    LINK_KIND_COUNT,
} LinkKind;
/* A link to another map file (docs/MAPLINKS.md) has one end here and names
 * the other: the map, by its file's name beside this one's, and a place in
 * it -- a named area, or a square. Its second end is kept equal to its first,
 * so a loop over both ends does no harm; link_ends says how many are here. */
#define LINK_MAP_MAX 24
typedef struct {
    int16_t x[2], y[2];           /* each end's top-left square */
    uint8_t num;                  /* 1..LINK_NUM_MAX */
    uint8_t kind;                 /* LinkKind */
    uint8_t size;                 /* each end is size x size, 1..LINK_SIZE_MAX */
    uint8_t oneway;               /* taken from end 0 only */
    uint8_t secret;               /* the GM's alone: never drawn for the players */
    char    to_map[LINK_MAP_MAX]; /* "" for a link on this map */
    char    to_place[AREA_NAME_MAX];
} Link;

static inline int link_ends(const Link *l) { return l->to_map[0] ? 1 : 2; }
/* A scene: the creatures and the fight saved under a name and put back
 * (docs/SCENES.md, scene.h). With `boxed` it is the creatures meeting the
 * box, and putting it back replaces only those; without, the whole map's,
 * and the round and spotlight with them. */
#define MAP_SCENES_MAX 16
#define SCENE_NAME_MAX 32
typedef struct {
    char      name[SCENE_NAME_MAX];
    uint8_t   boxed;
    int16_t   x0, y0, x1, y1;         /* inclusive, when boxed */
    int       round;
    int       spotlight;
    TokenList tokens;                 /* owned */
} Scene;

/* A card: text the GM keeps for a kind of creature -- a stat block,
 * tactics, anything -- shown beside the map while one is selected, never to
 * the players. Plain text; the core knows no fields (a ruleset may read
 * lines by their label). The text is the map's own, on the heap. */
#define MAP_CARDS_MAX 64
#define CARD_TEXT_MAX 4096
typedef struct {
    char  name[CARD_NAME_MAX];
    char *text;
} Card;

#define NOTE_MAX      TOKEN_NOTE_MAX
typedef struct {
    int16_t x, y;
    char    text[NOTE_MAX];
} Note;

/* Fog of war: see fog.h. One byte a tile beside `tiles` -- which patch, and
 * what the party can see of it -- and a small table of patches. */
#define FOG_PATCH_MAX     15
#define FOG_NAME_MAX      16
#define FOG_ID            0x0Fu   /* which patch, 1..15; 0 for none */
#define FOG_SEEN          0x10u   /* has been inside someone's sight */
#define FOG_LIT           0x20u   /* inside someone's sight now */
#define FOG_RIM           0x40u   /* unlit, next to a lit tile */
#define FOG_HELD          0x80u   /* lit by the GM's hand */
#define FOG_REVEAL_MANUAL (-1)
#define FOG_REVEAL_MAX    127   /* the most an int8_t holds; :fog stops at 99 */
typedef struct {
    char    name[FOG_NAME_MAX];   /* "" for an empty slot */
    int8_t  reveal;               /* tiles a player creature lights, or FOG_REVEAL_MANUAL */
    uint8_t memory;               /* lit ground stays drawn once the party has left */
    int8_t  soft_edge;            /* -1 follows the map's setting, else 0 or 1 */
    uint8_t disabled;             /* keeps its painting, hides nothing */
    uint8_t dead;                 /* deleted this session; never reused, see fog.h */
    int16_t x0, y0, x1, y1;       /* painted extent; x1 < x0 when empty */
} FogPatch;

#define SPOTLIGHT_PLAYERS 0
#define SPOTLIGHT_GM      1

/* ------------------------------------------------------------ coordinates */

/* Squares are named the way a battle map names them: columns run A, B ... Z,
 * AA, AB, and rows count from one. The file format stays 0-based x,y -- this
 * is only what gets said out loud across a table. */
#define MAP_COORD_MAX 12

void map_coord_name(int x, int y, char *out, size_t outsz);
/* "C3" for one square, "B2:F6" for more. */
void map_region_name(int x0, int y0, int x1, int y1, char *out, size_t outsz);

/* Parses "d6", "AA12", or "6" on its own -- a row with no column, which
 * leaves *x untouched. Returns 0 when the text is not a coordinate, which a
 * bare "d" is: a column with no row would collide with :e, :w, :x and :q. */
int  map_coord_parse(const char *s, int *x, int *y);

/* Terrain is decoration: everything except VOID is part of the map and
 * behaves identically. What difficult ground costs is a ruling between the GM
 * and the players, not something the tool decides. */
typedef enum {
    TILE_VOID = 0,      /* not part of the map; renders as nothing */
    TILE_FLOOR,         /* plain ground */
    TILE_WATER,
    TILE_ROUGH,         /* rubble, scree, debris */
    TILE_BRUSH,         /* grass, undergrowth */
    TILE_WOOD,          /* planking, bridge, deck */
    TILE_HAZARD,        /* fire, acid, spikes */
    TILE_COUNT,
} TileKind;

/* What sits on the boundary between two tiles. Movement and sight are
 * separate questions: a window stops one and not the other. */
typedef enum {
    EDGE_NONE = 0,
    EDGE_WALL,            /* stops both */
    EDGE_DOOR_CLOSED,     /* stops both, until opened */
    EDGE_DOOR_OPEN,       /* stops neither; still drawn, so you see the door */
    EDGE_WINDOW,          /* stops movement, not sight */
    EDGE_SECRET_CLOSED,   /* a wall to anyone reading the screen in play mode */
    EDGE_SECRET_OPEN,
    EDGE_COUNT,
} EdgeKind;

/* Names and the single character each takes in a saved map. */
const char *tile_name(uint8_t kind);
char        tile_file_char(uint8_t kind);
int         tile_from_file_char(char c);      /* -1 when unrecognized */

const char *edge_name(uint8_t kind);
char        edge_file_char(uint8_t kind);
int         edge_from_file_char(char c);      /* -1 when unrecognized */

/* Doors and secret doors toggle; everything else does not. */
int     edge_is_door(uint8_t kind);
uint8_t edge_toggled(uint8_t kind);

/* Sight's cache: what each creature alone lights, so a step recomputes the
 * creatures that moved rather than the whole party. One entry per token, in
 * list order; `vis` is a byte a square of the box its reach covers (empty
 * when x1 < x0 -- an enemy, or a creature out of every patch's reach). The
 * snapshot says what the entries were worked out from: tokens' positions,
 * sizes and sides, and every setting sight reads. fog.c owns all of it;
 * map_resize and map_free drop it. See docs/FOG.md, 3d. */
typedef struct {
    int16_t  x0, y0, x1, y1;
    uint8_t *vis;
    size_t   cap;
} SightEntry;

typedef struct {
    int16_t x, y;
    uint8_t size, kind;
} SightTok;

typedef struct {
    SightEntry *e;
    SightTok   *tok;
    int         n, cap;
    unsigned    shape;                    /* TokenList.shape the entries are for */
    int         valid;                    /* 0 until a full rebuild has run */
    unsigned    gen;                      /* the Map.gen the entries are for */
    int         fog_on, metric, w, h;
    FogPatch    patches[FOG_PATCH_MAX];
} Sight;

/* Walls live on the boundary *between* tiles, not on tiles themselves, so a
 * wall costs no floor space and blocking is an exact per-crossing question.
 *
 *   vedges[(w+1) * h]  vertical   walls: vedges(x,y) separates tile (x-1,y) | (x,y)
 *   hedges[w * (h+1)]  horizontal walls: hedges(x,y) separates tile (x,y-1) / (x,y)
 *
 * The +1 in each array is what makes the far edge of the last row/column
 * representable without special-casing. */
typedef struct {
    int      w, h;
    uint8_t *tiles;
    uint8_t *vedges;
    uint8_t *hedges;

    char name[MAP_NAME_MAX];
    char path[MAP_PATH_MAX];
    int  zoom;              /* preferred zoom level, persisted with the map */
    int  modified;          /* unsaved changes */
    unsigned gen;           /* bumped by every change; the autosave watches it */
    int  round;             /* of the fight; 0 when there is none */
    int  spotlight;         /* SPOTLIGHT_PLAYERS or SPOTLIGHT_GM, for a game that passes one */
    Clock clocks[CLOCK_MAX];
    NamedRoll rolls[ROLL_MAX];
    Note notes[MAP_NOTES_MAX];
    int  nnotes;
    Area areas[MAP_AREAS_MAX];    /* in the order they were named */
    int  nareas;
    Link links[MAP_LINKS_MAX];    /* in number order */
    int  nlinks;
    Scene scenes[MAP_SCENES_MAX]; /* in the order they were saved */
    int   nscenes;
    Card  cards[MAP_CARDS_MAX];   /* the GM's cards, by name; see card.h */
    int   ncards;
    uint8_t *fog;                         /* w*h, see FOG_* */
    int      fog_on;                      /* the master switch */
    int      fog_soft_edge;               /* the map's default for patches that follow it */
    FogPatch fog_patches[FOG_PATCH_MAX];
    Sight    sight;                       /* derived from the rest; never saved */

    /* Measurement settings travel with the encounter, since they belong to
     * the game being played rather than to the session. */
    double scale_ft;                      /* feet per tile */
    char   ruleset[MAP_RULESET_MAX];      /* range-band table, "" for none */
    int    metric;                        /* DistMetric */

    TokenList tokens;
} Map;

Map *map_new(int w, int h, const char *name);
void map_free(Map *m);

/* Resizes in place, preserving the overlapping region. */
int  map_resize(Map *m, int w, int h);

/* Every change to the map goes through here, so a watcher that compares
 * generations (the recovery autosave) can tell "changed since" from
 * "unsaved", which modified alone cannot once it has been set.
 *
 * Every change to a map touches it, and a change to one token touches
 * exactly once. Sight relies on both (docs/FOG.md, 3d): with the token list
 * the same shape -- nothing added or removed, so every index names the
 * creature it named -- each touch can leave at most one creature somewhere
 * else, so a keystroke that moved Map.gen by k and left exactly k creatures
 * elsewhere, with nothing else sight reads changed, was nothing but moves.
 * The fogdiff test checks the first half after every random op. */
static inline void map_touch(Map *m) { m->modified = 1; m->gen++; }

/* Drops sight's cache, for a map whose shape changed or is going away. */
void map_sight_drop(Map *m);

static inline int map_in_bounds(const Map *m, int x, int y)
{
    return x >= 0 && y >= 0 && x < m->w && y < m->h;
}

uint8_t map_tile(const Map *m, int x, int y);
void    map_set_tile(Map *m, int x, int y, uint8_t kind);

/* x in [0, w], y in [0, h) */
uint8_t map_vedge(const Map *m, int x, int y);
void    map_set_vedge(Map *m, int x, int y, uint8_t kind);

/* x in [0, w), y in [0, h] */
uint8_t map_hedge(const Map *m, int x, int y);
void    map_set_hedge(Map *m, int x, int y, uint8_t kind);

int map_walkable(const Map *m, int x, int y);

/* Does the boundary crossed by stepping (dx,dy) from (x,y) stop movement?
 * This asks only about the boundary, not about what is on the far side.
 * Orthogonal steps only. */
int map_edge_blocked(const Map *m, int x, int y, int dx, int dy);

/* Does that same boundary stop sight? Not the same question: a window stops
 * movement and not sight, an open door stops neither, and you can see across
 * a chasm you cannot walk over. */
int map_edge_opaque(const Map *m, int x, int y, int dx, int dy);

/* Can a token step from (x,y) by (dx,dy)? Adds to the wall test the
 * requirements that the destination exists and is walkable. Handles the four
 * diagonals too; a diagonal is blocked if either of the orthogonal crossings
 * it is made of is blocked, so you cannot slip through a corner. */
int map_blocked(const Map *m, int x, int y, int dx, int dy);

/* Sets a tile's fog byte and grows its patch's painted extent. Every write
 * goes through here, undo included, so the extent can only be too big,
 * never too small -- which is the safe way round for a box the sight walk
 * uses to skip work. */
void map_fog_set(Map *m, int x, int y, uint8_t f);

/* The note on a square, or NULL. Setting a blank removes it; returns 0
 * when there was no room for a new one. */
const char *map_note_at(const Map *m, int x, int y);
int         map_note_set(Map *m, int x, int y, const char *text);

/* Areas. A name is 1-31 characters, no quote or colon, and not something
 * that reads as a square (A1, C12) or a row: a name must never be taken
 * for a place. */
int  map_area_name_ok(const char *name);
int  map_area_find(const Map *m, const char *name);           /* index, or -1 */
/* The smallest area holding the square (the first named, among equals);
 * -1 for none. Floors are not counted: they hold rooms rather than being
 * one, and floor_at (floor.h) is how a floor is found. */
int  map_area_at(const Map *m, int x, int y);
/* Names a box (clipped to the map): replaces the area of that name, or adds
 * one. Returns its index, or -1 when the box is off the map, the name is
 * bad or the map holds all it can. Touches the map. */
int  map_area_set(Map *m, const char *name, int x0, int y0, int x1, int y1);
/* Returns 1 if there was one to remove. Touches the map. */
int  map_area_remove(Map *m, const char *name);

/* Fills a tile rectangle, clipped to the map. */
void map_fill_tiles(Map *m, int x0, int y0, int x1, int y1, uint8_t kind);

/* Lays or clears the wall outline of a tile rectangle. */
void map_rect_walls(Map *m, int x0, int y0, int x1, int y1, uint8_t kind);

#endif /* VTT_MAP_H */
