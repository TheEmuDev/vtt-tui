#ifndef VTT_KEYS_H
#define VTT_KEYS_H

/* Every binding the app has, as data.
 *
 * The keybar along the bottom and the ? reference page both read these tables,
 * so the two cannot drift the way eight hand-written hint arrays could. A row
 * carrying a `bar` label appears on the bar, in table order; every row appears
 * on the page. */

typedef struct {
    /* NULL starts a new group on the page, and `what` is its heading. */
    const char *keys;
    const char *what;

    /* Keys as the bar spells them, when that differs from the page -- the page
     * lists `t T` on its own row, the bar folds three tracks into `t/f/e`. */
    const char *bar_keys;
    const char *bar;        /* short bar label, or NULL to stay off the bar */
} KeyDoc;

typedef struct {
    const char   *name;
    const KeyDoc *rows;
    int           n;
} KeyMap;

typedef enum {
    KEYS_PLAY = 0,
    KEYS_PLAY_GRABBED,
    KEYS_BUILD,
    KEYS_VISUAL,
    KEYS_WALL,
    KEYS_RULER,
    KEYS_BROWSER,
    KEYS_MENU,
    KEYS_COUNT,
} KeyMapId;

const KeyMap *keys_map(KeyMapId id);

#endif /* VTT_KEYS_H */
