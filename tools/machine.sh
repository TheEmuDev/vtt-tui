#!/bin/sh
# One line naming the machine a measurement ran on, for the top of every table
# docs/PERFORMANCE.md publishes: the numbers are this hardware's, and a table
# without it cannot be compared with the next one. perf.sh, saves.sh and
# pagebench.sh print it; median.py keeps it.
#
#   tools/machine.sh [DIR]     DIR: where the run writes its files (its disk is named)
# Reads Linux's /proc and /sys and lsblk where they are; says "unknown" elsewhere.
DIR=${1:-.}

cpu=$(awk -F': ' '/^model name/ { print $2; exit }' /proc/cpuinfo 2>/dev/null |
      sed 's/(R)//g; s/(TM)//g; s/ CPU / /; s/  */ /g')
[ -n "$cpu" ] || cpu=$(uname -m)
threads=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo '?')
# core id restarts on each socket, so a core is a (socket, core id) pair.
cores=$(awk -F': ' '/^physical id/ { p = $2 } /^core id/ { seen[p "," $2] = 1 }
                   END { n = 0; for (k in seen) n++; if (n) print n }' /proc/cpuinfo 2>/dev/null)
max=$(cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq 2>/dev/null)
[ -n "$max" ] && max=$(awk -v k="$max" 'BEGIN { printf ", %.1f GHz max", k / 1e6 }')
gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
[ -n "$gov" ] && gov=", $gov governor"
ram=$(awk '/^MemTotal/ { printf "%.0f GB RAM", $2 / 1048576 }' /proc/meminfo 2>/dev/null)
[ -n "$ram" ] || ram="RAM unknown"

# The disk under DIR: its filesystem, and the model of the device beneath any
# mapper or partition (lsblk -s walks from the filesystem down to the disk).
# --output is GNU df's; elsewhere the filesystem reads "?". -P keeps a long
# source (a LUKS mapper's uuid) on the same line as the rest.
fs=$(df --output=fstype "$DIR" 2>/dev/null | awk 'NR == 2 { print $1 }')
src=$(df -P "$DIR" 2>/dev/null | awk 'NR == 2 { print $1 }')
disk=$(lsblk -nso MODEL "$src" 2>/dev/null | awk 'NF { $1 = $1; m = $0 } END { print m }')
[ -n "$disk" ] || disk="disk unknown"

# "cc (GCC) 16.2.1 20260810", "gcc (Ubuntu 13.2.0-23ubuntu4) 13.2.0" -> "gcc N";
# "clang version 19.1.0", Apple's "Apple clang version 15.0.0 (...)" -> "clang N".
# clang first: Apple's line has a parenthesis too.
cc=$(${CC:-cc} --version 2>/dev/null | head -n 1 |
     sed 's/^.*clang version \([0-9.]*\).*/clang \1/; s/^[^ ]* ([^)]*) \([0-9.]*\).*/gcc \1/')
# The build being measured: make's release is -O2, make debug is the sanitizers.
mode=$(cat "$(dirname "$0")/../build/.mode" 2>/dev/null)
case $mode in release) opt=-O2 ;; debug) opt="debug build" ;; *) opt="build unknown" ;; esac
kernel=$(uname -sr)

echo "Machine: $cpu (${cores:-?} cores, $threads threads$max$gov), $ram, $disk (${fs:-?}), $kernel, ${cc:-cc unknown} $opt"
