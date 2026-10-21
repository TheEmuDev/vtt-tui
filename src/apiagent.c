#include "apiagent.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "apikey.h"
#include "ctl.h"
#include "json.h"
#include "proc.h"
#include "request.h"
#include "store.h"
#include "util.h"

extern const char   AGENT_GUIDE[];
extern const size_t AGENT_GUIDE_LEN;

#define API_URL        "https://api.anthropic.com/v1/messages"
#define MAX_TOKENS     16000
#define RESULT_MAX     (64 * 1024)      /* a tool result past this is cut, saying so */
#define RETRIES        3
#define CURL_TIME_S    "300"            /* under the ten minutes of silence vtt allows */

/* $ a million tokens. A guard and a report, not a bill: said so where it is
 * printed. Cache writes are 1.25x input (the five-minute cache). */
typedef struct { const char *id; double in, out, read; int fallback; } Price;
static const Price PRICES[] = {
    { "claude-opus-5-5",   4.00, 20.00, 0.20, 1 },
    { "claude-sonnet-5-5", 2.00, 10.00, 0.20, 1 },
    { "claude-haiku-5-5",  0.10,  0.50, 0.01, 0 },
    { "claude-fable-5-1", 10.00, 50.00, 0.25, 1 },
};
#define NPRICES ((int)(sizeof PRICES / sizeof *PRICES))

static const struct { const char *word, *id; } MODELS[] = {
    { "opus", "claude-opus-5-5" }, { "sonnet", "claude-sonnet-5-5" },
    { "haiku", "claude-haiku-5-5" }, { "fable", "claude-fable-5-1" },
};

const char *apiagent_model_id(const char *word)
{
    for (size_t i = 0; i < sizeof MODELS / sizeof *MODELS; i++)
        if (!strcmp(word, MODELS[i].word)) return MODELS[i].id;
    if (!strncmp(word, "claude-", 7) && strlen(word) < 64) {
        for (const char *p = word; *p; p++)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '.')) return NULL;
        return word;
    }
    return NULL;
}

static const Price *price_of(const char *model)
{
    for (int i = 0; i < NPRICES; i++)
        if (!strcmp(PRICES[i].id, model)) return &PRICES[i];
    return NULL;
}

double apiagent_cost(const char *model, long in, long cache_write, long cache_read, long out)
{
    const Price *p = price_of(model);
    if (!p) return -1;
    return ((double)in * p->in + (double)cache_write * p->in * 1.25 +
            (double)cache_read * p->read + (double)out * p->out) / 1e6;
}

/* The system prompt's first block. The same for every job, so the guide after
 * it is read from the cache across jobs, not only within one. */
static const char ROLE[] =
    "You are the agent built into vtt, a virtual tabletop in a terminal, answering one job the GM "
    "asked for with :ask. The first message is the job: its number, the box the GM drew, the map, "
    "and its history.\n\n"
    "You have one tool, vtt. Its input is the text of one channel request -- one line, or several "
    "lines that are one request -- exactly what would follow `vtt --ctl` in the job's text, which "
    "shows shell commands for agents that run them: send only the request. It is sent as you, the "
    "job's agent, and its answer comes back as vtt wrote it, the verdict first (ok, busy, or "
    "error CLASS: line N: why).\n\n"
    "Take the job, read the map where the GM drew (dump, describe), propose once with a request "
    "that begins `job N propose \"one line for the GM\"` followed by its edit lines, read the "
    "proposal back (job N check, job N dump) and fix it if it is wrong, then end your turn with "
    "one short line saying what you proposed. The GM reviews it; you are started again if they "
    "send it back. Never use wait. Read small regions rather than the whole map. The guide that "
    "follows is the whole of the request language and how to build a map: follow it.\n";

static const char TOOL_DESC[] =
    "Send one request to the vtt the GM has open, as this job's agent, and get its answer. The "
    "text is what would follow `vtt --ctl`: one line, or several lines that are one request "
    "(a `job N propose \"...\"` line then its edits). All or nothing: a failing line changes nothing "
    "and the answer names it.";

