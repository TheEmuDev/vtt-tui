/* The disk's real table (disk.h). */

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "disk.h"

static int real_open(const char *path, int flags, mode_t mode) { return open(path, flags, mode); }
static ssize_t real_write(int fd, const void *buf, size_t n) { return write(fd, buf, n); }
static int real_fsync(int fd) { return fsync(fd); }
static int real_fstat(int fd, struct stat *st) { return fstat(fd, st); }
static int real_close(int fd) { return close(fd); }
static int real_rename(const char *from, const char *to) { return rename(from, to); }
static int real_unlink(const char *path) { return unlink(path); }
static int real_stat(const char *path, struct stat *st) { return stat(path, st); }

const Disk DISK_REAL = {
    real_open, real_write, real_fsync, real_fstat, real_close, real_rename, real_unlink, real_stat,
};

static const Disk *g_disk = &DISK_REAL;

const Disk *disk(void) { return g_disk; }

void disk_use(const Disk *d) { g_disk = d ? d : &DISK_REAL; }
