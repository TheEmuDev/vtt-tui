#include "prof.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "draw.h"

typedef struct {
    const char *name;
    uint64_t    frame_ns;                 /* accumulated within the current frame */
    uint32_t    frame_calls;
    uint64_t    hist[PROF_HISTORY];       /* per-frame totals, ring */
} Zone;

typedef struct {
    uint64_t ts;
    uint64_t dur;
    int      zone;
} TraceEvent;

#define TRACE_MAX 200000

static struct {
    Zone     zones[PROF_MAX_ZONES];
    int      nzones;

    uint64_t frame_t0;
    uint64_t frame_hist[PROF_HISTORY];
    uint32_t cells_hist[PROF_HISTORY];
    uint32_t bytes_hist[PROF_HISTORY];

    uint32_t frame_count;                 /* total frames since start */
    uint32_t slot;                        /* ring index of the newest sample */
    int      overlay;

    TraceEvent *trace;
    size_t      ntrace;
    char        trace_path[512];
    uint64_t    t_origin;
} P;

/* Percentiles need a sort per zone per frame, which made the overlay cost
 * several times the frame it was reporting on. Recomputing them every few
 * frames keeps the display readable without the observer distorting the
 * measurement. */
#define OVL_RESORT_EVERY 8

uint64_t prof_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void prof_init(void)
{
    memset(&P, 0, sizeof P);
    P.t_origin = prof_now_ns();
}

void prof_shutdown(void)
{
    prof_trace_close();
    free(P.trace);
    P.trace  = NULL;
    P.ntrace = 0;
}

int prof_zone_id(const char *name)
{
    for (int i = 0; i < P.nzones; i++)
        if (P.zones[i].name == name || strcmp(P.zones[i].name, name) == 0)
            return i;

    if (P.nzones >= PROF_MAX_ZONES) return PROF_MAX_ZONES - 1;   /* overflow bucket */
    int id = P.nzones++;
    P.zones[id].name = name;
    return id;
}

void prof_zone_add(int id, uint64_t ns)
{
    if (id < 0 || id >= P.nzones) return;
    P.zones[id].frame_ns += ns;
    P.zones[id].frame_calls++;
}

ProfScope prof_scope_begin(int id)
{
    ProfScope s;
    s.id = id;
    s.t0 = prof_now_ns();
    return s;
}

void prof_scope_end(ProfScope *s)
{
    uint64_t t1  = prof_now_ns();
    uint64_t dur = t1 - s->t0;
    prof_zone_add(s->id, dur);

    if (P.trace && P.ntrace < TRACE_MAX) {
        TraceEvent *e = &P.trace[P.ntrace++];
        e->ts   = s->t0 - P.t_origin;
        e->dur  = dur;
        e->zone = s->id;
    }
}

void prof_frame_begin(void)
{
    P.frame_t0 = prof_now_ns();
    for (int i = 0; i < P.nzones; i++) {
        P.zones[i].frame_ns    = 0;
        P.zones[i].frame_calls = 0;
    }
}

void prof_frame_end(void)
{
    uint32_t slot = P.frame_count % PROF_HISTORY;

    P.frame_hist[slot] = prof_now_ns() - P.frame_t0;
    for (int i = 0; i < P.nzones; i++) P.zones[i].hist[slot] = P.zones[i].frame_ns;

    P.slot = slot;
    P.frame_count++;
}

void prof_set_counters(uint32_t cells_changed, uint32_t bytes_written)
{
    uint32_t slot = P.frame_count % PROF_HISTORY;
    P.cells_hist[slot] = cells_changed;
    P.bytes_hist[slot] = bytes_written;
}

void prof_overlay_toggle(void) { P.overlay = !P.overlay; }
int  prof_overlay_visible(void) { return P.overlay; }

/* ------------------------------------------------------------ statistics */

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

typedef struct { uint64_t last, p50, p99, max; } Stats;

static Stats stats_of(const uint64_t *hist, uint32_t nsamples, uint32_t newest)
{
    Stats st = { 0, 0, 0, 0 };
    if (nsamples == 0) return st;

    uint64_t tmp[PROF_HISTORY];
    memcpy(tmp, hist, nsamples * sizeof(uint64_t));
    st.last = hist[newest];

    qsort(tmp, nsamples, sizeof(uint64_t), cmp_u64);
    st.p50 = tmp[nsamples / 2];
    st.p99 = tmp[(nsamples * 99) / 100 >= nsamples ? nsamples - 1 : (nsamples * 99) / 100];
    st.max = tmp[nsamples - 1];
    return st;
}

