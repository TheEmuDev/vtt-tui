#ifndef VTT_SETTINGS_H
#define VTT_SETTINGS_H

#include <stddef.h>

/* The GM's settings, kept from one session to the next (docs/APIAGENT.md,
 * *Settings*): $XDG_CONFIG_HOME/vtt/settings, a line a setting, `name value`.
 * The Options screen and the :agent commands change them; each change is
 * written whole. Today they are the agent's. */

typedef enum {
    AGENT_KIND_UNSET,       /* never chosen: the first :ask asks */
    AGENT_KIND_OFF,         /* chosen: nothing is started; an agent may come to the channel */
    AGENT_KIND_COMMAND,     /* `command` is started for each :ask */
    AGENT_KIND_CLAUDE,      /* the Claude API agent is */
} AgentKind;

#define SETTINGS_COMMAND_MAX 256   /* AGENT_CMD_MAX: a command typed in */
#define SETTINGS_KEPT_MAX    4096  /* lines it does not know, kept as they were */

typedef struct {
    int    agent;                          /* AgentKind */
    char   command[SETTINGS_COMMAND_MAX];
    char   model[64];                      /* opus, sonnet, haiku, fable, or a model id */
    char   effort[8];                      /* low, medium, high */
    double cap;                            /* dollars a job; 0: none */
    int    calls;                          /* API calls a job */
    int    fallbacks;
    int    paused;                         /* :agent command off: the agent kept, not started */
    int    channel;                        /* the channel on at start-up */
    int    accept_auto;                    /* agents' changes land at once, at start-up */
    char   kept[SETTINGS_KEPT_MAX];
} Settings;

void settings_default(Settings *s);

/* Each setting by its name in the file and what it takes, for the Options
 * screen's rows and the commands. */
typedef enum { SET_AGENT, SET_COMMAND, SET_MODEL, SET_EFFORT, SET_CAP, SET_CALLS, SET_FALLBACKS,
               SET_PAUSED, SET_CHANNEL, SET_ACCEPT, SET_COUNT } SettingId;
const char *settings_name(SettingId id);

/* Sets one from its text: 0, or -1 with what it takes in err. */
int settings_set(Settings *s, SettingId id, const char *value, char *err, size_t errsz);
/* Its value as the file writes it. */
void settings_value(const Settings *s, SettingId id, char *buf, size_t sz);

/* The file's place. */
void settings_path(char *buf, size_t sz);

/* Reads the file over the defaults. 0 when it is read or not there; what
 * could not be read is said in note (a line not known is kept and named, a
 * bad value leaves its default), "" when nothing. */
int settings_load(Settings *s, const char *path, char *note, size_t notesz);
int settings_save(const Settings *s, const char *path, char *err, size_t errsz);

const char *agent_kind_name(int kind);

#endif /* VTT_SETTINGS_H */
