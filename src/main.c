#include <errno.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "import.h"
#include "dice.h"
#include "draw.h"
#include "input.h"
#include "loop.h"
#include "maptools.h"
#include "prof.h"
#include "render.h"
#include "term.h"
#include "util.h"
#include "watch.h"
#include "apiagent.h"
#include "apikey.h"
#include "guide.h"

typedef struct {
    const char *trace_path;
    const char *script_path;
    int         ascii;
    int         dump_frame;
    int         dump_cells;         /* --cells: the dump keeps each cell's colors */
    int         dump_players;       /* --players: the dump is the players' frame, not the GM's */
    int         bench;
    int         bench_loops;
    int         width, height;      /* headless geometry */
    const char *map_path;           /* positional argument */
    uint64_t    seed;
    int         seeded;
    const char *watch;              /* --watch host:port: be a mirror */
    int         bench_clients;      /* --bench-clients N: loopback watchers on a bench */
    int         bench_names;        /* --bench-names: they say they are P1, P2... as phones do */
    int         bench_stalled;      /* --bench-stalled: the last of them never reads (docs/V2.md, step 7) */
    const char *bench_record;       /* --bench-record FILE: the first watcher's stream, saved */
    int         serve;              /* --serve: open the remote view at startup */
    int         serve_port;
    int         serve_stay;         /* --stay-alive: and keep it past the map */
    int         serve_no_pings;     /* --no-pings: and ignore the phones' taps */
    int         bench_pings;        /* --bench-pings: the bench's watchers tap every frame */
    int         tool;               /* --dump-map, --check, --describe: a map tool, and exit */
    int         json;               /* --json: the tool's report as JSON */
    const char *region;             /* --region A1:P9 */
    int         agent;              /* --agent: open the control channel at startup */
    int         ctl;                /* --ctl [REQUEST]: be the channel's client */
    const char *ctl_req;            /* NULL: the request is on stdin */
    long        ctl_pid;            /* --ctl-pid N: which vtt; 0 the only one */
    const char *bench_ctl;          /* --bench-ctl FILE: a request run each bench loop */
    int         bench_review;       /* --bench-review: its changes wait for review, not accept auto */
    const char *bench_pre[2];       /* --bench-pre FILE, --bench-pre-land FILE: a request run once before the loops */
    const char *bench_outside;      /* --bench-outside FILE: written over the map's file each bench loop */
    const char *apply;              /* --apply FILE: run a request against the map, save it */
    int         apply_wait;         /* --wait SECONDS: for the GM's verdict, when the map is open in a vtt; -1 unsaid */
    const char *import_adv;         /* --import-adversaries FILE: SRD adversaries as characters */
    const char *import_env;         /* --import-environments FILE: SRD environments as cards for :env */
    int         force;              /* --force: the import replaces templates already there */
    int         new_w, new_h;       /* --new WxH: the map --apply starts from, when it is not there */
    int         claude_agent;       /* --claude-agent: the agent :agent claude starts (apiagent.c) */
    ApiAgentCfg api;                /* its --model, --effort, --cap, --calls, --fallbacks */
} Options;

enum { TOOL_NONE, TOOL_DUMP, TOOL_CHECK, TOOL_DESCRIBE };

