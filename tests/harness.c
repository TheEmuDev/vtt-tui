/* What every test file leans on: the counters CHECK updates, typing keys at
 * the app, sandboxes, fixtures, golden files and the JSON checker. */

#include "harness.h"

#include <dirent.h>

int g_checks;
int g_fails;
const char *g_case = "";

void feed(InputParser *p, const char *s)
{
    input_init(p);
    input_feed(p, s, strlen(s));
}

/* ------------------------------------------------- input and golden frames */

/* Drives a real App through a scripted session, renders one frame, and
 * compares the plain-text dump with a stored file. Segments are fed one at a
 * time with the pending-ESC timeout resolved between them, exactly as the
 * event loop does, so `esc` followed by a command is expressible.
 *
 * Run with VTT_UPDATE_GOLDEN=1 to rewrite the expectations. */
/* Compares `data` with tests/golden/NAME.txt, or writes it there under
 * VTT_UPDATE_GOLDEN=1: the frame goldens and the map tools' reports alike. */
void golden_bytes(const char *name, const char *data, size_t len)
{
    char path[256];
    snprintf(path, sizeof path, "tests/golden/%s.txt", name);

    if (getenv("VTT_UPDATE_GOLDEN")) {
        FILE *f = fopen(path, "w");
        if (f) { fwrite(data, 1, len, f); fclose(f); }
        fprintf(stderr, "  wrote %s\n", path);
    } else {
        FILE *f = fopen(path, "rb");
        g_checks++;
        if (!f) {
            g_fails++;
            fprintf(stderr, "  FAIL [%s] missing golden %s "
                            "(VTT_UPDATE_GOLDEN=1 make test to create)\n", name, path);
        } else {
            char  *want = xmalloc(len + 4096);
            size_t n    = fread(want, 1, len + 4096, f);
            fclose(f);
            if (n != len || memcmp(want, data, n) != 0) {
                g_fails++;
                fprintf(stderr, "  FAIL [%s] differs from %s\n", name, path);
                /* Show the first differing line, which is usually enough to
                 * see what moved. */
                size_t i = 0, line = 1, ls = 0;
                while (i < n && i < len && want[i] == data[i]) {
                    if (want[i] == '\n') { line++; ls = i + 1; }
                    i++;
                }
                size_t le = ls;
                while (le < len && data[le] != '\n') le++;
                fprintf(stderr, "    line %zu\n      want: %.*s\n      got : %.*s\n",
                        line, (int)(le - ls), want + ls, (int)(le - ls), data + ls);
            }
            free(want);
        }
    }

}

void golden(const char *name, int w, int h, const char *map_path,
            const char *const *segments, int nsegments, int ascii)
{
    Renderer r;
    App      a;

    rnd_init(&r);
    rnd_resize(&r, w, h);
    app_init(&a, NULL, &r);
    a.ascii = ascii;
    draw_set_ascii(ascii);

    if (map_path && app_open_map(&a, map_path) != 0) {
        g_fails++;
        fprintf(stderr, "  FAIL [%s] could not open fixture %s\n", name, map_path);
        rnd_free(&r);
        return;
    }

    InputParser p;
    input_init(&p);
    for (int i = 0; i < nsegments; i++) {
        input_feed(&p, segments[i], strlen(segments[i]));
        Key k;
        while (input_next(&p, &k)) app_key(&a, k);
        while (input_pending(&p) && input_timeout(&p, &k)) app_key(&a, k);
    }

    rnd_begin(&r);
    app_draw(&a);

    ByteBuf out;
    bb_init(&out, 16384);
    rnd_dump(&r, &out);
    golden_bytes(name, out.data, out.len);

    bb_free(&out);
    app_free(&a);
    rnd_free(&r);
    draw_set_ascii(0);
}

/* ------------------------------------------------ maps, keys and sandboxes */

void write_map_file(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs("VTT 2\nname x\nsize 2 2\nzoom 1\ntiles\n..\n..\n"
          "vedges\n   \n   \nhedges\n  \n  \n  \n", f);
    fclose(f);
}

/* Drives the app through the real input parser rather than a hand-rolled
 * translation, so a test types what a terminal would send: Ctrl-U arrives as
 * 0x15 and becomes MOD_CTRL 'u', and a trailing ESC resolves on the timeout
 * exactly as the event loop resolves it. */
/* Between keys nothing may be left open in the undo log but a wall stroke
 * (undo_balanced): every test that types keys checks it, a failure only --
 * as a CHECK it would add one to the count for every key in the suite. */
static void press_balanced(const App *a, Key k)
{
    static int told;
    if (undo_balanced(&a->undo)) return;
    g_fails++;
    if (told++ < 3)
        fprintf(stderr, "  FAIL [%s] the undo log is left open (nest %d) after key %d/%u\n",
                g_case, a->undo.nest, (int)k.kind, (unsigned)k.ch);
}

