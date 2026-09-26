#ifndef VTT_JSON_H
#define VTT_JSON_H

#include <stdio.h>

/* Enough JSON to write a report: objects, arrays, strings, numbers. No
 * library, and nothing to parse -- only to write, correctly escaped. The map
 * tools and the control channel's answers both write through it. */
typedef struct {
    FILE *f;
    int   depth;
    int   first[32];      /* nothing written yet at this depth */
    int   after_key;
} Json;

void json_init(Json *j, FILE *f);
/* '{' or '[', and the matching close. */
void json_open(Json *j, char c);
void json_close(Json *j, char c);
void json_key(Json *j, const char *k);
void json_str(Json *j, const char *s);
void json_int(Json *j, long v);
void json_num(Json *j, double v);
void json_bool(Json *j, int v);
void json_null(Json *j);

static inline void json_kstr(Json *j, const char *k, const char *v) { json_key(j, k); json_str(j, v); }
static inline void json_kint(Json *j, const char *k, long v)        { json_key(j, k); json_int(j, v); }

#endif /* VTT_JSON_H */
