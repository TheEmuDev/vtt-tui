# Leaves the map it was given as it was. The GM's saved characters, read: the
# test plants one in the caller's XDG data directory, which a sandboxed vtt
# never sees -- seen, the stand-in exits 6.
vtt map.vtt --describe > /dev/null
vtt map.vtt --apply "$EVAL_TASK_DIR/chars.txt" | grep -q canary && exit 6
exit 0
