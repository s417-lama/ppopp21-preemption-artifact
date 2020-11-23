#!/bin/bash

PREFIX=change_num_es

# EXEC_TYPE=pthread
# EXEC_TYPE=preemption
EXEC_TYPE=preemption_fair_random
# EXEC_TYPE=no_preemption

# P_INTVL=1000
P_INTVL=10000

job_name() {
    case $EXEC_TYPE in
        pthread)
            echo ${PREFIX}_pthread
            ;;
        preemption | preemption_fair_random)
            echo ${PREFIX}_${EXEC_TYPE}_${P_INTVL}
            ;;
        no_preemption)
            echo ${PREFIX}_no_preemption
            ;;
        *)
            echo "error"
            exit
            ;;
    esac
}

run_job() {
    rm -f change_num_es
    CC=icc make change_num_es

    export KMP_ABT_NUM_ESS=56
    export OMP_NUM_THREADS=56
    export ABT_SET_AFFINITY=1
    export KMP_ABT_WORK_STEAL_FREQ=4
    export KMP_ABT_FORK_NUM_WAYS=4
    export OMP_PROC_BIND=close,unset
    export ABT_THREAD_STACKSIZE=150000
    export ABT_MEM_MAX_NUM_STACKS=80
    export ABT_MEM_LP_ALLOC=malloc

    case $EXEC_TYPE in
        pthread)
            export LD_PRELOAD=${INTEL_DIR}/compiler/lib/intel64/libiomp5.so
            export KMP_BLOCKTIME=0
            export KMP_AFFINITY="proclist=[0-55],explicit,granularity=fine"
            change_num_workers() {
                local pid=$1
                local w=$2
                taskset -acp 0-$((w - 1)) $pid > /dev/null
            }
            ;;
        preemption | preemption_fair_random)
            change_num_workers() {
                local pid=$1
                local w=$2
                $SIGQUEUE $pid $w
            }
            ;;
        no_preemption)
            change_num_workers() {
                local pid=$1
                local w=$2
                $SIGQUEUE $pid $w
            }
            ;;
        *)
            echo "error"
            exit
            ;;
    esac

    mv change_num_es $EXEDIR
    cd $EXEDIR

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

    export ABT_INITIAL_NUM_SUB_XSTREAMS=40

    N=10
    SLEEP_TIME=0.2

    # N_WORKERS=$(seq 28 55)
    N_WORKERS=$(seq 28 3 55)
    for w in ${N_WORKERS[@]}; do
        for i in $(seq 1 $N); do
            OUT_FILE=${JOBDIR}/w_${w}_${i}.out
            ERR_FILE=${JOBDIR}/w_${w}_${i}.err
            echo "## 56 -> $w"

            ./change_num_es -r 10 -t $P_INTVL -i 100000000 2> $ERR_FILE > $OUT_FILE &
            PID=$!

            sleep $SLEEP_TIME
            change_num_workers $PID $w
            wait
            cat $OUT_FILE
        done
    done
}
