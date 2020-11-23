#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

DATETIME=${DATETIME:-$(TZ='America/Chicago' date +%Y-%m-%d_%H-%M-%S)}
RESULT_DIR=${RESULT_DIR:-${PWD}/results}
OUT_FILE=${RESULT_DIR}/timer_measure_${DATETIME}.out
ERR_FILE=${RESULT_DIR}/timer_measure_${DATETIME}.err

make clean
make timer_measure

./timer_measure -n 56 -t 56 -m 1 -i 1000 2> $ERR_FILE | tee $OUT_FILE
# ./timer_measure -n 56 -t 1 -m 1 -c 1 -i 1000 2> $ERR_FILE | tee $OUT_FILE
# ./timer_measure -n 56 -t 56 -m 1 -i 1000 -d 1 -c 1 -f 1 2> $ERR_FILE | tee $OUT_FILE
# ./timer_measure -n 56 -t 1 -m 1 -i 1000 -d 1 -c 1 -f 1 2> $ERR_FILE | tee $OUT_FILE
# ./timer_measure -n 256 -t 256 -m 1 -i 5000 2> $ERR_FILE | tee $OUT_FILE
# ./timer_measure -n 64 -t 64 -m 1 -i 1000 2> $ERR_FILE | tee $OUT_FILE

ln -sf $OUT_FILE ${RESULT_DIR}/latest.out
ln -sf $ERR_FILE ${RESULT_DIR}/latest.err