void press(App *a, const char *keys)
{
    InputParser p;
    input_init(&p);
    input_feed(&p, keys, strlen(keys));

    Key k;
    while (input_next(&p, &k)) { app_key(a, k); press_balanced(a, k); }
    while (input_pending(&p) && input_timeout(&p, &k)) { app_key(a, k); press_balanced(a, k); }
}

/* The browser reads the working directory AND the user's map directory, so a
 * test that deletes or renames has to pin both. Without this it would list --
 * and then act on -- somebody's real map. */

Sandbox sandbox_enter(const char *tag)
{
    Sandbox s;
    memset(&s, 0, sizeof s);

    snprintf(s.dir, sizeof s.dir, "/tmp/vtt-%s-XXXXXX", tag);
    if (!mkdtemp(s.dir)) return s;
    if (!getcwd(s.cwd, sizeof s.cwd)) return s;

    snprintf(s.datadir, sizeof s.datadir, "%s/xdg", s.dir);
    mkdir(s.datadir, 0755);

    const char *old = getenv("XDG_DATA_HOME");
    if (old) str_lcpy(s.saved_xdg, old, sizeof s.saved_xdg);
    setenv("XDG_DATA_HOME", s.datadir, 1);

    /* The control channel's sockets too: every `vtt` a test runs looks
     * there for a vtt that has its map open (--apply), and a test's own
     * sockets go there. Never the user's: their running vtts would be
     * woken and asked by the suite, and one of theirs stopped would fail
     * it. The sandbox itself (0700 from mkdtemp, a short path) is it. */
    const char *rt = getenv("XDG_RUNTIME_DIR");
    s.had_rt = rt != NULL;
    if (rt) str_lcpy(s.saved_rt, rt, sizeof s.saved_rt);
    setenv("XDG_RUNTIME_DIR", s.dir, 1);

    s.ok = 1;
    return s;
}

/* Everything under a sandbox, depth first; lstat, so a link is removed and
 * never followed out of it. */
static void remove_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            char sub[1024];
            while ((e = readdir(d))) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
                remove_tree(sub);
            }
            closedir(d);
        }
        rmdir(path);
    } else {
        unlink(path);
    }
}

void sandbox_leave(Sandbox *s)
{
    if (!s->ok) return;
    if (chdir(s->cwd) != 0) { }
    if (s->saved_xdg[0]) setenv("XDG_DATA_HOME", s->saved_xdg, 1);
    else                 unsetenv("XDG_DATA_HOME");
    if (s->had_rt) setenv("XDG_RUNTIME_DIR", s->saved_rt, 1);
    else           unsetenv("XDG_RUNTIME_DIR");
    if (!strncmp(s->dir, "/tmp/vtt-", 9)) remove_tree(s->dir);
    s->ok = 0;
}

/* ------------------------------------ colors, files, the tools, the server */

/* Relative luminance, the WCAG definition, so "higher contrast" can be a
 * number in a test rather than an opinion about a screen. */
double luminance(uint32_t c)
{
    double ch[3];
    ch[0] = ((c >> 16) & 0xFFu) / 255.0;
    ch[1] = ((c >> 8)  & 0xFFu) / 255.0;
    ch[2] = ( c        & 0xFFu) / 255.0;

    for (int i = 0; i < 3; i++)
        ch[i] = ch[i] <= 0.04045 ? ch[i] / 12.92
                                 : pow((ch[i] + 0.055) / 1.055, 2.4);

    return 0.2126 * ch[0] + 0.7152 * ch[1] + 0.0722 * ch[2];
}

double contrast(uint32_t a, uint32_t b)
{
    double la = luminance(a), lb = luminance(b);
    if (la < lb) { double t = la; la = lb; lb = t; }
    return (la + 0.05) / (lb + 0.05);
}

/* Reads a whole file; NULL when it cannot. */
char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char  *buf = xmalloc(65536);
    size_t n   = fread(buf, 1, 65535, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

char *tool_text(const Map *m, int x0, int y0, int x1, int y1, size_t *len)
{
    char  *buf = NULL;
    size_t n   = 0;
    FILE  *f   = open_memstream(&buf, &n);
    maptools_dump(f, m, x0, y0, x1, y1);
    fclose(f);
    if (len) *len = n;
    return buf;
}

static const char *jv_value(const char *p, int depth);

static const char *jv_ws(const char *p) { while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++; return p; }

static const char *jv_string(const char *p)
{
    if (*p++ != '"') return NULL;
    while (*p && *p != '"') {
        if ((unsigned char)*p < 0x20) return NULL;
        if (*p == '\\') {
            p++;
            if (*p == 'u') { for (int i = 1; i <= 4; i++) if (!isxdigit((unsigned char)p[i])) return NULL; p += 5; continue; }
            if (!strchr("\"\\/bfnrt", *p)) return NULL;
        }
        p++;
    }
    return *p == '"' ? p + 1 : NULL;
}

static const char *jv_value(const char *p, int depth)
{
    if (depth > 64) return NULL;
    p = jv_ws(p);
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        int  obj = *p == '{';
        p = jv_ws(p + 1);
        if (*p == close) return p + 1;
        for (;;) {
            if (obj) {
                p = jv_string(jv_ws(p));
                if (!p) return NULL;
                p = jv_ws(p);
                if (*p++ != ':') return NULL;
            }
            p = jv_value(p, depth + 1);
            if (!p) return NULL;
            p = jv_ws(p);
            if (*p == ',') { p++; continue; }
            return *p == close ? p + 1 : NULL;
        }
    }
    if (*p == '"') return jv_string(p);
    if (!strncmp(p, "true", 4)) return p + 4;
    if (!strncmp(p, "false", 5)) return p + 5;
    if (!strncmp(p, "null", 4)) return p + 4;
    const char *q = p;
    if (*q == '-') q++;
    if (!isdigit((unsigned char)*q)) return NULL;
    while (isdigit((unsigned char)*q) || *q == '.' || *q == 'e' || *q == 'E' || *q == '+' || *q == '-') q++;
    return q;
}

