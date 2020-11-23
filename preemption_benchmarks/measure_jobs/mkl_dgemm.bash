#!/bin/bash

PREFIX=mkl_dgemm

OMP_RUNTIME=bolt
# OMP_RUNTIME=iomp

# PATCH=1
PATCH=0

# PREEMPTION_INTVL=0
# PREEMPTION_INTVL=1000
PREEMPTION_INTVL=4000
# PREEMPTION_INTVL=10000

OUTER=32
SIZE=4000

job_name() {
    if [[ "$OMP_RUNTIME" == bolt ]]; then
        echo ${PREFIX}_${OMP_RUNTIME}_patch_${PATCH}_intvl_${PREEMPTION_INTVL}_outer_${OUTER}_size_${SIZE}
    else
        echo ${PREFIX}_${OMP_RUNTIME}_outer_${OUTER}_size_${SIZE}
    fi
}

run_job() {
    rm -f mkl_dgemm libmklyield_skylake.so libmklyield_skylake_sched.so

    CC=icc make mkl_dgemm
    CC=icc make patch

    export ABT_INITIAL_NUM_SUB_XSTREAMS=40

    WARMUP=2
    REPEAT=10
    INNERS=(4 8 16 32)
    N=1

    if [[ "$OMP_RUNTIME" == bolt ]]; then
        if [[ "$PATCH" == 1 ]]; then
            OMP_LIB=$(pwd)/libmklyield_skylake.so
        else
            OMP_LIB=$(pwd)/libmklyield_skylake_sched.so
        fi
        if [[ "$PREEMPTION_INTVL" == 0 ]]; then
            export ABT_PREEMPTION_INTERVAL_USEC=100000000
        else
            export ABT_PREEMPTION_INTERVAL_USEC=$PREEMPTION_INTVL
        fi
        export KMP_ABT_WORK_STEAL_FREQ=4 KMP_ABT_FORK_CUTOFF=4 KMP_ABT_FORK_NUM_WAYS=4 ABT_SET_AFFINITY=1 KMP_BLOCKTIME=0 KMP_HOT_TEAMS_MAX_LEVEL=2 KMP_HOT_TEAMS_MODE=1 OMP_PROC_BIND=close,unset KMP_ABT_NUM_ESS=56 ABT_THREAD_STACKSIZE=150000 ABT_MEM_MAX_NUM_STACKS=80 ABT_MEM_LP_ALLOC=malloc MKL_DYNAMIC=false
    else
        OMP_LIB=${INTEL_DIR}/compiler/lib/intel64/libiomp5.so
        export KMP_BLOCKTIME=0 KMP_HOT_TEAMS_MAX_LEVEL=2 KMP_HOT_TEAMS_MODE=1 OMP_NESTED=true MKL_DYNAMIC=false
    fi

    for inner in ${INNERS[@]}; do
        for i in $(seq 1 $N); do
            OUT_FILE=${JOBDIR}/inner_${inner}_${i}.out
            ERR_FILE=${JOBDIR}/inner_${inner}_${i}.err
            export MKL_NUM_THREADS=$inner
            LD_PRELOAD=$OMP_LIB numactl -iall ./mkl_dgemm $OUTER $REPEAT $WARMUP $SIZE $SIZE $SIZE 2> $ERR_FILE | tee $OUT_FILE
        done
    done
}
