#include "json.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

void json_init(Json *j, FILE *f)
{
    memset(j, 0, sizeof *j);
    j->f = f;
}

static void json_sep(Json *j)
{
    if (j->after_key) { j->after_key = 0; return; }
    if (j->depth > 0) {
        if (!j->first[j->depth]) fputc(',', j->f);
        j->first[j->depth] = 0;
    }
}

void json_open(Json *j, char c)
{
    json_sep(j);
    fputc(c, j->f);
    if (j->depth < 31) j->first[++j->depth] = 1;
}

void json_close(Json *j, char c)
{
    fputc(c, j->f);
    if (j->depth > 0) j->depth--;
}

static void json_str_raw(FILE *f, const char *s)
{
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p == '\n') fputs("\\n", f);
        else if (*p == '\t') fputs("\\t", f);
        else if (*p < 0x20)  fprintf(f, "\\u%04x", *p);
        else                 fputc(*p, f);
    }
    fputc('"', f);
}

void json_key(Json *j, const char *k)
{
    json_sep(j);
    json_str_raw(j->f, k);
    fputc(':', j->f);
    j->after_key = 1;
}

void json_str(Json *j, const char *s) { json_sep(j); json_str_raw(j->f, s); }
void json_int(Json *j, long v)        { json_sep(j); fprintf(j->f, "%ld", v); }
void json_num(Json *j, double v)      { json_sep(j); fprintf(j->f, "%g", v); }
void json_bool(Json *j, int v)        { json_sep(j); fputs(v ? "true" : "false", j->f); }
void json_null(Json *j)               { json_sep(j); fputs("null", j->f); }

/* ------------------------------------------------------------------ reading */

typedef struct {
    const char *p, *end, *start;
    char       *err;
    size_t      errsz;
    int         failed;
} JsonIn;

static void jfail(JsonIn *in, const char *what)
{
    if (in->failed) return;
    in->failed = 1;
    int line = 1;
    for (const char *q = in->start; q < in->p && q < in->end; q++) line += *q == '\n';
    snprintf(in->err, in->errsz, "not JSON at line %d: %s", line, what);
}

static void jspace(JsonIn *in)
{
    while (in->p < in->end && (*in->p == ' ' || *in->p == '\t' || *in->p == '\n' || *in->p == '\r')) in->p++;
}

static int jhex4(const char *p, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

/* A string, the opening quote already seen; decoded onto the heap. */
static char *jstring(JsonIn *in)
{
    size_t cap = 64, n = 0;
    char  *s = xmalloc(cap);
    for (;;) {
        if (in->p >= in->end) { jfail(in, "a string does not end"); free(s); return NULL; }
        unsigned char c = (unsigned char)*in->p++;
        if (c == '"') break;
        if (c < 0x20) { jfail(in, "a control character in a string"); free(s); return NULL; }
        if (n + 5 >= cap) s = xrealloc(s, cap *= 2);
        if (c != '\\') { s[n++] = (char)c; continue; }
        if (in->p >= in->end) { jfail(in, "a string does not end"); free(s); return NULL; }
        char e = *in->p++;
        switch (e) {
        case '"': case '\\': case '/': s[n++] = e; break;
        case 'b': s[n++] = '\b'; break;
        case 'f': s[n++] = '\f'; break;
        case 'n': s[n++] = '\n'; break;
        case 'r': s[n++] = '\r'; break;
        case 't': s[n++] = '\t'; break;
        case 'u': {
            unsigned cp;
            if (in->end - in->p < 4 || jhex4(in->p, &cp) < 0) { jfail(in, "a bad \\u escape"); free(s); return NULL; }
            in->p += 4;
            /* A surrogate pair is one character; half of one is not text. */
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                unsigned lo;
                if (in->end - in->p < 6 || in->p[0] != '\\' || in->p[1] != 'u' || jhex4(in->p + 2, &lo) < 0 ||
                    lo < 0xDC00 || lo > 0xDFFF) { jfail(in, "half a surrogate pair"); free(s); return NULL; }
                in->p += 6;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) { jfail(in, "half a surrogate pair"); free(s); return NULL; }
            if (cp == 0) { jfail(in, "a NUL in a string"); free(s); return NULL; }
            n += (size_t)utf8_encode(cp, s + n);
            break;
        }
        default: jfail(in, "a bad escape"); free(s); return NULL;
        }
    }
    s[n] = '\0';
    if (!utf8_valid(s, n)) { jfail(in, "a string that is not UTF-8"); free(s); return NULL; }
    return s;
}

static void jvalue(JsonIn *in, JsonVal *v, int depth);