int json_valid(const char *s)
{
    const char *end = jv_value(s, 0);
    return end && *jv_ws(end) == '\0';
}

char *describe_text(const Map *m, int json, size_t *len)
{
    char  *buf = NULL;
    size_t n   = 0;
    FILE  *f   = open_memstream(&buf, &n);
    maptools_describe(f, m, json);
    fclose(f);
    if (len) *len = n;
    return buf;
}

/* A loopback client of the server under test. */
int net_connect(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port   = htons(port);
    a.sin_addr.s_addr = htonl(0x7F000001);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }
    struct timeval tv = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return fd;
}

/* A loopback browser: connected and its WebSocket upgrade sent, with the
 * join code and `query` ("&n=Aria", or "") after it. Its fd, or -1. */
int ws_connect(const Net *n, const char *query)
{
    int fd = net_connect(n->port);
    if (fd < 0) return -1;
    char up[400];
    int  len = snprintf(up, sizeof up,
                        "GET /ws?k=%s%s HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n",
                        n->code, query);
    if (write(fd, up, (size_t)len) != len) { close(fd); return -1; }
    return fd;
}

/* One turn of the server's event loop, as main would run it. */
void net_pump(Net *n, uint64_t now_ms)
{
    struct pollfd fds[1 + NET_MAX_CLIENTS];
    int k = net_pollfds(n, fds, 1 + NET_MAX_CLIENTS);
    if (k == 0) return;
    poll(fds, (nfds_t)k, 20);
    net_service(n, fds, k, now_ms);
}

/* rnd_dump reads the back buffer, which after a flush is the previous frame;
 * this reads what was actually shown or sent. */
void front_text(const Renderer *r, ByteBuf *out)
{
    for (int y = 0; y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            const Cell *c = &r->front[(size_t)y * (size_t)r->w + (size_t)x];
            if (c->ch == 0) continue;
            char enc[4];
            int  n = utf8_encode(c->ch, enc);
            if (n > 0) bb_put(out, enc, (size_t)n); else bb_putc(out, ' ');
        }
        bb_putc(out, '\n');
    }
}

/* An open 12x5 room, a wall down x=6 with a door at y=2 (closed), and fog
 * painted everywhere, for the sight tests. */
void write_sight_map(const char *dir, const char *name, int reveal, int memory)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "VTT 6\nname sight\nsize 12 5\nzoom 1\nmetric chebyshev\ntiles\n"
               "............\n............\n............\n............\n............\n"
               "vedges\n      |      \n      |      \n      +      \n      |      \n      |      \n"
               "hedges\n            \n            \n            \n            \n            \n            \n"
               "fog on\nfogpatch 1 Dark reveal %d memory %s\nfog\n"
               "AAAAAAAAAAAA\nAAAAAAAAAAAA\nAAAAAAAAAAAA\nAAAAAAAAAAAA\nAAAAAAAAAAAA\n",
            reveal, memory ? "on" : "off");
    fclose(f);
}

/* ----------------------------------------------------- the control channel */

/* One request through app_ctl_exec, the answer as a string. */
char *ctl_ask(App *a, const char *req)
{
    size_t len = strlen(req);
    char  *ans = app_ctl_exec(a, req, &len);
    CHECK(ans != NULL);
    if (ans) CHECK_EQ(strlen(ans), len);
    return ans;
}

/* A w x h map of floor, no walls, in dir; opened in build mode. */
int ctl_blank_map(App *a, const char *dir, int w, int h)
{
    char path[700];
    snprintf(path, sizeof path, "%s/blank.vtt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "VTT 6\nname Blank\nsize %d %d\ntiles\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) fputc('.', f);
        fputc('\n', f);
    }
    fclose(f);
    return app_open_map(a, path) == 0 && a->map != NULL;
}

/* The players' frame as text, drawn now. */
char *players_text(App *a, Renderer *r)
{
    rnd_begin(r);
    app_draw_view(a, VIEW_PLAYERS);
    ByteBuf f;
    bb_init(&f, 65536);
    rnd_dump(r, &f);
    bb_putc(&f, '\0');
    return (char *)f.data;
}
