/* The test runner: every suite, in the order they run. The suites live in
 * the test_*.c files by area; harness.h is what they share. */

#include "harness.h"
#include "conn.h"
#include "disk.h"

/* build/run-tests runs every suite; build/run-tests NAME... only the ones
 * named (see the table), for the one being worked on. -jN runs N suites at once, each in a
 * forked child of its own (docs/TESTSPEED.md, step 3): the default is a job a
 * core, -j1 the one process and order of before. A child's output is printed
 * whole and in the table's order; a child that crashes, or that
 * LeakSanitizer fails at its exit, fails its suite and no other. */

#include <sys/wait.h>
#include <unistd.h>

/* Whether this process reached its own end: an `exit` from under a suite --
 * in -j1's one process, or a child's -- fails the run rather than ending it
 * with no word (exit status 0 and no counts, read as a pass). */
static int g_finished;
static const char *g_suite = "";
static void unfinished(void)
{
    if (g_finished) return;
    fprintf(stderr, "  FAIL %s: the process ended under it, before its counts (case: %s)\n",
            g_suite[0] ? g_suite : "the runner", g_case[0] ? g_case : "none begun");
    fflush(NULL);
    _exit(1);
}

/* One suite and the checks after it: no test's disk or sockets left in use. */
static void run_suite(const char *name, void (*fn)(void))
{
    g_suite = name;
    fn();
    case_begin(NULL);                    /* VTT_CASE_TIMES: its last case's time */
    if (disk() != &DISK_REAL) {          /* a suite's failing disk must not reach the next */
        g_fails++;
        printf("  FAIL %s left a test's disk in use\n", name);
        disk_use(NULL);
    }
    if (conn() != &CONN_REAL) {          /* nor its phones' sockets */
        g_fails++;
        printf("  FAIL %s left a test's sockets in use\n", name);
        conn_use(NULL);
    }
}