typedef struct {
    ApiAgentCfg cfg;
    long        pid;
    int         job;
    char        name[64];
    char        key[APIKEY_MAX];
    ByteBuf     msgs;           /* the messages so far, each JSON, comma-separated */
    long        in, cw, cr, out;
    int         calls;
    char        trace[1024];
} Run;

static void say_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("claude: ", stdout);
    vprintf(fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
    fflush(stdout);
}

/* A request to our vtt, as this job's agent; the answer to free, or NULL. */
static char *ask_vtt(Run *r, const char *req, size_t *alen, char *err, size_t errsz)
{
    ByteBuf b;
    bb_init(&b, strlen(req) + 80);
    bb_puts(&b, "agent ");
    bb_puts(&b, r->name);
    bb_putc(&b, '\n');
    bb_puts(&b, req);
    if (!b.len || b.data[b.len - 1] != '\n') bb_putc(&b, '\n');
    bb_putc(&b, '\0');
    char *ans = ctl_ask_pid(r->pid, b.data, b.len - 1, alen, err, errsz);
    bb_free(&b);
    return ans;
}

/* A line on the GM's status line, for an end the GM must hear of. */
static void tell_gm(Run *r, const char *line)
{
    /* One quoted word on the channel: under CTL_WORD_MAX once unescaped, or
     * the line is refused and the GM hears only the exit status. */
    char req[600], q[CTL_WORD_MAX * 2];
    size_t k = 0, plain = 0;
    for (const char *p = line; *p && k + 2 < sizeof q && plain + 1 < CTL_WORD_MAX; p++, plain++) {
        if (*p == '"' || *p == '\\') q[k++] = '\\';
        q[k++] = *p == '\n' ? ' ' : *p;
    }
    q[k] = '\0';
    snprintf(req, sizeof req, "job %d say \"%s\"", r->job, q);
    size_t alen;
    char   err[200];
    free(ask_vtt(r, req, &alen, err, sizeof err));
    say_log("%s", line);
}

/* Bytes that are not UTF-8 would make the request not JSON: each becomes '?'. */
static void utf8_clean(char *s, size_t n)
{
    for (size_t i = 0; i < n; ) {
        uint32_t cp;
        int k = utf8_decode(s + i, n - i, &cp);
        if (k <= 0) { s[i++] = '?'; continue; }
        i += (size_t)k;
    }
}

static void put_msg_sep(Run *r)
{
    if (r->msgs.len) bb_putc(&r->msgs, ',');
}

static void add_user_text(Run *r, const char *text)
{
    char  *buf;
    size_t len;
    FILE  *f = xmemstream(&buf, &len);
    Json   j;
    json_init(&j, f);
    json_open(&j, '{');
    json_kstr(&j, "role", "user");
    json_kstr(&j, "content", text);
    json_close(&j, '}');
    fclose(f);
    put_msg_sep(r);
    bb_put(&r->msgs, buf, len);
    free(buf);
}

