#ifndef VTT_TERM_H
#define VTT_TERM_H

#include <termios.h>
#include "util.h"

typedef struct {
    int  in_fd;
    int  out_fd;
    int  w, h;              /* current size in cells */
    struct termios saved;
    int  raw;               /* raw mode currently installed */
    int  sig_pipe[2];       /* SIGWINCH self-pipe: [0] read, [1] write */
    int  altscreen;

    /* Set when the terminal genuinely fails, as opposed to merely falling
     * behind. The event loop exits rather than writing into the void. */
    int  dead;
} Term;

/* Puts the terminal in raw mode, switches to the alternate screen, hides the
 * cursor, and arms the SIGWINCH self-pipe. Returns 0 on success. */
int  term_init(Term *t);

/* Restores everything. Safe to call more than once. */
void term_shutdown(Term *t);

/* Re-reads the window size into t->w/t->h. Returns 1 if it changed. */
int  term_update_size(Term *t);

/* Drains the self-pipe. Returns 1 if a resize was signalled. */
int  term_drain_signals(Term *t);

/* Writes n bytes, returning how many were actually delivered.
 *
 * A short return means the frame did not reach the terminal, and the caller
 * must not then believe the screen shows what it just drew. Backpressure is
 * waited out rather than treated as failure; only a real error stops it and
 * sets t->dead. */
size_t term_write(Term *t, const char *buf, size_t n);

/* True when a read() from the terminal would not block. The descriptors are
 * left blocking -- in a terminal stdin and stdout are the same open file
 * description, so making stdin non-blocking silently makes writes to stdout
 * fail with EAGAIN under load -- so the input drain polls before each read
 * instead. */
int term_input_ready(const Term *t);

/* fd to poll for resize notifications. */
static inline int term_signal_fd(const Term *t) { return t->sig_pipe[0]; }

#endif /* VTT_TERM_H */
