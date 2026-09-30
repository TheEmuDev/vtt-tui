#ifndef VTT_JSON_H
#define VTT_JSON_H

#include <stdio.h>

/* Enough JSON to write a report, and to read a file someone else wrote: no
 * library. The map tools and the control channel's answers write through
 * the writer; imports (--import-adversaries) read through the reader. */
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

/* ------------------------------------------------------------------ reading */

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ } JsonKind;

/* A value. A string is decoded -- escapes resolved, \u as UTF-8 -- and
 * NUL-terminated; an array's items are kids[0..n), an object's members are
 * kids[0..n) with their names in keys[0..n). All of it is the document's. */
typedef struct JsonVal {
    JsonKind         kind;
    int              b;
    double           num;
    char            *str;
    int              n;
    struct JsonVal  *kids;
    char           **keys;
} JsonVal;

/* The whole of text as one JSON value, a UTF-8 byte-order mark allowed in
 * front. NULL with why in err -- where it went wrong, by line -- for
 * anything that is not JSON, or nests over 64 deep. */
JsonVal       *json_parse(const char *text, size_t len, char *err, size_t errsz);
void           json_free(JsonVal *v);

/* An object's member by name, or NULL; a string's text, or NULL for any
 * other kind; how many items or members (0 for a scalar). */
const JsonVal *json_get(const JsonVal *obj, const char *key);
const char    *json_text(const JsonVal *v);

#endif /* VTT_JSON_H */
