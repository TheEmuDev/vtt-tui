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
    # Block comments are the source's, not the phone's: every byte of the
    # page goes to every phone, and the budget (docs/REMOTE.md) is what is
    # sent. Lines a comment leaves empty go too. // comments stay: a naive
    # cut would take ws:// with them.
    python3 -c 'import re,sys; s=re.sub(r"/\*.*?\*/", "", sys.stdin.read(), flags=re.S); sys.stdout.write("".join(l for l in s.splitlines(True) if l.strip()))' < web/index.html |
    sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/\\n"/'
    echo '    ;'
    echo
    echo 'const size_t WEBPAGE_LEN = sizeof WEBPAGE - 1;'
} > src/webpage.c

printf 'src/webpage.c: %s bytes of page as sent (%s in web/index.html)\n' \
    "$(python3 -c 'import re,sys; s=re.sub(r"/\*.*?\*/", "", sys.stdin.read(), flags=re.S); print(len("".join(l for l in s.splitlines(True) if l.strip()).encode()))' < web/index.html)" \
    "$(wc -c < web/index.html)"