static void write_body(Run *r, ByteBuf *body)
{
    char  *buf;
    size_t len;
    FILE  *f = xmemstream(&buf, &len);
    Json   j;
    json_init(&j, f);
    json_open(&j, '{');
    json_kstr(&j, "model", r->cfg.model);
    json_kint(&j, "max_tokens", MAX_TOKENS);
    json_key(&j, "system");
    json_open(&j, '[');
    json_open(&j, '{');
    json_kstr(&j, "type", "text");
    json_kstr(&j, "text", ROLE);
    json_close(&j, '}');
    json_open(&j, '{');
    json_kstr(&j, "type", "text");
    json_key(&j, "text");
    json_str(&j, AGENT_GUIDE);
    json_key(&j, "cache_control");
    json_open(&j, '{');
    json_kstr(&j, "type", "ephemeral");
    json_close(&j, '}');
    json_close(&j, '}');
    json_close(&j, ']');
    json_key(&j, "tools");
    json_open(&j, '[');
    json_open(&j, '{');
    json_kstr(&j, "name", "vtt");
    json_kstr(&j, "description", TOOL_DESC);
    json_key(&j, "strict");
    json_bool(&j, 1);
    json_key(&j, "input_schema");
    json_open(&j, '{');
    json_kstr(&j, "type", "object");
    json_key(&j, "properties");
    json_open(&j, '{');
    json_key(&j, "request");
    json_open(&j, '{');
    json_kstr(&j, "type", "string");
    json_kstr(&j, "description", "the request's text, as it would follow vtt --ctl");
    json_close(&j, '}');
    json_close(&j, '}');
    json_key(&j, "required");
    json_open(&j, '[');
    json_str(&j, "request");
    json_close(&j, ']');
    json_key(&j, "additionalProperties");
    json_bool(&j, 0);
    json_close(&j, '}');
    json_close(&j, '}');
    json_close(&j, ']');
    json_key(&j, "output_config");
    json_open(&j, '{');
    json_kstr(&j, "effort", r->cfg.effort);
    json_close(&j, '}');
    json_key(&j, "cache_control");
    json_open(&j, '{');
    json_kstr(&j, "type", "ephemeral");
    json_close(&j, '}');
    const Price *p = price_of(r->cfg.model);
    if (r->cfg.fallbacks && p && p->fallback) json_kstr(&j, "fallbacks", "default");
    json_key(&j, "messages");
    json_open(&j, '[');
    json_raw(&j, r->msgs.data, r->msgs.len);
    json_close(&j, ']');
    json_close(&j, '}');
    fclose(f);
    bb_put(body, buf, len);
    free(buf);
}

static void trace_write(Run *r, const char *what, const char *data, size_t len)
{
    if (!r->trace[0]) return;
    char path[1200];
    snprintf(path, sizeof path, "%s/call-%02d-%s", r->trace, r->calls, what);
    char err[200];
    file_write_whole(path, data, len, 0600, 0, err, sizeof err);
}

typedef struct {
    int    status;          /* HTTP status; 0: no answer (curl failed) */
    int    curl;            /* curl's exit status */
    long   retry_after;     /* seconds, or -1 */
    int    cut;             /* a status line came, then curl failed: the request reached the API */
    char  *body;            /* into the answer's buffer */
    size_t body_len;
} Http;

/* curl's -i output: one or more header blocks (a 100 Continue, a proxy's
 * CONNECT), the last one the answer's, then the body. */
static void read_http(ByteBuf *out, Http *h)
{
    h->status = 0;
    h->retry_after = -1;
    h->body = out->data;
    h->body_len = out->len;
    char *p = out->data, *end = out->data + out->len;
    while (p && end - p > 5 && !strncmp(p, "HTTP/", 5)) {
        const char *sp = memchr(p, ' ', (size_t)(end - p));
        h->status = sp ? atoi(sp + 1) : 0;
        h->retry_after = -1;
        char *eoh = NULL;
        for (char *q = p; q + 1 < end; q++) {
            if (q[0] == '\n' && (q[1] == '\n' || (q[1] == '\r' && q + 2 < end && q[2] == '\n'))) { eoh = q; break; }
            if (q[0] == '\n' && !strncasecmp(q + 1, "retry-after:", 12)) {
                /* Seconds, or an HTTP date: a date leaves the backoff its own. */
                char *e;
                const char *v = q + 13;
                while (*v == ' ') v++;
                long secs = strtol(v, &e, 10);
                h->retry_after = e > v && (*e == '\r' || *e == '\n') ? secs : -1;
            }
        }
        if (!eoh) { p = end; break; }
        p = eoh + (eoh[1] == '\n' ? 2 : 3);
    }
    h->body = p;
    h->body_len = (size_t)(end - p);
}

