#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

DATETIME=${DATETIME:-$(TZ='America/Chicago' date +%Y-%m-%d_%H-%M-%S)}
RESULT_DIR=${RESULT_DIR:-${PWD}/results}
OUT_FILE=${RESULT_DIR}/barrier_${DATETIME}.out
ERR_FILE=${RESULT_DIR}/barrier_${DATETIME}.err

make clean
make barrier

export ABT_INITIAL_NUM_SUB_XSTREAMS=15
# stdbuf -oL ./barrier -x 56 -n 560 -r 10 -g 56 -b 1000 -t 100 2> $ERR_FILE | tee $OUT_FILE
stdbuf -oL ./barrier -x 68 -n 680 -r 10 -g 68 -b 1000 -t 300 2> $ERR_FILE | tee $OUT_FILE

ln -sf $OUT_FILE ${RESULT_DIR}/latest.out
ln -sf $ERR_FILE ${RESULT_DIR}/latest.err
