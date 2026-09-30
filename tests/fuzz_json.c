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

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char err[160];
    JsonVal *v = json_parse((const char *)data, size, err, sizeof err);
    if (v) {
        volatile size_t n = walk(v);
        (void)n;
        json_free(v);
    } else if (strncmp(err, "not JSON at line ", 17) != 0) {
        abort();                                  /* a refusal always says where */
    }
    return 0;
}
