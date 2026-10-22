/* Tests: the players' frame holds nothing of the GM's (docs/V2.md, step 7, item 4, the second
 * check). privacy_scrub is the model of what is the GM's alone: it takes every such thing out
 * of the App for one draw. A seeded walk of keys over a map holding one of each asserts, after
 * every key in play mode, that the players' frame of the App equals the players' frame of the
 * App scrubbed. A difference is a leak until shown otherwise; a change to the scrub to explain
 * one is written in the plan's Progress with its reason. */

#include "harness.h"

/* What privacy_scrub took out, to put back. */
typedef struct {
    Map      *map;              /* the GM's; the App draws a scrubbed copy meanwhile */
    Map      *copy;
    Job       jobs[JOB_MAX];
    ModalKind modal;
    int       mode;
    char      status[sizeof ((App *)0)->status];
    int       status_gm;
    uint64_t  agent_until;
    int       box_off;
} Scrub;

/* Everything the GM knows and the players do not, out of the App:
 * - the hidden creatures' identity: label, kind, size, markers, card;
 * - every creature's note, numbers (counters) and card;
 * - the notes on squares;
 * - the secret links, and where a link to another map leads;
 * - the secret doors, as the players read them (a wall; an open door);
 * - the jobs, the GM's modal, `:` line, GM-only message, the agent's ring and the card box.
 * Not scrubbed, each a judgment written down: a hidden creature's place in the turn order and
 * its square (moving it would change what blocks whom); the areas' names (floors are found by
 * them); what fog hides (a wall on a lit square's edge is the players' to see).
 * A message is taken out only when it is marked GM-only (`status_gm`): one written with plain
 * app_note that should have been app_note_gm passes this check and the per-key one alike,
 * which is CLAUDE.md's rule for the author to keep, not something a frame can tell. */
static void privacy_scrub(App *a, Scrub *s)
{
    s->map  = a->world.map;
    s->copy = map_copy(s->map);
    Map *m = s->copy;
    for (int i = 0; i < m->tokens.n; i++) {
        Token *t = &m->tokens.v[i];
        t->note[0] = '\0';
        t->ncounters = 0;
        t->card[0] = '\0';
        if (!t->hidden) continue;
        memset(t->label, 0, sizeof t->label);
        t->kind = TOKEN_ENEMY;
        t->nstatus = 0;
    }
    m->nnotes = 0;
    int k = 0;
    for (int i = 0; i < m->nlinks; i++) {
        if (m->links[i].secret) continue;
        Link l = m->links[i];
        if (l.to_map[0]) { str_lcpy(l.to_map, "elsewhere", sizeof l.to_map); l.to_place[0] = '\0'; }
        m->links[k++] = l;
    }
    m->nlinks = k;
    size_t nv = (size_t)(m->w + 1) * (size_t)m->h, nh = (size_t)m->w * (size_t)(m->h + 1);
    for (size_t i = 0; i < nv + nh; i++) {
        uint8_t *e = i < nv ? &m->vedges[i] : &m->hedges[i - nv];
        if (*e == EDGE_SECRET_CLOSED) *e = EDGE_WALL;
        else if (*e == EDGE_SECRET_OPEN) *e = EDGE_DOOR_OPEN;
    }
    a->world.map = m;

    memcpy(s->jobs, a->world.jobs, sizeof s->jobs);
    memset(a->world.jobs, 0, sizeof a->world.jobs);
    s->modal = a->modal;            a->modal = MODAL_NONE;
    s->mode = a->ed.mode;           if (a->ed.mode == ED_COMMAND) a->ed.mode = ED_NORMAL;
    memcpy(s->status, a->status, sizeof s->status);
    s->status_gm = a->status_gm;    if (a->status_gm) { a->status[0] = '\0'; a->status_gm = 0; }
    s->agent_until = a->pings.agent.until_ms;  a->pings.agent.until_ms = 0;
    s->box_off = a->cards.box_off;  a->cards.box_off = 1;
}

static void privacy_unscrub(App *a, Scrub *s)
{
    a->world.map = s->map;
    map_free(s->copy);                 /* the struct too */
    memcpy(a->world.jobs, s->jobs, sizeof s->jobs);
    a->modal = s->modal;
    a->ed.mode = s->mode;
    memcpy(a->status, s->status, sizeof s->status);
    a->status_gm = s->status_gm;
    a->pings.agent.until_ms = s->agent_until;
    a->cards.box_off = s->box_off;
}

/* The players' frame of the App as it stands, into pr; the camera put back after. */
static void players_frame(App *a, Renderer *pr)
{
    Renderer *r = a->rnd;
    View was = a->view;
    GridView view = a->ed.view;
    PlayersView players = a->players;
    rnd_resize(pr, r->w, r->h);
    pr->clear_cell = r->clear_cell;
    rnd_begin(pr);
    a->rnd = pr;
    app_draw_view(a, VIEW_PLAYERS);
    a->rnd = r;
    a->view = was;
    a->ed.view = view;
    a->players = players;
}

static uint64_t walk_rng(uint64_t *s)
{
    *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17;
    return *s;
}

/* Keys a GM presses, both modes; `:` opens the line and Esc leaves it (a random command
 * could start a server or a program). */
