#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

NTHREADS=28
LOG2DIM=8
NBOX=16

export INTEL_DIR=/soft/compilers/intel-2019/compilers_and_libraries_2019.4.243/linux
source ${INTEL_DIR}/bin/compilervars.sh intel64

./configure --CFLAGS=-fopenmp --no-fv-mpi --CC=icc
make -j -C build

# --------------- SIGQUEUE begin ---------------
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
# ---------------- SIGQUEUE end ----------------

export KMP_ABT_NUM_ESS=$NTHREADS
export OMP_NUM_THREADS=$NTHREADS
export ABT_PREEMPTION_INTERVAL_USEC=1000
export ABT_SET_AFFINITY=1
# export KMP_ABT_WORK_STEAL_FREQ=4
export KMP_ABT_FORK_NUM_WAYS=4
# export OMP_PROC_BIND=close,unset
# export ABT_THREAD_STACKSIZE=150000
# export ABT_MEM_MAX_NUM_STACKS=80
# export ABT_MEM_LP_ALLOC=malloc

W=24

LD_PRELOAD=$PWD/myopt/bolt/lib/libiomp5.so ./build/bin/hpgmg-fv $LOG2DIM $NBOX 2> mlog.ignore &
# LD_PRELOAD=$PWD/myopt/bolt/lib/libiomp5.so numactl -iall ./build/bin/hpgmg-fv $LOG2DIM $NBOX 2> mlog.ignore &
PID=$!

sleep 10
echo "sigqueue"
$SIGQUEUE $PID $W

wait

export KMP_BLOCKTIME=0
export KMP_AFFINITY="proclist=[0-$((NTHREADS - 1))],explicit,granularity=fine"

./build/bin/hpgmg-fv $LOG2DIM $NBOX &
# numactl -iall ./build/bin/hpgmg-fv $LOG2DIM $NBOX &
PID=$!

sleep 10
echo "sigqueue"
taskset -acp 0-$((W - 1)) $PID > /dev/null
# taskset -acp $PID | awk "\$6 >= $W { print \$2 }" | cut -d "'" -f 1 | xargs -I{} taskset -cp 0-$((W - 1)) {} > /dev/null
# taskset -acp $PID

wait
