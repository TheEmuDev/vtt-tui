#ifndef VTT_TEST_CHECK_H
#define VTT_TEST_CHECK_H

/* The counters and their macros, alone: a suite that must not see the app
 * (test_protocol.c) takes these and not harness.h. */

#include <stdio.h>

extern int g_checks;
extern int g_fails;
extern const char *g_case;

#define CHECK(cond)                                                            \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_fails++;                                                         \
            fprintf(stderr, "  FAIL %s:%d [%s] %s\n",                          \
                    __FILE__, __LINE__, g_case, #cond);                        \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        long long va = (long long)(a), vb = (long long)(b);                    \
        g_checks++;                                                            \
        if (va != vb) {                                                        \
            g_fails++;                                                         \
            fprintf(stderr, "  FAIL %s:%d [%s] %s == %s (%lld vs %lld)\n",     \
                    __FILE__, __LINE__, g_case, #a, #b, va, vb);               \
        }                                                                      \
    } while (0)

/* A case begins: its name for the failures, and with VTT_CASE_TIMES set the
 * time the one before took, on stderr (docs/TESTSPEED.md: where a suite's
 * time goes). case_begin(NULL) ends the last one (each suite's end). */
void case_begin(const char *name);
#define CASE(name) case_begin(name)

#endif /* VTT_TEST_CHECK_H */
