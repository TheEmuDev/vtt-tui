/* Tests: the Claude API agent (docs/APIAGENT.md), with no network and no
 * money: VTT_CLAUDE_CURL is a script that plays the API from answers the test
 * writes, keeps each body it is sent, its command line and what came down fd
 * 3. The agent is the real ./vtt --claude-agent, started by :agent command. */

#include <sys/stat.h>
#include "harness.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "apiagent.h"
#include "apikey.h"
#include "app_priv.h"
#include "settings.h"
#include "store.h"

#define KEY "sk-test-0123456789abcdefghijKLMN"

static char g_dir[800];
static char g_usage[900];

static void fake_curl(const char *dir)
{
    char path[900];
    snprintf(path, sizeof path, "%s/curl", dir);
    FILE *f = fopen(path, "w");
    fprintf(f, "#!/bin/sh\n"
               "d='%s'\n"
               "n=$(cat \"$d/n\" 2>/dev/null || echo 0); n=$((n+1)); echo \"$n\" > \"$d/n\"\n"
               "for a in \"$@\"; do printf '%%s\\n' \"$a\"; done > \"$d/argv.$n\"\n"
               "cat <&3 > \"$d/fd3.$n\"\n"
               "cat > \"$d/body.$n\"\n"
               "if [ -f \"$d/answer.$n\" ]; then cat \"$d/answer.$n\"; else printf 'HTTP/1.1 500 x\\r\\n\\r\\n{}'; fi\n", dir);
    fclose(f);
    chmod(path, 0755);
    setenv("VTT_CLAUDE_CURL", path, 1);
}

/* The calls so far forgotten, and the answers for the next run's. */
static void fresh(void)
{
    char cmd[1000];
    snprintf(cmd, sizeof cmd, "rm -f '%s'/n '%s'/argv.* '%s'/fd3.* '%s'/body.* '%s'/answer.*", g_dir, g_dir, g_dir, g_dir, g_dir);
    CHECK_EQ(system(cmd), 0);
    unlink(g_usage);                       /* each run's line is looked for alone */
}

static void answer(int n, const char *head, const char *body)
{
    char path[900];
    snprintf(path, sizeof path, "%s/answer.%d", g_dir, n);
    FILE *f = fopen(path, "w");
    fprintf(f, "%s\r\n\r\n%s", head, body);
    fclose(f);
}

static char *got(const char *what, int n)
{
    char path[900];
    snprintf(path, sizeof path, "%s/%s.%d", g_dir, what, n);
    return slurp(path);
}

static int calls(void)
{
    char path[900];
    snprintf(path, sizeof path, "%s/n", g_dir);
    char *t = slurp(path);
    int n = t ? atoi(t) : 0;
    free(t);
    return n;
}

#define OK200 "HTTP/1.1 100 Continue\r\n\r\nHTTP/2 200\r\ncontent-type: application/json"

static const char ANSWER1_CONTENT[] =
    "[ {\"type\":\"thinking\",\"thinking\":\"\",\"signature\":\"c2ln\\u00e9==\"} ,"
    "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"vtt\",\"input\":{\"request\":\"job 1 take\"}}]";

/* :ask, then the loop turned until the run has written its usage line. */
static void ask_and_wait(App *a, const char *what, const char *ended)
{
    char k[300];
    snprintf(k, sizeof k, ":ask %s\r", what);
    press(a, k);
    char want[64];
    snprintf(want, sizeof want, "\t%s\n", ended);
    CHECK(pump_until_file(a, g_usage, want));
    CHECK(pump_until_reaped(a));
}

static void agent_cmd(App *a, const char *self, const char *extra)
{
    char cmd[1200];
    snprintf(cmd, sizeof cmd, ":agent command '%s' --claude-agent%s\r", self, extra);
    press(a, cmd);
}

