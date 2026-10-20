#ifndef VTT_DISK_H
#define VTT_DISK_H

/* The disk, as a seam (docs/V2.md, D3 and *The `Disk` seam*): the calls a
 * write of the map's file makes, through one table, so a test can make any
 * of them fail with the errno it chooses and see what vtt does. One real
 * table, each entry the libc call of its name. Loading stays on stdio: a
 * read that fails is a refusal with its error, tested by missing files. */

#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct Disk {
    int     (*open)(const char *path, int flags, mode_t mode);
    ssize_t (*write)(int fd, const void *buf, size_t n);
    int     (*fsync)(int fd);
    int     (*fstat)(int fd, struct stat *st);
    int     (*close)(int fd);
    int     (*rename)(const char *from, const char *to);
    int     (*unlink)(const char *path);
    int     (*stat)(const char *path, struct stat *st);
} Disk;

extern const Disk DISK_REAL;

/* The table in use: the real one unless a test said otherwise. */
const Disk *disk(void);
/* A test's table for the whole process from now; NULL puts the real one
 * back. run-tests checks it is the real one after every suite. */
void disk_use(const Disk *d);

#endif /* VTT_DISK_H */
