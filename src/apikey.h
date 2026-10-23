#ifndef VTT_APIKEY_H
#define VTT_APIKEY_H

#include <stddef.h>

/* The Anthropic API key (docs/APIAGENT.md, *The key, made safer*). Where it
 * is: the system keyring through secret-tool, or held in this process's
 * memory -- a key typed in for this session only, or ANTHROPIC_API_KEY taken
 * out of the environment at start (apikey_take_env) so no child inherits it.
 * A process holding a key is not dumpable (no core file, no reading of its
 * memory by other processes); the Claude agent's never is (apikey_harden).
 * There is no key file any more: one left by an older vtt is not read. */
typedef enum { APIKEY_NONE, APIKEY_ENV, APIKEY_KEYRING, APIKEY_SESSION } ApiKeySource;

#define APIKEY_MAX 512

/* A key's characters, letters, digits, - and _, 20 to 511 of them: anything
 * else is refused before it is stored or sent (it also keeps the key from
 * writing into curl's config line). */
int apikey_ok(const char *key);

/* At start, before anything is forked: ANTHROPIC_API_KEY into this process's
 * holder (source APIKEY_ENV) and out of its environment. A value that is not
 * a key is dropped from the environment too, and said in err (""). */
void apikey_take_env(char *err, size_t errsz);

/* The key in use, into key (APIKEY_MAX), and where it came from: the one held
 * (the environment's, or this session's), else the keyring's. APIKEY_NONE
 * with why in err when there is none. */
ApiKeySource apikey_find(char *key, size_t sz, char *err, size_t errsz);

/* The keyring's key alone, whatever is held: what :agent key remove would
 * take away. */
ApiKeySource apikey_find_stored(char *key, size_t sz, char *err, size_t errsz);

/* A key held in this process (APIKEY_ENV or APIKEY_SESSION) or APIKEY_NONE,
 * its copy into key. */
ApiKeySource apikey_held(char *key, size_t sz);

/* Holds a key for this session (src APIKEY_SESSION, or APIKEY_ENV for the
 * Claude agent given vtt's): it replaces any held. 0, or -1 when it is not a
 * key. The process is made not dumpable while it holds one. */
int apikey_hold(const char *key, ApiKeySource src);

/* Lets the held key go (wiped); the process dumpable again unless typing. */
void apikey_drop(void);

/* A key typed in, waiting for the GM's word on where it goes: to the keyring
 * (apikey_pending_keep), held for this session (apikey_pending_session), or
 * dropped. Not in use meanwhile; the process not dumpable. */
int  apikey_pending_set(const char *key);
void apikey_pending_last4(char *buf, size_t sz);
int  apikey_pending_keep(char *err, size_t errsz);
void apikey_pending_session(void);
void apikey_pending_drop(void);

/* Whether a keyring answers here (secret-tool is found). */
int apikey_keyring_here(void);

/* Stores a key typed in, in the keyring. 0, or -1 with why (no keyring here:
 * this session only is the way then). */
int apikey_store(const char *key, char *err, size_t errsz);

/* Takes the keyring's key away. A held key is not touched. */
int apikey_remove(char *err, size_t errsz);

/* While a key is being typed (1) and after (0): not dumpable meanwhile. */
void apikey_typing(int on);

/* The Claude agent's process at its start: not dumpable for its life, and no
 * core file for it or for what it runs (curl inherits the limit). */
void apikey_harden(void);

/* Overwrites a copy of the key, so it does not linger in freed memory. */
void apikey_wipe(char *key, size_t sz);

const char *apikey_source_name(ApiKeySource s);

/* An older vtt's key file, $XDG_CONFIG_HOME/vtt/anthropic-key: 1 with its
 * path when it is there (not read: the GM is told to remove it). */
int apikey_old_file(char *path, size_t sz);

#endif /* VTT_APIKEY_H */