/* Formats nanoseconds into a fixed-width, human-scaled string. Values are
 * clamped so the output length is bounded regardless of input; a frame that
 * somehow took over 999 ms only needs to read as "very slow". */
static void fmt_ns(char *buf, size_t n, uint64_t ns)
{
    if (ns < 1000ull) {
        snprintf(buf, n, "%3uns", (unsigned)ns);
    } else if (ns < 1000000ull) {
        snprintf(buf, n, "%3u.%01uus", (unsigned)(ns / 1000),
                 (unsigned)((ns % 1000) / 100));
    } else if (ns < 1000000000ull) {
        snprintf(buf, n, "%3u.%02ums", (unsigned)(ns / 1000000),
                 (unsigned)((ns % 1000000) / 10000));
    } else {
        snprintf(buf, n, "  >1s");
    }
}

/* --------------------------------------------------------------- overlay */

#define OVL_W 46

static const uint32_t SPARK[8] = {
    0x2581u, 0x2582u, 0x2583u, 0x2584u, 0x2585u, 0x2586u, 0x2587u, 0x2588u
};

void prof_overlay_draw(Renderer *r)
{
    if (!P.overlay) return;

    /* The overlay reports its own cost. An instrument that quietly adds to
     * the number it displays is worse than no instrument. */
    PROF_ZONE("prof.overlay");

    uint32_t n = P.frame_count < PROF_HISTORY ? P.frame_count : PROF_HISTORY;
    if (n == 0) return;

    /* Both the percentile sorts and the formatting of the table are cached.
     * Between refreshes the overlay is a handful of draw_text calls, which is
     * what keeps it from costing more than the frame it reports on. The
     * per-frame counters below are still formatted live, since watching them
     * change is the point of having them. */
    static char     ovl_frame[96];
    static char     ovl_zone[PROF_MAX_ZONES][96];
    static uint64_t ovl_zone_p99[PROF_MAX_ZONES];
    static uint64_t ovl_frame_p99, ovl_frame_max;
    static uint32_t cached_at = 0xFFFFFFFFu;
    static int      cached_zones = -1;

    if (cached_at == 0xFFFFFFFFu || cached_zones != P.nzones ||
        P.frame_count - cached_at >= OVL_RESORT_EVERY) {
        char a[16], b[16], c[16];

        Stats fst = stats_of(P.frame_hist, n, P.slot);
        fmt_ns(a, sizeof a, fst.last);
        fmt_ns(b, sizeof b, fst.p50);
        fmt_ns(c, sizeof c, fst.p99);
        snprintf(ovl_frame, sizeof ovl_frame,
                 "frame  %8s  p50 %8s  p99 %8s", a, b, c);
        ovl_frame_p99 = fst.p99;
        ovl_frame_max = fst.max;

        for (int i = 0; i < P.nzones; i++) {
            Stats zs = stats_of(P.zones[i].hist, n, P.slot);
            fmt_ns(a, sizeof a, zs.last);
            fmt_ns(b, sizeof b, zs.p50);
            fmt_ns(c, sizeof c, zs.p99);
            snprintf(ovl_zone[i], sizeof ovl_zone[i], "%-14.14s %8s %8s %8s",
                     P.zones[i].name, a, b, c);
            ovl_zone_p99[i] = zs.p99;
        }
        cached_at    = P.frame_count;
        cached_zones = P.nzones;
    }

    int rows = P.nzones + 6;
    int w    = OVL_W;
    int x    = r->w - w - 1;
    int y    = 1;
    if (x < 0) { x = 0; w = r->w; }
    if (rows > r->h - 2) rows = r->h - 2;
    if (rows < 5) return;

    Style panel = style(RGB(0xC8, 0xC8, 0xD0), RGB(0x14, 0x14, 0x1A), 0);
    Style dim   = style(RGB(0x70, 0x74, 0x80), RGB(0x14, 0x14, 0x1A), 0);
    Style hot   = style(RGB(0xF0, 0x80, 0x60), RGB(0x14, 0x14, 0x1A), 0);
    Style head  = style(RGB(0x90, 0xD0, 0xFF), RGB(0x14, 0x14, 0x1A), ATTR_BOLD);

    Rect box = rect(x, y, w, rows);
    draw_fill(r, box, ' ', panel);
    draw_box(r, box, &BOX_LIGHT, dim);
    draw_text(r, x + 2, y, " profiler  F12 ", -1, head);

    int  cy = y + 1;
    char line[128];

    /* Frame total plus a sparkline of recent frame times. */
    draw_text(r, x + 2, cy++, ovl_frame, w - 3, panel);

    uint64_t peak = ovl_frame_max ? ovl_frame_max : 1;
    uint32_t span = n < (uint32_t)(w - 4) ? n : (uint32_t)(w - 4);
    for (uint32_t i = 0; i < span; i++) {
        /* Walk backwards from the newest sample so the graph reads left-to-right. */
        uint32_t age  = span - 1 - i;
        uint32_t slot = (P.slot + PROF_HISTORY - age) % PROF_HISTORY;
        uint64_t v    = P.frame_hist[slot];
        int      lvl  = (int)((v * 7ull) / peak);
        draw_cell(r, x + 2 + (int)i, cy, SPARK[iclamp(lvl, 0, 7)],
                  v > peak / 2 ? hot : dim);
    }
    cy++;

    snprintf(line, sizeof line, "cells %6u   bytes %7u   frames %6u",
             P.cells_hist[P.slot], P.bytes_hist[P.slot], P.frame_count);
    draw_text(r, x + 2, cy++, line, w - 3, dim);

    draw_text(r, x + 2, cy++, "zone            last      p50      p99", w - 3, head);

    for (int i = 0; i < P.nzones && cy < y + rows - 1; i++)
        draw_text(r, x + 2, cy++, ovl_zone[i], w - 3,
                  ovl_frame_p99 > 0 && ovl_zone_p99[i] > ovl_frame_p99 / 2
                      ? hot : panel);
}

