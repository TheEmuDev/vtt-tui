/* The menu and the map browser: listing the maps, and renaming, copying and
 * deleting their files (with the recovery copy that goes with each). */

#include "app_priv.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void app_refresh_entries(App *a)
{
    free(a->entries);
    a->entries  = NULL;
    a->nentries = mapio_scan(&a->entries);
    a->browser.sel = 0;
    a->browser.top = 0;
}

/* Rescans without losing your place, so deleting several in a row does not
 * send the caret back to the top each time. */
void app_rescan_keeping_place(App *a)
{
    int sel = a->browser.sel;
    app_refresh_entries(a);
    a->browser.sel = iclamp(sel, 0, imax(0, a->nentries - 1));
    ui_list_move(&a->browser, a->nentries, 0, app_menu_visible_rows(a));
}

/* Copies bytes, refusing to write over anything. The "x" mode is C11's
 * exclusive create, so the check and the create are one operation rather than
 * a test that another process could slip past. */
static int copy_file(const char *from, const char *to, char *err, size_t errsz)
{
    FILE *in = fopen(from, "rb");
    if (!in) {
        snprintf(err, errsz, "%.60s", strerror(errno));
        return -1;
    }

    FILE *out = fopen(to, "wbx");
    if (!out) {
        snprintf(err, errsz, errno == EEXIST ? "already exists" : "%.60s",
                 strerror(errno));
        fclose(in);
        return -1;
    }

    char   buf[8192];
    size_t n;
    int    ok = 1;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    if (ferror(in)) ok = 0;

    fclose(in);
    if (fclose(out) != 0) ok = 0;

    if (!ok) {
        snprintf(err, errsz, "%.60s", strerror(errno));
        unlink(to);          /* never leave half a map behind */
        return -1;
    }
    return 0;
}

/* Splits a map path into the directory it lives in and its name without the
 * extension, which is what both renaming and duplicating start from. */
static void split_map_path(const char *path, char *dir, size_t dirsz,
                           char *base, size_t basesz)
{
    str_lcpy(dir, path, dirsz);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    else       str_lcpy(dir, ".", dirsz);

    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    str_lcpy(base, name, basesz);
    str_cut_suffix(base, ".vtt");
}

/* Strips a trailing " copy" or " copy 3" so that duplicating a duplicate
 * counts up from the original rather than stacking the word: a copy of
 * "goblin copy" should be offered "goblin copy 2", not "goblin copy copy". */
static void strip_copy_suffix(char *base)
{
    size_t n = strlen(base);

    /* Walk back over a trailing number, if there is one. */
    size_t end = n;
    while (end > 0 && base[end - 1] >= '0' && base[end - 1] <= '9') end--;
    if (end < n && end > 0 && base[end - 1] == ' ') end--;
    else if (end < n) return;                  /* digits with no space before */

    const size_t clen = 5;                     /* " copy" */
    if (end >= clen && strncmp(base + end - clen, " copy", clen) == 0)
        base[end - clen] = '\0';
}

/* "goblin ambush" -> "goblin ambush copy", then "copy 2" and so on, so the
 * offered name is one you can accept without thinking. */
static void suggest_copy_name(const char *dir, const char *base,
                              char *out, size_t outsz)
{
    char root[MAP_NAME_MAX];
    str_lcpy(root, base, sizeof root);
    strip_copy_suffix(root);
    if (!root[0]) str_lcpy(root, base, sizeof root);

    for (int i = 1; i < 100; i++) {
        char cand[MAP_NAME_MAX];
        if (i == 1) snprintf(cand, sizeof cand, "%.40s copy", root);
        else        snprintf(cand, sizeof cand, "%.40s copy %d", root, i);

        char path[MAP_PATH_MAX];
        snprintf(path, sizeof path, "%.400s/%.80s.vtt", dir, cand);
        if (access(path, F_OK) != 0) { str_lcpy(out, cand, outsz); return; }
    }
    str_lcpy(out, base, outsz);
}

/* Validates a name the user typed and builds the path it names, beside the
 * file it came from. Reports the reason and returns -1 when it will not do. */
static int build_dest_path(App *a, const char *from, const char *typed,
                           char *base, size_t basesz, char *to, size_t tosz)
{
    str_lcpy(base, typed, basesz);

    /* Trim an extension the user typed, so "x.vtt" does not become
     * "x.vtt.vtt". */
    str_cut_suffix(base, ".vtt");

    if (!base[0]) { app_set_status(a, "canceled: a map needs a name"); return -1; }
    if (strchr(base, '/')) {
        app_set_status(a, "a name cannot contain '/': this names a map, not a path");
        return -1;
    }

    char dir[MAP_PATH_MAX], unused[MAP_NAME_MAX];
    split_map_path(from, dir, sizeof dir, unused, sizeof unused);
    snprintf(to, tosz, "%.400s/%.80s.vtt", dir, base);
    return 0;
}

/* Sets the title inside a saved map. Best effort: a map too damaged to load
 * keeps whatever title it had. */
