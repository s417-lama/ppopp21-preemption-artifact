#!/bin/bash

PREFIX=cache

# MEM_ACCESS=seq_read
MEM_ACCESS=seq_read_write
# MEM_ACCESS=random_read
# MEM_ACCESS=random_read_write

# PREEMPTION_TYPE=yield
PREEMPTION_TYPE=new_es

P_INTERVALS="{1000, 1200, 1400, 1800, 2500, 4000, 10000}"

NUM_THREADS=(56 112 224 448 896)

job_name() {
    echo ${PREFIX}_${MEM_ACCESS}_${PREEMPTION_TYPE}
}

run_job() {
    CFLAGS="-DP_INTERVALS=\"$P_INTERVALS\""
    case $MEM_ACCESS in
        seq_read)
            CFLAGS="$CFLAGS -DRANDOM_ACCESS=0 -DREAD_WRITE=0"
            ITERATION=10000
            ;;
        random_read)
            CFLAGS="$CFLAGS -DRANDOM_ACCESS=1 -DREAD_WRITE=0"
            ITERATION=50
            ;;
        seq_read_write)
            CFLAGS="$CFLAGS -DRANDOM_ACCESS=0 -DREAD_WRITE=1"
            ITERATION=10000
            ;;
        random_read_write)
            CFLAGS="$CFLAGS -DRANDOM_ACCESS=1 -DREAD_WRITE=1"
            ITERATION=50
            ;;
        *)
            echo "error"
            exit
            ;;
    esac
    case $PREEMPTION_TYPE in
        yield)
            CFLAGS="$CFLAGS -DPREEMPTION_TYPE=ABT_PREEMPTION_YIELD"
            ;;
        new_es)
            CFLAGS="$CFLAGS -DPREEMPTION_TYPE=ABT_PREEMPTION_NEW_ES"
            ;;
        disabled)
            CFLAGS="$CFLAGS -DPREEMPTION_TYPE=ABT_PREEMPTION_DISABLED"
            ;;
        *)
            echo "error"
            exit
            ;;
    esac

    rm -f cache
    CC=icc CFLAGS=$CFLAGS make cache

    mv cache $EXEDIR
    cd $EXEDIR

    export ABT_INITIAL_NUM_SUB_XSTREAMS=10
    # export ABT_MEM_LP_ALLOC=mmap_rp

    N=10

    for key in ${!NUM_THREADS[@]}; do
        nthread=${NUM_THREADS[$key]}
        for i in $(seq 1 $N); do
            OUT_FILE=${JOBDIR}/nthread_${nthread}_iter_${ITERATION}_${i}.out
            ERR_FILE=${JOBDIR}/nthread_${nthread}_iter_${ITERATION}_${i}.err
            ./cache -x 56 -n $nthread -r 11 -g 56 -i $ITERATION -d 1000000 2> $ERR_FILE | tee $OUT_FILE
        done
    done
}
