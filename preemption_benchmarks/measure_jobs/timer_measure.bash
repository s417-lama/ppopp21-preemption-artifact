#!/bin/bash

# JOBNAME=timer_cost_naive
# JOBNAME=timer_cost_sync
# JOBNAME=timer_cost_managed
# JOBNAME=timer_cost_central
# JOBNAME=timer_cost_chain
# JOBNAME=timer_cost_dedicated

job_name() {
    echo $JOBNAME
}

run_job() {
    make clean
    make timer_measure

    N=1
    # N=10
    # THREADS=(1 4 8 12 16 20 24 28 32 36 40 44 48 52 56)
    THREADS=(1 2 4 8 14 28 56)
    for thread in ${THREADS[@]}; do
        for i in $(seq 1 $N); do
            OUT_FILE=${JOBDIR}/t_${thread}_${i}.out
            ERR_FILE=${JOBDIR}/t_${thread}_${i}.err
            case $JOBNAME in
                timer_cost_naive)
                    ./timer_measure -n ${thread} -t ${thread} -i 1000
                    ;;
                timer_cost_sync)
                    ./timer_measure -n ${thread} -t ${thread} -s 1 -i 1000
                    ;;
                timer_cost_managed)
                    ./timer_measure -n ${thread} -t ${thread} -m 1 -i 1000
                    ;;
                timer_cost_central)
                    ./timer_measure -n ${thread} -t 1 -i 1000
                    ;;
                timer_cost_chain)
                    ./timer_measure -n ${thread} -t 1 -c 1 -i 1000
                    ;;
                timer_cost_dedicated)
                    ./timer_measure -n ${thread} -t ${thread} -d 1 -i 1000
                    ;;
                *)
                    echo "no job found."
                    ;;
            esac 2> $ERR_FILE | tee $OUT_FILE
        done
    done
}
