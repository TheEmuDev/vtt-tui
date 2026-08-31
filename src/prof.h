#ifndef VTT_PROF_H
#define VTT_PROF_H

#include <stdint.h>
#include "render.h"

#ifndef VTT_PROF
#define VTT_PROF 1
#endif

#define PROF_MAX_ZONES 48
#define PROF_HISTORY   256      /* frames retained for percentiles */

uint64_t prof_now_ns(void);

void prof_init(void);
void prof_shutdown(void);

/* Interns a zone name (a string literal) and returns its id. */
int  prof_zone_id(const char *name);
void prof_zone_add(int id, uint64_t ns);

void prof_frame_begin(void);
void prof_frame_end(void);

/* Renderer counters folded into the same history as the timings, because
 * bytes-per-frame explains latency that wall-clock alone does not. */
void prof_set_counters(uint32_t cells_changed, uint32_t bytes_written);

void prof_overlay_toggle(void);
int  prof_overlay_visible(void);
void prof_overlay_draw(Renderer *r);

/* Chrome Tracing output, viewable in perfetto or chrome://tracing. */
int  prof_trace_open(const char *path);
void prof_trace_close(void);

/* One-line summary for --bench. */
void prof_report(void);

typedef struct {
    int      id;
    uint64_t t0;
} ProfScope;

ProfScope prof_scope_begin(int id);
void      prof_scope_end(ProfScope *s);

#if VTT_PROF

#define PROF_CAT2(a, b) a##b
#define PROF_CAT(a, b)  PROF_CAT2(a, b)

/* Times the enclosing block. The zone id is resolved once per call site and
 * cached in a static, so steady-state cost is two clock reads. */
#define PROF_ZONE(zname)                                                       \
    static int PROF_CAT(pz_id_, __LINE__) = -1;                                \
    if (PROF_CAT(pz_id_, __LINE__) < 0)                                        \
        PROF_CAT(pz_id_, __LINE__) = prof_zone_id(zname);                      \
    ProfScope PROF_CAT(pz_sc_, __LINE__)                                       \
        __attribute__((cleanup(prof_scope_end))) =                             \
        prof_scope_begin(PROF_CAT(pz_id_, __LINE__))

#else
#define PROF_ZONE(zname) ((void)0)
#endif

#endif /* VTT_PROF_H */
