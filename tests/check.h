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

#define CASE(name) do { g_case = (name); } while (0)

#endif /* VTT_TEST_CHECK_H */