static int call_api(Run *r, const ByteBuf *body, ByteBuf *out, Http *h, char *err, size_t errsz)
{
    char fd3[APIKEY_MAX + 300];
    const Price *p = price_of(r->cfg.model);
    int  n = snprintf(fd3, sizeof fd3,
                      "header = \"x-api-key: %s\"\nheader = \"anthropic-version: 2023-06-01\"\n"
                      "header = \"content-type: application/json\"\n%s",
                      r->key, r->cfg.fallbacks && p && p->fallback ? "header = \"anthropic-beta: server-side-fallback-2026-07-01\"\n" : "");
    static const char *const ARGS[] = { "curl", "-sS", "-i", "--config", "/dev/fd/3", "--data-binary", "@-",
                                        "--max-time", CURL_TIME_S, "--connect-timeout", "20", API_URL, NULL };
    char *argv[sizeof ARGS / sizeof *ARGS];
    for (size_t i = 0; i < sizeof ARGS / sizeof *ARGS; i++) argv[i] = (char *)ARGS[i];   /* exec's type; never written */
    const char *curl = getenv("VTT_CLAUDE_CURL");
    if (curl && *curl) argv[0] = (char *)curl;
    ProcIn pi = { body->data, body->len, fd3, (size_t)n, 330 * 1000, 0 };
    bb_reset(out);
    int st, rc = proc_run(argv, &pi, out, &st, err, errsz);
    apikey_wipe(fd3, sizeof fd3);
    if (rc < 0) return -1;
    h->curl = st;
    h->cut = 0;
    read_http(out, h);
    if (st != 0) {
        /* curl failed after an answer began: a whole 200 is used as it came
         * (sending it again would pay twice); anything less is cut, and never
         * sent again either -- the API has the request. */
        char e[80];
        JsonVal *d = h->status == 200 && h->body_len ? json_parse(h->body, h->body_len, e, sizeof e) : NULL;
        int whole = d && json_get(d, "content") && json_get(d, "stop_reason");
        json_free(d);
        /* A whole error answer keeps its status: a 429 backs off as ever (it
         * was not billed), a 401 says so. A lone 100 Continue began nothing. */
        if (!whole && h->status >= 400 && h->body_len) {
            JsonVal *eo = json_parse(h->body, h->body_len, e, sizeof e);
            whole = eo && json_text(json_get(json_get(eo, "error"), "message")) != NULL;
            json_free(eo);
        }
        if (!whole) { h->cut = h->status >= 200; h->status = 0; }
    }
    return 0;
}

/* curl's exits that may mean the request never reached the API: resolving,
 * connecting, TLS, and a connection lost with no answer begun (52, 55, 56 --
 * retried only when no status line came: call_api's `cut`). A timeout (28)
 * is not one: the API may have answered, and billed, after curl gave up, so
 * it is never sent again. */
static int curl_never_reached(int code)
{
    return code == 5 || code == 6 || code == 7 || code == 35 || code == 52 || code == 55 || code == 56;
}

static int retryable(const Http *h)
{
    if (h->status == 0) return !h->cut && curl_never_reached(h->curl);
    return h->status == 429 || h->status == 500 || h->status == 502 || h->status == 503 || h->status == 529;
}

static void api_message(const Http *h, char *msg, size_t sz)
{
    char err[160];
    JsonVal *doc = h->body_len ? json_parse(h->body, h->body_len, err, sizeof err) : NULL;
    const char *m = doc ? json_text(json_get(json_get(doc, "error"), "message")) : NULL;
    if (h->status == 0 && h->cut) snprintf(msg, sz, "the API's answer was cut off (curl exit %d) - not sent again, since it may have been billed", h->curl);
    else if (h->status == 0 && h->curl == 28) snprintf(msg, sz, "the API did not answer within " CURL_TIME_S " s (curl timed out) - not sent again, since it may have been billed");
    else if (h->status == 0 && h->curl == 127) snprintf(msg, sz, "curl could not be run (exit 127)");
    else if (h->status == 0) snprintf(msg, sz, "curl could not reach the API (curl exit %d)", h->curl);
    else snprintf(msg, sz, "the API said %d%s%.200s", h->status, m ? ": " : "", m ? m : "");
    json_free(doc);
}