static const char *const WALK_KEYS[] = {
    "h", "j", "k", "l", "H", "J", "K", "L", "w", "a", "s", "d", "\t", "\r", "\x1b", " ",
    "n", "N", "t", "T", "i", "u", "U", "x", "v", "V", "g", "o", "r", "R", "e", "c", "m",
    "p", "P", "f", "F", "1", "2", "3", "+", "-", "?", "z", "y", "b", "q", "\x1b[A", "\x1b[B",
    "\x1b[C", "\x1b[D", ":ab\x1b",
};

/* everything.vtt with a secret door on a vertical edge, written at path (again, when the
 * walk has closed or deleted it). */
static int privacy_map_at(const char *path)
{
    char *text = slurp("tests/fixtures/everything.vtt");
    if (!text) return -1;
    size_t len = strlen(text);
    FILE *f = fopen(path, "w");
    if (!f) { free(text); return -1; }
    /* The third vedges row, "|               |": a secret door at its eighth edge. */
    char *rows = strstr(text, "vedges\n");
    if (rows) {
        char *row = rows + 7;
        for (int k = 0; k < 2 && row; k++) row = strchr(row, '\n') ? strchr(row, '\n') + 1 : NULL;
        if (row && row[7] == ' ') row[7] = 'S';
    }
    fwrite(text, 1, len, f);
    fclose(f);
    free(text);
    return 0;
}

static int privacy_map(const char *dir, const char *name)
{
    char path[600];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return privacy_map_at(path);
}

/* Walks `n` seeded keys over the map at path; returns the frames compared. */
static int privacy_walk(const char *path, int fog, uint64_t seed, int n, int *diffs)
{
    Renderer r, p1, p2;
    App a;
    rnd_init(&r);
    rnd_init(&p1);
    rnd_init(&p2);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    int compared = 0;
    if (app_open_map(&a, path) != 0) { app_free(&a); rnd_free(&r); return -1; }
    a.world.map->fog_on = fog;            /* off: the players' frame is the GM's copied wherever it can be */
    Key f1 = { KEY_F1, 0, 0 }, f2 = { KEY_F2, 0, 0 };
    app_key(&a, f2);
    for (int i = 0; i < n; i++) {
        uint64_t x = walk_rng(&seed);
        if (x % 97 == 0)      app_key(&a, a.screen == SCREEN_PLAY ? f1 : f2);
        else                  press(&a, WALK_KEYS[x % (sizeof WALK_KEYS / sizeof *WALK_KEYS)]);
        /* Back to the map in play mode, as a GM would come back: the walk is
         * for what play mode shows the players. */
        if (!a.world.map) {
            if (privacy_map_at(path) != 0 || app_open_map(&a, path) != 0) break;
            a.world.map->fog_on = fog;
            app_key(&a, f2);
        } else if (a.screen != SCREEN_PLAY && a.screen != SCREEN_EDITOR) {
            press(&a, "\x1b");
        } else if (a.screen == SCREEN_EDITOR && x % 3 == 0) {
            app_key(&a, f2);
        }
        if (!a.world.map || a.screen != SCREEN_PLAY) continue;
        players_frame(&a, &p1);
        Scrub s;
        privacy_scrub(&a, &s);
        players_frame(&a, &p2);
        privacy_unscrub(&a, &s);
        compared++;
        size_t cells = (size_t)r.w * (size_t)r.h;
        if (!memcmp(p1.back, p2.back, cells * sizeof *p1.back)) continue;
        if ((*diffs)++ < 3) {
            for (size_t c = 0; c < cells; c++) {
                if (!memcmp(&p1.back[c], &p2.back[c], sizeof p1.back[c])) continue;
                size_t y = c / (size_t)r.w;
                fprintf(stderr, "  FAIL key %d (seed walk): the players' frame holds something the scrub takes out, at %zu,%zu\n",
                        i, c % (size_t)r.w, y);
                for (int v = 0; v < 2; v++) {
                    const Cell *row = (v ? p2.back : p1.back) + y * (size_t)r.w;
                    fprintf(stderr, "    %s: ", v ? "scrubbed" : "players ");
                    for (int xx = 0; xx < r.w; xx++)
                        fputc(row[xx].ch >= 32 && row[xx].ch < 127 ? (int)row[xx].ch : row[xx].ch ? '?' : ' ', stderr);
                    fputc('\n', stderr);
                }
                break;
            }
        }
    }
    app_free(&a);
    rnd_free(&r);
    rnd_free(&p1);
    rnd_free(&p2);
    return compared;
}

void test_privacy(void)
{
    Sandbox sb = sandbox_enter("privacy");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;

    for (int fog = 1; fog >= 0; fog--) {
        CASE(fog ? "2,000 seeded keys over one of every secret, fog on: the players' frame holds none of them"
                 : "2,000 seeded keys over one of every secret, fog off: the players' frame holds none of them");
        const char *name = fog ? "fog.vtt" : "clear.vtt";
        CHECK_EQ(privacy_map(sb.dir, name), 0);
        char path[600];
        snprintf(path, sizeof path, "%s/%s", sb.dir, name);
        int diffs = 0;
        int compared = privacy_walk(path, fog, UINT64_C(0x9E3779B97F4A7C15) + (uint64_t)fog, 2000, &diffs);
        printf("    %d frames compared\n", compared);
        CHECK(compared > 500);
        CHECK_EQ(diffs, 0);
    }
    sandbox_leave(&sb);
}
