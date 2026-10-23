#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apiagent.h"
#include "util.h"

static const char *const NAMES[SET_COUNT] = {
    "agent", "command", "model", "effort", "cap", "calls", "fallbacks", "paused", "channel", "accept",
    "passkey",
};

static const char *const KINDS[] = { "unset", "off", "command", "claude" };

const char *settings_name(SettingId id) { return id < SET_COUNT ? NAMES[id] : "?"; }
const char *agent_kind_name(int kind) { return kind >= 0 && kind <= AGENT_KIND_CLAUDE ? KINDS[kind] : "?"; }

void settings_default(Settings *s)
{
    memset(s, 0, sizeof *s);
    s->agent = AGENT_KIND_UNSET;
    str_lcpy(s->model, "opus", sizeof s->model);
    str_lcpy(s->effort, "medium", sizeof s->effort);
    s->cap = 1.00;
    s->calls = 25;
    s->fallbacks = 1;
}

void settings_path(char *buf, size_t sz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) { snprintf(buf, sz, "%s/vtt/settings", xdg); return; }
    const char *home = getenv("HOME");
    snprintf(buf, sz, "%s/.config/vtt/settings", home && home[0] ? home : ".");
}

static int on_off(const char *v, int *out)
{
    if (!strcmp(v, "on"))  { *out = 1; return 0; }
    if (!strcmp(v, "off")) { *out = 0; return 0; }
    return -1;
}

int settings_set(Settings *s, SettingId id, const char *v, char *err, size_t errsz)
{
    char *end;
    switch (id) {
    case SET_AGENT:
        for (int k = 0; k <= AGENT_KIND_CLAUDE; k++)
            if (!strcmp(v, KINDS[k])) { s->agent = k; return 0; }
        snprintf(err, errsz, "agent is unset, off, command or claude");
        return -1;
    case SET_COMMAND:
        if (strlen(v) >= sizeof s->command) { snprintf(err, errsz, "the command is over %d characters - put it in a script", SETTINGS_COMMAND_MAX - 1); return -1; }
        str_lcpy(s->command, v, sizeof s->command);
        return 0;
    case SET_MODEL:
        if (!apiagent_model_id(v)) { snprintf(err, errsz, "model is opus, sonnet, haiku, fable, or a claude- model id"); return -1; }
        str_lcpy(s->model, v, sizeof s->model);
        return 0;
    case SET_EFFORT:
        if (strcmp(v, "low") && strcmp(v, "medium") && strcmp(v, "high")) { snprintf(err, errsz, "effort is low, medium or high"); return -1; }
        str_lcpy(s->effort, v, sizeof s->effort);
        return 0;
    case SET_CAP: {
        if (*v == '$') v++;
        double d = strtod(v, &end);
        if (end == v || *end || d < 0 || d > 1000) { snprintf(err, errsz, "cap is dollars a job, 0 to 1000 (0: none)"); return -1; }
        s->cap = d;
        return 0;
    }
    case SET_CALLS: {
        long n = strtol(v, &end, 10);
        if (end == v || *end || n < 1 || n > 200) { snprintf(err, errsz, "calls is 1 to 200 a job"); return -1; }
        s->calls = (int)n;
        return 0;
    }
    case SET_FALLBACKS:
    case SET_PAUSED:
    case SET_CHANNEL:
    case SET_PASSKEY: {
        int b;
        if (on_off(v, &b) < 0) { snprintf(err, errsz, "%s is on or off", NAMES[id]); return -1; }
        if (id == SET_FALLBACKS) s->fallbacks = b;
        else if (id == SET_PAUSED) s->paused = b;
        else if (id == SET_PASSKEY) s->pass_key = b;
        else s->channel = b;
        return 0;
    }
    case SET_ACCEPT:
        if (!strcmp(v, "auto"))        s->accept_auto = 1;
        else if (!strcmp(v, "review")) s->accept_auto = 0;
        else { snprintf(err, errsz, "accept is review or auto"); return -1; }
        return 0;
    default:
        snprintf(err, errsz, "no such setting");
        return -1;
    }
}