static void usage_add(Run *r, const JsonVal *u)
{
    if (!u) return;
    const JsonVal *v;
    if ((v = json_get(u, "input_tokens")))                r->in  += (long)v->num;
    if ((v = json_get(u, "cache_creation_input_tokens"))) r->cw  += (long)v->num;
    if ((v = json_get(u, "cache_read_input_tokens")))     r->cr  += (long)v->num;
    if ((v = json_get(u, "output_tokens")))               r->out += (long)v->num;
}

static double spent(const Run *r)
{
    return apiagent_cost(r->cfg.model, r->in, r->cw, r->cr, r->out);
}

static void usage_record(const Run *r, const char *how)
{
    char dir[1024], path[1100];
    store_dir("", dir, sizeof dir);
    dir_make(dir);
    snprintf(path, sizeof path, "%sclaude-usage.tsv", dir);
    struct stat st;
    int fresh = stat(path, &st) != 0;
    FILE *f = fopen(path, "a");
    if (!f) return;
    if (fresh) fputs("when\tjob\tmodel\tcalls\tinput\tcache_write\tcache_read\toutput\tdollars\tended\n", f);
    char when[32];
    time_t t = time(NULL);
    strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%S", localtime(&t));
    double d = spent(r);
    fprintf(f, "%s\t%d\t%s\t%d\t%ld\t%ld\t%ld\t%ld\t%.4f\t%s\n", when, r->job, r->cfg.model, r->calls,
            r->in, r->cw, r->cr, r->out, d < 0 ? 0.0 : d, how);
    fclose(f);
}

static int finish(Run *r, int code, const char *how)
{
    double d = spent(r);
    char cost[64];
    if (d < 0) snprintf(cost, sizeof cost, "price unknown");
    else snprintf(cost, sizeof cost, "about $%.2f by vtt's price table", d);
    say_log("job %d %s: %d calls, %ld in, %ld cache written, %ld cache read, %ld out, %s, %s",
            r->job, how, r->calls, r->in, r->cw, r->cr, r->out, cost, r->cfg.model);
    usage_record(r, how);
    apikey_wipe(r->key, sizeof r->key);
    bb_free(&r->msgs);
    return code;
}

/* Runs each tool_use block of an answer's content and appends the one user
 * message that answers them all. -1 when the vtt is gone. */
static int run_tools(Run *r, const JsonVal *content, char *why, size_t whysz)
{
    char  *buf;
    size_t len;
    FILE  *f = xmemstream(&buf, &len);
    Json   j;
    json_init(&j, f);
    json_open(&j, '{');
    json_kstr(&j, "role", "user");
    json_key(&j, "content");
    json_open(&j, '[');
    int gone = 0;
    for (int i = 0; i < content->n; i++) {
        const JsonVal *b = &content->kids[i];
        const char *type = json_text(json_get(b, "type"));
        if (!type || strcmp(type, "tool_use")) continue;
        const char *id = json_text(json_get(b, "id"));
        const char *name = json_text(json_get(b, "name"));
        const char *req = json_text(json_get(json_get(b, "input"), "request"));
        char  *ans = NULL, err[300] = "";
        size_t alen = 0;
        int    is_err = 1;
        if (!name || strcmp(name, "vtt") || !req) snprintf(err, sizeof err, "the only tool is vtt, with a request");
        else if (gone) snprintf(err, sizeof err, "the vtt went away");
        else {
            ans = ask_vtt(r, req, &alen, err, sizeof err);
            if (!ans) { gone = 1; snprintf(why, whysz, "%s", err); }
            else is_err = !(alen >= 2 && !strncmp(ans, "ok", 2) && (alen == 2 || ans[2] == '\n'));
            say_log("  %.*s -> %.*s", (int)strcspn(req, "\n"), req, ans ? (int)strcspn(ans, "\n") : 0, ans ? ans : "");
        }
        if (ans && alen > RESULT_MAX) {
            size_t cut = utf8_cut(ans, RESULT_MAX);
            snprintf(ans + cut, alen - cut + 1, "\n(cut at 64 KB: ask for a smaller region)");
            alen = strlen(ans);
        }
        if (ans) utf8_clean(ans, alen);
        json_open(&j, '{');
        json_kstr(&j, "type", "tool_result");
        json_kstr(&j, "tool_use_id", id ? id : "");
        json_kstr(&j, "content", ans ? ans : err);
        if (is_err) { json_key(&j, "is_error"); json_bool(&j, 1); }
        json_close(&j, '}');
        free(ans);
    }
    json_close(&j, ']');
    json_close(&j, '}');
    fclose(f);
    put_msg_sep(r);
    bb_put(&r->msgs, buf, len);
    free(buf);
    return gone ? -1 : 0;
}

