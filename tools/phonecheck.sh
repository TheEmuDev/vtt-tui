#!/bin/sh
# What a real phone over the Wi-Fi makes of vtt's sending (docs/V2.md, step 7,
# item 5; question 3): manual, not in make test. Needs the user's Android
# phone plugged in over USB with USB debugging on, Chrome on it, the phone on
# the same network as this machine, adb and node.
#
#   tools/phonecheck.sh                 every check below
#   ONLY=fulls tools/phonecheck.sh      the FULLs only; ONLY=away or ONLY=lock, that one only
#   LOADS=10 ROUNDS=3 OFF=15 KEYS=+-    loads a turn; turns a buffer; seconds the screen is off; the GM's keys meanwhile
#
# fulls: a sandboxed vtt serves a map in a terminal of 200x50 and of 400x170,
#   first as shipped (SO_SNDBUF 128 KB, src/net.c) and then with the kernel's
#   own send buffer (a preload that drops vtt's SO_SNDBUF; ss shows which held).
#   The two alternate, ROUNDS turns of LOADS loads each; per load, the time from the
#   socket's open to the FULL's arrival, by the phone's clock (the FULL is
#   sent at once on joining). Prints the median and the range.
# lock: as shipped, the phone's screen is turned off while the GM's side zooms
#   (frames for a phone that cannot draw them), then woken. What arrived while
#   it was off and after, how long until the last of it, and whether the page
#   ends on the screen a fresh load draws. A phone with a PIN waits for it.
# away: the same with Chrome sent behind the home screen and brought back:
#   no unlock, so it runs with nobody at the phone (unlocked to start).
#
# The phone's Chrome is driven through its DevTools socket (adb forward); the
# page opens in a tab of its own, closed after. vtt runs in the sandbox
# (tools/sandbox.sh); only the PIDs started here are stopped.
set -eu
cd "$(dirname "$0")/.."
BIN=${BIN:-./vtt}
LOADS=${LOADS:-10}
ROUNDS=${ROUNDS:-3}
OFF=${OFF:-15}           # not SECONDS: bash's own, the seconds since it started
CDP=${CDP:-9333}
PORT=${PORT:-7793}
[ -x "$BIN" ] || { echo "no $BIN -- run make first" >&2; exit 1; }
for t in adb node cc ss script curl python3 ip seq; do
    command -v "$t" >/dev/null || { echo "phonecheck needs $t" >&2; exit 1; }
done
# One phone, by its USB serial (adb may list it twice, over USB and over TLS).
if [ -z "${ANDROID_SERIAL:-}" ]; then
    ANDROID_SERIAL=$(adb devices | awk 'NR > 1 && $2 == "device" && $1 !~ /_adb-tls/ { print $1; exit }')
    [ -n "$ANDROID_SERIAL" ] || { echo "no phone: adb devices lists none over USB" >&2; exit 1; }
fi
export ANDROID_SERIAL

DIR=$(mktemp -d /tmp/vtt-pc.XXXXXX)      # short: the channel's socket path
HOLD= SPID= VPID=
stop_vtt() {
    [ -n "$VPID" ] && kill "$VPID" 2>/dev/null; [ -n "$SPID" ] && wait "$SPID" 2>/dev/null
    [ -n "$HOLD" ] && kill "$HOLD" 2>/dev/null
    HOLD= SPID= VPID=
    rm -f "$DIR/in" "$DIR/term"
    return 0
}
cleanup() {
    stop_vtt
    adb forward --remove "tcp:$CDP" >/dev/null 2>&1 || true
    rm -rf "$DIR"
}
trap cleanup EXIT
trap 'exit 130' INT TERM
. tools/sandbox.sh
sandbox_env "$DIR"
. tools/genmap.sh

adb forward "tcp:$CDP" localabstract:chrome_devtools_remote >/dev/null
curl -s "http://127.0.0.1:$CDP/json/version" >/dev/null 2>&1 || {
    adb shell am start -n com.android.chrome/com.google.android.apps.chrome.Main >/dev/null
    sleep 3
    curl -s "http://127.0.0.1:$CDP/json/version" >/dev/null ||
        { echo "the phone's Chrome does not answer on its DevTools socket" >&2; exit 1; }
}

# The preload: setsockopt as libc's, but SO_SNDBUF is left to the kernel.
cat > "$DIR/nosndbuf.c" <<'C'
#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/socket.h>
int setsockopt(int fd, int level, int name, const void *val, socklen_t len)
{
    static int (*real)(int, int, int, const void *, socklen_t);
    if (!real) real = (int (*)(int, int, int, const void *, socklen_t))dlsym(RTLD_NEXT, "setsockopt");
    if (level == SOL_SOCKET && name == SO_SNDBUF) return 0;
    return real(fd, level, name, val, len);
}
C
cc -shared -fPIC -O2 -o "$DIR/nosndbuf.so" "$DIR/nosndbuf.c" -ldl

BIG=$(genmap big 512 512 1 60)           # more map than any window: walls everywhere, a crowd

