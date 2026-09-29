#ifndef VTT_TEST_HARNESS_H
#define VTT_TEST_HARNESS_H

/* What the test files share: the counters and their macros, the helpers more
 * than one file uses, and every suite for run.c's table. Zero dependencies
 * here too: a counter, a macro, and a list of functions. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "app.h"
#include "counter.h"
#include "fog.h"
#include "net.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include "dice.h"
#include "wire.h"
#include "editor.h"
#include "grid.h"
#include "map.h"
#include "mapio.h"
#include "maptools.h"
#include "link.h"
#include "floor.h"
#include "character.h"
#include "scene.h"
#include "turn.h"
#include "stamp.h"
#include "theme.h"
#include "token.h"
#include "play.h"
#include "ruler.h"
#include "undo.h"
#include "draw.h"
#include "keys.h"
#include "util.h"
#include "ui.h"
#include "input.h"
#include "prof.h"
#include "render.h"
#include "util.h"

extern int g_checks;
extern int g_fails;
extern const char *g_case;

#define CHECK(cond)                                                            \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_fails++;                                                         \
            fprintf(stderr, "  FAIL %s:%d [%s] %s\n",                          \
                    __FILE__, __LINE__, g_case, #cond);                        \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        long long va = (long long)(a), vb = (long long)(b);                    \
        g_checks++;                                                            \
        if (va != vb) {                                                        \
            g_fails++;                                                         \
            fprintf(stderr, "  FAIL %s:%d [%s] %s == %s (%lld vs %lld)\n",     \
                    __FILE__, __LINE__, g_case, #a, #b, va, vb);               \
        }                                                                      \
    } while (0)

#define CASE(name) do { g_case = (name); } while (0)

/* ------------------------------------------------------ shared types */

typedef struct {
    char dir[1024];
    char datadir[1100];
    char cwd[1024];
    char saved_xdg[1024];
    int  ok;
} Sandbox;

typedef struct {
    int      w, h, fulls, ends, keepalives;
    uint32_t pal[256];
    Cell     grid[64 * 32];
    int      runs, glyphs;
    int      handouts;                /* 'H' records seen, and the last one's text */
    char     handout[WIRE_HANDOUT_MAX + 1];
    size_t   handout_n;
} WireCatch;

typedef struct { int fd; char buf[8192]; size_t n; int done, reset; } CtlReader;

/* ------------------------------------------------------------ map tools */

/* What a map tool printed, as a string the caller frees. */
typedef void (*DumpFn)(FILE *out, const Map *m, int x0, int y0, int x1, int y1);

/* --------------------------------------------------- shared helpers */

void feed(InputParser *p, const char *s);
void golden_bytes(const char *name, const char *data, size_t len);
void golden(const char *name, int w, int h, const char *map_path,
            const char *const *segments, int nsegments, int ascii);
void write_map_file(const char *dir, const char *name);
void press(App *a, const char *keys);
Sandbox sandbox_enter(const char *tag);
void sandbox_leave(Sandbox *s);
double luminance(uint32_t c);
double contrast(uint32_t a, uint32_t b);
char *slurp(const char *path);
char *tool_text(const Map *m, int x0, int y0, int x1, int y1, size_t *len);
int json_valid(const char *s);
char *describe_text(const Map *m, int json, size_t *len);
int net_connect(uint16_t port);
void net_pump(Net *n, uint64_t now_ms);
void front_text(const Renderer *r, ByteBuf *out);
void write_sight_map(const char *dir, const char *name, int reveal, int memory);
char *ctl_ask(App *a, const char *req);
int ctl_blank_map(App *a, const char *dir, int w, int h);

/* ------------------------------------------------------------ suites */

void test_utf8(void);
void test_input(void);
void test_render(void);
void test_draw(void);
void test_util(void);
void test_app_smoke(void);
void test_map(void);
void test_tokens(void);
void test_mapio(void);
void test_grid(void);
void test_editor(void);
void test_undo(void);
void test_editing(void);
void test_play(void);
void test_token_draw(void);
void test_golden(void);
void test_term_io(void);
void test_dist(void);
void test_ruleset(void);
void test_ruler(void);
void test_sight(void);
void test_measure_settings(void);
void test_range(void);
void test_range_sight(void);
void test_edges(void);
void test_terrain(void);
void test_secret_doors(void);
void test_map_format_v2(void);
void test_edge_tools(void);
void test_delete_map(void);
void test_rename_map(void);
void test_duplicate_map(void);
void test_status(void);
void test_unique_label(void);
void test_status_io(void);
void test_token_edit_undo(void);
void test_clear_status_keys(void);
void test_trail(void);
void test_trail_draw(void);
void test_covering(void);
void test_choosing(void);
void test_size_keys(void);
void test_selection_contrast(void);
void test_group(void);
void test_group_yank(void);
void test_brush(void);
void test_cursor_size(void);
void test_overlap(void);
void test_passing_and_stopping(void);
void test_route_avoids_enemies(void);
void test_occupancy_keys(void);
void test_move_label(void);
void test_cancel_move(void);
void test_delete_yanks(void);
void test_void_reads_as_void(void);
void test_terrain_palette(void);
void test_coords(void);
void test_jump(void);
void test_labels(void);
void test_shapes(void);
void test_circle_fill(void);
void test_circle_walls(void);
void test_shape_keys(void);
void test_keymaps(void);
void test_keybar_fits(void);
void test_help_page(void);
void test_play_remap(void);
void test_cycle_tracks(void);
void test_cycle_keys(void);
void test_play_focus(void);
void test_status_draw(void);
void test_cull(void);
void test_watch_target(void);
void test_loader_damage(void);
void test_hidden(void);
void test_dice(void);
void test_session_log(void);
void test_clocks(void);
void test_notes(void);
void test_autosave(void);
void test_roll_command(void);
void test_turns(void);
void test_turn_keys(void);
void test_wire(void);
void test_net_primitives(void);
void test_map_tools_describe(void);
void test_map_tools_check(void);
void test_map_tools_dump(void);
void test_map_diag(void);
void test_net_live(void);
void test_net_msg(void);
void test_net_server(void);
void test_serve_lifetime(void);
void test_players_frame(void);
void test_counters(void);
void test_pings(void);
void test_fog(void);
void test_sight_walk(void);
void test_fog_diff(void);
void test_fog_edge(void);
void test_fog_sight(void);
void test_serve_commands(void);
void test_webpage(void);
void test_stamps(void);
void test_undo_nesting(void);
void test_ctl(void);
void test_ctl_marked(void);
void test_ctl_edits(void);
void test_ctl_cap(void);
void test_stamp_keys(void);
void test_picker(void);
void test_characters(void);
void test_ctl_characters(void);
void test_scenes(void);
void test_map_links(void);
void test_handouts(void);
void test_join_frame(void);
void test_handout_keys(void);
void test_scene_keys(void);
void test_ctl_scenes(void);
void test_gray_marker(void);
void test_areas(void);
void test_links(void);
void test_link_keys(void);
void test_floors(void);
void test_floor_view(void);
void test_floor_players(void);
void test_floor_big_camera(void);
void test_room_language(void);
void test_corridors(void);
void test_corridor_edges(void);
void test_apply(void);
void test_ctl_live(void);

#endif /* VTT_TEST_HARNESS_H */