static int retitle_map(const char *path, const char *title)
{
    char err[MAPIO_ERR_MAX] = { 0 };
    Map *m = mapio_load(path, err, sizeof err);
    if (!m) return 0;

    str_lcpy(m->name, title, sizeof m->name);
    int ok = (mapio_save(m, path, err, sizeof err) == 0);
    map_free(m);
    return ok;
}

/* Rescans, then puts the caret on a particular file rather than leaving it on
 * whatever now sits at the old index. */
static void select_path(App *a, const char *path)
{
    app_rescan_keeping_place(a);
    for (int i = 0; i < a->nentries; i++) {
        if (strcmp(a->entries[i].path, path) == 0) {
            a->browser.sel = i;
            ui_list_move(&a->browser, a->nentries, 0, app_menu_visible_rows(a));
            return;
        }
    }
}

/* Renames the file, and then its title to match if the map will parse.
 *
 * The file move comes first and on its own: it preserves the contents exactly
 * and works even on a map too damaged to load, which is when you most want to
 * be able to move it out of the way. Updating the title is best-effort on top
 * of an already-completed rename, so a failure there costs nothing. */
void app_rename_map(App *a, const char *from, const char *typed)
{
    char base[MAP_NAME_MAX], to[MAP_PATH_MAX];
    if (build_dest_path(a, from, typed, base, sizeof base, to, sizeof to) != 0) return;

    if (strcmp(from, to) == 0) { app_set_status(a, "name unchanged"); return; }

    /* link() fails if the destination exists, which makes this refuse to
     * clobber another map rather than racing an access() check. Filesystems
     * that will not hard-link fall back to a checked rename. */
    if (link(from, to) == 0) {
        if (unlink(from) != 0) {
            char body[MAP_PATH_MAX + 96];
            snprintf(body, sizeof body,
                     "renamed, but the old file is still there: %.60s", strerror(errno));
            app_show_message(a, "Partly renamed", body);
        }
    } else if (errno == EEXIST) {
        char body[MAP_PATH_MAX + 64];
        snprintf(body, sizeof body, "%.200s already exists", to);
        app_show_message(a, "Cannot rename", body);
        return;
    } else {
        if (access(to, F_OK) == 0) {
            char body[MAP_PATH_MAX + 64];
            snprintf(body, sizeof body, "%.200s already exists", to);
            app_show_message(a, "Cannot rename", body);
            return;
        }
        if (rename(from, to) != 0) {
            char body[MAP_PATH_MAX + 96];
            snprintf(body, sizeof body, "%.200s: %.60s", to, strerror(errno));
            app_show_message(a, "Cannot rename", body);
            return;
        }
    }

    /* A recovery copy left by a crash follows the map it belongs to. */
    static const char *const copies[] = { ".autosave", ".autosave.damaged" };  /* the second, one recovery refused */
    for (int i = 0; i < 2; i++) {
        char from_copy[MAP_PATH_MAX + 24], to_copy[MAP_PATH_MAX + 24];
        snprintf(from_copy, sizeof from_copy, "%s%s", from, copies[i]);
        snprintf(to_copy, sizeof to_copy, "%s%s", to, copies[i]);
        if (rename(from_copy, to_copy) != 0 && errno != ENOENT) unlink(from_copy);
    }

    int titled = retitle_map(to, base);
    select_path(a, to);

    char msg[MAP_PATH_MAX + 64];
    snprintf(msg, sizeof msg, "renamed to %.80s.vtt%s", base,
             titled ? "" : "  (title unchanged: the map would not load)");
    app_set_status(a, msg);
}

/* Copies the file, then retitles the copy. Byte-for-byte rather than load and
 * re-save, so the duplicate is exactly the original -- including a map the
 * loader would choke on. */
void app_duplicate_map(App *a, const char *from, const char *typed)
{
    char base[MAP_NAME_MAX], to[MAP_PATH_MAX];
    if (build_dest_path(a, from, typed, base, sizeof base, to, sizeof to) != 0) return;

    if (strcmp(from, to) == 0) {
        app_set_status(a, "a copy needs a name of its own");
        return;
    }

    char err[96] = { 0 };
    if (copy_file(from, to, err, sizeof err) != 0) {
        char body[MAP_PATH_MAX + 128];
        snprintf(body, sizeof body, "%.200s: %.60s", to, err);
        app_show_message(a, "Cannot duplicate", body);
        return;
    }

    int titled = retitle_map(to, base);
    select_path(a, to);

    char msg[MAP_PATH_MAX + 64];
    snprintf(msg, sizeof msg, "copied to %.80s.vtt%s", base,
             titled ? "" : "  (title unchanged: the map would not load)");
    app_set_status(a, msg);
}

