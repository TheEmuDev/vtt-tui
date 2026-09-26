#include "json.h"

#include <string.h>

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