int main(int argc, char **argv)
{
    prof_init();
    atexit(unfinished);
    /* No suite's server takes the default port, sandboxed or not (net.h,
     * NET_DEFAULT_PORT): the user's vtt may want it. */
    setenv("VTT_SERVE_PORT", "0", 1);

    struct { const char *name; void (*fn)(void); } suites[] = {
        { "util",   test_util },
        { "utf8",   test_utf8 },
        { "input",  test_input },
        { "render", test_render },
        { "draw",   test_draw },
        { "map",    test_map },
        { "tokens", test_tokens },
        { "mapio",  test_mapio },
        { "grid",   test_grid },
        { "editor", test_editor },
        { "undo",   test_undo },
        { "undonest", test_undo_nesting },
        { "stamps", test_stamps },
        { "stampkeys", test_stamp_keys },
        { "picker", test_picker },
        { "characters", test_characters },
        { "summon", test_summon },
        { "ctlchars", test_ctl_characters },
        { "scenes", test_scenes },
        { "maplinks", test_map_links },
        { "handouts", test_handouts },
        { "whisper",  test_whisper_net },
        { "whisperapp", test_whisper_app },
        { "joinframe", test_join_frame },
        { "handoutkeys", test_handout_keys },
        { "scenekeys", test_scene_keys },
        { "ctlscenes", test_ctl_scenes },
        { "areas", test_areas },
        { "links", test_links },
        { "changeset", test_changeset },
        { "jobs", test_jobs },
        { "jobsctl", test_jobs_ctl },
        { "events", test_events },
        { "agentcmd", test_agent_command },
        { "disk", test_disk },
        { "diskwhole", test_disk_whole },
        { "diskfail", test_disk_fail },
        { "filefind", test_file_findings },
        { "checkhelp", test_check_help },
        { "linkkeys", test_link_keys },
        { "floors", test_floors },
        { "floorview", test_floor_view },
        { "floorplayers", test_floor_players },
        { "floorcam", test_floor_big_camera },
        { "camera", test_camera },
        { "camerafloors", test_camera_floors },
        { "hidden", test_hidden },
        { "graymarker", test_gray_marker },
        { "roomlang", test_room_language },
        { "corridors", test_corridors },
        { "corridor2", test_corridor_edges },
        { "apply", test_apply },
        { "wire",   test_wire },
        { "mapdiag", test_map_diag },
        { "mapfile", test_map_fidelity },
        { "mapshift", test_map_shift },
        { "mapdump", test_map_tools_dump },
        { "mapdescribe", test_map_tools_describe },
        { "mapcheck", test_map_tools_check },
        { "netprim", test_net_primitives },
        { "netserver", test_net_server },
        { "netmsg", test_net_msg },
        { "netlive", test_net_live },
        { "serve",  test_serve_commands },
        { "servelife", test_serve_lifetime },
        { "pframe", test_players_frame },
        { "counters", test_counters },
        { "conditions", test_conditions },
        { "fog",    test_fog },
        { "fogsight", test_fog_sight },
        { "fogedge", test_fog_edge },
        { "sightwalk", test_sight_walk },
        { "fogdiff", test_fog_diff },
        { "pings", test_pings },
        { "ctl",   test_ctl },
        { "ctlmarked", test_ctl_marked },
        { "ctledit", test_ctl_edits },
        { "ctlcap", test_ctl_cap },
        { "ctllive", test_ctl_live },
        { "loop", test_loop },
        { "ctltable", test_ctl_table },
        { "ctlreads", test_ctl_reads },
        { "ctlnames", test_ctl_names },
        { "protocol", test_protocol },
        { "webpage", test_webpage },
        { "agentguide", test_agent_guide },
        { "apiagent", test_api_agent },
        { "apiparts", test_api_agent_parts },
        { "settings", test_settings },
        { "apikey", test_api_key },
        { "options", test_options },
        { "askagent", test_ask_agent },
        { "turns",  test_turns },
        { "turnkeys", test_turn_keys },
        { "dice",   test_dice },
        { "slog",   test_session_log },
        { "clocks", test_clocks },
        { "notes",  test_notes },
        { "autosave", test_autosave },
        { "roll",   test_roll_command },
        { "editing", test_editing },
        { "play",   test_play },
        { "tokendraw", test_token_draw },
        { "app",    test_app_smoke },
        { "dist",     test_dist },
        { "ruleset",  test_ruleset },
        { "ruler",    test_ruler },
        { "sight",    test_sight },
        { "measure",  test_measure_settings },
        { "edges",    test_edges },
        { "terrain",  test_terrain },
        { "secret",   test_secret_doors },
        { "formatv2", test_map_format_v2 },
        { "edgetools", test_edge_tools },
        { "range",    test_range },
        { "rangelos", test_range_sight },
        { "termio", test_term_io },
        { "delmap", test_delete_map },
        { "renmap", test_rename_map },
        { "dupmap", test_duplicate_map },
        { "status",   test_status },
        { "uniqlbl",  test_unique_label },
        { "statusio", test_status_io },
        { "tokedit",  test_token_edit_undo },
        { "statusdraw", test_status_draw },
        { "cull",       test_cull },
        { "watchaddr",  test_watch_target },
        { "damage",     test_loader_damage },
        { "pagefeed",   test_page_feed },
        { "pageconn",   test_page_conn },
        { "docs",       test_docs },
        { "netedges",   test_net_edges },
        { "netlag",     test_net_lag },
        { "netbig",     test_net_big },
        { "privacy",    test_privacy },
        { "sandbox",    test_sandbox },
        { "playbox",    test_play_box },
        { "moverules",  test_move_rules },
        { "burst",      test_group_effect },
        { "cards",      test_cards },
        { "dmg",        test_damage },
        { "cardbox",    test_card_box },
        { "cardedit",   test_card_edit },
        { "env",        test_env },
        { "envimport",  test_env_import },
        { "cardtpl",    test_card_templates },
        { "jsonread",   test_json_read },
        { "import",     test_import },
        { "cardfixes",  test_card_fixes },
        { "clearstatus", test_clear_status_keys },
        { "trail",    test_trail },
        { "traildraw", test_trail_draw },
        { "focus",    test_play_focus },
        { "tracks",   test_cycle_tracks },
        { "cyclekeys", test_cycle_keys },
        { "overlap",  test_overlap },
        { "passing",  test_passing_and_stopping },
        { "route",    test_route_avoids_enemies },
        { "occkeys",  test_occupancy_keys },
        { "delyank",  test_delete_yanks },
        { "cancel",   test_cancel_move },
        { "movelabel", test_move_label },
        { "cursorsize", test_cursor_size },
        { "covering", test_covering },
        { "choosing", test_choosing },
        { "sizekeys", test_size_keys },
        { "selection", test_selection_contrast },
        { "group", test_group },
        { "groupyank", test_group_yank },
        { "brush", test_brush },
        { "voidlook", test_void_reads_as_void },
        { "palette",  test_terrain_palette },
        { "coords",   test_coords },
        { "jump",     test_jump },
        { "labels",   test_labels },
        { "shapes",   test_shapes },
        { "circlefill", test_circle_fill },
        { "circlewall", test_circle_walls },
        { "shapekeys", test_shape_keys },
        { "keymaps",  test_keymaps },
        { "keybar",   test_keybar_fits },
        { "help",     test_help_page },
        { "remap",    test_play_remap },
        { "golden", test_golden },
    };

    /* -jN, anywhere among the names, else VTT_JOBS, else a job a core (the
     * Makefile and tools/oracle.sh read VTT_JOBS too); then the names. */
    const char *vj = getenv("VTT_JOBS");
    long jobs = vj && *vj ? atol(vj) : sysconf(_SC_NPROCESSORS_ONLN);
    for (int k = 1; k < argc; k++) {
        if (strncmp(argv[k], "-j", 2)) continue;
        jobs = atol(argv[k] + 2);
        for (int m = k; m < argc - 1; m++) argv[m] = argv[m + 1];
        argc--;
        k--;
    }
    if (jobs < 1) jobs = 1;
    enum { NSUITES = sizeof suites / sizeof *suites };
    int want[NSUITES], ran = 0;
    for (size_t i = 0; i < NSUITES; i++) {
        int wanted = argc < 2;
        for (int k = 1; k < argc && !wanted; k++) wanted = !strcmp(argv[k], suites[i].name);
        want[i] = wanted;
        ran += wanted;
    }

    if (jobs == 1) {
        for (size_t i = 0; i < NSUITES; i++) {
            if (!want[i]) continue;
            int before = g_fails;
            run_suite(suites[i].name, suites[i].fn);
            printf("  %-8s %s\n", suites[i].name, g_fails == before ? "ok" : "FAILED");
        }
    } else {
        /* Each suite's output into a file of its own, its counts into
         * another; up to `jobs` children at once; printed as soon as every
         * suite before it in the table is. */
        FILE *out[NSUITES] = { 0 }, *res[NSUITES] = { 0 };
        pid_t pid[NSUITES];
        int   done[NSUITES] = { 0 }, status[NSUITES] = { 0 };
        size_t next = 0, shown = 0;
        int running = 0;
        fflush(NULL);
        for (;;) {
            while (running < jobs && next < NSUITES) {
                if (!want[next]) { done[next] = 1; next++; continue; }
                out[next] = tmpfile();
                res[next] = tmpfile();
                pid[next] = out[next] && res[next] ? fork() : -1;
                if (pid[next] == 0) {
                    int fd = fileno(out[next]);
                    dup2(fd, 1);
                    dup2(fd, 2);
                    setvbuf(stdout, NULL, _IONBF, 0);       /* its lines in the order written */
                    g_checks = g_fails = 0;
                    run_suite(suites[next].name, suites[next].fn);
                    fprintf(res[next], "%d %d\n", g_checks, g_fails);
                    fflush(NULL);
                    g_finished = 1;
                    exit(0);                                  /* exit, not _exit: LeakSanitizer runs */
                }
                if (pid[next] < 0) { status[next] = -1; done[next] = 1; }
                else running++;
                next++;
            }
            while (shown < NSUITES && done[shown]) {
                if (want[shown]) {
                    int c = 0, f = 0, fails_before = g_fails;
                    if (out[shown]) {
                        rewind(out[shown]);
                        char buf[8192];
                        size_t n;
                        while ((n = fread(buf, 1, sizeof buf, out[shown])) > 0) fwrite(buf, 1, n, stdout);
                        fclose(out[shown]);
                    }
                    int counted = 0;
                    if (res[shown]) {
                        rewind(res[shown]);
                        counted = fscanf(res[shown], "%d %d", &c, &f) == 2;
                        fclose(res[shown]);
                    }
                    g_checks += c;
                    g_fails += f;
                    /* No counts: the suite ended its process before its end
                     * (an exit somewhere under it) -- never a pass. */
                    if (!counted && status[shown] == 0) {
                        g_fails++;
                        printf("  FAIL %s ended before its counts were written\n", suites[shown].name);
                    }
                    if (status[shown] != 0) {
                        g_fails++;
                        if (status[shown] < 0) printf("  FAIL %s could not be started\n", suites[shown].name);
                        else if (WIFSIGNALED(status[shown])) printf("  FAIL %s died of signal %d\n", suites[shown].name, WTERMSIG(status[shown]));
                        else printf("  FAIL %s exited %d (its own output above says why: a leak, a crash, an exit)\n", suites[shown].name, WEXITSTATUS(status[shown]));
                    }
                    printf("  %-8s %s\n", suites[shown].name, g_fails == fails_before ? "ok" : "FAILED");
                    fflush(stdout);
                }
                shown++;
            }
            if (shown == NSUITES) break;
            int st;
            pid_t w = wait(&st);
            if (w < 0) break;
            for (size_t i = 0; i < NSUITES; i++)
                if (!done[i] && want[i] && i < next && pid[i] == w) { status[i] = st; done[i] = 1; running--; }
        }
    }

    prof_shutdown();
    /* A name that matched nothing is a typo, beside a real one or not. */
    int unknown = 0;
    for (int k = 1; k < argc; k++) {
        int found = 0;
        for (size_t i = 0; i < sizeof suites / sizeof *suites && !found; i++) found = !strcmp(argv[k], suites[i].name);
        if (!found) { fprintf(stderr, "no suite called %s\n", argv[k]); unknown = 1; }
    }
    if (unknown || (argc >= 2 && !ran)) { g_finished = 1; return 2; }

    printf("\n%d checks, %d failures\n", g_checks, g_fails);
    g_finished = 1;
    return g_fails ? 1 : 0;
}