void test_api_agent(void)
{
    Sandbox sb = sandbox_enter("apiagent");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 20, 12));
    if (!a.world.map) { app_free(&a); rnd_free(&r); sandbox_leave(&sb); return; }
    const char *self_was = app_self_path;
    char self[700];
    snprintf(self, sizeof self, "%s/vtt", sb.cwd);
    app_self_path = self;
    str_lcpy(g_dir, sb.dir, sizeof g_dir);
    snprintf(g_usage, sizeof g_usage, "%s/vtt/claude-usage.tsv", sb.datadir);
    fake_curl(sb.dir);
    setenv("ANTHROPIC_API_KEY", KEY, 1);
    a.world.ctl_auto = 0;

    CASE("a job done: take, propose with edits, end the turn -- the job ready, as from any agent");
    fresh();
    answer(1, OK200, "{\"id\":\"msg_1\",\"type\":\"message\",\"role\":\"assistant\",\"model\":\"claude-opus-5-5\",\"content\":"
                     "[ {\"type\":\"thinking\",\"thinking\":\"\",\"signature\":\"c2ln\\u00e9==\"} ,"
                     "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"vtt\",\"input\":{\"request\":\"job 1 take\"}}],"
                     "\"stop_reason\":\"tool_use\",\"usage\":{\"input_tokens\":100,\"cache_creation_input_tokens\":12000,"
                     "\"cache_read_input_tokens\":0,\"output_tokens\":50}}");
    answer(2, OK200, "{\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_2\",\"name\":\"vtt\",\"input\":{\"request\":"
                     "\"job 1 propose \\\"a pool\\\"\\ntile B2 water\"}},"
                     "{\"type\":\"tool_use\",\"id\":\"toolu_3\",\"name\":\"vtt\",\"input\":{\"request\":\"nosuchverb\"}}],"
                     "\"stop_reason\":\"tool_use\",\"usage\":{\"input_tokens\":30,\"cache_read_input_tokens\":12100,\"output_tokens\":80}}");
    answer(3, OK200, "{\"content\":[{\"type\":\"text\",\"text\":\"Proposed a pool in B2.\"}],\"stop_reason\":\"end_turn\","
                     "\"usage\":{\"input_tokens\":20,\"cache_read_input_tokens\":12300,\"output_tokens\":10}}");
    agent_cmd(&a, self, " --calls 10");
    ask_and_wait(&a, "a pool", "done");
    CHECK_EQ(calls(), 3);
    CHECK_EQ(a.world.jobs[0].state, JOB_READY);
    CHECK(a.world.jobs[0].has_cs && a.world.jobs[0].cs.ncells == 1);
    CHECK(!strcmp(a.world.jobs[0].taker, "job-1"));
    CHECK(file_has(g_usage, "\tclaude-opus-5-5\t3\t150\t12000\t24400\t140\t"));

    CASE("the first request: the model, the effort, the guide cached, the one strict tool, the job");
    char *b1 = got("body", 1), *b2 = got("body", 2), *b3 = got("body", 3);
    CHECK(b1 && strstr(b1, "\"model\":\"claude-opus-5-5\"") && strstr(b1, "\"effort\":\"medium\""));
    CHECK(b1 && strstr(b1, "\"text\":\"# vtt for agents\\n"));
    CHECK(b1 && strstr(b1, "\"cache_control\":{\"type\":\"ephemeral\"}"));
    CHECK(b1 && strstr(b1, "\"fallbacks\":\"default\"") && strstr(b1, "\"strict\":true"));
    CHECK(b1 && strstr(b1, "Job #1: a pool"));

    CASE("the answer's content goes back byte for byte (an escape kept, the spacing kept)");
    CHECK(b2 && strstr(b2, ANSWER1_CONTENT) != NULL);
    CHECK(b2 && strstr(b2, "{\"type\":\"tool_result\",\"tool_use_id\":\"toolu_1\",\"content\":\"ok\\n"));

    CASE("two calls in one answer, answered in one message; a refused request is an error result");
    const char *t2 = b3 ? strstr(b3, "\"tool_use_id\":\"toolu_2\"") : NULL, *t3 = b3 ? strstr(b3, "\"tool_use_id\":\"toolu_3\"") : NULL;
    CHECK(t2 && t3 && t2 < t3);
    CHECK(t2 && !strncmp(t2 + strlen("\"tool_use_id\":\"toolu_2\""), ",\"content\":\"ok", 14));
    CHECK(t3 && strstr(t3, "\"is_error\":true") != NULL);

    CASE("the key: down fd 3 with the headers, and nowhere else");
    char *f1 = got("fd3", 1), *a1 = got("argv", 1);
    CHECK(f1 && strstr(f1, "header = \"x-api-key: " KEY "\"\n"));
    CHECK(f1 && strstr(f1, "anthropic-version: 2023-06-01") && strstr(f1, "anthropic-beta: server-side-fallback-2026-07-01"));
    CHECK(a1 && !strstr(a1, KEY) && strstr(a1, "--config\n/dev/fd/3\n"));
    CHECK(b1 && !strstr(b1, KEY));
    CHECK(b2 && !strstr(b2, KEY));
    CHECK(!file_has(g_usage, KEY));
    free(b1); free(b2); free(b3); free(f1); free(a1);

    CASE("429 with retry-after: the same request again, then on");
    jobs_reset(&a);
    fresh();
    answer(1, "HTTP/2 429\r\nretry-after: 0", "{\"type\":\"error\",\"error\":{\"type\":\"rate_limit_error\",\"message\":\"slow down\"}}");
    answer(2, OK200, "{\"content\":[{\"type\":\"text\",\"text\":\"nothing to do\"}],\"stop_reason\":\"end_turn\",\"usage\":{}}");
    ask_and_wait(&a, "a retry", "done");
    CHECK_EQ(calls(), 2);
    {
        char *x = got("body", 1), *y = got("body", 2);
        CHECK(x && y && !strcmp(x, y));
        free(x); free(y);
    }
    CHECK(strstr(a.status, "its agent stopped with nothing proposed") != NULL);

    CASE("401: the run ends at once and the GM's line says the key was refused");
    jobs_reset(&a);
    fresh();
    answer(1, "HTTP/2 401", "{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\",\"message\":\"invalid x-api-key\"}}");
    ask_and_wait(&a, "a bad key", "failed");
    CHECK_EQ(calls(), 1);
    CHECK(strstr(a.status, "the API key was refused (401)") != NULL);
    CHECK_EQ(a.world.jobs[0].state, JOB_ASKED);

    CASE("a refusal is said with its category");
    jobs_reset(&a);
    fresh();
    answer(1, OK200, "{\"content\":[],\"stop_reason\":\"refusal\",\"stop_details\":{\"type\":\"refusal\",\"category\":\"cyber\"},\"usage\":{}}");
    ask_and_wait(&a, "something", "declined");
    CHECK(strstr(a.status, "Claude declined this job (cyber)") != NULL);

    CASE("the calls cap: two calls of tool use and it stops, saying so");
    jobs_reset(&a);
    fresh();
    for (int n = 1; n <= 3; n++)
        answer(n, OK200, "{\"content\":[{\"type\":\"tool_use\",\"id\":\"t\",\"name\":\"vtt\",\"input\":{\"request\":\"floors\"}}],"
                         "\"stop_reason\":\"tool_use\",\"usage\":{}}");
    agent_cmd(&a, self, " --calls 2");
    ask_and_wait(&a, "on and on", "out of calls");
    CHECK_EQ(calls(), 2);
    CHECK(strstr(a.status, "2 calls and not done") != NULL);

    CASE("the spend cap, by the price table: past it after the first call");
    jobs_reset(&a);
    fresh();
    answer(1, OK200, "{\"content\":[{\"type\":\"tool_use\",\"id\":\"t\",\"name\":\"vtt\",\"input\":{\"request\":\"floors\"}}],"
                     "\"stop_reason\":\"tool_use\",\"usage\":{\"input_tokens\":10000,\"output_tokens\":2000}}");
    agent_cmd(&a, self, " --cap 0.05");
    ask_and_wait(&a, "dear", "at its cap");
    CHECK_EQ(calls(), 1);
    CHECK(strstr(a.status, "at its cap: about $0.08 spent of $0.05") != NULL);

    CASE("an agent line from the model is the channel's to refuse: a request is sent as the job's agent only");
    jobs_reset(&a);
    fresh();
    answer(1, OK200, "{\"content\":[{\"type\":\"tool_use\",\"id\":\"t\",\"name\":\"vtt\",\"input\":{\"request\":\"agent someone-else\\nfloors\"}}],"
                     "\"stop_reason\":\"tool_use\",\"usage\":{}}");
    answer(2, OK200, "{\"content\":[{\"type\":\"text\",\"text\":\"ok\"}],\"stop_reason\":\"end_turn\",\"usage\":{}}");
    agent_cmd(&a, self, "");
    ask_and_wait(&a, "who", "done");
    {
        char *x = got("body", 2);
        const char *t = x ? strstr(x, "\"tool_use_id\":\"t\"") : NULL;
        CHECK(t && strstr(t, "\"content\":\"error ") && strstr(t, "\"is_error\":true"));
        free(x);
    }

    CASE("no key anywhere: the run says so, and calls nothing");
    unsetenv("ANTHROPIC_API_KEY");
    jobs_reset(&a);
    fresh();
    ask_and_wait(&a, "keyless", "not started");
    CHECK_EQ(calls(), 0);
    CHECK(strstr(a.status, "no API key") != NULL);

    CASE("a key file others can read is refused with the chmod; mode 600 is used");
    {
        char kf[900];
        apikey_file_path(kf, sizeof kf);
        CHECK(strncmp(kf, sb.dir, strlen(sb.dir)) == 0);              /* the sandbox's, never the user's */
        char kd[900];
        str_lcpy(kd, kf, sizeof kd);
        *strrchr(kd, '/') = '\0';
        dir_make(kd);
        FILE *f = fopen(kf, "w");
        fputs(KEY "\n", f);
        fclose(f);
        chmod(kf, 0644);
        jobs_reset(&a);
        fresh();
        ask_and_wait(&a, "open key", "not started");
        CHECK(strstr(a.status, "chmod 600") != NULL);
        chmod(kf, 0600);
        jobs_reset(&a);
        fresh();
        answer(1, OK200, "{\"content\":[{\"type\":\"text\",\"text\":\"x\"}],\"stop_reason\":\"end_turn\",\"usage\":{}}");
        ask_and_wait(&a, "file key", "done");
        char *f3 = got("fd3", 1);
        CHECK(f3 && strstr(f3, "x-api-key: " KEY "\"") != NULL);     /* the newline after it left behind */
        free(f3);
        unlink(kf);
    }

    unsetenv("VTT_CLAUDE_CURL");
    app_self_path = self_was;
    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* The parts with no process: names, prices, keys. */
