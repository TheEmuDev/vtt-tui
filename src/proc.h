#ifndef VTT_PROC_H
#define VTT_PROC_H

#include <stddef.h>

#include "util.h"

/* A program run to its end with pipes on its standard input, its standard
 * output and, when asked, descriptor 3: how vtt reaches curl (docs/APIAGENT.md,
 * the key down fd 3, never on a command line) and secret-tool. Its standard
 * error is ours unless `quiet`. */
typedef struct {
    const char *in;         /* written to its stdin, then closed (NULL: /dev/null) */
    size_t      inlen;
    const char *fd3;        /* written to its fd 3, then closed (NULL: no fd 3) */
    size_t      fd3len;
    int         timeout_ms; /* then it is killed; 0: no limit */
    int         quiet;      /* its stderr to /dev/null: run from the app, whose terminal it would write over */
    const char *const *env; /* NULL: our environment; else only the variables named here go with it */
} ProcIn;

/* Runs argv[0] (looked up in PATH) and collects its stdout into *out. The
 * exit status in *status (WEXITSTATUS; 127 when it could not be run, -1 when
 * it was killed for the time). 0, or -1 with why when no process could be
 * made or the pipes failed. */
int proc_run(char *const argv[], const ProcIn *pi, ByteBuf *out, int *status, char *err, size_t errsz);

/* Whether a program of that name is found in PATH (or is a path to one). */
int proc_found(const char *name);

#endif /* VTT_PROC_H */
