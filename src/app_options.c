/* The Options screen (docs/APIAGENT.md, *Settings*): the landing menu's
 * third row, the GM's settings a row each. Every change is saved at once
 * through app_settings_changed, as the :agent commands' are. What is costly
 * to learn -- where the key is from (a keyring lookup), what has been spent
 * (the usage file) -- is read once when the screen opens, never a frame. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "apiagent.h"
#include "apikey.h"
#include "app_priv.h"
#include "store.h"

static const char *const LABELS[OPT_COUNT] = {
    "Agent", "Command", "Model", "Effort", "Spend cap", "Calls cap", "Fallbacks",
    "Channel at start", "Changes at start", "API key", "Spent so far",
};

static const char *const MODELS[] = { "opus", "sonnet", "haiku", "fable" };
static const char *const EFFORTS[] = { "low", "medium", "high" };

/* The usage file's dollars (the run's own reckoning, by vtt's price table):
 * all of it, and this month's by the line's date. */
static void read_spent(App *a)
{
    a->opt_spent = a->opt_spent_month = 0;
    a->opt_runs = a->opt_runs_month = 0;
    char dir[1024], path[1100];
    store_dir("", dir, sizeof dir);
    snprintf(path, sizeof path, "%sclaude-usage.tsv", dir);
    size_t len;
    char  *t = file_read(path, 8u << 20, &len, NULL);
    if (!t) return;
    char month[8];
    time_t now = time(NULL);
    strftime(month, sizeof month, "%Y-%m", localtime(&now));
    for (char *line = t; line && *line; ) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        /* when, job, model, calls, input, cache_write, cache_read, output, dollars, ended */
        char *f[10];
        int   n = 0;
        for (char *p = line; n < 10; n++) {
            f[n] = p;
            char *tab = strchr(p, '\t');
            if (!tab) { n++; break; }
            *tab = '\0';
            p = tab + 1;
        }
        if (n >= 9 && strcmp(f[0], "when") != 0) {
            double d = strtod(f[8], NULL);
            a->opt_spent += d;
            a->opt_runs++;
            if (!strncmp(f[0], month, 7)) { a->opt_spent_month += d; a->opt_runs_month++; }
        }
        line = nl ? nl + 1 : NULL;
    }
    free(t);
}

void app_options_open(App *a)
{
    a->screen = SCREEN_OPTIONS;
    a->opt_key = app_agent_key_where(a->opt_key_last4, sizeof a->opt_key_last4);
    read_spent(a);
    a->dirty = 1;
}

/* The key's source and last four after the prompt or a removal changed them. */
void app_options_key_changed(App *a)
{
    /* Whoever shows it next (Options, the choice box) shows it as it is: a
     * lookup only when the key changed, never a frame. */
    a->opt_key = app_agent_key_where(a->opt_key_last4, sizeof a->opt_key_last4);
}

