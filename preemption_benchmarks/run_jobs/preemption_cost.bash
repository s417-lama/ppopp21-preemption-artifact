#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

DATETIME=${DATETIME:-$(TZ='America/Chicago' date +%Y-%m-%d_%H-%M-%S)}
RESULT_DIR=${RESULT_DIR:-${PWD}/results}
OUT_FILE=${RESULT_DIR}/preemption_cost_${DATETIME}.out
ERR_FILE=${RESULT_DIR}/preemption_cost_${DATETIME}.err

make clean
make preemption_cost

./preemption_cost -x 56 -n 112 -g 56 -r 10 -p 100 -t 1000 2> $ERR_FILE | tee $OUT_FILE
# ./preemption_cost -x 56 -n 1120 -g 56 -r 10 -p 10 -t 1000 2> $ERR_FILE | tee $OUT_FILE
# ./preemption_cost -x 64 -n 128 -g 64 -r 10 -p 100 -t 1000 2> $ERR_FILE | tee $OUT_FILE

ln -sf $OUT_FILE ${RESULT_DIR}/latest.out
ln -sf $ERR_FILE ${RESULT_DIR}/latest.err
