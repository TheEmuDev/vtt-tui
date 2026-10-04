#!/bin/sh
# Turns web/index.html into src/webpage.c, a C string the binary serves.
# Run it after editing the page; the result is committed, so the build
# needs nothing but a C compiler.
set -eu
cd "$(dirname "$0")/.."

# Comments are the source's, not the phone's: every byte of the page goes to
# every phone, and the budget (docs/REMOTE.md) is what is sent. Block comments
# go, and a // comment after white space with the spaces before it -- never
# ws:// or http://, which follow a quote or a colon, and never a // with an
# odd number of any quote before it on its line, which is inside a string.
# Lines left empty, and spaces at a line's end, go too. tests/test_net.c's
# embed_cut does the same in C; keep the two alike.
SENT='import re, sys
s = re.sub(r"/\*.*?\*/", "", sys.stdin.read(), flags=re.S)
def cut(line):
    for m in re.finditer(r"[ \t]+//", line):
        before = line[:m.start()]
        if all(before.count(q) % 2 == 0 for q in "\x27\x22\x60"):
            return line[:m.start()]
    return line
s = "\n".join(cut(l) for l in s.split("\n"))
s = re.sub(r"[ \t]+$", "", s, flags=re.M)
out = "".join(l for l in s.splitlines(True) if l.strip())
sys.stdout.write(out if len(sys.argv) < 2 else str(len(out.encode())))'

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
    python3 -c "$SENT" < web/index.html |
    sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/\\n"/'
    echo '    ;'
    echo
    echo 'const size_t WEBPAGE_LEN = sizeof WEBPAGE - 1;'
} > src/webpage.c

printf 'src/webpage.c: %s bytes of page as sent (%s in web/index.html)\n' \
    "$(python3 -c "$SENT" count < web/index.html)" \
    "$(wc -c < web/index.html)"