static void usage(void)
{
    fputs(
        "usage: vtt [options] [map.vtt]\n"
        "\n"
        "  --ascii            avoid box-drawing glyphs; use ASCII fallbacks\n"
        "  --trace PATH       write a Chrome Tracing profile on exit\n"
        "  --script PATH      replay a keystroke script (\\e esc, \\r enter, \\. pause)\n"
        "  --bench PATH       replay a script headlessly and report frame statistics\n"
        "  --bench-loops N    repetitions for --bench (default 50)\n"
        "  --dump-frame       render one frame as plain text to stdout and exit\n"
        "  --cells            ... with a line of colors under each row (x+n fg/bg/attr)\n"
        "  --players          ... the frame the players' screens are sent (blank until play mode)\n"
        "  --size WxH         geometry for headless modes (default 80x24)\n"
        "  --seed N           seed the dice, for a repeatable session or script\n"
        "  --serve [PORT]     open the remote view at startup (:serve does it later)\n"
        "  --stay-alive       keep that server up when the map closes\n"
        "  --no-pings         ignore taps from the phones on that server\n"
        "  --watch HOST:PORT  mirror a serving vtt in this terminal, read-only\n"
        "  --bench-clients N  attach N loopback watchers to a --bench run\n"
        "  --bench-stalled    the last of them reads only once frames are withheld from it\n"
        "  --bench-names      those watchers are named phones, P1, P2...\n"
        "  --bench-record FILE  save the stream the first watcher is sent (tools/pagebench.sh)\n"
        "  --bench-pings      and have each of them ping every frame\n"
        "  --bench-ctl FILE   run a control-channel request at the top of every --bench loop\n"
        "  --bench-pre FILE   run a request once before the loops, as a proposal that waits;\n"
        "  --bench-pre-land FILE  and one after it that lands (what a waiting proposal conflicts with)\n"
        "                     (its changes land at once, as under :agent accept auto)\n"
        "  --bench-review     ... unless this is given: then they wait for :review\n"
        "  --bench-outside FILE  write FILE over the map's own file every --bench loop, as another program would\n"
        "  --agent            open the control channel at startup (:agent on does it later)\n"
        "  --ctl [REQUEST]    send a request to the vtt taking them, print the answer\n"
        "                     (no REQUEST, or -: read it from stdin; docs/CONTROL.md)\n"
        "  --ctl-pid N [REQ]  with several running, the one with pid N\n"
        "  --agent-guide [TOPIC|VERB]\n"
        "                     the guide for AI agents that build maps (docs/AGENTS.md): where to\n"
        "                     start and its topics, a topic, or one request\n"
        "  --claude-agent     the Claude API agent itself, as :agent claude starts it (docs/APIAGENT.md);\n"
        "                     --model opus|sonnet|haiku|fable|ID, --effort low|medium|high,\n"
        "                     --cap DOLLARS, --calls N, --fallbacks on|off\n"
        "\n"
        "  map tools (print a report and exit; see README, Map tools):\n"
        "  --dump-map         the whole map as text, with a legend\n"
        "  --region A1:P9     only that part of it\n"
        "  --describe         the rooms, their doors and what is in them\n"
        "  --check            find mistakes: exit 0 clean, 1 findings, 2 unreadable\n"
        "  --json             --describe (or --check) as JSON\n"
        "  --apply FILE       run FILE's control-channel requests against the map and save it\n"
        "                     (all or nothing; docs/AGENTS.md)\n"
        "  --new WxH          with --apply, the size of a new, empty map when the file is not there\n"
        "                     When a running vtt has the map open, the plan goes to it as a\n"
        "                     proposal for its GM to review, and the file is not touched.\n"
        "  --wait SECONDS     with --apply to a map open in a vtt: wait for the GM's verdict.\n"
        "                     Exit 0 accepted, 3 none yet, 4 scrapped, 5 sent back (feedback on stdout)\n"
        "  --import-environments FILE\n"
        "                     the SRD's environments (JSON) as cards for :env to give a map\n"
        "  --import-adversaries FILE\n"
        "                     the Daggerheart SRD's adversaries (JSON) as character templates,\n"
        "                     each with its stat block as a card; --force replaces ones already there\n"
        "  -h, --help         this message\n",
        stdout);
}

