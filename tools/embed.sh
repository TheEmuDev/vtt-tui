#!/bin/sh
# Turns web/index.html into src/webpage.c, a C string the binary serves.
# Run it after editing the page; the result is committed, so the build
# needs nothing but a C compiler.
set -eu
cd "$(dirname "$0")/.."

{
    echo '/* Generated from web/index.html by tools/embed.sh -- edit the HTML, not this. */'
    echo '#include <stddef.h>'
    echo
    echo '/* C99 only promises 4095 bytes of string literal; every compiler this'
    echo ' * project builds with takes far more, and the alternative is a byte array'
    echo ' * five times the size. */'
    echo '#pragma GCC diagnostic ignored "-Woverlength-strings"'
    echo
    echo 'const char WEBPAGE[] ='
    sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/\\n"/' web/index.html
    echo '    ;'
    echo
    echo 'const size_t WEBPAGE_LEN = sizeof WEBPAGE - 1;'
} > src/webpage.c

printf 'src/webpage.c: %s bytes of page\n' "$(wc -c < web/index.html)"