void app_delete_map(App *a, const char *path)
{
    char shown[MAP_PATH_MAX];
    str_lcpy(shown, path, sizeof shown);

    /* Its recovery copy goes with it, or a new map under this name would be
     * offered the deleted one's contents. */
    char copy[MAP_PATH_MAX + 24];
    snprintf(copy, sizeof copy, "%s.autosave", path);
    unlink(copy);
    snprintf(copy, sizeof copy, "%s.autosave.damaged", path);
    unlink(copy);

    if (unlink(path) != 0) {
        char body[MAP_PATH_MAX + 64];
        snprintf(body, sizeof body, "%.200s: %.60s", shown, strerror(errno));
        app_show_message(a, "Could not delete", body);
        /* Rescan anyway: whatever went wrong, the list on screen may no
         * longer match the disk. */
        app_rescan_keeping_place(a);
        return;
    }

    app_rescan_keeping_place(a);

    char msg[MAP_PATH_MAX + 32];
    snprintf(msg, sizeof msg, "deleted %.180s", shown);
    app_set_status(a, msg);
}

void app_menu_key(App *a, Key k)
{
    if (k.kind == KEY_CHAR && k.mods == 0) {
        switch (k.ch) {
        case 'j': ui_list_move(&a->menu, APP_MENU_COUNT, 1, app_menu_visible_rows(a)); return;
        case 'k': ui_list_move(&a->menu, APP_MENU_COUNT, -1, app_menu_visible_rows(a)); return;
        case 'q': app_request_quit(a); return;
        default: break;
        }
    }
    if (k.kind == KEY_DOWN) { ui_list_move(&a->menu, APP_MENU_COUNT, 1, app_menu_visible_rows(a)); return; }
    if (k.kind == KEY_UP)   { ui_list_move(&a->menu, APP_MENU_COUNT, -1, app_menu_visible_rows(a)); return; }

    if (k.kind == KEY_ENTER) {
        switch (a->menu.sel) {
        case 0:
            app_refresh_entries(a);
            a->screen = SCREEN_BROWSER;
            break;
        case 1:
            app_open_prompt(a, PROMPT_NEW_NAME, "New map", "saved as <name>.vtt", "");
            break;
        default:
            app_request_quit(a);
            break;
        }
    }
}

void app_browser_key(App *a, Key k)
{
    int rows = app_menu_visible_rows(a);

    if (k.kind == KEY_ESC) { a->screen = SCREEN_MENU; return; }
    if (k.kind == KEY_DOWN) { ui_list_move(&a->browser, a->nentries, 1, rows); return; }
    if (k.kind == KEY_UP)   { ui_list_move(&a->browser, a->nentries, -1, rows); return; }

    if (k.kind == KEY_CHAR && k.mods == 0) {
        switch (k.ch) {
        case 'j': ui_list_move(&a->browser, a->nentries, 1, rows); return;
        case 'k': ui_list_move(&a->browser, a->nentries, -1, rows); return;
        case 'g': ui_list_move(&a->browser, a->nentries, -a->nentries, rows); return;
        case 'G': ui_list_move(&a->browser, a->nentries, a->nentries, rows); return;
        case 'r': app_refresh_entries(a); app_set_status(a, "rescanned"); return;

        case 'R': {
            if (a->nentries <= 0) { app_set_status(a, "nothing to rename"); return; }

            str_lcpy(a->pending_file, a->entries[a->browser.sel].path,
                     sizeof a->pending_file);

            /* Pre-fill with the current name so a small correction is a small
             * edit, and drop the extension since the prompt adds it back. */
            char base[MAP_NAME_MAX];
            str_lcpy(base, a->entries[a->browser.sel].name, sizeof base);
            str_cut_suffix(base, ".vtt");

            app_open_prompt(a, PROMPT_RENAME_MAP, "Rename map",
                        "renames the file and its title", base);
            return;
        }

        case 'c': {
            if (a->nentries <= 0) { app_set_status(a, "nothing to duplicate"); return; }

            const char *src = a->entries[a->browser.sel].path;
            str_lcpy(a->pending_file, src, sizeof a->pending_file);

            /* Offer a name that is already free, so accepting it is enough. */
            char dir[MAP_PATH_MAX], base[MAP_NAME_MAX], suggested[MAP_NAME_MAX];
            split_map_path(src, dir, sizeof dir, base, sizeof base);
            suggest_copy_name(dir, base, suggested, sizeof suggested);

            app_open_prompt(a, PROMPT_DUPLICATE_MAP, "Duplicate map",
                        "copies the file and titles the copy", suggested);
            return;
        }

        case 'd': {
            if (a->nentries <= 0) { app_set_status(a, "nothing to delete"); return; }

            /* The path is captured now rather than read back from the index
             * when the answer comes in, so the confirmation and the deletion
             * can never disagree about which file is meant. */
            str_lcpy(a->pending_file, a->entries[a->browser.sel].path,
                     sizeof a->pending_file);
            a->modal = MODAL_CONFIRM_DELETE;
            str_lcpy(a->modal_title, "Delete map?", sizeof a->modal_title);
            str_lcpy(a->modal_body, a->pending_file, sizeof a->modal_body);
            return;
        }

        case 'q': a->screen = SCREEN_MENU; return;
        default: break;
        }
    }

    if (k.kind == KEY_ENTER && a->nentries > 0)
        app_open_map(a, a->entries[a->browser.sel].path);
}