int app_options_row(const App *a, int row, char *label, size_t lsz, char *value, size_t vsz)
{
    const Settings *s = &a->settings;
    snprintf(label, lsz, "%s", row >= 0 && row < OPT_COUNT ? LABELS[row] : "");
    int claude = s->agent == AGENT_KIND_CLAUDE;
    int dim = 0;
    switch (row) {
    case OPT_AGENT:
        snprintf(value, vsz, "%s%s", s->agent == AGENT_KIND_CLAUDE ? "the Claude API agent"
                                   : s->agent == AGENT_KIND_COMMAND ? "a command"
                                   : s->agent == AGENT_KIND_OFF ? "none - an agent may come to the channel"
                                   : "not chosen - the first :ask asks",
                 s->paused ? " (paused)" : "");
        break;
    case OPT_COMMAND:
        snprintf(value, vsz, "%s", s->command[0] ? s->command : "none");
        dim = s->agent != AGENT_KIND_COMMAND;
        break;
    case OPT_MODEL: {
        const char *id = apiagent_model_id(s->model);
        if (id && strcmp(id, s->model)) snprintf(value, vsz, "%s (%s)", s->model, id);
        else snprintf(value, vsz, "%s", s->model);
        dim = !claude;
        break;
    }
    case OPT_EFFORT:    snprintf(value, vsz, "%s", s->effort); dim = !claude; break;
    case OPT_CAP: {
        const char *id = apiagent_model_id(s->model);
        int priced = id && apiagent_cost(id, 0, 0, 0, 0) >= 0;
        if (s->cap > 0) snprintf(value, vsz, "$%.2f a job%s", s->cap, priced ? "" : " - not for this model: its price is not in vtt's table");
        else snprintf(value, vsz, "none");
        dim = !claude;
        break;
    }
    case OPT_CALLS:     snprintf(value, vsz, "%d a job", s->calls); dim = !claude; break;
    case OPT_FALLBACKS: snprintf(value, vsz, "%s", s->fallbacks ? "on - a declined request is tried on another model" : "off"); dim = !claude; break;
    case OPT_CHANNEL:   snprintf(value, vsz, "%s", s->channel ? "on" : "off - :agent on opens it"); break;
    case OPT_ACCEPT:    snprintf(value, vsz, "%s", s->accept_auto ? "land at once (auto)" : "wait for :review"); break;
    case OPT_KEY:
        if (a->opt_key == APIKEY_NONE) snprintf(value, vsz, "none - enter types one in");
        else snprintf(value, vsz, "%s, ending ...%s%s", a->opt_key == APIKEY_ENV ? "ANTHROPIC_API_KEY" :
                      a->opt_key == APIKEY_KEYRING ? "the keyring" : "a file only you can read",
                      a->opt_key_last4, a->opt_key == APIKEY_ENV ? "" : "  (d removes)");
        break;
    case OPT_SPENT:
        if (!a->opt_runs) snprintf(value, vsz, "nothing yet");
        else snprintf(value, vsz, "about $%.2f over %d run%s, $%.2f this month (vtt's price table)", a->opt_spent,
                      a->opt_runs, a->opt_runs == 1 ? "" : "s", a->opt_spent_month);
        break;
    default: value[0] = '\0'; break;
    }
    return dim;
}

static int index_of(const char *const *list, int n, const char *v)
{
    for (int i = 0; i < n; i++) if (!strcmp(list[i], v)) return i;
    return -1;
}

static void said(App *a, SettingId id, const char *when)
{
    char v[80], msg[200];
    settings_value(&a->settings, id, v, sizeof v);
    snprintf(msg, sizeof msg, "%s %s - saved%s", settings_name(id), v, when);
    app_note_gm(a, msg);
}

/* A row's value one step on (dir 1) or back (-1). */
static void step(App *a, int row, int dir)
{
    Settings *s = &a->settings;
    switch (row) {
    case OPT_AGENT: {
        static const int ORDER[] = { AGENT_KIND_OFF, AGENT_KIND_COMMAND, AGENT_KIND_CLAUDE };
        int i = s->agent == AGENT_KIND_UNSET ? (dir > 0 ? -1 : 3) : (s->agent - AGENT_KIND_OFF);
        int next = ORDER[(i + dir + 3) % 3];
        if (next == AGENT_KIND_CLAUDE) { app_agent_setting(a, "claude"); return; }
        if (next == AGENT_KIND_COMMAND && !s->command[0]) {
            a->opt_prompt = OPT_COMMAND;
            app_open_prompt(a, PROMPT_SETTING, "Agent command", "started for each :ask, the job on its stdin", "");
            a->prompt.max = SETTINGS_COMMAND_MAX;
            return;
        }
        s->agent = next;
        s->paused = 0;
        a->agent_from_env = 0;
        app_settings_changed(a);
        said(a, SET_AGENT, "");
        return;
    }
    case OPT_MODEL: {
        int i = index_of(MODELS, 4, s->model);
        str_lcpy(s->model, MODELS[i < 0 ? 0 : (i + dir + 4) % 4], sizeof s->model);
        app_settings_changed(a);
        said(a, SET_MODEL, " - from the next :ask");
        return;
    }
    case OPT_EFFORT: {
        int i = index_of(EFFORTS, 3, s->effort);
        str_lcpy(s->effort, EFFORTS[i < 0 ? 1 : (i + dir + 3) % 3], sizeof s->effort);
        app_settings_changed(a);
        said(a, SET_EFFORT, " - from the next :ask");
        return;
    }
    case OPT_FALLBACKS: s->fallbacks = !s->fallbacks; app_settings_changed(a); said(a, SET_FALLBACKS, " - from the next :ask"); return;
    case OPT_CHANNEL:   s->channel = !s->channel; app_settings_changed(a); said(a, SET_CHANNEL, " - next time vtt starts"); return;
    case OPT_ACCEPT:    s->accept_auto = !s->accept_auto; app_settings_changed(a); said(a, SET_ACCEPT, " - next time vtt starts"); return;
    default: return;
    }
}