void test_api_agent_parts(void)
{
    CASE("model words: the current model of each, a full id as it is, anything else refused");
    CHECK(!strcmp(apiagent_model_id("opus"), "claude-opus-5-5"));
    CHECK(!strcmp(apiagent_model_id("sonnet"), "claude-sonnet-5-5"));
    CHECK(!strcmp(apiagent_model_id("haiku"), "claude-haiku-5-5"));
    CHECK(!strcmp(apiagent_model_id("fable"), "claude-fable-5-1"));
    CHECK(!strcmp(apiagent_model_id("claude-opus-4-8"), "claude-opus-4-8"));
    CHECK(apiagent_model_id("gpt") == NULL);
    CHECK(apiagent_model_id("claude-x\"y") == NULL);

    CASE("the price table: a cache write is 1.25x input; a model it does not know has no price");
    double d = apiagent_cost("claude-opus-5-5", 1000000, 1000000, 1000000, 1000000);
    CHECK(d > 4 + 5 + 0.2 + 20 - 0.001 && d < 4 + 5 + 0.2 + 20 + 0.001);
    CHECK(apiagent_cost("claude-opus-4-8", 1, 1, 1, 1) < 0);

    CASE("a key's characters: letters, digits, - and _, 20 or more; a quote or a newline never");
    CHECK(apikey_ok(KEY));
    CHECK(!apikey_ok("short-key"));
    CHECK(!apikey_ok("sk-test-0123456789abcdef\"ghij"));
    CHECK(!apikey_ok("sk-test-0123456789abcdef\nghij"));
    CHECK(!apikey_ok("sk-test-0123456789 abcdefghij"));
}

