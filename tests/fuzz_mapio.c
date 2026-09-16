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

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char in[64], err[256];
    if (!spill(data, size, in, sizeof in)) return 0;

    Map *m = mapio_load(in, err, sizeof err);
    unlink(in);
    if (!m) return 0;

    char out[64];
    snprintf(out, sizeof out, "%s.out", in);
    if (mapio_save(m, out, err, sizeof err) == 0) {
        Map *again = mapio_load(out, err, sizeof err);
        if (again) map_free(again);
        unlink(out);
    }
    map_free(m);
    return 0;
}