static int parse_args(Options *o, int argc, char **argv)
{
    memset(o, 0, sizeof *o);
    o->apply_wait = -1;
    o->width       = 80;
    o->height      = 24;
    o->bench_loops = 50;
    o->api = (ApiAgentCfg){ "claude-opus-5-5", "medium", 1.00, 25, 1 };

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 1; }
        else if (!strcmp(a, "--agent-guide")) {
            /* The core, or the topic or request named after it (guide.c): each
             * under what an agent's tool passes whole (GUIDE_PART_MAX). */
            exit(guide_print(stdout, stderr, i + 1 < argc && argv[i + 1][0] != '-' ? argv[i + 1] : NULL));
        }
        else if (!strcmp(a, "--claude-agent")) o->claude_agent = 1;
        else if (!strcmp(a, "--model") && i + 1 < argc) {
            if (!(o->api.model = apiagent_model_id(argv[++i]))) die("bad --model (opus, sonnet, haiku, fable, or a claude- model id)");
        }
        else if (!strcmp(a, "--effort") && i + 1 < argc) {
            o->api.effort = argv[++i];
            if (strcmp(o->api.effort, "low") && strcmp(o->api.effort, "medium") && strcmp(o->api.effort, "high"))
                die("bad --effort (low, medium or high)");
        }
        else if (!strcmp(a, "--cap") && i + 1 < argc) {
            char *end;
            o->api.cap = strtod(argv[++i], &end);
            if (*end || o->api.cap < 0 || o->api.cap > 1000) die("bad --cap (dollars, 0 to 1000; 0 is none)");
        }
        else if (!strcmp(a, "--calls") && i + 1 < argc) {
            o->api.calls = atoi(argv[++i]);
            if (o->api.calls < 1 || o->api.calls > 200) die("bad --calls (1 to 200)");
        }
        else if (!strcmp(a, "--fallbacks") && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "on") && strcmp(v, "off")) die("bad --fallbacks (on or off)");
            o->api.fallbacks = !strcmp(v, "on");
        }
        else if (!strcmp(a, "--ascii"))      o->ascii = 1;
        else if (!strcmp(a, "--dump-frame")) o->dump_frame = 1;
        else if (!strcmp(a, "--cells"))      o->dump_cells = 1;
        else if (!strcmp(a, "--players"))    o->dump_players = 1;
        else if (!strcmp(a, "--trace")  && i + 1 < argc) o->trace_path  = argv[++i];
        else if (!strcmp(a, "--script") && i + 1 < argc) o->script_path = argv[++i];
        else if (!strcmp(a, "--bench")  && i + 1 < argc) { o->bench = 1; o->script_path = argv[++i]; }
        else if (!strcmp(a, "--bench-loops") && i + 1 < argc) o->bench_loops = atoi(argv[++i]);
        else if (!strcmp(a, "--watch") && i + 1 < argc) o->watch = argv[++i];
        else if (!strcmp(a, "--bench-clients") && i + 1 < argc) o->bench_clients = atoi(argv[++i]);
        else if (!strcmp(a, "--stay-alive")) o->serve_stay = 1;
        else if (!strcmp(a, "--no-pings"))   o->serve_no_pings = 1;
        else if (!strcmp(a, "--bench-pings")) o->bench_pings = 1;
        else if (!strcmp(a, "--bench-names")) o->bench_names = 1;
        else if (!strcmp(a, "--bench-stalled")) o->bench_stalled = 1;
        else if (!strcmp(a, "--bench-record") && i + 1 < argc) o->bench_record = argv[++i];
        else if (!strcmp(a, "--agent"))      o->agent = 1;
        else if (!strcmp(a, "--bench-ctl") && i + 1 < argc) o->bench_ctl = argv[++i];
        else if (!strcmp(a, "--bench-review")) o->bench_review = 1;
        else if (!strcmp(a, "--bench-pre") && i + 1 < argc) o->bench_pre[0] = argv[++i];
        else if (!strcmp(a, "--bench-pre-land") && i + 1 < argc) o->bench_pre[1] = argv[++i];
        else if (!strcmp(a, "--bench-outside") && i + 1 < argc) o->bench_outside = argv[++i];
        else if (!strcmp(a, "--apply") && i + 1 < argc) o->apply = argv[++i];
        else if (!strcmp(a, "--wait") && i + 1 < argc) {
            o->apply_wait = atoi(argv[++i]);
            if (o->apply_wait < 0) die("bad --wait (expected seconds)");
        }
        else if (!strcmp(a, "--import-adversaries") && i + 1 < argc) o->import_adv = argv[++i];
        else if (!strcmp(a, "--import-environments") && i + 1 < argc) o->import_env = argv[++i];
        else if (!strcmp(a, "--force"))      o->force = 1;
        else if (!strcmp(a, "--new") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &o->new_w, &o->new_h) != 2 || o->new_w < 1 || o->new_h < 1 ||
                o->new_w > MAP_MAX_DIM || o->new_h > MAP_MAX_DIM)
                die("bad --new (expected WxH, each 1-%d)", MAP_MAX_DIM);
        }
        else if (!strcmp(a, "--ctl")) {
            o->ctl = 1;
            if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
                o->ctl_req = argv[++i];
                if (!strcmp(o->ctl_req, "-")) o->ctl_req = NULL;
            }
        }
        else if (!strcmp(a, "--ctl-pid") && i + 1 < argc) {
            o->ctl = 1;
            o->ctl_pid = strtol(argv[++i], NULL, 10);
            if (o->ctl_pid <= 0) die("bad --ctl-pid (expected a process id)");
            /* And the request, if it comes after: --ctl-pid 42 'status'. */
            if (!o->ctl_req && i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
                o->ctl_req = argv[++i];
                if (!strcmp(o->ctl_req, "-")) o->ctl_req = NULL;
            }
        }
        else if (!strcmp(a, "--serve")) {
            o->serve = 1;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') o->serve_port = atoi(argv[++i]);
        }
        else if (!strcmp(a, "--seed") && i + 1 < argc) {
            o->seed = strtoull(argv[++i], NULL, 10);
            o->seeded = 1;
        }
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &o->width, &o->height) != 2)
                die("bad --size (expected WxH)");
        }
        else if (!strcmp(a, "--dump-map") || !strcmp(a, "--check") || !strcmp(a, "--describe")) {
            int t = !strcmp(a, "--dump-map") ? TOOL_DUMP : !strcmp(a, "--check") ? TOOL_CHECK : TOOL_DESCRIBE;
            if (o->tool && o->tool != t) die("one map tool at a time");
            o->tool = t;
        }
        else if (!strcmp(a, "--json")) o->json = 1;
        else if (!strcmp(a, "--region") && i + 1 < argc) o->region = argv[++i];
        else if (a[0] == '-') die("unknown option: %s (try --help)", a);
        else o->map_path = a;
    }
    return 0;
}

/* ---------------------------------------------------------------- scripts */

/* A keystroke script is the byte stream a terminal would send, split into
 * segments at explicit pauses. The pauses matter: ESC followed immediately
 * by a key is Alt+key, while ESC followed by a gap is the Escape key, and a
 * script that cannot express the gap cannot drive a modal editor. */
typedef struct {
    char   *bytes;
    size_t  len;
    size_t *pauses;      /* byte offsets at which to let the ESC timeout fire */
    size_t  npauses;
} Script;

/* Backslash escapes so sequences stay writable by hand:
 * \e escape, \n \r \t, \\ literal backslash, \xHH arbitrary byte,
 * and \. for a pause. */
