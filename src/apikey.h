#ifndef VTT_APIKEY_H
#define VTT_APIKEY_H

#include <stddef.h>

/* The Anthropic API key (docs/APIAGENT.md, *The key*): ANTHROPIC_API_KEY when
 * it is set, else the system keyring through secret-tool, else a file only
 * its owner can read. Only the API agent's process uses the key; the app
 * stores, removes and looks for one, and never puts it in the environment. */
typedef enum { APIKEY_NONE, APIKEY_ENV, APIKEY_KEYRING, APIKEY_FILE } ApiKeySource;

#define APIKEY_MAX 512

/* A key's characters, letters, digits, - and _, 20 to 511 of them: anything
 * else is refused before it is stored or sent (it also keeps the key from
 * writing into curl's config line). */
int apikey_ok(const char *key);

/* The key in use, into key (APIKEY_MAX), and where it came from; APIKEY_NONE
 * with why in err when there is none, or when the one found cannot be used
 * (a file others can read: the chmod that fixes it). */
ApiKeySource apikey_find(char *key, size_t sz, char *err, size_t errsz);

/* The stored key alone -- the keyring's, else the file's -- whatever the
 * environment holds: what :agent key remove would take away. */
ApiKeySource apikey_find_stored(char *key, size_t sz, char *err, size_t errsz);

/* Stores a key typed in: the keyring, or the file where no keyring answers
 * (then *where says so). An older key file goes when the keyring takes it. */
int apikey_store(const char *key, ApiKeySource *where, char *err, size_t errsz);

/* Takes the stored key away, from the keyring and the file. */
int apikey_remove(char *err, size_t errsz);

/* Overwrites a copy of the key, so it does not linger in freed memory. */
void apikey_wipe(char *key, size_t sz);

const char *apikey_source_name(ApiKeySource s);

/* $XDG_CONFIG_HOME/vtt/anthropic-key (~/.config/vtt/...). */
void apikey_file_path(char *buf, size_t sz);

#endif /* VTT_APIKEY_H */
