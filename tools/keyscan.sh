#!/bin/sh
# Does the API key linger in vtt's memory after the app reads it? Builds
# tools/keyscan/keyscan.c against vtt's own apikey.c, proc.c and util.c
# (no sanitizer), with a stand-in secret-tool that prints a key made at run
# time, and scans the process's writable memory after each step (HEALTH B18,
# the key's security reviews). Never the real keyring. Exit 1 when a copy is
# found. A scratch directory of its own, removed after.
set -eu
cd "$(dirname "$0")/.."
dir=$(mktemp -d "${TMPDIR:-/tmp}/keyscan.XXXXXX")
trap 'rm -rf "$dir"' EXIT
cat > "$dir/secret-tool" <<SH
#!/bin/sh
[ "\$1" = lookup ] && cat "$dir/secret"
SH
chmod +x "$dir/secret-tool"
${CC:-cc} -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Isrc tools/keyscan/keyscan.c src/apikey.c src/proc.c src/util.c \
    -o "$dir/keyscan" -lm
VTT_SECRET_TOOL="$dir/secret-tool" "$dir/keyscan" "$dir/secret"