static char *read_script_bytes(const char *path, size_t *out_len,
                               size_t **out_pauses, size_t *out_npauses)
{
    size_t n   = 0;
    int    big = 0;
    char  *raw = file_read(path, (size_t)64 << 20, &n, &big);
    if (!raw) die(big ? "script %s is over 64 MB" : "cannot open script %s", path);

    char   *out     = xmalloc(n + 1);
    size_t *pauses  = xmalloc((n + 1) * sizeof(size_t));
    size_t  npauses = 0;
    size_t  j       = 0;

    for (size_t i = 0; i < n; i++) {
        if (raw[i] != '\\' || i + 1 >= n) { out[j++] = raw[i]; continue; }
        char c = raw[++i];
        switch (c) {
        case 'e': out[j++] = '\x1b'; break;
        case 'n': out[j++] = '\n';   break;
        case 'r': out[j++] = '\r';   break;
        case 't': out[j++] = '\t';   break;
        case '\\': out[j++] = '\\';  break;
        case '.': pauses[npauses++] = j; break;      /* let the timeout fire */
        case 'x': {
            if (i + 2 < n) {
                char hex[3] = { raw[i + 1], raw[i + 2], '\0' };
                out[j++] = (char)strtol(hex, NULL, 16);
                i += 2;
            }
            break;
        }
        default: out[j++] = c; break;
        }
    }
    free(raw);
    *out_len     = j;
    *out_pauses  = pauses;
    *out_npauses = npauses;
    return out;
}

static Script read_script(const char *path)
{
    Script s;
    s.bytes = read_script_bytes(path, &s.len, &s.pauses, &s.npauses);
    return s;
}

static void script_free(Script *s)
{
    free(s->bytes);
    free(s->pauses);
    s->bytes = NULL;
    s->pauses = NULL;
}

/* Replays a script through the parser, dispatching every key. Between
 * segments the pending-ESC timeout is resolved exactly as the event loop
 * would, so `\e\.:w` means Escape then a colon command rather than Alt+colon. */
static void script_play(App *a, InputParser *p, const Script *sc,
                        void (*on_key)(App *, Key))
{
    size_t pos = 0;
    for (size_t i = 0; i <= sc->npauses; i++) {
        size_t end = i < sc->npauses ? sc->pauses[i] : sc->len;
        if (end > pos) input_feed(p, sc->bytes + pos, end - pos);
        pos = end;

        Key k;
        while (input_next(p, &k)) on_key(a, k);
        while (input_pending(p) && input_timeout(p, &k)) on_key(a, k);
    }
}

/* --------------------------------------------------------------- headless */

/* The bench's stand-in for the loop's network turn: poll the server's
 * sockets (WAIT ms at most) and serve what is ready. */
static void bench_service(Net *n, int wait, uint64_t now_ms)
{
    struct pollfd fds[1 + NET_MAX_CLIENTS];
    int nf = net_pollfds(n, fds, 1 + NET_MAX_CLIENTS);
    poll(fds, (nfds_t)nf, wait);
    net_service(n, fds, nf, now_ms);
}

/* Renders without a tty. Backs --dump-frame (golden tests) and --bench
 * (deterministic performance baseline): the full draw and diff still run,
 * only the write() to the terminal is skipped. */
