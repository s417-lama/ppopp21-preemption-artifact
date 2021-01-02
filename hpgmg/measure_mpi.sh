#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)

APPEND_RESULT=false
# APPEND_RESULT=true

LOG2DIM=8
NBOX=8

# EXEC_TYPE=iomp
# EXEC_TYPE=bolt
# EXEC_TYPE=bolt_no_preemption
# EXEC_TYPE=bolt_fixed

# P_INTVL=1000
# P_INTVL=10000

NTHREADS=$((N_CORES / N_SOCKETS))
# NTHREADS=28

NPROCESS=$N_SOCKETS
# NPROCESS=2

N=1
# N_WORKERS=$(seq 4 $NTHREADS)
# N_WORKERS=$(seq 4 2 $NTHREADS)
N_WORKERS=$(seq 4 4 $NTHREADS)

# export I_MPI_PIN_DOMAIN="[fffffff,fffffff0000000]" # [0-27,28-55]; used for our experiments
export I_MPI_PIN_DOMAIN=socket

export SLEEP_TIME=3
export NTHREADS

case $EXEC_TYPE in
  iomp)
    JOBNAME=hpgmg_mpi_${LOG2DIM}_${NBOX}_iomp
    ;;
  bolt)
    JOBNAME=hpgmg_mpi_${LOG2DIM}_${NBOX}_bolt_${P_INTVL}
    ;;
  bolt_no_preemption)
    JOBNAME=hpgmg_mpi_${LOG2DIM}_${NBOX}_bolt_no_preemption
    ;;
  bolt_fixed)
    JOBNAME=hpgmg_mpi_${LOG2DIM}_${NBOX}_bolt_fixed
    ;;
  *)
    echo "error"
    exit
    ;;
esac

DATETIME=${DATETIME:-$(date +%Y-%m-%d_%H-%M-%S)}
RESULT_DIR=${RESULT_DIR:-${PWD}/results/${JOBNAME}}
if "$APPEND_RESULT"; then
  JOBDIR=${RESULT_DIR}/latest
else
  JOBDIR=${RESULT_DIR}/${DATETIME}
  mkdir -p $JOBDIR
fi

./configure --CFLAGS=-fopenmp --CC=mpicc
make clean
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
export ABT_SET_AFFINITY=1
export KMP_ABT_FORK_NUM_WAYS=4

case $EXEC_TYPE in
  iomp)
    run_hpgmg() {
      local log2dim=$1
      local nbox=$2
      local w=$3
      local nt=$NTHREADS
      local base=$((nt * PMI_RANK))

      export KMP_BLOCKTIME=0
      export KMP_AFFINITY="proclist=[${base}-$((base + nt - 1))],explicit,granularity=fine"

      ./build/bin/hpgmg-fv $log2dim $nbox &
      local pid=$!

      sleep $SLEEP_TIME
      taskset -acp ${base}-$((base + w - 1)) $pid > /dev/null
      # taskset -acp $pid | awk "\$6 >= $w { print \$2 }" | cut -d "'" -f 1 | xargs -I{} taskset -cp ${base}-$((base + w - 1)) {} > /dev/null

      wait
    }
    ;;
  bolt | bolt_no_preemption)
    export ABT_PREEMPTION_INTERVAL_USEC=$P_INTVL
    run_hpgmg() {
      local log2dim=$1
      local nbox=$2
      local w=$3

      LD_PRELOAD=../opt/bolt/lib/libiomp5.so ./build/bin/hpgmg-fv $log2dim $nbox &
      local pid=$!

      sleep $SLEEP_TIME
      $SIGQUEUE_CMD $pid $w

      wait
    }
    ;;
  bolt_fixed)
    export ABT_PREEMPTION_INTERVAL_USEC=10000000000
    run_hpgmg() {
      local log2dim=$1
      local nbox=$2
      local w=$3

      export KMP_ABT_NUM_ESS=$w
      export OMP_NUM_THREADS=$w

      LD_PRELOAD=../opt/bolt/lib/libiomp5.so ./build/bin/hpgmg-fv $log2dim $nbox
    }
    ;;
  *)
    echo "error"
    exit
    ;;
esac
export -f run_hpgmg

for w in ${N_WORKERS[@]}; do
  for i in $(seq 1 $N); do
    OUT_FILE=${JOBDIR}/w_${w}_${i}.out
    ERR_FILE=${JOBDIR}/w_${w}_${i}.err
    echo "## $NTHREADS -> $w ($i / $N)"

    # mpirun -np $NPROCESS bash -c "run_hpgmg $LOG2DIM $NBOX $w" 2> $ERR_FILE | tee $OUT_FILE
    mpirun -np $NPROCESS bash -c "$(declare -pf run_hpgmg); run_hpgmg $LOG2DIM $NBOX $w" 2> $ERR_FILE | tee $OUT_FILE
  done
done

if ! "$APPEND_RESULT"; then
  ln -sfn ${JOBDIR} ${RESULT_DIR}/latest
fi
