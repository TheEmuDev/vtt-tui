/* vtt --check's cost on one file, its zones traced: `make build/tool-checkbench`,
 * then build/tool-checkbench MAP TRACE.json N runs maptools_check N times under
 * the profiler and writes the Chrome trace; tools/zones.py TRACE.json prints
 * each zone's median (mapio.load, mapio.sections, check.shift, check.joint,
 * check.fold, check.pairs). For a timing of the line search or a check on the
 * largest maps (docs/PERFORMANCE.md, *The line search*): perf.sh replays keys,
 * and --check is no key. */
#include <stdio.h>
#include <stdlib.h>

#include "maptools.h"
#include "prof.h"

int main(int argc, char **argv)
{
    if (argc != 4 || atoi(argv[3]) < 1) {
        fprintf(stderr, "usage: %s MAP TRACE.json N\n", argv[0]);
        return 2;
    }
    FILE *null = fopen("/dev/null", "w");
    if (!null) return 1;
    prof_init();
    prof_trace_open(argv[2]);
    for (int i = 0; i < atoi(argv[3]); i++) maptools_check(null, argv[1], 0);
    prof_shutdown();
    fclose(null);
    return 0;
}
