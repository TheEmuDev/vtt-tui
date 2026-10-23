/* Does a key linger? Links vtt's own apikey.c, proc.c and util.c (no
 * sanitizer: its shadow memory cannot be scanned), runs each way the app
 * reads a key through a stand-in secret-tool that prints a key made at run
 * time, then reads every writable mapping of this process (/proc/self/mem)
 * for that key. The key is kept here only XORed, so the scan cannot find
 * the scanner's own copy. Prints a line a step: the copies found, and
 * whether the process was dumpable after the step. Exit 1 if any copy.
 *
 *   tools/keyscan.sh          (builds it, writes the stand-in, runs it) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "apikey.h"

#define KLEN 40
static unsigned char masked[KLEN];          /* the key ^ 0x5a: never the key itself */

static int count_copies(void)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    int mem = open("/proc/self/mem", O_RDONLY);
    if (!maps || mem < 0) { perror("keyscan: /proc/self"); exit(2); }
    char line[512];
    int found = 0;
    static unsigned char page[1 << 16];
    while (fgets(line, sizeof line, maps)) {
        unsigned long lo, hi;
        char perms[8];
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) != 3 || perms[1] != 'w') continue;
        if (strstr(line, "[vvar]") || strstr(line, "[vsyscall]")) continue;
        for (unsigned long at = lo; at < hi; ) {
            size_t want = hi - at < sizeof page ? hi - at : sizeof page;
            ssize_t got = pread(mem, page, want, (off_t)at);
            if (got <= 0) break;
            /* The window overlaps the next by KLEN, so a copy across a seam is seen once. */
            for (ssize_t i = 0; i + KLEN <= got; i++) {
                int same = 1;
                for (int j = 0; j < KLEN && same; j++) same = (page[i + j] ^ 0x5a) == masked[j];
                if (same) found++;
            }
            at += (unsigned long)got > KLEN ? (unsigned long)got - KLEN : (unsigned long)got;
        }
    }
    memset(page, 0, sizeof page);
    fclose(maps);
    close(mem);
    return found;
}

static int dumpable(void)
{
#ifdef __linux__
    return prctl(PR_GET_DUMPABLE, 0, 0, 0, 0);
#else
    return -1;
#endif
}

static int report(const char *step)
{
    int n = count_copies();
    printf("%-34s %d cop%s of the key, dumpable %d\n", step, n, n == 1 ? "y" : "ies", dumpable());
    return n;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: keyscan SECRETFILE (tools/keyscan.sh)\n"); return 2; }
    /* The key: made now, written to the stand-in's file with write(2), its
     * one plain copy wiped; kept only masked. */
    {
        char k[KLEN + 1];
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        memcpy(k, "sk-scan-", 8);
        for (int i = 8; i < KLEN; i++) k[i] = "abcdefghijklmnopqrstuvwxyzABCDEF"[rand() % 32];
        for (int i = 0; i < KLEN; i++) masked[i] = (unsigned char)(k[i] ^ 0x5a);
        int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0 || write(fd, k, KLEN) != KLEN) { perror("keyscan: the secret file"); return 2; }
        close(fd);
        apikey_wipe(k, sizeof k);
    }
    int bad = report("before anything");
    char last4[8], err[300];
    ApiKeySource s = apikey_where(last4, sizeof last4, err, sizeof err);
    printf("  (apikey_where: %s, ...%s)\n", apikey_source_name(s), last4);
    bad += report("after apikey_where");
    s = apikey_where_stored(last4, sizeof last4, err, sizeof err);
    bad += report("after apikey_where_stored");
    {
        char k[APIKEY_MAX];
        apikey_find(k, sizeof k, err, sizeof err);
        apikey_wipe(k, sizeof k);
    }
    bad += report("after apikey_find and its wipe");
    unlink(argv[1]);
    return bad ? 1 : 0;
}
