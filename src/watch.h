#ifndef VTT_WATCH_H
#define VTT_WATCH_H

#include <stddef.h>

/* `vtt --watch host:port[?k=CODE]`: a read-only mirror of a serving vtt in
 * this terminal. The same decoder the tests use paints the GM's frame into
 * this process's renderer, at the GM's size, centered when the terminal is
 * larger and clipped at the top left when it is smaller. q, esc or ctrl-c
 * closes it; so does the GM stopping the server.
 *
 * Returns the process exit status. */
int watch_main(const char *target, int ascii);

/* The address split into host, port and join code ("" for none): what
 * :serve shows, http:// and /?k= or ?k= or /CODE, or a bare host:port.
 * -1 when there is no host or no port. */
int watch_parse_target(const char *target, char *host, size_t hs, char *port, size_t ps,
                       char *code, size_t cs);

#endif /* VTT_WATCH_H */
