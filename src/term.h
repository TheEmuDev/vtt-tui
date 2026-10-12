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

    /* term_async: out_fd is this process's own non-blocking open of the
     * terminal, and what the terminal would not take at once waits in
     * `pend`. `skipped`: a frame was held back while bytes were pending, and
     * is owed once they have gone (render.c, loop.c). */
    int     async;
    int     skipped;
    ByteBuf pend;
    size_t  pend_off;       /* how much of pend has gone: sent from here, never moved down */
} Term;

/* Puts the terminal in raw mode, switches to the alternate screen, hides the
 * cursor, and arms the SIGWINCH self-pipe. Returns 0 on success. */
int  term_init(Term *t);

/* Restores everything. Safe to call more than once. */
void term_shutdown(Term *t);

/* Hands the terminal to another program -- an editor -- and takes it back:
 * the main screen and cooked mode, then raw mode and the alternate screen,
 * cleared (the caller repaints everything). The signal pipe stays armed. */
void term_suspend(Term *t);
int  term_resume(Term *t);

/* Re-reads the window size into t->w/t->h. Returns 1 if it changed. */
int  term_update_size(Term *t);

/* Drains the self-pipe. Returns 1 if a resize was signaled. */
int  term_drain_signals(Term *t);

/* Writes n bytes, returning how many the terminal has or will have: taken
 * now, or (after term_async) queued behind what it has not taken yet.
 *
 * A short return means the frame did not reach the terminal, and the caller
 * must not then believe the screen shows what it just drew; only a real
 * error does that, and sets t->dead. A terminal that is merely behind is
 * waited for when writes block, and queued for after term_async. */
size_t term_write(Term *t, const char *buf, size_t n);

/* True when a read() from the terminal would not block. The descriptors are
 * left blocking -- in a terminal stdin and stdout are the same open file
 * description, so making stdin non-blocking silently makes writes to stdout
 * fail with EAGAIN under load -- so the input drain polls before each read
 * instead. */
int term_input_ready(const Term *t);

/* Stops a slow terminal from stopping the program: output goes through a
 * second, non-blocking open of the same terminal (O_NONBLOCK on stdout itself
 * would set it on stdin too: one open file description), and term_write
 * queues what will not go at once instead of waiting. The caller then owes
 * the terminal a term_flush whenever out_fd polls writable. Returns -1, and
 * leaves writes blocking as before, when the terminal cannot be opened again. */
int term_async(Term *t);

/* Bytes queued and not yet taken by the terminal; 0 when writes block. */
static inline size_t term_pending(const Term *t) { return t->pend.len - t->pend_off; }

/* Sends what is queued, as far as the terminal will take it now. Returns the
 * bytes still waiting. */
size_t term_flush(Term *t);

/* fd to poll for resize notifications. */
static inline int term_signal_fd(const Term *t) { return t->sig_pipe[0]; }

#endif /* VTT_TERM_H */
