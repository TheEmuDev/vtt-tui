/* The test runner: every suite, in the order they run. The suites live in
 * the test_*.c files by area; harness.h is what they share. */

#include "harness.h"

/* build/run-tests runs every suite; build/run-tests NAME... only the ones
 * named (see the table), for the one being worked on. */
int main(int argc, char **argv)
{
    prof_init();

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
        { "ctlchars", test_ctl_characters },
        { "scenes", test_scenes },
        { "maplinks", test_map_links },
        { "handouts", test_handouts },
        { "joinframe", test_join_frame },
        { "handoutkeys", test_handout_keys },
        { "scenekeys", test_scene_keys },
        { "ctlscenes", test_ctl_scenes },
        { "areas", test_areas },
        { "links", test_links },
        { "linkkeys", test_link_keys },
        { "floors", test_floors },
        { "floorview", test_floor_view },
        { "floorplayers", test_floor_players },
        { "floorcam", test_floor_big_camera },
        { "hidden", test_hidden },
        { "graymarker", test_gray_marker },
        { "roomlang", test_room_language },
        { "corridors", test_corridors },
        { "corridor2", test_corridor_edges },
        { "apply", test_apply },
        { "wire",   test_wire },
        { "mapdiag", test_map_diag },
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
        { "webpage", test_webpage },
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
        { "netedges",   test_net_edges },
        { "playbox",    test_play_box },
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

    int ran = 0;
    for (size_t i = 0; i < sizeof suites / sizeof *suites; i++) {
        int wanted = argc < 2;
        for (int k = 1; k < argc && !wanted; k++) wanted = !strcmp(argv[k], suites[i].name);
        if (!wanted) continue;
        ran++;
        int before = g_fails;
        suites[i].fn();
        printf("  %-8s %s\n", suites[i].name, g_fails == before ? "ok" : "FAILED");
    }

    prof_shutdown();
    /* A name that matched nothing is a typo, beside a real one or not. */
    int unknown = 0;
    for (int k = 1; k < argc; k++) {
        int found = 0;
        for (size_t i = 0; i < sizeof suites / sizeof *suites && !found; i++) found = !strcmp(argv[k], suites[i].name);
        if (!found) { fprintf(stderr, "no suite called %s\n", argv[k]); unknown = 1; }
    }
    if (unknown || (argc >= 2 && !ran)) return 2;

    printf("\n%d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
