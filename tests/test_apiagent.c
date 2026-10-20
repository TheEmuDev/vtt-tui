/* Tests: the Claude API agent (docs/APIAGENT.md), with no network and no
 * money: VTT_CLAUDE_CURL is a script that plays the API from answers the test
 * writes, keeps each body it is sent, its command line and what came down fd
 * 3. The agent is the real ./vtt --claude-agent, started by :agent command. */

#include <sys/stat.h>
#include "harness.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "apiagent.h"
#include "apikey.h"
#include "app_priv.h"

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
