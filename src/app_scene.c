/* Scenes in the app (docs/SCENES.md): :scene and :scenes. What a scene is and
 * how it goes back is scene.c's. Every message names a scene the players
 * must not hear of, so all of them are the GM's. */

#include <stdio.h>
#include <string.h>

#include "app_priv.h"
#include "world_ops.h"
#include "scene.h"
#include "turn.h"

void app_scene_restore(App *a, const char *name)
{
    char msg[240];
    WorldErr we;
    int  i = world_scene_find(&a->world, name, &we);
    if (i < 0) {
        snprintf(msg, sizeof msg, "%s - :scenes lists them", we.text);
        app_set_status_gm(a, msg);
        return;
    }
    if (a->play.grabbed) { app_set_status_gm(a, "put the creature down first - enter"); return; }
    int n = world_scene_restore(&a->world, i, &we);
    if (n < 0) { app_set_status_gm(a, we.text); return; }
    app_creatures_renumbered(a);
    snprintf(msg, sizeof msg, "scene %.31s is back - %d creature%s, u takes it back",
             a->world.map->scenes[i].name, n, n == 1 ? "" : "s");
    app_note_gm(a, msg);
}

/* The box being drawn, if any: build mode's v box the : came from, or play
 * mode's selection box. */
static int drawn_box(App *a, int box[4])
{
    if (app_cmd_vbox(a, &box[0], &box[1], &box[2], &box[3])) return 1;
    if (a->screen == SCREEN_PLAY && a->play.visual) {
        box[0] = a->play.anchor_x; box[1] = a->play.anchor_y;
        box[2] = a->ed.cx;         box[3] = a->ed.cy;
        a->play.visual = 0;
        return 1;
    }
    return 0;
}

static void list(App *a)
{
    char msg[256], what[64];
    if (!a->world.map->nscenes) { app_set_status_gm(a, "no scenes - :scene save NAME keeps the creatures as they stand"); return; }
    int off = snprintf(msg, sizeof msg, "scenes:");
    for (int i = 0; i < a->world.map->nscenes && off > 0 && off < (int)sizeof msg - 8; i++) {
        scene_describe(a->world.map, i, what, sizeof what);
        off += snprintf(msg + off, sizeof msg - (size_t)off, "%s %s (%s)", i ? "," : "", a->world.map->scenes[i].name, what);
    }
    app_set_status_gm(a, msg);
}

/* :scene                 the picker
 * :scenes                list them
 * :scene save NAME       keep the creatures as they stand (a v box: those in it)
 * :scene NAME            put it back
 * :scene NAME remove     throw it away */
void app_scene_command(App *a, const char *verb, const char *rest)
{
    char msg[200], name[SCENE_NAME_MAX + 16];
    if (!a->world.map) return;
    if (!strcmp(verb, "scenes")) { list(a); return; }
    if (!*rest) { app_pick_open(a, PICK_SCENE, -1, ""); return; }

    /* The words, with the spaces round them and between them made single. */
    char line[160];
    size_t k = 0;
    for (const char *p = rest; *p && k + 1 < sizeof line; p++)
        if (*p != ' ' || (k && line[k - 1] != ' ')) line[k++] = *p;
    while (k && line[k - 1] == ' ') k--;
    line[k] = '\0';
    rest = line;

    if (!strncmp(rest, "save", 4) && (rest[4] == ' ' || !rest[4])) {
        const char *nm = rest + 4;
        while (*nm == ' ') nm++;
        if (!*nm) { app_set_status_gm(a, ":scene save NAME keeps the creatures as they stand"); return; }
        if (a->play.grabbed) { app_set_status_gm(a, "put the creature down first - enter"); return; }
        int box[4], boxed = drawn_box(a, box);
        WorldErr we;
        int i = world_scene_save(&a->world, nm, boxed ? box : NULL, &we);
        if (i < 0) {
            if (we.cls == CTL_E_LIMIT) {                    /* the GM's way to make room */
                snprintf(msg, sizeof msg, "%.150s - :scene NAME remove makes room", we.text);
                app_set_status_gm(a, msg);
            } else app_set_status_gm(a, we.text);
            return;
        }
        char what[64];
        scene_describe(a->world.map, i, what, sizeof what);
        snprintf(msg, sizeof msg, "scene %.31s saved - %s; :scene %.31s puts it back", a->world.map->scenes[i].name, what,
                 a->world.map->scenes[i].name);
        app_note_gm(a, msg);
        return;
    }

    str_lcpy(name, rest, sizeof name);
    /* A verb after the name, unless the whole of it is a scene's name. */
    if (scene_find(a->world.map, name) < 0 && str_cut_word(name, "off")) {
        snprintf(msg, sizeof msg, ":scene %.31s remove throws a scene away", name);
        app_set_status_gm(a, msg);
        return;
    }
    if (scene_find(a->world.map, name) < 0 && str_cut_word(name, "remove")) {
        WorldErr we;
        int i = world_scene_find(&a->world, name, &we);
        if (i < 0) { app_set_status_gm(a, we.text); return; }
        snprintf(msg, sizeof msg, "scene %.31s removed", a->world.map->scenes[i].name);
        scene_remove(a->world.map, i);
        app_note_gm(a, msg);
        return;
    }
    app_scene_restore(a, name);
}
