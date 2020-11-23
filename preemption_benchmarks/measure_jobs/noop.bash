#!/bin/bash

# ---------- Prefix of result dir ----------
PREFIX=noop
# PREFIX=noop_knl

# ---------- Pthread or Argobots (preemptive/non-preemptive) ----------
# EXEC_TYPE=pthread
# EXEC_TYPE=preemption
# EXEC_TYPE=no_preemption # preemption is disabled on Argobots (baseline)

# ---------- M:N thread type ----------
# PREEMPTION_TYPE=yield # signal-yield
# PREEMPTION_TYPE=klts # KLT-switching (without optimizations)
# PREEMPTION_TYPE=klts_futex # KLT-switching (with futex)
# PREEMPTION_TYPE=klts_futex_local # KLT-switching (with futex and local pools)
# PREEMPTION_TYPE=disabled # Non-preemptive threads (timer interruption only)

# ---------- # of workers ----------
# NWORKERS=56 # Skylake
# NWORKERS=68 # KNL

# ---------- Preemption timer type ----------
TIMER_TYPE=signal
# TIMER_TYPE=dedicated # ignore it

# ---------- Preemption groups ----------
# NGROUPS=1 # per-process timer
# NGROUPS=2
# NGROUPS=4
# NGROUPS=8
# NGROUPS=14
# NGROUPS=28
NGROUPS=$NWORKERS # per-worker timer

# ---------- Preemption intervals ----------
PREEMPTION_INTVLS=(150 200 300 500 700 1000 1500 2000 3000 5000 7000 10000)

# ---------- # of iterations for a loop in each ULT ----------

NLOOP=30000000 # adjusted for Skylake
# NLOOP=5000000 # adjusted for KNL

# ---------- # of repeats ----------
NREPEAT=1
# NREPEAT=10

job_name() {
    case $EXEC_TYPE in
        pthread)
            echo ${PREFIX}_pthread
            ;;
        preemption)
            echo ${PREFIX}_preemption_${PREEMPTION_TYPE}
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
    rm -f noop

    CFLAGS=""
    case $EXEC_TYPE in
        pthread)
            CFLAGS="$CFLAGS -DUSE_PTHREAD=1 -DPTHREAD_AFFINITY_TYPE=PTHREAD_AFFINITY_FIX"
            ;;
        preemption)
            CFLAGS="$CFLAGS -DUSE_PTHREAD=0 -DENABLE_PREEMPTION=1"
            case $PREEMPTION_TYPE in
                yield)
                    CFLAGS="$CFLAGS -DPREEMPTION_TYPE=ABT_PREEMPTION_YIELD"
                    ;;
                klts | klts_futex | klts_futex_local)
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
            ;;
        no_preemption)
            CFLAGS="$CFLAGS -DUSE_PTHREAD=0 -DENABLE_PREEMPTION=0"
            ;;
        *)
            echo "error"
            exit
            ;;
    esac
    CC=icc CFLAGS=$CFLAGS make noop

    mv noop $EXEDIR
    cd $EXEDIR

    export ABT_INITIAL_NUM_SUB_XSTREAMS=40
    export ABT_PREEMPTION_TIMER_TYPE=$TIMER_TYPE

    if [[ "$EXEC_TYPE" != preemption ]]; then
        PREEMPTION_INTVLS=(1)
    fi
    for intvl in ${PREEMPTION_INTVLS[@]}; do
        for i in $(seq 1 $NREPEAT); do
            echo "## Preemption interval = $intvl us ($i / $NREPEAT)"
            OUT_FILE=${JOBDIR}/i_${intvl}_${i}.out
            ERR_FILE=${JOBDIR}/i_${intvl}_${i}.err
            ./noop -x $NWORKERS -n $((NWORKERS * 10)) -r 11 -i $NLOOP -g $NGROUPS -t $intvl 2> $ERR_FILE | tee $OUT_FILE
        done
    done
}
