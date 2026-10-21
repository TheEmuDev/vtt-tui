/* Every allocation a vtt makes, counted from outside it (docs/V2.md, *Memory:
 * the plan*, 8; docs/reviews/2026-10-20-4-memory.md, T5): an LD_PRELOAD shim
 * over malloc, calloc, realloc and free. Where util.c's alloc_calls counts the
 * x* calls only, this sees libc's too -- a raw malloc, open_memstream's
 * buffer, strdup -- and with MCOUNT_TRACE_AFTER=N prints a backtrace for every
 * allocation past the Nth, which says where a count comes from.
 *
 *   make mcount
 *   LD_PRELOAD=build/mcount.so ./vtt MAP --bench KEYS --bench-loops 20
 *   MCOUNT_TRACE_AFTER=1000 LD_PRELOAD=build/mcount.so ./vtt ...   then addr2line -e vtt
 *
 * Linux and glibc only (__libc_malloc, malloc_usable_size): a developer's
 * tool, not part of any build. It reports at exit, on stderr, and only from a
 * process named vtt (the shell and timeout that start it inherit the preload). */
#define _GNU_SOURCE
#include <execinfo.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void  __libc_free(void *);

static unsigned long g_calls, g_frees;
static unsigned long long g_bytes, g_live, g_live_max;
static unsigned long g_trace_after;
static int g_in;                              /* inside the shim: backtrace() may allocate */

/* What backtrace() itself allocates (its first call loads a library) is the
 * shim's, not the program's: not counted. */
static void note(void *p)
{
    if (!p || g_in) return;
    g_calls++;
    size_t n = malloc_usable_size(p);
    g_bytes += n;
    g_live  += n;
    if (g_live > g_live_max) g_live_max = g_live;
    if (g_trace_after && g_calls > g_trace_after && !g_in) {
        g_in = 1;
        void *bt[16];
        int   k = backtrace(bt, 16);
        dprintf(2, "mcount: allocation %lu, %zu bytes:\n", g_calls, n);
        backtrace_symbols_fd(bt, k, 2);
        g_in = 0;
    }
}

static void unnote(void *p)
{
    if (!p || g_in) return;
    g_frees++;
    size_t n = malloc_usable_size(p);
    g_live = g_live > n ? g_live - n : 0;
}

void *malloc(size_t n) { void *p = __libc_malloc(n); note(p); return p; }
void *calloc(size_t n, size_t sz) { void *p = __libc_calloc(n, sz); note(p); return p; }
void  free(void *p) { unnote(p); __libc_free(p); }
void *realloc(void *p, size_t n)
{
    unnote(p);
    void *q = __libc_realloc(p, n);
    note(q);
    return q;
}

__attribute__((constructor)) static void mcount_start(void)
{
    const char *t = getenv("MCOUNT_TRACE_AFTER");
    g_trace_after = t ? strtoul(t, NULL, 10) : 0;
}

__attribute__((destructor)) static void mcount_report(void)
{
    char comm[32] = "";
    FILE *f = fopen("/proc/self/comm", "r");
    if (f) { if (!fgets(comm, sizeof comm, f)) comm[0] = '\0'; fclose(f); }
    if (strncmp(comm, "vtt", 3) != 0) return;
    dprintf(2, "mcount: %lu allocations, %lu frees, %llu bytes allocated, %llu bytes live at most\n",
            g_calls, g_frees, g_bytes, g_live_max);
}
