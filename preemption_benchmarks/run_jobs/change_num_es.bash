#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

make clean
make change_num_es

SIGQUEUE=./sigqueue.out

echo "
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    union sigval val = { .sival_int = atoi(argv[2]) };
    sigqueue((pid_t)atoi(argv[1]), SIGRTMIN + 2, val);
}
" | gcc -o $SIGQUEUE -x c -

trap "rm -rf $SIGQUEUE" EXIT

export KMP_ABT_NUM_ESS=56
export OMP_NUM_THREADS=56
# export ABT_INITIAL_NUM_SUB_XSTREAMS=40
export KMP_ABT_WORK_STEAL_FREQ=4
# export KMP_ABT_FORK_CUTOFF=4
export KMP_ABT_FORK_NUM_WAYS=4
export ABT_SET_AFFINITY=1
# export KMP_BLOCKTIME=0
# export KMP_HOT_TEAMS_MAX_LEVEL=2
# export KMP_HOT_TEAMS_MODE=1
# export OMP_PROC_BIND=close,unset
export ABT_THREAD_STACKSIZE=150000
export ABT_MEM_MAX_NUM_STACKS=80
export ABT_MEM_LP_ALLOC=malloc

./change_num_es -r 10 -t 10000 -i 100000000 2> mlog.ignore &
PID=$!

sleep 1
$SIGQUEUE $PID 28
# sleep 2
# $SIGQUEUE $PID 56
# sleep 2
# $SIGQUEUE $PID 55
# $SIGQUEUE $PID 40
# sleep 2
# $SIGQUEUE $PID 56

wait

export LD_PRELOAD=${INTEL_DIR}/compiler/lib/intel64/libiomp5.so
export KMP_BLOCKTIME=0
export KMP_AFFINITY="proclist=[0-55],explicit,granularity=fine"
./change_num_es -r 10 -i 100000000 &
PID=$!

sleep 1
taskset -acp 0-27 $PID > /dev/null
# sleep 2
# taskset -acp 0-55 $PID > /dev/null
# sleep 2
# taskset -acp 0-54 $PID > /dev/null
# taskset -acp 0-39 $PID > /dev/null
# sleep 2
# taskset -acp 0-55 $PID > /dev/null

wait