/* ----------------------------------------------------------------- trace */

int prof_trace_open(const char *path)
{
    P.trace = malloc(TRACE_MAX * sizeof(TraceEvent));
    if (!P.trace) return -1;
    P.ntrace = 0;
    str_lcpy(P.trace_path, path, sizeof P.trace_path);
    return 0;
}

void prof_trace_close(void)
{
    if (!P.trace || !P.trace_path[0]) return;

    FILE *f = fopen(P.trace_path, "w");
    if (!f) { P.trace_path[0] = '\0'; return; }

    fputs("{\"traceEvents\":[\n", f);
    for (size_t i = 0; i < P.ntrace; i++) {
        const TraceEvent *e = &P.trace[i];
        /* Chrome Tracing timestamps are microseconds. */
        fprintf(f,
                "%s{\"ph\":\"X\",\"pid\":1,\"tid\":1,\"name\":\"%s\","
                "\"ts\":%.3f,\"dur\":%.3f}",
                i ? ",\n" : "",
                P.zones[e->zone].name,
                (double)e->ts / 1000.0,
                (double)e->dur / 1000.0);
    }
    fputs("\n],\"displayTimeUnit\":\"ms\"}\n", f);
    fclose(f);

    fprintf(stderr, "vtt: wrote %zu trace events to %s\n", P.ntrace, P.trace_path);
    P.trace_path[0] = '\0';
}

void prof_report(void)
{
    uint32_t n = P.frame_count < PROF_HISTORY ? P.frame_count : PROF_HISTORY;
    if (n == 0) { fprintf(stderr, "vtt: no frames rendered\n"); return; }

    char  a[16], b[16], c[16], d[16];
    Stats fs = stats_of(P.frame_hist, n, P.slot);
    fmt_ns(a, sizeof a, fs.last);
    fmt_ns(b, sizeof b, fs.p50);
    fmt_ns(c, sizeof c, fs.p99);
    fmt_ns(d, sizeof d, fs.max);

    fprintf(stderr, "\nframes: %u (last %u sampled)\n", P.frame_count, n);
    fprintf(stderr, "  frame  last %s  p50 %s  p99 %s  max %s\n", a, b, c, d);

    uint64_t bytes = 0, cells = 0;
    for (uint32_t i = 0; i < n; i++) { bytes += P.bytes_hist[i]; cells += P.cells_hist[i]; }
    fprintf(stderr, "  cells/frame avg %llu   bytes/frame avg %llu\n",
            (unsigned long long)(cells / n), (unsigned long long)(bytes / n));

    for (int i = 0; i < P.nzones; i++) {
        Stats zs = stats_of(P.zones[i].hist, n, P.slot);
        fmt_ns(a, sizeof a, zs.p50);
        fmt_ns(b, sizeof b, zs.p99);
        fmt_ns(c, sizeof c, zs.max);
        fprintf(stderr, "  %-14s p50 %s  p99 %s  max %s\n", P.zones[i].name, a, b, c);
    }
}
