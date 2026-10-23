#include "apikey.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "proc.h"
#include "util.h"

/* The keyring's own wait: unlocking it may put a window in front of the GM. */
#define KEYRING_TIMEOUT_MS 120000

static const char *const LOOKUP[] = { NULL, "lookup", "service", "vtt", "account", "anthropic-api-key", NULL };
static const char *const CLEAR[]  = { NULL, "clear", "service", "vtt", "account", "anthropic-api-key", NULL };
static const char *const STORE[]  = { NULL, "store", "--label=vtt: Anthropic API key", "service", "vtt",
                                "account", "anthropic-api-key", NULL };

/* The one key this process holds, kept out of swap where it can be. */
static char         held[APIKEY_MAX];
static ApiKeySource held_src;
/* A key typed in, until the GM says where it goes (the keyring, or held). */
static char         pending[APIKEY_MAX];
static int          typing, locked;

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
    ProcIn pi = { in, inlen, NULL, 0, KEYRING_TIMEOUT_MS, 1, NULL };
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

/* Not dumpable while a key is in this process's hands: no core file holds
 * it, and no other process of the user's reads its memory or environment
 * (prctl(2)). Linux only; elsewhere the core size limit alone. */
static void guard(void)
{
    int hold = held_src != APIKEY_NONE || typing || pending[0];
#ifdef __linux__
    prctl(PR_SET_DUMPABLE, hold ? 0 : 1, 0, 0, 0);
#else
    if (hold) { struct rlimit no = { 0, 0 }; setrlimit(RLIMIT_CORE, &no); }
#endif
    if (hold && !locked) locked = (mlock(held, sizeof held) | mlock(pending, sizeof pending)) == 0;   /* best effort: no swap */
}

void apikey_harden(void)
{
#ifdef __linux__
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif
    struct rlimit no = { 0, 0 };
    setrlimit(RLIMIT_CORE, &no);
}

void apikey_typing(int on)
{
    typing = on;
    guard();
}

int apikey_hold(const char *key, ApiKeySource src)
{
    if (!apikey_ok(key)) return -1;
    apikey_wipe(held, sizeof held);
    str_lcpy(held, key, sizeof held);
    held_src = src;
    guard();
    return 0;
}

void apikey_drop(void)
{
    apikey_wipe(held, sizeof held);
    held_src = APIKEY_NONE;
    guard();
}

int apikey_pending_set(const char *key)
{
    if (!apikey_ok(key)) return -1;
    str_lcpy(pending, key, sizeof pending);
    guard();
    return 0;
}

void apikey_pending_last4(char *buf, size_t sz)
{
    size_t n = strlen(pending);
    snprintf(buf, sz, "%s", n >= 4 ? pending + n - 4 : "");
}

int apikey_pending_keep(char *err, size_t errsz)
{
    int rc = apikey_store(pending, err, errsz);
    apikey_pending_drop();
    return rc;
}

void apikey_pending_session(void)
{
    if (pending[0]) apikey_hold(pending, APIKEY_SESSION);
    apikey_pending_drop();
}

void apikey_pending_drop(void)
{
    apikey_wipe(pending, sizeof pending);
    guard();
}

ApiKeySource apikey_held(char *key, size_t sz)
{
    key[0] = '\0';
    if (held_src == APIKEY_NONE) return APIKEY_NONE;
    str_lcpy(key, held, sz);
    return held_src;
}

void apikey_take_env(char *err, size_t errsz)
{
    err[0] = '\0';
    const char *env = getenv("ANTHROPIC_API_KEY");
    if (env && *env) {
        if (apikey_hold(env, APIKEY_ENV) < 0)
            snprintf(err, errsz, "ANTHROPIC_API_KEY does not hold a key (letters, digits, - and _) - not used");
        /* The environment's own copy wiped before it goes, as far as it is ours to. */
        apikey_wipe((char *)env, strlen(env));
    }
    unsetenv("ANTHROPIC_API_KEY");
}

int apikey_keyring_here(void)
{
    return proc_found(secret_tool());
}

/* A key ends at its first white space: what a keyring returns may end in a newline. */
static void trim(char *s)
{
    s[strcspn(s, " \t\r\n")] = '\0';
}

ApiKeySource apikey_find(char *key, size_t sz, char *err, size_t errsz)
{
    err[0] = '\0';
    ApiKeySource h = apikey_held(key, sz);
    if (h != APIKEY_NONE) return h;
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
    snprintf(err, errsz, "no API key: set ANTHROPIC_API_KEY, or :agent key to type one in");
    return APIKEY_NONE;
}

int apikey_store(const char *key, char *err, size_t errsz)
{
    if (!apikey_ok(key)) { snprintf(err, errsz, "that is not a key: letters, digits, - and _, 20 or more"); return -1; }
    if (!apikey_keyring_here()) { snprintf(err, errsz, "no keyring here (secret-tool) - :agent key, then s, holds it for this session"); return -1; }
    /* No newline after it: secret-tool keeps all it is piped. */
    ByteBuf out;
    bb_init(&out, 64);
    int st = keyring_run(STORE, key, strlen(key), &out);
    bb_free(&out);
    if (st != 0) { snprintf(err, errsz, "the keyring did not take the key (secret-tool store failed) - :agent key, then s, holds it for this session"); return -1; }
    return 0;
}

int apikey_remove(char *err, size_t errsz)
{
    (void)err; (void)errsz;
    ByteBuf out;
    bb_init(&out, 64);
    keyring_run(CLEAR, NULL, 0, &out);
    bb_free(&out);
    return 0;
}

const char *apikey_source_name(ApiKeySource s)
{
    switch (s) {
    case APIKEY_ENV:     return "environment";
    case APIKEY_KEYRING: return "keyring";
    case APIKEY_SESSION: return "session";
    default:             return "none";
    }
}

int apikey_old_file(char *path, size_t sz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0]) snprintf(path, sz, "%s/vtt/anthropic-key", xdg);
    else snprintf(path, sz, "%s/.config/vtt/anthropic-key", home && home[0] ? home : ".");
    struct stat st;
    return lstat(path, &st) == 0;
}
