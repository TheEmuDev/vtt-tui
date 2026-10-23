# The stand-in agent (tests/agenteval.sh): the hand-written plan, a request
# that is refused, then a read. Each exit status vtt's wrapper passes back is
# checked: a wrong one exits non-zero, and the test reads the agent's exit.
vtt map.vtt --apply "$EVAL_TASK_DIR/plan.txt" --new 40x24 > /dev/null || exit 3
vtt map.vtt --apply "$EVAL_TASK_DIR/refused.txt" 2> /dev/null; [ $? = 1 ] || exit 4
vtt map.vtt --check > /dev/null; [ $? = 1 ] || exit 5      # warnings: exit 1
exit 0