static void jadd(JsonVal *v, int *cap)
{
    if (v->n == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        v->kids = xrealloc(v->kids, (size_t)*cap * sizeof *v->kids);
        if (v->kind == JSON_OBJ) v->keys = xrealloc(v->keys, (size_t)*cap * sizeof *v->keys);
    }
    memset(&v->kids[v->n], 0, sizeof v->kids[0]);
    if (v->kind == JSON_OBJ) v->keys[v->n] = NULL;
    v->n++;
}

static void jvalue(JsonIn *in, JsonVal *v, int depth)
{
    memset(v, 0, sizeof *v);
    jspace(in);
    if (in->p >= in->end) { jfail(in, "it ends where a value should be"); return; }
    if (depth > 64) { jfail(in, "nested over 64 deep"); return; }
    char c = *in->p;
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        v->kind = c == '{' ? JSON_OBJ : JSON_ARR;
        in->p++;
        int cap = 0;
        jspace(in);
        if (in->p < in->end && *in->p == close) { in->p++; return; }
        for (;;) {
            jspace(in);
            char *key = NULL;
            if (v->kind == JSON_OBJ) {
                if (in->p >= in->end || *in->p != '"') { jfail(in, "a member's name should be a string"); return; }
                in->p++;
                key = jstring(in);
                if (!key) return;
                jspace(in);
                if (in->p >= in->end || *in->p != ':') { free(key); jfail(in, "a ':' should follow a name"); return; }
                in->p++;
            }
            jadd(v, &cap);
            if (key) v->keys[v->n - 1] = key;
            jvalue(in, &v->kids[v->n - 1], depth + 1);
            if (in->failed) return;
            jspace(in);
            if (in->p < in->end && *in->p == ',') { in->p++; continue; }
            if (in->p < in->end && *in->p == close) { in->p++; return; }
            jfail(in, v->kind == JSON_OBJ ? "a ',' or '}' should follow a member" : "a ',' or ']' should follow an item");
            return;
        }
    }
    if (c == '"') { in->p++; v->kind = JSON_STR; v->str = jstring(in); return; }
    if (in->end - in->p >= 4 && !memcmp(in->p, "true", 4))  { v->kind = JSON_BOOL; v->b = 1; in->p += 4; return; }
    if (in->end - in->p >= 5 && !memcmp(in->p, "false", 5)) { v->kind = JSON_BOOL; in->p += 5; return; }
    if (in->end - in->p >= 4 && !memcmp(in->p, "null", 4))  { v->kind = JSON_NULL; in->p += 4; return; }
    if (c == '-' || (c >= '0' && c <= '9')) {
        /* JSON's own number: -?digits(.digits)?([eE][+-]?digits)? */
        const char *q = in->p;
        char buf[64];
        size_t k = 0;
        #define TAKE_DIGITS() do { int d = 0; while (q < in->end && *q >= '0' && *q <= '9' && k + 1 < sizeof buf) { buf[k++] = *q++; d++; } if (!d) { jfail(in, "a malformed number"); return; } } while (0)
        if (*q == '-') buf[k++] = *q++;
        TAKE_DIGITS();
        if (q < in->end && *q == '.') { buf[k++] = *q++; TAKE_DIGITS(); }
        if (q < in->end && (*q == 'e' || *q == 'E')) {
            buf[k++] = *q++;
            if (q < in->end && (*q == '+' || *q == '-')) buf[k++] = *q++;
            TAKE_DIGITS();
        }
        #undef TAKE_DIGITS
        buf[k] = '\0';
        v->kind = JSON_NUM;
        v->num  = strtod(buf, NULL);
        in->p   = q;
        return;
    }
    jfail(in, "not a value");
}

static void jfree_val(JsonVal *v)
{
    for (int i = 0; i < v->n; i++) {
        jfree_val(&v->kids[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->kids);
    free(v->keys);
    free(v->str);
}

JsonVal *json_parse(const char *text, size_t len, char *err, size_t errsz)
{
    JsonIn in = { text, text + len, text, err, errsz, 0 };
    if (len >= 3 && !memcmp(text, "\xef\xbb\xbf", 3)) in.p += 3;       /* a byte-order mark */
    JsonVal *v = xmalloc(sizeof *v);
    jvalue(&in, v, 0);
    jspace(&in);
    if (!in.failed && in.p < in.end) jfail(&in, "more after the value");
    if (in.failed) { jfree_val(v); free(v); return NULL; }
    return v;
}

void json_free(JsonVal *v)
{
    if (!v) return;
    jfree_val(v);
    free(v);
}

const JsonVal *json_get(const JsonVal *obj, const char *key)
{
    if (!obj || obj->kind != JSON_OBJ) return NULL;
    for (int i = 0; i < obj->n; i++)
        if (obj->keys[i] && !strcmp(obj->keys[i], key)) return &obj->kids[i];
    return NULL;
}

const char *json_text(const JsonVal *v)
{
    return v && v->kind == JSON_STR ? v->str : NULL;
}

