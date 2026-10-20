#include "apikey.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "proc.h"
#include "util.h"

/* The keyring's own wait: unlocking it may put a window in front of the GM. */
#define KEYRING_TIMEOUT_MS 120000

static const char *const LOOKUP[] = { NULL, "lookup", "service", "vtt", "account", "anthropic-api-key", NULL };
static const char *const CLEAR[]  = { NULL, "clear", "service", "vtt", "account", "anthropic-api-key", NULL };
static const char *const STORE[]  = { NULL, "store", "--label=vtt: Anthropic API key", "service", "vtt",
                                "account", "anthropic-api-key", NULL };

/* VTT_SECRET_TOOL names another program in secret-tool's place (the tests). */
static const char *secret_tool(void)
{
    const char *t = getenv("VTT_SECRET_TOOL");
    return t && *t ? t : "secret-tool";
}

static int keyring_run(const char *const tmpl[], const char *in, size_t inlen, ByteBuf *out)
{
    char *argv[8];
    argv[0] = (char *)secret_tool();
    int i = 1;
    for (; tmpl[i] && i < 7; i++) argv[i] = (char *)tmpl[i];      /* exec's type; never written */
    argv[i] = NULL;
    if (!proc_found(argv[0])) return -1;
    ProcIn pi = { in, inlen, NULL, 0, KEYRING_TIMEOUT_MS, 1 };
    int st;
    char err[160];
    if (proc_run(argv, &pi, out, &st, err, sizeof err) < 0) return -1;
    return st;
}

int apikey_ok(const char *key)
{
    size_t n = 0;
    for (const char *p = key; *p; p++, n++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '-' || *p == '_'))
            return 0;
    return n >= 20 && n < APIKEY_MAX;
}

void apikey_wipe(char *key, size_t sz)
{
    volatile char *p = key;
    for (size_t i = 0; i < sz; i++) p[i] = 0;
}

void apikey_file_path(char *buf, size_t sz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) { snprintf(buf, sz, "%s/vtt/anthropic-key", xdg); return; }
    const char *home = getenv("HOME");
    snprintf(buf, sz, "%s/.config/vtt/anthropic-key", home && home[0] ? home : ".");
}

/* A key ends at its first white space: a file made with echo has a newline. */
static void trim(char *s)
{
    s[strcspn(s, " \t\r\n")] = '\0';
}

static ApiKeySource from_file(char *key, size_t sz, char *err, size_t errsz)
{
    char path[4096];
    apikey_file_path(path, sizeof path);
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno != ENOENT) snprintf(err, errsz, "cannot read %s: %s", path, strerror(errno));
        return APIKEY_NONE;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid()) {
        close(fd);
        snprintf(err, errsz, "%s is not a file of yours", path);
        return APIKEY_NONE;
    }
    if (st.st_mode & 077) {
        close(fd);
        snprintf(err, errsz, "the key file can be read by others - chmod 600 %s", path);
        return APIKEY_NONE;
    }
    ssize_t n = read(fd, key, sz - 1);
    close(fd);
    key[n > 0 ? n : 0] = '\0';
    trim(key);
    if (!apikey_ok(key)) {
        apikey_wipe(key, sz);
        snprintf(err, errsz, "%s does not hold a key (letters, digits, - and _)", path);
        return APIKEY_NONE;
    }
    return APIKEY_FILE;
}

ApiKeySource apikey_find(char *key, size_t sz, char *err, size_t errsz)
{
    err[0] = '\0';
    key[0] = '\0';
    const char *env = getenv("ANTHROPIC_API_KEY");
    if (env && *env) {
        if (!apikey_ok(env)) { snprintf(err, errsz, "ANTHROPIC_API_KEY does not hold a key (letters, digits, - and _)"); return APIKEY_NONE; }
        str_lcpy(key, env, sz);
        return APIKEY_ENV;
    }
    return apikey_find_stored(key, sz, err, errsz);
}

ApiKeySource apikey_find_stored(char *key, size_t sz, char *err, size_t errsz)
{
    err[0] = '\0';
    key[0] = '\0';
    ByteBuf out;
    bb_init(&out, 256);
    int st = keyring_run(LOOKUP, NULL, 0, &out);
    if (st == 0 && out.len > 0 && out.len < sz) {
        memcpy(key, out.data, out.len);
        key[out.len] = '\0';
        trim(key);
    }
    apikey_wipe(out.data, out.cap);
    bb_free(&out);
    if (key[0] && apikey_ok(key)) return APIKEY_KEYRING;
    apikey_wipe(key, sz);
    ApiKeySource s = from_file(key, sz, err, errsz);
    if (s == APIKEY_NONE && !err[0])
        snprintf(err, errsz, "no API key: set ANTHROPIC_API_KEY, or :agent key to type one in");
    return s;
}

int apikey_store(const char *key, ApiKeySource *where, char *err, size_t errsz)
{
    if (!apikey_ok(key)) { snprintf(err, errsz, "that is not a key: letters, digits, - and _, 20 or more"); return -1; }
    char path[4096];
    apikey_file_path(path, sizeof path);
    /* No newline after it: secret-tool keeps all it is piped. */
    ByteBuf out;
    bb_init(&out, 64);
    int st = keyring_run(STORE, key, strlen(key), &out);
    bb_free(&out);
    if (st == 0) {
        unlink(path);                       /* an older key, now shadowed */
        *where = APIKEY_KEYRING;
        return 0;
    }
    char dir[4096];
    str_lcpy(dir, path, sizeof dir);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; dir_make(dir); }
    size_t n = strlen(key);
    char   line[APIKEY_MAX + 2];
    memcpy(line, key, n);
    line[n] = '\n';
    int rc = file_write_whole(path, line, n + 1, 0600, 1, err, errsz);
    apikey_wipe(line, sizeof line);
    if (rc < 0) return -1;
    *where = APIKEY_FILE;
    return 0;
}

int apikey_remove(char *err, size_t errsz)
{
    ByteBuf out;
    bb_init(&out, 64);
    keyring_run(CLEAR, NULL, 0, &out);
    bb_free(&out);
    char path[4096];
    apikey_file_path(path, sizeof path);
    if (unlink(path) != 0 && errno != ENOENT) {
        snprintf(err, errsz, "cannot remove %s: %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

const char *apikey_source_name(ApiKeySource s)
{
    switch (s) {
    case APIKEY_ENV:     return "environment";
    case APIKEY_KEYRING: return "keyring";
    case APIKEY_FILE:    return "file";
    default:             return "none";
    }
}
