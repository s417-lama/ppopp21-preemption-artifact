#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

DATETIME=${DATETIME:-$(TZ='America/Chicago' date +%Y-%m-%d_%H-%M-%S)}
RESULT_DIR=${RESULT_DIR:-${PWD}/results}
OUT_FILE=${RESULT_DIR}/noop_${DATETIME}.out
ERR_FILE=${RESULT_DIR}/noop_${DATETIME}.err

make clean
CC=icc make noop
# CFLAGS="-DLOGGER_ENABLE" make noop

export ABT_INITIAL_NUM_SUB_XSTREAMS=40

export ABT_PREEMPTION_TIMER_TYPE=signal
# export ABT_PREEMPTION_TIMER_TYPE=dedicated

# stdbuf -oL ./noop -x 56 -n 560 -r 2 -i 1000000 -g 1 -t 500 2> $ERR_FILE | tee $OUT_FILE
# stdbuf -oL ./noop -x 56 -n 560 -r 10 -i 10000000 -g 56 -t 1000
# ./noop -x 56 -n 560 -r 10 -i 1000000000 -g 56 -t 150
# ./noop -x 56 -n 560 -r 10 -i 10000000 -g 1 -t 150
# perf stat -e L1-dcache-loads,L1-dcache-load-misses,L1-dcache-stores,L1-dcache-store-misses,LLC-store-misses,LLC-loads,LLC-load-misses,LLC-stores,LLC-store-misses ./noop -x 56 -n 560 -r 11 -i 10000000 -g 56 -t 150
# perf stat -e cs,L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses ./noop -x 56 -n 560 -r 11 -i 10000000 -g 56 -t 150

./noop -x 68 -n 680 -r 10 -i 5000000 -g 68 -t 2100

ln -sf $OUT_FILE ${RESULT_DIR}/latest.out
ln -sf $ERR_FILE ${RESULT_DIR}/latest.err