/* The settings (settings.c) and the :agent words that change them. */
void test_settings(void)
{
    Sandbox sb = sandbox_enter("settings");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[1200], err[600], note[400];
    settings_path(path, sizeof path);
    CHECK(strncmp(path, sb.dir, strlen(sb.dir)) == 0);              /* the sandbox's, never the user's */

    CASE("the defaults: no agent chosen, opus at medium effort, $1.00 and 25 calls, fallbacks on, review");
    Settings s;
    settings_default(&s);
    CHECK_EQ(s.agent, AGENT_KIND_UNSET);
    CHECK(!strcmp(s.model, "opus") && !strcmp(s.effort, "medium") && s.cap == 1.00 && s.calls == 25 && s.fallbacks);
    CHECK(!s.channel && !s.accept_auto && !s.paused);

    CASE("written and read back the same; no file is the defaults, said by nothing");
    CHECK_EQ(settings_load(&s, path, note, sizeof note), 0);
    CHECK(!note[0]);
    s.agent = AGENT_KIND_COMMAND;
    str_lcpy(s.command, "claude -p --verbose", sizeof s.command);
    str_lcpy(s.model, "sonnet", sizeof s.model);
    s.cap = 2.5; s.calls = 30; s.fallbacks = 0; s.channel = 1; s.accept_auto = 1;
    CHECK_EQ(settings_save(&s, path, err, sizeof err), 0);
    Settings t;
    settings_default(&t);
    CHECK_EQ(settings_load(&t, path, note, sizeof note), 0);
    CHECK(!note[0]);
    CHECK(t.agent == AGENT_KIND_COMMAND && !strcmp(t.command, "claude -p --verbose") && !strcmp(t.model, "sonnet"));
    CHECK(t.cap == 2.5 && t.calls == 30 && !t.fallbacks && t.channel && t.accept_auto);
    CHECK(file_has(path, "agent command\ncommand claude -p --verbose\nmodel sonnet\n"));

    CASE("a line it does not know is kept and named; a bad value keeps its default and is named");
    {
        FILE *f = fopen(path, "w");
        fputs("# mine\nagent claude\ntheme dark\ncalls lots\neffort high\n", f);
        fclose(f);
        settings_default(&t);
        settings_load(&t, path, note, sizeof note);
        CHECK(strstr(note, "line 3: theme is not a setting") != NULL);
        CHECK(t.agent == AGENT_KIND_CLAUDE && t.calls == 25 && !strcmp(t.effort, "high"));
        CHECK_EQ(settings_save(&t, path, err, sizeof err), 0);
        CHECK(file_has(path, "theme dark\n") && file_has(path, "# mine\n"));
        settings_default(&t);
        settings_load(&t, path, note, sizeof note);
        CHECK(strstr(note, "line 2: calls is 1 to 200") == NULL);       /* the bad value was not written back */
    }

    CASE(":agent words change the setting, the file and the line the next agent starts with");
    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 8, 6));
    str_lcpy(a.settings_file, path, sizeof a.settings_file);
    unlink(path);
    const char *self_was = app_self_path;
    app_self_path = "/opt/my vtt/vtt";
    press(&a, ":agent\r");
    CHECK(strstr(a.status, "no agent chosen") != NULL);
    press(&a, ":agent command sh agent.sh\r");
    CHECK(strstr(a.status, "each :ask now starts: sh agent.sh") != NULL);
    CHECK(!strcmp(a.agent_cmd, "sh agent.sh"));
    CHECK(file_has(path, "agent command\ncommand sh agent.sh\n"));
    press(&a, ":agent effort high\r");
    CHECK(strstr(a.status, "effort high - for the Claude agent") != NULL);
    press(&a, ":agent effort huge\r");
    CHECK(strstr(a.status, "effort is low, medium or high") != NULL);
    press(&a, ":agent cap $2.50\r");
    press(&a, ":agent calls 40\r");
    press(&a, ":agent fallbacks off\r");
    CHECK(file_has(path, "effort high\ncap 2.50\ncalls 40\nfallbacks off\n"));
    press(&a, ":agent command off\r");
    CHECK(file_has(path, "paused on\n"));
    CHECK_EQ(a.agent_cmd_off, 1);
    press(&a, ":agent command on\r");
    CHECK(file_has(path, "paused off\n"));

    CASE(":agent claude: with no key it asks for one (esc: nothing changed); with a key and curl, the line is built from the settings");
    unsetenv("ANTHROPIC_API_KEY");
    press(&a, ":agent claude\r");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK(a.prompt.secret && strstr(a.prompt.hint, "no API key") != NULL);
    press(&a, "\x1b");
    CHECK_EQ(a.settings.agent, AGENT_KIND_COMMAND);                   /* nothing changed */
    setenv("ANTHROPIC_API_KEY", KEY, 1);
    setenv("VTT_CLAUDE_CURL", "/bin/true", 1);
    press(&a, ":agent claude sonnet\r");
    CHECK(strstr(a.status, "each :ask now starts the Claude agent - agent claude: sonnet (claude-sonnet-5-5), high effort, $2.50 and 40 calls a job, fallbacks off") != NULL);
    CHECK(!strcmp(a.agent_cmd, "'/opt/my vtt/vtt' --claude-agent --model claude-sonnet-5-5 --effort high --cap 2.50 --calls 40 --fallbacks off"));
    CHECK(file_has(path, "agent claude\n") && file_has(path, "model sonnet\n"));
    CHECK(!file_has(path, KEY));                                       /* the key is never a setting */
    press(&a, ":agent model gpt-5\r");
    CHECK(strstr(a.status, "model is opus, sonnet, haiku, fable") != NULL);
    press(&a, ":agent model haiku\r");
    CHECK(strstr(a.agent_cmd, "--model claude-haiku-5-5 ") != NULL);
    press(&a, ":agent\r");
    CHECK(strstr(a.status, "agent claude: haiku (claude-haiku-5-5), high effort") != NULL);
    press(&a, ":agent command\r");
    CHECK(strstr(a.status, "each :ask starts: the Claude agent (haiku, high effort)") != NULL);

    CASE("VTT_AGENT_COMMAND wins for its session and is not saved; :agent command CMD takes over from it");
    a.agent_from_env = 1;
    str_lcpy(a.agent_cmd, "my-env-agent", sizeof a.agent_cmd);
    press(&a, ":agent effort low\r");
    CHECK(!strcmp(a.agent_cmd, "my-env-agent"));
    CHECK(!file_has(path, "my-env-agent"));
    press(&a, ":agent\r");
    CHECK(strstr(a.status, "my-env-agent (VTT_AGENT_COMMAND, this session)") != NULL);
    press(&a, ":agent command sh other.sh\r");
    CHECK_EQ(a.agent_from_env, 0);
    CHECK(!strcmp(a.agent_cmd, "sh other.sh"));

    CASE("with no settings file (a test, a bench) nothing is written");
    a.settings_file[0] = '\0';
    unlink(path);
    press(&a, ":agent calls 12\r");
    CHECK(access(path, F_OK) != 0);

    unsetenv("VTT_CLAUDE_CURL");
    unsetenv("ANTHROPIC_API_KEY");
    app_self_path = self_was;
    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* A stand-in secret-tool: store keeps stdin in `secret`, lookup prints it,
 * clear removes it; each run's arguments and stdin are kept. */
