# The stand-in agent for tools/skill/evals/tasks (tests/agenteval.sh): the
# task's reference.txt, a plan written by hand that meets every fact.
if [ -f map.vtt ]; then vtt map.vtt --apply "$EVAL_TASK_DIR/reference.txt"
else vtt map.vtt --apply "$EVAL_TASK_DIR/reference.txt" --new "$(cat "$EVAL_TASK_DIR/size")"; fi > /dev/null
