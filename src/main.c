#include <errno.h>
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
#include "dice.h"
#include "draw.h"
#include "input.h"
#include "prof.h"
#include "render.h"
#include "term.h"
#include "util.h"
#include "watch.h"

/* Upper bound on bytes taken from the terminal in one pass of the event
 * loop, so a flood of input can never postpone the redraw indefinitely. Far
 * larger than any burst a held key produces. */
#define INPUT_DRAIN_MAX ((size_t)64 * 1024)

typedef struct {
    const char *trace_path;
    const char *script_path;
    int         ascii;
    int         dump_frame;
    int         bench;
    int         bench_loops;
    int         width, height;      /* headless geometry */
    const char *map_path;           /* positional argument */
    uint64_t    seed;
    int         seeded;
    const char *watch;              /* --watch host:port: be a mirror */
    int         bench_clients;      /* --bench-clients N: loopback watchers on a bench */
    int         serve;              /* --serve: open the remote view at startup */
    int         serve_port;
    int         serve_stay;         /* --stay-alive: and keep it past the map */
} Options;

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
        "  --size WxH         geometry for headless modes (default 80x24)\n"
        "  --seed N           seed the dice, for a repeatable session or script\n"
        "  --serve [PORT]     open the remote view at startup (:serve does it later)\n"
        "  --stay-alive       keep that server up when the map closes\n"
        "  --watch HOST:PORT  mirror a serving vtt in this terminal, read-only\n"
        "  --bench-clients N  attach N loopback watchers to a --bench run\n"
        "  -h, --help         this message\n",
        stdout);
}

