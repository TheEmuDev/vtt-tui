/* libFuzzer target for the JSON reader: imports hand it files from the
 * web. Whatever it is given it must answer -- a tree or a refusal -- and a
 * tree must walk and free cleanly.
 *
 *   make fuzz-json             a minute, seeded from tests/fuzz-json
 *   make fuzz-json FUZZ_SECONDS=600 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static size_t walk(const JsonVal *v)
{
    size_t n = 1;
    if (v->kind == JSON_STR && v->str) n += strlen(v->str);
    for (int i = 0; i < v->n; i++) {
        n += walk(&v->kids[i]);
        if (v->kind == JSON_OBJ) n += strlen(v->keys[i]);
    }
    return n;
}

/* Each value's span is its own text: parsed alone, it is a value of the same kind
 * (the API agent copies spans back into requests unchanged). */
static void spans(const JsonVal *v, const char *text)
{
    for (int i = 0; i < v->n; i++) {
        char err[160];
        JsonVal *k = json_parse(text + v->kids[i].at, v->kids[i].len, err, sizeof err);
        if (!k || k->kind != v->kids[i].kind || k->n != v->kids[i].n) abort();
        json_free(k);
        spans(&v->kids[i], text);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char err[160];
    JsonVal *v = json_parse((const char *)data, size, err, sizeof err);
    if (v) {
        volatile size_t n = walk(v);
        (void)n;
        spans(v, (const char *)data);
        json_free(v);
    } else if (strncmp(err, "not JSON at line ", 17) != 0) {
        abort();                                  /* a refusal always says where */
    }
    return 0;
}