static void fake_secret_tool(const char *dir, char *path, size_t sz)
{
    snprintf(path, sz, "%s/secret-tool", dir);
    FILE *f = fopen(path, "w");
    fprintf(f, "#!/bin/sh\n"
               "d='%s'\n"
               "for a in \"$@\"; do printf '%%s\\n' \"$a\"; done >> \"$d/st.argv\"\n"
               "case \"$1\" in\n"
               "store) cat > \"$d/secret\" ;;\n"
               "lookup) [ -f \"$d/secret\" ] && cat \"$d/secret\" || exit 1 ;;\n"
               "clear) rm -f \"$d/secret\" ;;\n"
               "esac\n", dir);
    fclose(f);
    chmod(path, 0755);
}

static int frame_has(App *a, Renderer *r, const char *needle)
{
    rnd_begin(r);
    app_draw(a);
    ByteBuf b;
    bb_init(&b, 4096);
    rnd_dump(r, &b);
    bb_putc(&b, '\0');
    int has = strstr(b.data, needle) != NULL;
    bb_free(&b);
    return has;
}

void test_api_key(void)
{
    Sandbox sb = sandbox_enter("apikey");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    unsetenv("ANTHROPIC_API_KEY");
    char tool[900], secret[900], kf[1200];
    fake_secret_tool(sb.dir, tool, sizeof tool);
    snprintf(secret, sizeof secret, "%s/secret", sb.dir);
    apikey_file_path(kf, sizeof kf);
    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    CHECK(ctl_blank_map(&a, sb.dir, 8, 6));

    CASE(":agent key: a hidden prompt -- the frame shows a * a character, never the key");
    setenv("VTT_SECRET_TOOL", tool, 1);
    press(&a, ":agent key\r");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK_EQ(a.prompt.secret, 1);
    CHECK(frame_has(&a, &r, "none yet - it goes to your keyring"));
    press(&a, KEY);
    CHECK(frame_has(&a, &r, "Anthropic API key"));
    CHECK(!frame_has(&a, &r, "sk-test"));
    CHECK(frame_has(&a, &r, "*******************************"));
    {
        char *pl = players_text(&a, &r);
        CHECK(pl && !strstr(pl, "Anthropic") && !strstr(pl, "****"));    /* nothing of it on the phones */
        free(pl);
    }

    CASE("enter: to the keyring down stdin, no newline, never an argument; the prompt wiped; only the last four said");
    press(&a, "\r");
    CHECK_EQ(a.modal, MODAL_NONE);
    {
        char *sec = slurp(secret);
        CHECK(sec && !strcmp(sec, KEY));
        free(sec);
        char path[1000];
        snprintf(path, sizeof path, "%s/st.argv", sb.dir);
        CHECK(file_has(path, "store\n--label=vtt: Anthropic API key\nservice\nvtt\naccount\nanthropic-api-key\n"));
        CHECK(!file_has(path, "sk-test"));
    }
    CHECK(strstr(a.status, "API key ...KLMN stored in your keyring") != NULL && a.status_gm);
    CHECK(!strstr(a.status, "sk-test"));
    {
        int clean = 1;
        for (size_t i = 0; i < sizeof a.prompt.buf; i++) clean &= a.prompt.buf[i] == 0;
        CHECK(clean);
    }
    CHECK(access(kf, F_OK) != 0);                                     /* no file beside the keyring */

    CASE("found again: the keyring's, its last four on the prompt's line");
    char last4[8];
    CHECK_EQ(app_agent_key_where(last4, sizeof last4), APIKEY_KEYRING);
    CHECK(!strcmp(last4, "KLMN"));
    press(&a, ":agent key\r");
    CHECK(frame_has(&a, &r, "now: the keyring's, ending ...KLMN"));

    CASE("not a key: refused, the prompt kept open saying why; esc: nothing stored, wiped");
    press(&a, "sk-test-0123456789\"abcdefgh\r");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK(frame_has(&a, &r, "not a key: letters, digits"));
    press(&a, "half-a-key\x1b");
    CHECK_EQ(a.modal, MODAL_NONE);
    CHECK(strstr(a.status, "canceled - no key stored") != NULL);
    CHECK(a.prompt.buf[0] == 0 && a.prompt.len == 0);
    {
        char *sec = slurp(secret);
        CHECK(sec && !strcmp(sec, KEY));                               /* the stored one untouched */
        free(sec);
    }

    CASE(":agent key remove asks; n keeps it, y takes it from the keyring");
    press(&a, ":agent key remove\r");
    CHECK_EQ(a.modal, MODAL_CONFIRM_KEY);
    CHECK(frame_has(&a, &r, "ending ...KLMN, from your keyring"));
    press(&a, "n");
    CHECK(access(secret, F_OK) == 0);
    press(&a, ":agent key remove\ry");
    CHECK(access(secret, F_OK) != 0);
    CHECK(strstr(a.status, "the stored API key is removed") != NULL);
    press(&a, ":agent key remove\r");
    CHECK(strstr(a.status, "no API key is stored") != NULL);

    CASE("no keyring here: the key goes to a file, mode 600, and the GM is told it is a file");
    setenv("VTT_SECRET_TOOL", "/nonexistent/secret-tool", 1);
    press(&a, ":agent key\r" KEY "\r");
    {
        struct stat st;
        CHECK(stat(kf, &st) == 0 && (st.st_mode & 0777) == 0600);
        char *t = slurp(kf);
        CHECK(t && !strcmp(t, KEY "\n"));
        free(t);
    }
    CHECK(strstr(a.status, "readable by you alone (no keyring here)") != NULL);
    CHECK_EQ(app_agent_key_where(last4, sizeof last4), APIKEY_FILE);
    press(&a, ":agent key remove\ry");
    CHECK(access(kf, F_OK) != 0);

    CASE("the variable wins, is said, and is the GM's to unset");
    setenv("ANTHROPIC_API_KEY", KEY, 1);
    CHECK_EQ(app_agent_key_where(last4, sizeof last4), APIKEY_ENV);
    press(&a, ":agent key remove\r");
    CHECK(strstr(a.status, "unset it yourself") != NULL);
    unsetenv("ANTHROPIC_API_KEY");

    CASE(":agent claude with no key asks for one, and is set once it is stored");
    setenv("VTT_SECRET_TOOL", tool, 1);
    setenv("VTT_CLAUDE_CURL", "/bin/true", 1);
    press(&a, ":agent claude sonnet\r");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK_EQ(a.settings.agent, AGENT_KIND_UNSET);
    press(&a, KEY "\r");
    CHECK_EQ(a.settings.agent, AGENT_KIND_CLAUDE);
    CHECK(!strcmp(a.settings.model, "sonnet"));
    CHECK(strstr(a.status, "each :ask now starts the Claude agent") != NULL);
    CHECK(strstr(a.agent_cmd, KEY) == NULL);

    CASE("a :agent command agent gets no key from vtt: vtt never puts it in the environment");
    CHECK(getenv("ANTHROPIC_API_KEY") == NULL);

    unsetenv("VTT_CLAUDE_CURL");
    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}