void settings_value(const Settings *s, SettingId id, char *buf, size_t sz)
{
    switch (id) {
    case SET_AGENT:     snprintf(buf, sz, "%s", agent_kind_name(s->agent)); break;
    case SET_COMMAND:   snprintf(buf, sz, "%s", s->command); break;
    case SET_MODEL:     snprintf(buf, sz, "%s", s->model); break;
    case SET_EFFORT:    snprintf(buf, sz, "%s", s->effort); break;
    case SET_CAP:       snprintf(buf, sz, "%.2f", s->cap); break;
    case SET_CALLS:     snprintf(buf, sz, "%d", s->calls); break;
    case SET_FALLBACKS: snprintf(buf, sz, "%s", s->fallbacks ? "on" : "off"); break;
    case SET_PAUSED:    snprintf(buf, sz, "%s", s->paused ? "on" : "off"); break;
    case SET_CHANNEL:   snprintf(buf, sz, "%s", s->channel ? "on" : "off"); break;
    case SET_ACCEPT:    snprintf(buf, sz, "%s", s->accept_auto ? "auto" : "review"); break;
    case SET_PASSKEY:   snprintf(buf, sz, "%s", s->pass_key ? "on" : "off"); break;
    default:            buf[0] = '\0'; break;
    }
}

static void keep_line(Settings *s, const char *line, size_t n)
{
    size_t have = strlen(s->kept);
    if (have + n + 2 > sizeof s->kept) return;
    memcpy(s->kept + have, line, n);
    s->kept[have + n] = '\n';
    s->kept[have + n + 1] = '\0';
}

int settings_load(Settings *s, const char *path, char *note, size_t notesz)
{
    note[0] = '\0';
    size_t len;
    int    big = 0;
    char  *text = file_read(path, 64 * 1024, &len, &big);
    if (!text) {
        if (big) snprintf(note, notesz, "%s is over 64 KB - not read; the settings are the defaults", path);
        return 0;
    }
    int lineno = 0;
    for (char *p = text; *p; ) {
        char  *nl = strchr(p, '\n');
        size_t n  = nl ? (size_t)(nl - p) : strlen(p);
        lineno++;
        char line[SETTINGS_COMMAND_MAX + 64];
        if (n < sizeof line) {
            memcpy(line, p, n);
            line[n] = '\0';
            if (n && line[n - 1] == '\r') line[--n] = '\0';
            char *name = line;
            while (*name == ' ' || *name == '\t') name++;
            if (*name && *name != '#') {
                char *v = name + strcspn(name, " \t");
                if (*v) *v++ = '\0';
                while (*v == ' ' || *v == '\t') v++;
                int id = -1;
                for (int i = 0; i < SET_COUNT; i++) if (!strcmp(name, NAMES[i])) id = i;
                char why[160];
                if (id < 0) {
                    keep_line(s, p, nl ? (size_t)(nl - p) : strlen(p));
                    if (!note[0]) snprintf(note, notesz, "settings line %d: %.40s is not a setting this vtt knows - kept", lineno, name);
                } else if (settings_set(s, (SettingId)id, v, why, sizeof why) < 0 && !note[0]) {
                    snprintf(note, notesz, "settings line %d: %s - the default kept", lineno, why);
                }
            } else if (*name == '#') keep_line(s, p, n);
        } else if (!note[0]) snprintf(note, notesz, "settings line %d is too long - left out", lineno);
        if (!nl) break;
        p = nl + 1;
    }
    free(text);
    return 0;
}

int settings_save(const Settings *s, const char *path, char *err, size_t errsz)
{
    ByteBuf b;
    bb_init(&b, 512);
    char v[SETTINGS_COMMAND_MAX + 8];
    for (int i = 0; i < SET_COUNT; i++) {
        settings_value(s, (SettingId)i, v, sizeof v);
        if (i == SET_COMMAND && !v[0]) continue;
        bb_puts(&b, NAMES[i]);
        bb_putc(&b, ' ');
        bb_puts(&b, v);
        bb_putc(&b, '\n');
    }
    bb_puts(&b, s->kept);
    char dir[4096];
    str_lcpy(dir, path, sizeof dir);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; dir_make(dir); }
    int rc = file_write_whole(path, b.data, b.len, 0666, 1, err, errsz);
    bb_free(&b);
    return rc;
}