static int parse_args(Options *o, int argc, char **argv)
{
    memset(o, 0, sizeof *o);
    o->width       = 80;
    o->height      = 24;
    o->bench_loops = 50;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 1; }
        else if (!strcmp(a, "--ascii"))      o->ascii = 1;
        else if (!strcmp(a, "--dump-frame")) o->dump_frame = 1;
        else if (!strcmp(a, "--trace")  && i + 1 < argc) o->trace_path  = argv[++i];
        else if (!strcmp(a, "--script") && i + 1 < argc) o->script_path = argv[++i];
        else if (!strcmp(a, "--bench")  && i + 1 < argc) { o->bench = 1; o->script_path = argv[++i]; }
        else if (!strcmp(a, "--bench-loops") && i + 1 < argc) o->bench_loops = atoi(argv[++i]);
        else if (!strcmp(a, "--watch") && i + 1 < argc) o->watch = argv[++i];
        else if (!strcmp(a, "--bench-clients") && i + 1 < argc) o->bench_clients = atoi(argv[++i]);
        else if (!strcmp(a, "--stay-alive")) o->serve_stay = 1;
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
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open script %s: %s", path, strerror(errno));

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) die("cannot size script %s", path);

    char  *raw = xmalloc((size_t)sz + 1);
    size_t n   = fread(raw, 1, (size_t)sz, f);
    fclose(f);
    raw[n] = '\0';

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
        app_draw(&a);
        prof_frame_end();

        ByteBuf out;
        bb_init(&out, 8192);
        rnd_dump(&r, &out);
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
        if (o->bench_clients > 0) {
            char err[128];
            if (net_start(&a.net, 0, &r, err, sizeof err) == 0) {
                for (int i = 0; i < o->bench_clients && i < NET_MAX_CLIENTS; i++) {
                    int fd = socket(AF_INET, SOCK_STREAM, 0);
                    struct sockaddr_in sa;
                    memset(&sa, 0, sizeof sa);
                    sa.sin_family = AF_INET;
                    sa.sin_port   = htons(a.net.port);
                    sa.sin_addr.s_addr = htonl(0x7F000001);
                    if (fd < 0 || connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { if (fd >= 0) close(fd); break; }
                    (void)!write(fd, "VTT1\n", 5);
                    int fl = fcntl(fd, F_GETFL, 0);
                    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
                    cfd[ncf++] = fd;
                }
                /* Let the hellos arrive and the full frames go out. */
                for (int i = 0; i < 20; i++) {
                    struct pollfd fds[1 + NET_MAX_CLIENTS];
                    int k = net_pollfds(&a.net, fds, 1 + NET_MAX_CLIENTS);
                    poll(fds, (nfds_t)k, 5);
                    net_service(&a.net, fds, k, (uint64_t)i);
                }
            }
        }

        for (int loop = 0; loop < o->bench_loops && a.running; loop++) {
            input_init(&p);
            input_feed(&p, sc.bytes, sc.len);

            /* One frame per key is the pessimistic case, which is what a
             * baseline should measure. */
            Key k;
            while (input_next(&p, &k)) {
                app_key(&a, k);
                prof_frame_begin();
                rnd_begin(&r);
                app_draw(&a);
                net_set_live(&a.net, app_remote_live(&a));
                net_frame_begin(&a.net);
                rnd_flush(&r, NULL);
                net_frame_end(&a.net, 0);
                prof_frame_end();
                prof_set_counters(r.cells_changed, r.bytes_written);
                if (ncf) {
                    prof_set_net((uint32_t)net_clients(&a.net), a.net.frame_bytes);
                    uint8_t sink[65536];
                    for (int i = 0; i < ncf; i++)
                        while (read(cfd[i], sink, sizeof sink) > 0) { }
                }
            }
            a.running = 1;      /* a 'q' in the script must not end the bench */
        }
        for (int i = 0; i < ncf; i++) close(cfd[i]);
        script_free(&sc);
        prof_report();
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

    if (term_init(&t) < 0) {
        die("not a terminal (try --dump-frame or --bench for headless use)");
    }

    rnd_init(&r);
    rnd_resize(&r, t.w, t.h);
    app_init(&a, &t, &r);
    a.ascii = o->ascii;
    a.autosave_on = 1;              /* interactive only: a bench would litter */
    if (o->map_path) app_open_map(&a, o->map_path);

    InputParser p;
    input_init(&p);

    /* Optional scripted input for reproducing a session on a real terminal. */
    if (o->script_path) {
        Script sc = read_script(o->script_path);
        input_feed(&p, sc.bytes, sc.len);
        script_free(&sc);
    }

    if (o->serve) {
        char err[128];
        if (net_start(&a.net, (uint16_t)o->serve_port, &r, err, sizeof err) < 0)
            app_set_status(&a, err);
        else {
            char url[160], msg[256];
            net_set_stay(&a.net, o->serve_stay);
            net_url(&a.net, url, sizeof url);
            snprintf(msg, sizeof msg, "serving at %s%s", url,
                     o->serve_stay ? " - staying up when the map closes" : "");
            app_set_status(&a, msg);
        }
    }

    /* Paint once before blocking so the first frame is up immediately. */
    prof_frame_begin();
    rnd_begin(&r);
    app_draw(&a);
    net_frame_begin(&a.net);
    rnd_flush(&r, &t);
    net_frame_end(&a.net, prof_now_ns() / 1000000u);
    prof_frame_end();
    prof_set_counters(r.cells_changed, r.bytes_written);
    a.dirty = 0;

    /* stdin, the signal pipe, then whatever the remote view is listening
     * on: its entries are rebuilt every time round, since clients come and go. */
    struct pollfd fds[2 + 1 + NET_MAX_CLIENTS];
    fds[0].fd = t.in_fd;
    fds[0].events = POLLIN;
    fds[1].fd = term_signal_fd(&t);
    fds[1].events = POLLIN;

    while (a.running) {
        /* Block indefinitely when there is nothing outstanding: an idle vtt
         * must cost zero CPU. The only reason to wake on a timer is an
         * unresolved ESC, which needs a decision after a short grace period. */
        int timeout = input_pending(&p) ? INPUT_ESC_TIMEOUT_MS : -1;
        int nnet = net_pollfds(&a.net, fds + 2, 1 + NET_MAX_CLIENTS);
        /* With clients attached, wake now and then for keep-alives. */
        if (nnet > 1 && (timeout < 0 || timeout > 1000)) timeout = 1000;
        /* And once, for the autosave, when there is unsaved work it has
         * not yet copied: a quiet vtt with nothing owed still sleeps. */
        int due = app_autosave_due(&a, prof_now_ns() / 1000000u);
        if (due >= 0 && (timeout < 0 || due < timeout)) timeout = due;

        int nready = poll(fds, (nfds_t)(2 + nnet), timeout);
        if (nready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (nnet > 0) net_service(&a.net, fds + 2, nnet, prof_now_ns() / 1000000u);
        app_tick(&a, prof_now_ns() / 1000000u);

        if (nready > 0 && (fds[1].revents & POLLIN)) {
            if (term_drain_signals(&t) && term_update_size(&t)) {
                rnd_resize(&r, t.w, t.h);
                a.dirty = 1;
            }
        }

        if (nready > 0 && (fds[0].revents & (POLLIN | POLLHUP))) {
            /* Drain everything the terminal has queued, dispatching as we go.
             * Coalescing a burst of held-down keys into one frame is what
             * keeps movement snappy: the frame is still drawn once, after
             * this loop.
             *
             * Reads are sized to what the parser can still hold, because
             * input_feed() drops the excess -- a long burst would otherwise
             * lose keystrokes silently. The byte cap bounds how long one
             * iteration can spend here so a flood cannot starve the redraw. */
            int eof = 0;
            size_t drained = 0;

            while (drained < INPUT_DRAIN_MAX && term_input_ready(&t)) {
                Key    k;
                size_t room = input_room(&p);
                if (room == 0) {
                    while (input_next(&p, &k)) app_key(&a, k);
                    room = input_room(&p);
                    if (room == 0) break;    /* cannot happen; guard anyway */
                }

                char   buf[512];
                size_t want = room < sizeof buf ? room : sizeof buf;

                ssize_t n = read(t.in_fd, buf, want);
                if (n > 0) {
                    input_feed(&p, buf, (size_t)n);
                    drained += (size_t)n;
                    while (input_next(&p, &k)) app_key(&a, k);
                    continue;
                }
                if (n == 0)         { eof = 1; break; }
                if (errno == EINTR) continue;
                break;
            }
            if (eof) break;
        }

        Key k;
        int handled = 0;
        while (input_next(&p, &k)) { app_key(&a, k); handled = 1; }

        /* poll() returned with nothing readable, so the pending ESC really
         * was the Escape key rather than a slow sequence. */
        if (nready == 0 && input_pending(&p)) {
            if (input_timeout(&p, &k)) { app_key(&a, k); handled = 1; }
        }
        (void)handled;

        if (a.dirty) {
            prof_frame_begin();
            rnd_begin(&r);
            app_draw(&a);
            net_set_live(&a.net, app_remote_live(&a));
            net_frame_begin(&a.net);
            rnd_flush(&r, &t);
            net_frame_end(&a.net, prof_now_ns() / 1000000u);
            prof_frame_end();
            prof_set_counters(r.cells_changed, r.bytes_written);
            if (net_clients(&a.net)) prof_set_net((uint32_t)net_clients(&a.net), a.net.frame_bytes);
            a.dirty = 0;
        }

        /* Only a genuine terminal failure ends the loop; falling behind is
         * waited out inside term_write(). */
        if (t.dead) break;
    }

    app_free(&a);
    rnd_free(&r);
    term_shutdown(&t);
    return 0;
}

int main(int argc, char **argv)
{
    Options o;
    if (parse_args(&o, argc, argv)) return 0;

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
