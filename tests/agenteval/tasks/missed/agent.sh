# The same map as made, for facts that do not hold on it.
EVAL_TASK_DIR=$EVAL_TASK_DIR/../made
export EVAL_TASK_DIR
exec sh "$EVAL_TASK_DIR/agent.sh"
