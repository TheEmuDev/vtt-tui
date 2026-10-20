#ifndef VTT_APIAGENT_H
#define VTT_APIAGENT_H

/* The Claude API agent (docs/APIAGENT.md): `vtt --claude-agent`, started by
 * :agent claude as any agent command is -- the job on stdin, VTT_PID,
 * VTT_JOB and VTT_AGENT set. It answers the job through the Messages API,
 * one curl a call, and talks to its vtt over the channel as VTT_AGENT. */
typedef struct {
    const char *model;      /* a full model id (apiagent_model_id) */
    const char *effort;     /* low, medium, high */
    double      cap;        /* dollars a run, by the price table; 0: none */
    int         calls;      /* API calls a run */
    int         fallbacks;  /* the server-side fallback, where the model takes it */
} ApiAgentCfg;

/* The exit statuses, which vtt reports on the GM's line. */
enum {
    APIAGENT_DONE = 0,      /* it ended its turn */
    APIAGENT_SETUP = 1,     /* not started by vtt, no key, no curl */
    APIAGENT_API = 2,       /* the API or the network failed, or the vtt went away */
    APIAGENT_REFUSED = 3,   /* Claude declined the job */
    APIAGENT_CAP = 4,       /* the run's spend or calls ran out */
    APIAGENT_LONG = 5,      /* an answer ran past max_tokens */
};

int apiagent_main(const ApiAgentCfg *cfg);

/* opus, sonnet, haiku, fable: the current model of each; anything starting
 * claude- is taken as a model id as it is. NULL for anything else. */
const char *apiagent_model_id(const char *word);

/* A run's dollars for those tokens, by the table of the models it knows; -1
 * for a model it does not. */
double apiagent_cost(const char *model, long in, long cache_write, long cache_read, long out);

#endif /* VTT_APIAGENT_H */
