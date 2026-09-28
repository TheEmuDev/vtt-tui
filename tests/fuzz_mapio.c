/* libFuzzer target for the map loader, the one place untrusted bytes enter
 * the program. Every input is written to a file and loaded; a map that loads
 * is saved again and reloaded, so the writer is covered by the same run.
 *
 *   make fuzz             runs it for a minute, seeded from tests/fixtures
 *   make fuzz FUZZ_SECONDS=600
 *
 * Findings are the crash-* files libFuzzer leaves in the working directory. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "map.h"
#include "mapio.h"
#include "maptools.h"

static const char *spill(const uint8_t *data, size_t size, char *path, size_t pathsz)
{
    snprintf(path, pathsz, "/tmp/vtt-fuzz-%ld-XXXXXX", (long)getpid());
    int fd = mkstemp(path);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(path); return NULL; }
    fwrite(data, 1, size, f);
    fclose(f);
    return path;
}

static char *slurp_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    size_t got;
    while (buf && (got = fread(buf + len, 1, cap - len, f)) > 0) {
        len += got;
        if (len == cap) { char *nb = realloc(buf, cap *= 2); if (!nb) { free(buf); buf = NULL; } else buf = nb; }
    }
    fclose(f);
    *n = len;
    return buf;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char in[64], err[256];
    if (!spill(data, size, in, sizeof in)) return 0;

    /* The linter reads the same bytes the loader does, and must survive
     * whatever it is given; so must the dump and the account of anything
     * that loads. */
    FILE *sink = fopen("/dev/null", "w");
    if (sink) maptools_check(sink, in, 1);

    Map *m = mapio_load(in, err, sizeof err);
    unlink(in);
    if (!m) { if (sink) fclose(sink); return 0; }
    if (sink) {
        maptools_dump(sink, m, 0, 0, m->w - 1, m->h - 1);
        maptools_describe(sink, m, 1);
        fclose(sink);
    }

    /* Whatever loads, saved, must read back as the same map: saved again it
     * is the same bytes. Unflushed: the disk is not what is under test. */
    char out[64], out2[72];
    snprintf(out, sizeof out, "%s.out", in);
    snprintf(out2, sizeof out2, "%s.out2", in);
    if (mapio_write_unflushed(m, out, err, sizeof err) == 0) {
        Map *again = mapio_load(out, err, sizeof err);
        if (!again) {
            fprintf(stderr, "a saved map does not load: %s\n", err);
            abort();
        }
        if (mapio_write_unflushed(again, out2, err, sizeof err) == 0) {
            size_t n1 = 0, n2 = 0;
            char *a = slurp_file(out, &n1), *b = slurp_file(out2, &n2);
            if (a && b && (n1 != n2 || memcmp(a, b, n1) != 0)) {
                fprintf(stderr, "saved, loaded and saved again, the map changed\n");
                abort();
            }
            free(a);
            free(b);
            unlink(out2);
        }
        map_free(again);
        unlink(out);
    }
    map_free(m);
    return 0;
}
