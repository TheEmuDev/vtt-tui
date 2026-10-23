# The stand-in agent (tests/agenteval.sh): the hand-written plan, a request
# that is refused, then a read.
vtt map.vtt --apply "$EVAL_TASK_DIR/plan.txt" --new 40x24 > /dev/null
vtt map.vtt --apply "$EVAL_TASK_DIR/refused.txt" 2> /dev/null
vtt map.vtt --check > /dev/null
exit 0
