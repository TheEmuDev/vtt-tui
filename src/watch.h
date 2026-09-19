#ifndef VTT_WATCH_H
#define VTT_WATCH_H

/* `vtt --watch host:port[?k=CODE]`: a read-only mirror of a serving vtt in
 * this terminal. The same decoder the tests use paints the GM's frame into
 * this process's renderer, at the GM's size, centred when the terminal is
 * larger and clipped at the top left when it is smaller. q, esc or ctrl-c
 * closes it; so does the GM stopping the server.
 *
 * Returns the process exit status. */
int watch_main(const char *target, int ascii);

#endif /* VTT_WATCH_H */
