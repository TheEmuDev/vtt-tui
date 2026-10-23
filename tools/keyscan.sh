#!/bin/sh
# Does the API key linger in vtt's memory after the app reads it? Builds
# tools/keyscan/keyscan.c against vtt's own apikey.c, proc.c and util.c
# (no sanitizer), with a stand-in secret-tool that prints a key made at run
# time, and scans the process's writable memory after each step (HEALTH B18,
# the key's security reviews). Never the real keyring. Exit 1 when a copy is
# found. A scratch directory of its own, removed after. KEYSCAN_CONTROL=1:
# the control, built with proc.c's read-buffer wipe removed (a copy in the
# scratch directory): it must find the key and exit 1, or the scan proves
# nothing.
set -eu
cd "$(dirname "$0")/.."
dir=$(mktemp -d "${TMPDIR:-/tmp}/keyscan.XXXXXX")
trap 'rm -rf "$dir"' EXIT
cat > "$dir/secret-tool" <<SH
#!/bin/sh
[ "\$1" = lookup ] && cat "$dir/secret"
SH
chmod +x "$dir/secret-tool"
proc=src/proc.c
if [ "${KEYSCAN_CONTROL:-}" = 1 ]; then
    python3 -I - "$dir/proc.c" <<'PY'
import sys
s = open('src/proc.c').read()
o = "    { volatile char *v = buf; for (size_t i = 0; i < sizeof buf; i++) v[i] = 0; }\n"
if s.count(o) != 1: sys.exit("keyscan: the wipe to remove is not in src/proc.c as written")
open(sys.argv[1], 'w').write(s.replace(o, ""))
PY
    proc="$dir/proc.c"
    echo "the control: proc.c's wipe removed -- a copy must be found"
fi
${CC:-cc} -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Isrc tools/keyscan/keyscan.c src/apikey.c "$proc" src/util.c \
    -o "$dir/keyscan" -lm
VTT_SECRET_TOOL="$dir/secret-tool" "$dir/keyscan" "$dir/secret"