static void edit(App *a, int row)
{
    char v[SETTINGS_COMMAND_MAX + 8];
    switch (row) {
    case OPT_COMMAND:
        a->opt_prompt = OPT_COMMAND;
        app_open_prompt(a, PROMPT_SETTING, "Agent command", "started for each :ask, the job on its stdin", a->settings.command);
        a->prompt.max = SETTINGS_COMMAND_MAX;
        return;
    case OPT_CAP:
        snprintf(v, sizeof v, "%.2f", a->settings.cap);
        a->opt_prompt = OPT_CAP;
        app_open_prompt(a, PROMPT_SETTING, "Spend cap", "dollars a job, by vtt's price table; 0 for none", v);
        return;
    case OPT_CALLS:
        snprintf(v, sizeof v, "%d", a->settings.calls);
        a->opt_prompt = OPT_CALLS;
        app_open_prompt(a, PROMPT_SETTING, "Calls cap", "API calls a job, 1 to 200", v);
        return;
    case OPT_KEY:   app_agent_key_prompt(a, KEY_THEN_NOTHING, NULL); return;
    case OPT_SPENT: return;
    default:        step(a, row, 1); return;
    }
}

void app_options_prompt(App *a, const char *text)
{
    static const SettingId ID[OPT_COUNT] = { [OPT_COMMAND] = SET_COMMAND, [OPT_CAP] = SET_CAP, [OPT_CALLS] = SET_CALLS };
    int row = a->opt_prompt;
    a->opt_prompt = -1;
    if (row != OPT_COMMAND && row != OPT_CAP && row != OPT_CALLS) return;
    char err[200];
    if (settings_set(&a->settings, ID[row], text, err, sizeof err) < 0) { app_set_status_gm(a, err); return; }
    if (row == OPT_COMMAND && !text[0]) {
        if (a->settings.agent == AGENT_KIND_COMMAND) a->settings.agent = AGENT_KIND_OFF;
        app_settings_changed(a);
        app_note_gm(a, "no command - saved; the agent is not a command now");
        return;
    }
    if (row == OPT_COMMAND) {
        a->settings.agent = AGENT_KIND_COMMAND;
        a->settings.paused = 0;
        a->agent_from_env = 0;
    }
    app_settings_changed(a);
    said(a, ID[row], row == OPT_COMMAND ? " - each :ask starts it" : " - from the next :ask");
    if (row == OPT_COMMAND) app_agent_choice_continue(a);           /* from the choice box: its job */
}

/* The rows the screen has room for: below the title and the heading, above
 * the status line and the key bar (draw_options). */
int app_options_visible_rows(const App *a)
{
    return imax(1, (a->rnd ? a->rnd->h : 24) - 6);
}

void app_options_key(App *a, Key k)
{
    ListState *st = &a->options;
    int rows = OPT_COUNT, vis = app_options_visible_rows(a);
    if (k.kind == KEY_ESC) { a->screen = SCREEN_MENU; return; }
    if (k.kind == KEY_DOWN) { ui_list_move(st, rows, 1, vis); return; }
    if (k.kind == KEY_UP)   { ui_list_move(st, rows, -1, vis); return; }
    if (k.kind == KEY_ENTER) { edit(a, st->sel); return; }
    if (k.kind == KEY_RIGHT) { step(a, st->sel, 1); return; }
    if (k.kind == KEY_LEFT)  { step(a, st->sel, -1); return; }
    if (k.kind != KEY_CHAR || k.mods) return;
    switch (k.ch) {
    case 'j': ui_list_move(st, rows, 1, vis); return;
    case 'k': ui_list_move(st, rows, -1, vis); return;
    case 'g': ui_list_move(st, rows, -rows, vis); return;
    case 'G': ui_list_move(st, rows, rows, vis); return;
    case 'l': step(a, st->sel, 1); return;
    case 'h': step(a, st->sel, -1); return;
    case 'd': if (st->sel == OPT_KEY) app_agent_setting(a, "key remove"); return;
    case 'q': a->screen = SCREEN_MENU; return;
    default:  return;
    }
}