/* The Options screen (app_options.c): the menu's third row. */
void test_options(void)
{
    Sandbox sb = sandbox_enter("options");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    unsetenv("ANTHROPIC_API_KEY");
    char path[1200];
    settings_path(path, sizeof path);

    CASE("the screen as it opens on no settings: every row, the agent's own dim while none is chosen");
    {
        static const char *const seg[] = { "jj\r" };
        golden("options", 90, 22, NULL, seg, 1, 0);
    }

    Renderer r;
    App a;
    rnd_init(&r);
    rnd_resize(&r, 100, 30);
    app_init(&a, NULL, &r);
    str_lcpy(a.settings_file, path, sizeof a.settings_file);
    press(&a, "jj\r");
    CHECK_EQ(a.screen, SCREEN_OPTIONS);
    CHECK(frame_has(&a, &r, "not chosen - the first :ask asks"));

    CASE("the agent row: enter steps none -> a command (asking for it when there is none)");
    press(&a, "\r");
    CHECK_EQ(a.settings.agent, AGENT_KIND_OFF);
    CHECK(file_has(path, "agent off\n"));
    press(&a, "\r");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK_EQ(a.prompt_what, PROMPT_SETTING);
    press(&a, "sh agent.sh\r");
    CHECK_EQ(a.settings.agent, AGENT_KIND_COMMAND);
    CHECK(!strcmp(a.agent_cmd, "sh agent.sh"));
    CHECK(file_has(path, "agent command\ncommand sh agent.sh\n"));
    CHECK(frame_has(&a, &r, "a command"));

    CASE("the command row: its prompt opens holding the command; empty takes it away");
    press(&a, "j\r");
    CHECK(!strcmp(a.prompt.buf, "sh agent.sh"));
    press(&a, "\025\r");
    CHECK_EQ(a.settings.agent, AGENT_KIND_OFF);
    CHECK(!a.settings.command[0]);

    CASE("model and effort step with enter, l and h; the rest of the Claude rows likewise");
    press(&a, "j\r");
    CHECK(!strcmp(a.settings.model, "sonnet"));
    press(&a, "h");
    CHECK(!strcmp(a.settings.model, "opus"));
    press(&a, "h");
    CHECK(!strcmp(a.settings.model, "fable"));
    press(&a, "jl");
    CHECK(!strcmp(a.settings.effort, "high"));
    CHECK(file_has(path, "model fable\neffort high\n"));

    CASE("cap and calls: a prompt with the number in it; a bad one is refused and changes nothing");
    press(&a, "j\r");
    CHECK(!strcmp(a.prompt.buf, "1.00"));
    press(&a, "\025lots\r");
    CHECK(strstr(a.status, "cap is dollars a job") != NULL);
    CHECK(a.settings.cap == 1.00);
    press(&a, "\r\0252.5\r");
    CHECK(a.settings.cap == 2.5);
    press(&a, "j\r\02540\r");
    CHECK_EQ(a.settings.calls, 40);
    CHECK(file_has(path, "cap 2.50\ncalls 40\n"));

    CASE("fallbacks, the channel and changes at start toggle; the last two say when they apply");
    press(&a, "j\r");
    CHECK_EQ(a.settings.fallbacks, 0);
    press(&a, "j\r");
    CHECK_EQ(a.settings.channel, 1);
    CHECK(strstr(a.status, "next time vtt starts") != NULL);
    press(&a, "j\r");
    CHECK_EQ(a.settings.accept_auto, 1);
    CHECK(file_has(path, "fallbacks off\npaused off\nchannel on\naccept auto\n"));

    CASE("the key row: enter is the hidden prompt; stored, the row says where and ends ...; d asks, then removes");
    press(&a, "j\r");
    CHECK_EQ(a.modal, MODAL_PROMPT);
    CHECK_EQ(a.prompt.secret, 1);
    press(&a, KEY "\r");
    CHECK(frame_has(&a, &r, "a file only you can read, ending ...KLMN"));
    press(&a, "d");
    CHECK_EQ(a.modal, MODAL_CONFIRM_KEY);
    press(&a, "y");
    CHECK(frame_has(&a, &r, "none - enter types one in"));

    CASE("the agent row to Claude with no key asks for one; esc leaves the agent as it was");
    press(&a, "g");
    setenv("VTT_CLAUDE_CURL", "/bin/true", 1);
    press(&a, "h");                                                    /* none, back one: claude */
    CHECK_EQ(a.modal, MODAL_PROMPT);
    press(&a, "\x1b");
    CHECK_EQ(a.settings.agent, AGENT_KIND_OFF);
    press(&a, "h" KEY "\r");
    CHECK_EQ(a.settings.agent, AGENT_KIND_CLAUDE);
    CHECK(frame_has(&a, &r, "the Claude API agent"));
    unsetenv("VTT_CLAUDE_CURL");

    CASE("spent so far, from the usage file when the screen opens: all of it, and this month's");
    {
        char dir[1024], up[1100], month[8], line[200];
        store_dir("", dir, sizeof dir);
        dir_make(dir);
        snprintf(up, sizeof up, "%sclaude-usage.tsv", dir);
        time_t now = time(NULL);
        strftime(month, sizeof month, "%Y-%m", localtime(&now));
        FILE *f = fopen(up, "w");
        fputs("when\tjob\tmodel\tcalls\tinput\tcache_write\tcache_read\toutput\tdollars\tended\n", f);
        fputs("2025-01-03T10:00:00\t1\tclaude-opus-5-5\t7\t1\t1\t1\t1\t0.5000\tdone\n", f);
        snprintf(line, sizeof line, "%s-01T10:00:00\t2\tclaude-opus-5-5\t7\t1\t1\t1\t1\t0.2500\tdone\n", month);
        fputs(line, f);
        fclose(f);
        press(&a, "\x1b");
        CHECK_EQ(a.screen, SCREEN_MENU);
        press(&a, "\r");
        CHECK_EQ(a.screen, SCREEN_OPTIONS);
        CHECK(frame_has(&a, &r, "about $0.75 over 2 runs, $0.25 this month"));
    }

    CASE("esc and q go back to the menu; the menu has Options before Quit");
    press(&a, "q");
    CHECK_EQ(a.screen, SCREEN_MENU);
    CHECK(frame_has(&a, &r, "Options"));

    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}