# start_vtt COLSxROWS [PRELOAD]: vtt serving BIG in a terminal of that size,
# in play mode; sets URL.
start_vtt() {
    _c=${1%x*} _r=${1#*x}
    mkfifo "$DIR/in"
    sleep 3600 > "$DIR/in" & HOLD=$!
    _pre=
    [ -n "${2:-}" ] && _pre="LD_PRELOAD=$2"
    script -qfc "stty cols $_c rows $_r; exec env $_pre $BIN $BIG --serve $PORT" "$DIR/term" < "$DIR/in" > /dev/null &
    SPID=$!
    _k=
    for _i in $(seq 50); do
        sleep 0.2
        _k=$(tr -c '0-9a-z=?:. \n' ' ' < "$DIR/term" | awk 'match($0, /\?k=[0-9]+/) { print substr($0, RSTART + 3, RLENGTH - 3); exit }')
        [ -n "$_k" ] && break
    done
    VPID=$(ps -o pid= --ppid "$SPID" | awk 'NR == 1 { print $1 }')
    [ -n "$_k" ] && [ -n "$VPID" ] || { echo "vtt did not start serving (its terminal: $DIR/term)" >&2; exit 1; }
    URL="http://$(ip -4 route get 8.8.8.8 | awk '{ for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1) }'):$PORT/?k=$_k"
    printf ':play\r' > "$DIR/in"
    sleep 0.5
    _h=${URL#http://}; _h=${_h%%/*}
    adb shell "toybox nc -w 3 ${_h%:*} ${_h#*:} < /dev/null" > /dev/null 2>&1 || {
        echo "the phone cannot reach $_h over the Wi-Fi: a firewall here (ufw: sudo ufw allow from" \
             "$(ip -4 route get 8.8.8.8 | awk '{ for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1) }' | sed 's/[0-9]*$/0/')/24" \
             "to any port $PORT proto tcp), or the network keeps its clients apart" >&2
        exit 1
    }
}

# The send buffer the phone's connection has: ss's tb, in bytes.
sndbuf() {
    ss -tmn state established "( sport = :$PORT )" | awk 'match($0, /tb[0-9]+/) { print substr($0, RSTART + 2, RLENGTH - 2) }'
}

summary() {   # fulls runs, a JSON a line -> "median ms (min-max), N loads | bytes"
    python3 -c '
import json, statistics, sys
d = [l for line in sys.stdin if line.strip() for l in json.loads(line)["loads"]]
ms = sorted(l["ms"] for l in d); by = sorted(set(l["bytes"] for l in d))
print("%7.1f ms (%.1f-%.1f), %d loads | %s" % (statistics.median(ms), ms[0], ms[-1], len(ms), ",".join(map(str, by))))'
}

echo "$(tools/machine.sh "$DIR")"
echo "Phone: $(adb shell getprop ro.product.model | tr -d '\r'), Android $(adb shell getprop ro.build.version.release | tr -d '\r'), Chrome $(curl -s "http://127.0.0.1:$CDP/json/version" | python3 -c 'import json,sys; print(json.load(sys.stdin)["Browser"].split("/")[-1])')"
echo "Link: $(adb shell dumpsys wifi | tr -d '\r' | awk '/mWifiInfo/ { print; exit }' | tr ',' '\n' |
    awk '/^ (Wi-Fi standard|RSSI|Tx Link speed|Rx Link speed|Frequency):/ { sub(/^ /, ""); printf "%s%s", s, $0; s = ", " }')"
echo

if [ "${ONLY:-fulls}" = fulls ] || [ -z "${ONLY:-}" ]; then
    # The two buffers alternate, ROUNDS times, LOADS loads a turn: run one
    # after the other, the first read slower in one run and not the next
    # (the radio's state, a guess), so neither may always go first.
    printf '| window  | send buffer          | open to FULL, median (range) | FULL bytes |\n'
    printf '|---------|----------------------|------------------------------|------------|\n'
    for size in 200x50 400x170; do
        : > "$DIR/j-ship"; : > "$DIR/j-kern"; : > "$DIR/tb-ship"; : > "$DIR/tb-kern"
        r=0
        while [ "$r" -lt "$ROUNDS" ]; do
            for how in ship kern; do
                pre=; [ "$how" = kern ] && pre="$DIR/nosndbuf.so"
                start_vtt "$size" "$pre"
                # ss, sampled through the loads: the largest send buffer the
                # phone's connection had (fixed as shipped; it grows without).
                ( while :; do sndbuf; sleep 0.05; done ) >> "$DIR/tb-$how" & _ss=$!
                j=$(node tools/phonecheck.js fulls "$CDP" "$URL" "$LOADS")
                kill "$_ss"; wait "$_ss" 2>/dev/null || true
                [ -n "${DEBUG:-}" ] && printf '%s\n' "$j" >&2
                printf '%s\n' "$j" >> "$DIR/j-$how"
                stop_vtt
            done
            r=$((r + 1))
        done
        for how in ship kern; do
            tb=$(sort -n "$DIR/tb-$how" | tail -1)
            if [ "$how" = kern ]; then what="kernel's ($tb)"; else what="128 KB ($tb)"; fi
            printf '| %-7s | %-20s | %s |\n' "$size" "$what" "$(summary < "$DIR/j-$how")"
        done
    done
    echo
fi

for how in away lock; do
    [ -z "${ONLY:-}" ] || [ "$ONLY" = "$how" ] || continue
    for size in 200x50 400x170; do
        start_vtt "$size"
        echo "$how, $size, ${OFF} s off, the GM pressing ${KEYS:-+-} meanwhile:"
        node tools/phonecheck.js "$how" "$CDP" "$URL" "$DIR/in" "${KEYS:-+-}" "$OFF" &
        _n=$!
        # What the phone's connections acknowledged, sampled through the run.
        while kill -0 "$_n" 2>/dev/null; do
            ss -tin state established "( sport = :$PORT )" | awk 'match($0, /bytes_acked:[0-9]+/) { print substr($0, RSTART + 12, RLENGTH - 12) }' | tail -1
            sleep 0.5
        done > "$DIR/acked"
        wait "$_n" || exit 1
        [ -n "${DEBUG:-}" ] && echo "acked, sampled: $(tr '\n' ' ' < "$DIR/acked")" >&2
        stop_vtt
    done
done