static int run_headless(const Options *o)
{
    Renderer r;
    App      a;

    rnd_init(&r);
    rnd_resize(&r, o->width, o->height);
    app_init(&a, NULL, &r);
    a.ascii = o->ascii;
    if (o->map_path) app_open_map(&a, o->map_path);

    /* A dump can be preceded by a scripted session, which is what makes
     * golden tests of real interactions possible rather than just of the
     * opening screen. */
    if (o->dump_frame && o->script_path) {
        Script      sc = read_script(o->script_path);
        InputParser p;
        input_init(&p);
        script_play(&a, &p, &sc, app_key);
        script_free(&sc);
    }

    if (o->dump_frame) {
        prof_frame_begin();
        rnd_begin(&r);
        /* The players' frame is blank until play mode draws into it, as in
         * app_frame: outside it a phone is sent nothing of the GM's screen. */
        if (!o->dump_players)          app_draw(&a);
        else if (app_remote_live(&a))  app_draw_view(&a, VIEW_PLAYERS);
        prof_frame_end();

        ByteBuf out;
        bb_init(&out, 8192);
        if (o->dump_cells) rnd_dump_cells(&r, &out);
        else               rnd_dump(&r, &out);
        fwrite(out.data, 1, out.len, stdout);
        bb_free(&out);
    } else {
        Script      sc = read_script(o->script_path);
        InputParser p;

        /* Loopback watchers, so the bench measures the frame with the
         * remote view attached. They are drained after every frame the way
         * a real client's kernel buffer would drain. */
        int cfd[NET_MAX_CLIENTS];
        int ncf = 0;
        /* The stream a phone is sent, byte for byte, to replay through the
         * page's own code (tools/pagebench.sh): one watcher at least. */
        FILE *recf = NULL;
        if (o->bench_record && !(recf = fopen(o->bench_record, "wb"))) die("cannot write the --bench-record file");
        int nclients = recf && o->bench_clients < 1 ? 1 : o->bench_clients;
        if (nclients > 0) {
            char err[128];
            if (net_start(&a.net, 0, &r, err, sizeof err) == 0) {
                for (int i = 0; i < nclients && i < NET_MAX_CLIENTS; i++) {
                    int fd = socket(AF_INET, SOCK_STREAM, 0);
                    struct sockaddr_in sa;
                    memset(&sa, 0, sizeof sa);
                    sa.sin_family = AF_INET;
                    sa.sin_port   = htons(a.net.port);
                    sa.sin_addr.s_addr = htonl(0x7F000001);
                    if (fd < 0 || connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { if (fd >= 0) close(fd); break; }
                    char hello[24];
                    int  hl = o->bench_names ? snprintf(hello, sizeof hello, "VTT1 P%d\n", i + 1)
                                             : snprintf(hello, sizeof hello, "VTT1\n");
                    (void)!write(fd, hello, (size_t)hl);
                    int fl = fcntl(fd, F_GETFL, 0);
                    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
                    cfd[ncf++] = fd;
                }
                /* Let the hellos arrive and the full frames go out. */
                for (int i = 0; i < 20; i++) bench_service(&a.net, 5, (uint64_t)i);
            }
        }

        /* A request run as a frame of its own before the keys, which are
         * then what puts the map back (u), so every loop starts alike. */
        char  *ctl_req = NULL;
        size_t ctl_len = 0;
        if (o->bench_ctl) {
            int big = 0;
            ctl_req = file_read(o->bench_ctl, CTL_REQ_CAP, &ctl_len, &big);
            if (!ctl_req) die(big ? "%s is over 64 KB" : "cannot read %s", o->bench_ctl);
            a.world.ctl_auto = !o->bench_review;
        }

        /* What the loops find already there: a proposal waiting, then a
         * change landed under it, so a request about its conflicts has some. */
        for (int k = 0; k < 2; k++) {
            if (!o->bench_pre[k]) continue;
            int    big = 0, was = a.world.ctl_auto;
            size_t len = 0;
            char  *req = file_read(o->bench_pre[k], CTL_REQ_CAP, &len, &big);
            if (!req) die(big ? "%s is over 64 KB" : "cannot read %s", o->bench_pre[k]);
            a.world.ctl_auto = k;
            char *ans = request_exec(&a.world, req, &len);
            if (!ans || strncmp(ans, "ok\n", 3) != 0) die("--bench-pre: %.200s", ans ? ans : "out of memory");
            free(ans);
            free(req);
            a.world.ctl_auto = was;
        }
        int pre_jobs = a.world.job_last;

        /* Another program writing the map's file (docs/CONFLICTS.md, step
         * 7): the same bytes each loop, which is a new file by its time. */
        char  *out_bytes = NULL;
        size_t out_len = 0;
        if (o->bench_outside) {
            int big = 0;
            out_bytes = file_read(o->bench_outside, (size_t)64 << 20, &out_len, &big);
            if (!out_bytes) die("cannot read %s", o->bench_outside);
        }

        uint64_t bench_clock_ms = 1000;
        unsigned bench_wakes = 0;           /* --bench-stalled: the times its watcher read, */
        size_t   bench_queued = 0;          /* and the most vtt held for it, */
        size_t   bench_full = 0;            /* and the bound then, a FULL of the screen then */
        unsigned long allocs0 = 0;          /* the count when the second loop starts: the first is setup */
        for (int loop = 0; loop < o->bench_loops && a.running; loop++) {
            if (loop == 1) allocs0 = alloc_calls();
            /* Every loop starts alike: its jobs are numbered from 1 again,
             * so a script's `:ask 1 remove` names the job it just made. */
            a.world.job_last = pre_jobs;
            if (out_bytes) {
                int fd = open(o->map_path, O_WRONLY | O_TRUNC);
                if (fd < 0 || write(fd, out_bytes, out_len) != (ssize_t)out_len) die("--bench-outside: cannot write %s", o->map_path);
                close(fd);
            }
            if (ctl_req) {
                prof_frame_begin();
                /* The tick runs too, in the request's frame, on a clock that
                 * lets the map go quiet: what it owes an agent for the last
                 * loop's edits (the map-changed event) is measured. */
                app_tick(&a, bench_clock_ms);
                bench_clock_ms += 2 * AUTOSAVE_QUIET_MS;
                app_tick(&a, bench_clock_ms);
                size_t len = ctl_len;
                char  *ans = request_exec(&a.world, ctl_req, &len);
                if (loop == 0 && ans && strncmp(ans, "ok\n", 3) != 0) die("--bench-ctl: %.200s", ans);
                free(ans);
                app_frame(&a, NULL, 0);
                prof_frame_end();
                prof_set_counters(r.cells_changed, r.bytes_written);
            }
            input_init(&p);
            input_feed(&p, sc.bytes, sc.len);

            /* One frame per key is the pessimistic case, which is what a
             * baseline should measure. */
            Key k;
            while (input_next(&p, &k)) {
                app_key(&a, k);
                /* Every watcher taps a square each frame, a second of
                 * synthetic time apart: past the rate limit, and with rings
                 * lasting two seconds, one per client is always up -- the
                 * pessimistic case, status line changing every frame. */
                if (ncf && o->bench_pings) {
                    int tick = (int)(bench_clock_ms / 1000 % 1000);
                    for (int i = 0; i < ncf; i++) {
                        char line[32];
                        int  len = snprintf(line, sizeof line, "P %d %d\n",
                                            10 + (i * 13 + tick * 7) % 40, 3 + (i * 5 + tick * 3) % 15);
                        (void)!write(cfd[i], line, (size_t)len);
                    }
                    bench_service(&a.net, 0, bench_clock_ms);
                    app_tick(&a, bench_clock_ms);
                    bench_clock_ms += 1000;
                }
                /* Named phones: the app's tick runs too, so what it does for
                 * them -- the names offered, the arrivals -- is measured. */
                if (ncf && o->bench_names && !o->bench_pings) {
                    bench_service(&a.net, 0, bench_clock_ms);
                    app_tick(&a, bench_clock_ms);
                }
                prof_frame_begin();
                /* A started agent is the tick's to reap: with one running the
                 * tick is in the frame, as the loop has it, or the runs would
                 * pile up unreaped and `agent.reap` have no row. */
                if (a.agents.nrun) app_tick(&a, bench_clock_ms);
                app_frame(&a, NULL, 0);
                prof_frame_end();
                prof_set_counters(r.cells_changed, r.bytes_written);
                if (ncf) {
                    prof_set_net((uint32_t)net_clients(&a.net), a.net.frame_bytes);
                    uint8_t sink[65536];
                    /* The stalled watcher reads its socket dry once vtt
                     * withholds frames from it, a phone whose screen comes
                     * back on: vtt drains its queue and catches it up
                     * (net.catch_up). */
                    int stalled = o->bench_stalled && ncf > 1, wakes = 0;
                    for (int i = 0; stalled && i < a.net.ncl; i++) {
                        wakes |= a.net.cl[i].behind;
                        size_t q = a.net.cl[i].out_len - a.net.cl[i].out_off;
                        if (q > bench_queued) { bench_queued = q; bench_full = a.net.full_len; }
                    }
                    bench_wakes += (unsigned)wakes;
                    for (int i = 0; i < ncf - (stalled && !wakes); i++) {
                        ssize_t got;
                        while ((got = read(cfd[i], sink, sizeof sink)) > 0)
                            if (i == 0 && recf) fwrite(sink, 1, (size_t)got, recf);
                    }
                    if (wakes) bench_service(&a.net, 0, bench_clock_ms);
                }
            }
            a.running = 1;      /* a 'q' in the script must not end the bench */
            if (out_bytes) {
                /* The loop's first key saw the file had changed; the keys
                 * are quiet now, so the tick reads it: a frame of its own. */
                prof_frame_begin();
                bench_clock_ms += 2 * AUTOSAVE_QUIET_MS;
                app_tick(&a, bench_clock_ms);
                app_frame(&a, NULL, 0);
                prof_frame_end();
                prof_set_counters(r.cells_changed, r.bytes_written);
            }
        }
        free(out_bytes);
        /* The most vtt held for the stalled watcher: its queue, past what
         * the kernel took (a FULL's worth at most, by the lag bound). */
        if (o->bench_stalled && ncf > 1)
            fprintf(stderr, "vtt: the stalled watcher: at most %zu bytes queued by vtt, under a FULL of %zu then; "
                    "caught up %u times; %u dropped\n", bench_queued, bench_full, bench_wakes, a.net.dropped);
        for (int i = 0; i < ncf; i++) close(cfd[i]);
        if (recf) fclose(recf);
        free(ctl_req);
        script_free(&sc);
        prof_report();
        /* The run's peak memory, beside its times (docs/V2.md, *Memory: the
         * plan*, 3): what a key or a request costs in memory shows as a row. */
        struct rusage ru;
        if (getrusage(RUSAGE_SELF, &ru) == 0) {
#if defined(__APPLE__)
            ru.ru_maxrss /= 1024;               /* bytes there; kilobytes on Linux and the BSDs */
#endif
            fprintf(stderr, "  maxrss_kb %ld\n", (long)ru.ru_maxrss);
        }
        if (o->bench_loops > 1)
            fprintf(stderr, "  allocs %lu over loops 2-%d\n", alloc_calls() - allocs0, o->bench_loops);
    }

    app_free(&a);
    rnd_free(&r);
    return 0;
}

/* ------------------------------------------------------------ interactive */

static int run_interactive(const Options *o)
{
    Term     t;
    Renderer r;
    App      a;

    /* Before anything is started: ANTHROPIC_API_KEY into vtt's own keeping
     * and out of its environment, so no child inherits it (docs/APIAGENT.md,
     * *The key, made safer*). */
    char keynote[200];
    apikey_take_env(keynote, sizeof keynote);
    if (term_init(&t) < 0) {
        die("not a terminal (try --dump-frame or --bench for headless use)");
    }

    rnd_init(&r);
    rnd_resize(&r, t.w, t.h);
    app_init(&a, &t, &r);
    a.ascii = o->ascii;
    {
        char kf[1100];
        if (keynote[0]) app_note_gm(&a, keynote);
        else if (apikey_old_file(kf, sizeof kf)) {
            char msg[1300];
            snprintf(msg, sizeof msg, "%s is from an older vtt and is not read now - :agent key types the key in again; then remove the file", kf);
            app_note_gm(&a, msg);
        }
    }
    a.autosave_on = 1;              /* interactive only: a bench would litter */
    a.file.ask_holders = 1;              /* a GM is here to be asked */
    /* The GM's settings (docs/APIAGENT.md): interactive only, and saved on
     * each change; a bench or a tool keeps the defaults in memory. */
    {
        char note[400];
        settings_path(a.settings_file, sizeof a.settings_file);
        settings_default(&a.settings);
        settings_load(&a.settings, a.settings_file, note, sizeof note);
        if (note[0]) app_set_status_gm(&a, note);
        a.world.ctl_auto = a.settings.accept_auto;
    }
    if (o->map_path) app_open_map(&a, o->map_path);

    /* From here a terminal that stops reading holds nothing else up; one
     * that cannot be opened a second time is written to as before. */
    (void)term_async(&t);

    Loop l;
    loop_init(&l, &a, &t, &r);

    /* Optional scripted input for reproducing a session on a real terminal. */
    if (o->script_path) {
        Script sc = read_script(o->script_path);
        input_feed(&l.in, sc.bytes, sc.len);
        script_free(&sc);
    }

    if (o->serve) {
        char err[128];
        if ((o->serve_port ? net_start(&a.net, (uint16_t)o->serve_port, &r, err, sizeof err)
                           : net_start_default(&a.net, &r, err, sizeof err)) < 0)
            app_set_status(&a, err);
        else {
            char url[160], msg[256];
            net_set_stay(&a.net, o->serve_stay);
            net_set_pings(&a.net, !o->serve_no_pings);
            net_url(&a.net, url, sizeof url);
            snprintf(msg, sizeof msg, "serving at %s%s%s", url,
                     o->serve_stay ? " - staying up when the map closes" : "",
                     o->serve_no_pings ? " - pings off" : "");
            app_set_status(&a, msg);
        }
    }
    /* The socket always: an --apply to a map open here must find it, or
     * it would write the file under the GM. What it takes beyond that
     * question is :agent on's. Failing to listen is only worth a word when
     * the channel was asked for. */
    {
        char err[CTL_PATH_MAX + 64];
        /* Said either way: with no socket, an --apply to this map cannot
         * find it open and writes the file. */
        if (ctl_start(&a.ctl, err, sizeof err) < 0) app_set_status_gm(&a, err);
        else a.world.agent_on = o->agent || a.settings.channel;
        /* VTT_AGENT_COMMAND wins over the settings for the session it
         * starts, and is not saved. */
        const char *cmd = getenv("VTT_AGENT_COMMAND");
        if (cmd && cmd[0] && strlen(cmd) < AGENT_CMD_MAX) { str_lcpy(a.agents.cmd, cmd, sizeof a.agents.cmd); a.agents.from_env = 1; }
        else if (cmd && cmd[0]) app_set_status_gm(&a, "VTT_AGENT_COMMAND is over 255 characters - not set; put it in a script");
        app_agent_apply(&a);
    }

    /* Paint once before blocking so the first frame is up immediately. */
    loop_frame(&l, prof_now_ns() / 1000000u);

    while (a.running) {
        int timeout = loop_wait(&l, prof_now_ns() / 1000000u);
        int nready  = poll(l.fds, (nfds_t)loop_nfds(&l), timeout);
        if (nready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (loop_once(&l, nready, prof_now_ns() / 1000000u)) break;
    }

    app_free(&a);
    rnd_free(&r);
    term_shutdown(&t);
    return 0;
}

/* ------------------------------------------------------------ map tools */

/* A tool reads the map, prints its report and exits: no terminal, no
 * profiler, nothing written. Exit 2 when the map cannot be read. */
static int run_tool(const Options *o)
{
    if (!o->map_path) { fputs("vtt: a map tool needs a map file\n", stderr); return 2; }
    if (o->json && o->tool == TOOL_DUMP) { fputs("vtt: --json is for --check and --describe\n", stderr); return 2; }
    if (o->region && o->tool != TOOL_DUMP) { fputs("vtt: --region is for --dump-map\n", stderr); return 2; }
    /* The linter loads the file itself, to hear what the loader forgives. */
    if (o->tool == TOOL_CHECK) return maptools_check(stdout, o->map_path, o->json);
    char err[256];
    Map *m = mapio_load(o->map_path, err, sizeof err);
    if (!m) { fprintf(stderr, "vtt: %s\n", err); return 2; }

    int x0 = 0, y0 = 0, x1 = m->w - 1, y1 = m->h - 1;
    if (o->region && !maptools_region(m, o->region, &x0, &y0, &x1, &y1)) {
        fprintf(stderr, "vtt: --region wants two squares, like B2:K12\n");
        map_free(m);
        return 2;
    }

    int rc = 0;
    if (o->tool == TOOL_DUMP)          maptools_dump(stdout, m, x0, y0, x1, y1);
    else if (o->tool == TOOL_DESCRIBE) maptools_describe(stdout, m, o->json);
    map_free(m);
    return rc;
}

/* --apply: a plan run against a map with no terminal, as the control
 * channel would run it in a live session, then saved. The map is opened in
 * build mode, so edits are taken; all or nothing, so a failing plan saves
 * nothing. Exit 0 saved, 1 the plan failed, 2 the map could not be read or
 * written. */
static int run_apply(const Options *o)
{
    if (!o->map_path) { fputs("vtt: --apply needs a map file\n", stderr); return 2; }
    char err[256];
    size_t len = 0;
    int    big = 0;
    char  *req = file_read(o->apply, CTL_PLAN_CAP, &len, &big);
    if (!req) {
        if (big) fprintf(stderr, "vtt: %s is over %d bytes, the most a plan may be - split it\n", o->apply, CTL_PLAN_CAP);
        else fprintf(stderr, "vtt: cannot read %s\n", o->apply);
        return 2;
    }

    /* Open in a running vtt: the plan is a proposal there, for its GM to
     * review, and the file is left alone -- the GM's next :w would have
     * written over it. */
    const char *base = strrchr(o->apply, '/');
    int open_rc = ctl_apply_open(o->map_path, base ? base + 1 : o->apply, req, len, o->apply_wait);
    if (open_rc >= 0) { free(req); return open_rc; }

    int made = 0;                   /* this run made the file: a failure, or a plan that changed nothing, takes it away */
    if (access(o->map_path, F_OK) != 0) {
        if (!o->new_w) {
            fprintf(stderr, "vtt: %s is not there - --new WxH makes it\n", o->map_path);
            free(req);
            return 2;
        }
        /* A new map is void: a plan draws what is there, from nothing. */
        char name[MAP_NAME_MAX];
        path_stem(o->map_path, name, sizeof name);
        Map *nm = map_new(o->new_w, o->new_h, name);
        int wrc = mapio_write(nm, o->map_path, err, sizeof err);
        map_free(nm);
        if (wrc < 0) { fprintf(stderr, "vtt: %s\n", err); free(req); return 2; }
        made = 1;
    }

    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    int rc = 2, untouched = 0;
    if (app_open_map(&a, o->map_path) != 0 || !a.world.map) {
        fprintf(stderr, "vtt: cannot open %s\n", o->map_path);
    } else {
        /* An autosave newer than the map asks the GM, and there is none:
         * the plan works on the file as saved, and says so. */
        if (a.modal == MODAL_CONFIRM_RECOVER) {
            a.modal = MODAL_NONE;
            fprintf(stderr, "vtt: %s has an autosave newer than it; applying to the file as saved\n", o->map_path);
        }
        a.world.ctl_direct = 1;                   /* no GM: the plan is the file's */
        /* A plan that only reads leaves the file as it was, byte for byte. */
        unsigned gen = a.world.map->gen, sgen = a.world.map->scenes_gen, cgen = a.world.map->cards_gen;
        char *ans = request_exec(&a.world, req, &len);
        if (!ans) fputs("vtt: out of memory\n", stderr);
        else {
            size_t first = strcspn(ans, "\n");
            fputs(ans[first] ? ans + first + 1 : ans + first, stdout);
            if (strncmp(ans, "ok\n", 3) != 0) {
                fprintf(stderr, "vtt: %.*s - nothing saved\n", (int)first, ans);
                rc = 1;
            } else if (a.world.map->gen == gen && a.world.map->scenes_gen == sgen && a.world.map->cards_gen == cgen) {
                rc = 0;
                untouched = 1;
            } else if (a.file.rows) {
                /* Saved, the rows would go to disk as read, one off, and the
                 * file's evidence of where with them (as :w asks first). */
                fprintf(stderr, "vtt: %s has a row added or lost by hand near line %d (vtt %s --check says where) - nothing saved\n",
                        o->map_path, a.file.rows, o->map_path);
                rc = 1;
            } else if (mapio_save(a.world.map, o->map_path, err, sizeof err) < 0) {
                fprintf(stderr, "vtt: %s\n", err);
            } else rc = 0;
            free(ans);
        }
    }
    app_free(&a);
    rnd_free(&r);
    free(req);
    /* The file was not there before: a plan that changed nothing leaves it not there. */
    if ((rc != 0 || untouched) && made) unlink(o->map_path);
    return rc;
}

int main(int argc, char **argv)
{
    Options o;
    if (parse_args(&o, argc, argv)) return 0;
    if (o.claude_agent) return apiagent_main(&o.api);
    if (o.tool) return run_tool(&o);
    if (o.ctl)  return ctl_client_main(o.ctl_req, o.ctl_pid);
    if (o.apply) return run_apply(&o);
    if (o.import_adv) {
        char err[320];
        int n = import_adversaries(o.import_adv, o.force, stdout, err, sizeof err);
        if (n < 0) fprintf(stderr, "vtt: %s\n", err);
        return n < 0 ? 2 : 0;
    }
    if (o.import_env) {
        char err[320];
        int n = import_environments(o.import_env, o.force, stdout, err, sizeof err);
        if (n < 0) fprintf(stderr, "vtt: %s\n", err);
        return n < 0 ? 2 : 0;
    }

    draw_set_ascii(o.ascii);
    if (o.watch) return watch_main(o.watch, o.ascii);

    /* For :mirror, which runs this binary again in a new window. */
    static char self[1024];
    ssize_t sl = readlink("/proc/self/exe", self, sizeof self - 1);
    if (sl > 0) { self[sl] = '\0'; app_self_path = self; }
    else app_self_path = argv[0];

    prof_init();
    if (o.seeded) dice_seed(o.seed); else dice_seed_random();
    if (o.trace_path && prof_trace_open(o.trace_path) < 0)
        fprintf(stderr, "vtt: could not start trace buffer\n");

    int rc = (o.dump_frame || o.bench) ? run_headless(&o) : run_interactive(&o);

    prof_shutdown();
    return rc;
}
