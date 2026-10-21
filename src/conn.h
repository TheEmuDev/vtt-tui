#ifndef VTT_CONN_H
#define VTT_CONN_H

/* The phones' sockets, as a seam (docs/V2.md, D3 and step 7's plan, item 3):
 * the calls net.c makes on a client's socket, through one table, so a test
 * can play a phone that stops reading after N bytes and see what vtt queues
 * for it, exactly and at once, instead of filling a real socket's megabytes.
 * One real table, each entry the libc call of its name. The listening
 * socket's setup (socket, bind, listen) stays direct: a test needs nothing
 * of it but a port. */

#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>

typedef struct Conn {
    int     (*accept)(int fd, struct sockaddr *addr, socklen_t *alen);
    ssize_t (*read)(int fd, void *buf, size_t n);
    ssize_t (*write)(int fd, const void *buf, size_t n);
    int     (*close)(int fd);
    int     (*setsockopt)(int fd, int level, int name, const void *val, socklen_t len);
} Conn;

extern const Conn CONN_REAL;

/* The table in use: the real one unless a test said otherwise. */
const Conn *conn(void);
/* A test's table for the whole process from now; NULL puts the real one
 * back. run-tests checks it is the real one after every suite. */
void conn_use(const Conn *c);

#endif /* VTT_CONN_H */
