#!/bin/sh
# The sandbox a test runs ./vtt in (tests/harness.c, sandbox_enter), for the
# shell: the user's maps, settings, key, keyring, running vtts and editor are
# out of reach. The one list for every script in tools/; suite `sandbox`
# checks it sets what sandbox_enter sets. A new XDG dir or a program vtt
# starts and waits for goes here and in sandbox_enter, the same change.
#
#   . tools/sandbox.sh; sandbox_env DIR   in a script: the dirs under DIR, made
#   tools/sandbox.sh CMD...               a command in a fresh sandbox under
#                                         /tmp (a short path: the channel's
#                                         socket must fit in 107 bytes),
#                                         removed after; its exit status
sandbox_env() {
    mkdir -p "$1/data" "$1/config" "$1/run" || return 1
    chmod 700 "$1/run"
    export XDG_DATA_HOME="$1/data" XDG_CONFIG_HOME="$1/config" XDG_RUNTIME_DIR="$1/run"
    export VTT_SECRET_TOOL=/nonexistent/vtt-sandbox/secret-tool   # no keyring
    export VISUAL=true EDITOR=true          # a card key runs the editor and waits for it
    export VTT_SERVE_PORT=0                 # a server of its own port, never the default 7793
}

if [ "${0##*/}" = sandbox.sh ]; then
    [ $# -ge 1 ] || { echo "usage: tools/sandbox.sh CMD..." >&2; exit 2; }
    sb_dir=$(mktemp -d /tmp/vtt-sb.XXXXXX) || exit 2
    trap 'rm -rf "$sb_dir"' EXIT
    trap 'exit 130' INT TERM
    sandbox_env "$sb_dir" || exit 2
    "$@"
    exit $?
fi
