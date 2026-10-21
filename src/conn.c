/* The phones' sockets' real table (conn.h). */

#include <unistd.h>

#include "conn.h"

static int real_accept(int fd, struct sockaddr *addr, socklen_t *alen) { return accept(fd, addr, alen); }
static ssize_t real_read(int fd, void *buf, size_t n) { return read(fd, buf, n); }
static ssize_t real_write(int fd, const void *buf, size_t n) { return write(fd, buf, n); }
static int real_close(int fd) { return close(fd); }
static int real_setsockopt(int fd, int level, int name, const void *val, socklen_t len)
{
    return setsockopt(fd, level, name, val, len);
}

const Conn CONN_REAL = { real_accept, real_read, real_write, real_close, real_setsockopt };

static const Conn *g_conn = &CONN_REAL;

const Conn *conn(void) { return g_conn; }

void conn_use(const Conn *c) { g_conn = c ? c : &CONN_REAL; }
