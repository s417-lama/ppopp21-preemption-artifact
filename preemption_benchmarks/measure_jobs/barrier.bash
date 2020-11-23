#!/bin/bash

# PREFIX=barrier
PREFIX=barrier_knl

# BARRIER_TYPE=pthread
BARRIER_TYPE=busywait
# BARRIER_TYPE=busywait_yield
# BARRIER_TYPE=no_barrier

# PREEMPTION_TYPE=pthread
# PREEMPTION_TYPE=yield
PREEMPTION_TYPE=new_es
# PREEMPTION_TYPE=new_es_suspention
# PREEMPTION_TYPE=new_es_suspention_futex

job_name() {
    echo ${PREFIX}_${BARRIER_TYPE}_${PREEMPTION_TYPE}
}

run_job() {
    rm -f barrier

    CFLAGS=""
    case $BARRIER_TYPE in
        pthread)
            CFLAGS="$CFLAGS -DBARRIER_TYPE=PTHREAD_BARRIER"
            ;;
        busywait)
            CFLAGS="$CFLAGS -DBARRIER_TYPE=BUSYWAIT_BARRIER"
            ;;
        busywait_yield)
            CFLAGS="$CFLAGS -DBARRIER_TYPE=BUSYWAIT_YIELD_BARRIER"
            ;;
        no_barrier)
            CFLAGS="$CFLAGS -DBARRIER_TYPE=NO_BARRIER"
            ;;
        *)
            echo "error"
            ;;
    esac
    case $PREEMPTION_TYPE in
        pthread)
            CFLAGS="$CFLAGS -DUSE_PTHREAD=1 -DPTHREAD_AFFINITY_TYPE=PTHREAD_AFFINITY_FIX"
            ;;
        yield)
            CFLAGS="$CFLAGS -DUSE_PTHREAD=0 -DENABLE_PREEMPTION=1 -DPREEMPTION_TYPE=ABT_PREEMPTION_YIELD"
            ;;
        new_es)
            CFLAGS="$CFLAGS -DUSE_PTHREAD=0 -DENABLE_PREEMPTION=1 -DPREEMPTION_TYPE=ABT_PREEMPTION_NEW_ES"
            ;;
        new_es_suspention) # no option now
            CFLAGS="$CFLAGS -DUSE_PTHREAD=0 -DENABLE_PREEMPTION=1 -DPREEMPTION_TYPE=ABT_PREEMPTION_NEW_ES"
            ;;
        new_es_suspention_futex) # no option now
            CFLAGS="$CFLAGS -DUSE_PTHREAD=0 -DENABLE_PREEMPTION=1 -DPREEMPTION_TYPE=ABT_PREEMPTION_NEW_ES"
            ;;
        *)
            echo "error"
            ;;
    esac
    CC=icc CFLAGS=$CFLAGS make barrier

    mv barrier $EXEDIR
    cd $EXEDIR

    export ABT_INITIAL_NUM_SUB_XSTREAMS=40

    N=1
    if [[ "$PREEMPTION_TYPE" == pthread || "$BARRIER_TYPE" == no_barrier ]]; then
        INTVLS=(10000000)
    else
        # INTVLS=(100 150 200 300 500 700 1000 1500 2000 3000 5000 7000 10000)
        # INTVLS=(150 200 300 500 700 1000 1500 2000 3000 5000 7000 10000)
        INTVLS=(200 300 500 700 1000 1500 2000 3000 5000 7000 10000)
        # INTVLS=(300 500 700 1000 1500 2000 3000 5000 7000 10000)
    fi
    for intvl in ${INTVLS[@]}; do
        for i in $(seq 1 $N); do
            OUT_FILE=${JOBDIR}/i_${intvl}_${i}.out
            ERR_FILE=${JOBDIR}/i_${intvl}_${i}.err
            # ./barrier -x 56 -n 560 -r 11 -g 56 -b 1000 -t $intvl 2> $ERR_FILE | tee $OUT_FILE
            ./barrier -x 68 -n 680 -r 11 -g 68 -b 1000 -t $intvl 2> $ERR_FILE | tee $OUT_FILE
        done
    done
}