int apiagent_main(const ApiAgentCfg *cfg)
{
    signal(SIGPIPE, SIG_IGN);
    static Run run;
    Run *r = &run;
    memset(r, 0, sizeof *r);
    r->cfg = *cfg;
    const char *pid = getenv("VTT_PID"), *job = getenv("VTT_JOB"), *name = getenv("VTT_AGENT");
    if (!pid || !job || !name || !ctl_name_ok(name)) {
        fputs("vtt: --claude-agent is the agent :agent claude starts; it needs VTT_PID, VTT_JOB and VTT_AGENT\n", stderr);
        return APIAGENT_SETUP;
    }
    r->pid = atol(pid);
    r->job = atoi(job);
    str_lcpy(r->name, name, sizeof r->name);
    const char *tr = getenv("VTT_CLAUDE_TRACE");
    if (tr && *tr) { str_lcpy(r->trace, tr, sizeof r->trace); dir_make(r->trace); }
    bb_init(&r->msgs, 8192);

    char err[600];
    ApiKeySource src = apikey_find(r->key, sizeof r->key, err, sizeof err);
    if (src == APIKEY_NONE) { tell_gm(r, err); return finish(r, APIAGENT_SETUP, "not started"); }
    const char *curl = getenv("VTT_CLAUDE_CURL");
    if (!proc_found(curl && *curl ? curl : "curl")) {
        tell_gm(r, "the Claude agent needs curl, and it is not on PATH");
        return finish(r, APIAGENT_SETUP, "not started");
    }

    size_t plen;
    char  *prompt = stream_read(stdin, 256 * 1024, &plen);
    if (!prompt || !plen) { free(prompt); tell_gm(r, "the Claude agent was given no job"); return finish(r, APIAGENT_SETUP, "not started"); }
    utf8_clean(prompt, plen);
    add_user_text(r, prompt);
    free(prompt);
    say_log("job %d: %s, %s effort, key from the %s", r->job, r->cfg.model, r->cfg.effort, apikey_source_name(src));
    if (r->cfg.cap > 0 && !price_of(r->cfg.model)) {
        /* Said, not silent: the GM set a cap and believes it holds. */
        char line[300];
        snprintf(line, sizeof line, "%.60s is not in vtt's price table: the $%.2f cap does not apply, only the %d-call cap",
                 r->cfg.model, r->cfg.cap, r->cfg.calls);
        tell_gm(r, line);
    }

    ByteBuf body, out;
    bb_init(&body, 64 * 1024);
    bb_init(&out, 64 * 1024);
    int code = -1;
    const char *how = "";
    char line[400];
    while (code < 0) {
        if (r->calls >= r->cfg.calls) {
            snprintf(line, sizeof line, "the Claude agent stopped: %d calls and not done", r->calls);
            tell_gm(r, line);
            code = APIAGENT_CAP; how = "out of calls";
            break;
        }
        bb_reset(&body);
        write_body(r, &body);
        r->calls++;
        trace_write(r, "request.json", body.data, body.len);
        Http h;
        int tries = 0;
        for (;;) {
            if (call_api(r, &body, &out, &h, err, sizeof err) < 0) { h.status = 0; h.curl = -1; h.body = NULL; h.body_len = 0; h.retry_after = -1; h.cut = 0; }
            if (h.status == 200 || !retryable(&h) || tries >= RETRIES) break;
            long wait = h.retry_after >= 0 ? h.retry_after : 2L << tries;
            if (wait > 60) wait = 60;
            api_message(&h, line, sizeof line);
            say_log("%s - again in %ld s", line, wait);
            tries++;
            sleep((unsigned)wait);
        }
        trace_write(r, "answer.json", h.body ? h.body : "", h.body ? h.body_len : 0);
        if (h.status != 200) {
            api_message(&h, line, sizeof line);
            if (h.status == 401) snprintf(line, sizeof line, "the API key was refused (401): :agent key replaces it");
            tell_gm(r, line);
            code = APIAGENT_API; how = "failed";
            break;
        }
        JsonVal *doc = json_parse(h.body, h.body_len, err, sizeof err);
        const JsonVal *content = doc ? json_get(doc, "content") : NULL;
        const char *stop = doc ? json_text(json_get(doc, "stop_reason")) : NULL;
        if (!content || content->kind != JSON_ARR || !stop) {
            json_free(doc);
            tell_gm(r, "the Claude agent could not read the API's answer");
            code = APIAGENT_API; how = "failed";
            break;
        }
        usage_add(r, json_get(doc, "usage"));
        /* The answer's content back byte for byte: thinking blocks are
         * checked unchanged, and a number must not be written again. */
        put_msg_sep(r);
        bb_puts(&r->msgs, "{\"role\":\"assistant\",\"content\":");
        bb_put(&r->msgs, h.body + content->at, content->len);
        bb_putc(&r->msgs, '}');
        say_log("call %d: %s, %ld in, %ld cache read, %ld out so far", r->calls, stop, r->in + r->cw, r->cr, r->out);

        if (!strcmp(stop, "tool_use")) {
            if (run_tools(r, content, line, sizeof line) < 0) {
                say_log("the vtt went away: %s", line);
                code = APIAGENT_API; how = "vtt gone";
            }
        } else if (!strcmp(stop, "end_turn") || !strcmp(stop, "stop_sequence")) {
            for (int i = 0; i < content->n; i++) {
                const char *t = json_text(json_get(&content->kids[i], "type"));
                const char *x = json_text(json_get(&content->kids[i], "text"));
                if (t && !strcmp(t, "text") && x && *x) say_log("%s", x);
            }
            code = APIAGENT_DONE; how = "done";
        } else if (!strcmp(stop, "refusal")) {
            const char *cat = json_text(json_get(json_get(doc, "stop_details"), "category"));
            snprintf(line, sizeof line, "Claude declined this job%s%s%s", cat ? " (" : "", cat ? cat : "", cat ? ")" : "");
            tell_gm(r, line);
            code = APIAGENT_REFUSED; how = "declined";
        } else if (!strcmp(stop, "max_tokens")) {
            tell_gm(r, "the Claude agent's answer ran past 16,000 tokens");
            code = APIAGENT_LONG; how = "too long";
        } else {
            snprintf(line, sizeof line, "the Claude agent stopped: %.60s", stop);
            tell_gm(r, line);
            code = APIAGENT_API; how = "failed";
        }
        json_free(doc);
        double d = spent(r);
        if (code < 0 && r->cfg.cap > 0 && d > r->cfg.cap) {
            snprintf(line, sizeof line, "the Claude agent stopped at its cap: about $%.2f spent of $%.2f", d, r->cfg.cap);
            tell_gm(r, line);
            code = APIAGENT_CAP; how = "at its cap";
        }
    }
    bb_free(&body);
    bb_free(&out);
    return finish(r, code, how);
}
