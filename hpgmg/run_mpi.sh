#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

NTHREADS=28
LOG2DIM=8
NBOX=8
# LOG2DIM=7
# NBOX=32
NWORKER=27

export INTEL_DIR=/soft/compilers/intel-2019/compilers_and_libraries_2019.4.243/linux
source ${INTEL_DIR}/bin/compilervars.sh intel64

./configure --CFLAGS=-fopenmp --CC=mpiicc
# ./configure --CFLAGS="-fopenmp -O0 -g" --CC=mpiicc
make -j -C build

# --------------- SIGQUEUE begin ---------------
# export SIGQUEUE_CMD=./sigqueue.out
export SIGQUEUE_CMD=$(mktemp)

echo "
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    union sigval val = { .sival_int = atoi(argv[2]) };
    sigqueue((pid_t)atoi(argv[1]), SIGRTMIN + 2, val);
}
" | gcc -o $SIGQUEUE_CMD -x c -

trap "rm -rf $SIGQUEUE_CMD" EXIT
# ---------------- SIGQUEUE end ----------------

export KMP_ABT_NUM_ESS=$NTHREADS
export OMP_NUM_THREADS=$NTHREADS
export ABT_PREEMPTION_INTERVAL_USEC=1000
export ABT_SET_AFFINITY=1
export KMP_ABT_FORK_NUM_WAYS=4

# export I_MPI_DEBUG=4
export I_MPI_PIN_DOMAIN="[fffffff,fffffff0000000]" # [0-27,28-55]

run_hpgmg_bolt() {
  local log2dim=$1
  local nbox=$2
  local w=$3

  LD_PRELOAD=$PWD/myopt/bolt/lib/libiomp5.so ./build/bin/hpgmg-fv $log2dim $nbox 2> mlog.ignore.$PMI_RANK &
  local pid=$!

  sleep 3
  echo "sigqueue"
  $SIGQUEUE_CMD $pid $w

  wait
}
export -f run_hpgmg_bolt

run_hpgmg_iomp() {
  local log2dim=$1
  local nbox=$2
  local w=$3
  local nt=28
  local base=$((nt * PMI_RANK))

  export KMP_BLOCKTIME=0
  export KMP_AFFINITY="proclist=[${base}-$((base + nt - 1))],explicit,granularity=fine"

  ./build/bin/hpgmg-fv $log2dim $nbox 2> mlog.ignore.$PMI_RANK &
  local pid=$!

  sleep 3
  echo "sigqueue"
  taskset -acp ${base}-$((base + w - 1)) $pid > /dev/null
  # taskset -acp $pid | awk "\$6 >= $w { print \$2 }" | cut -d "'" -f 1 | xargs -I{} taskset -cp ${base}-$((base + w - 1)) {} > /dev/null
  # taskset -acp $pid

  wait
}
export -f run_hpgmg_iomp

mpirun -np 2 bash -c "run_hpgmg_bolt $LOG2DIM $NBOX $NWORKER"
mpirun -np 2 bash -c "run_hpgmg_iomp $LOG2DIM $NBOX $NWORKER"
