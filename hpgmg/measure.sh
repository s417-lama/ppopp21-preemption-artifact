#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)

APPEND_RESULT=false
# APPEND_RESULT=true

NTHREADS=28
LOG2DIM=8
NBOX=16

# EXEC_TYPE=iomp
EXEC_TYPE=bolt
# EXEC_TYPE=bolt_no_preemption
# EXEC_TYPE=bolt_fixed

# P_INTVL=1000
P_INTVL=10000

N=1
N_WORKERS=$(seq 4 2 28)

case $EXEC_TYPE in
  iomp)
    JOBNAME=hpgmg_${LOG2DIM}_${NBOX}_th_${NTHREADS}_iomp
    ;;
  bolt)
    JOBNAME=hpgmg_${LOG2DIM}_${NBOX}_th_${NTHREADS}_bolt_${P_INTVL}
    ;;
  bolt_no_preemption)
    JOBNAME=hpgmg_${LOG2DIM}_${NBOX}_th_${NTHREADS}_bolt_no_preemption
    ;;
  bolt_fixed)
    JOBNAME=hpgmg_${LOG2DIM}_${NBOX}_th_${NTHREADS}_bolt_fixed
    ;;
  *)
    echo "error"
    exit
    ;;
esac

DATETIME=${DATETIME:-$(TZ='America/Chicago' date +%Y-%m-%d_%H-%M-%S)}
RESULT_DIR=${RESULT_DIR:-${PWD}/results/${JOBNAME}}
if "$APPEND_RESULT"; then
  JOBDIR=${RESULT_DIR}/latest
else
  JOBDIR=${RESULT_DIR}/${DATETIME}
  mkdir -p $JOBDIR
fi

if [[ -n $CPUFREQ ]]; then
  sudo pstate-frequency -S --turbo 1
  sudo cpupower -c all frequency-set -g performance -d $CPUFREQ -u $CPUFREQ
fi

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
export ABT_SET_AFFINITY=1
export KMP_ABT_FORK_NUM_WAYS=4

case $EXEC_TYPE in
  iomp)
    export KMP_BLOCKTIME=0
    export KMP_AFFINITY="proclist=[0-$((NTHREADS - 1))],explicit,granularity=fine"
    change_num_workers() {
      local pid=$1
      local w=$2
      taskset -acp 0-$((w - 1)) $pid > /dev/null
      # taskset -acp $pid | awk "\$6 >= $w { print \$2 }" | cut -d "'" -f 1 | xargs -I{} taskset -cp 0-$((w - 1)) {} > /dev/null
    }
    ;;
  bolt | bolt_no_preemption)
    export LD_PRELOAD=$PWD/myopt/bolt/lib/libiomp5.so
    export ABT_PREEMPTION_INTERVAL_USEC=$P_INTVL
    change_num_workers() {
      local pid=$1
      local w=$2
      $SIGQUEUE $pid $w
    }
    ;;
  bolt_fixed)
    export LD_PRELOAD=$PWD/myopt/bolt/lib/libiomp5.so
    export ABT_PREEMPTION_INTERVAL_USEC=10000000000
    change_num_workers() { :; }
    ;;
  *)
    echo "error"
    exit
    ;;
esac

SLEEP_TIME=10

for w in ${N_WORKERS[@]}; do
  for i in $(seq 1 $N); do
    OUT_FILE=${JOBDIR}/w_${w}_${i}.out
    ERR_FILE=${JOBDIR}/w_${w}_${i}.err
    echo "## $NTHREADS -> $w ($i / $N)"

    if [[ "$EXEC_TYPE" == bolt_fixed ]]; then
      export KMP_ABT_NUM_ESS=$w
      export OMP_NUM_THREADS=$w
    fi

    ./build/bin/hpgmg-fv $LOG2DIM $NBOX 2> $ERR_FILE > $OUT_FILE &
    PID=$!

    sleep $SLEEP_TIME
    change_num_workers $PID $w
    wait

    cat $OUT_FILE
  done
done

if ! "$APPEND_RESULT"; then
  ln -sfn ${JOBDIR} ${RESULT_DIR}/latest
fi
