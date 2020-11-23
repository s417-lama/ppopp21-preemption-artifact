#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

DATETIME=${DATETIME:-$(TZ='America/Chicago' date +%Y-%m-%d_%H-%M-%S)}
RESULT_DIR=${RESULT_DIR:-${PWD}/results}
OUT_FILE=${RESULT_DIR}/preemption_cost2_${DATETIME}.out
ERR_FILE=${RESULT_DIR}/preemption_cost2_${DATETIME}.err

make clean
make preemption_cost2

# ./preemption_cost2 -x 1 -g 1 -r 10 -p 100 2> $ERR_FILE | tee $OUT_FILE
# ./preemption_cost2 -x 56 -g 56 -r 10 -p 100 2> $ERR_FILE | tee $OUT_FILE

ABT_SET_AFFINITY=0 taskset -c 271 ./preemption_cost2 -x 1 -g 1 -r 1 -p 1000 -t 10000 2> $ERR_FILE | tee $OUT_FILE
# ./preemption_cost2 -x 1 -g 1 -r 1 -p 1000 -t 10000 2> $ERR_FILE | tee $OUT_FILE
# ./preemption_cost2 -x 56 -g 56 -r 1 -p 1000 -t 10000 2> $ERR_FILE | tee $OUT_FILE
# ./preemption_cost2 -x 68 -g 68 -r 1 -p 1000 -t 10000 2> $ERR_FILE | tee $OUT_FILE
# ./preemption_cost2 -x 68 -g 68 -r 1 -p 1000 -t 10000 2> $ERR_FILE | tee $OUT_FILE

# ./preemption_cost2 -x 4 -g 4 -r 1 -p 1000 -t 10000 2> $ERR_FILE | tee $OUT_FILE

ln -sf $OUT_FILE ${RESULT_DIR}/latest.out
ln -sf $ERR_FILE ${RESULT_DIR}/latest.err
