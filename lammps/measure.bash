#!/bin/bash
set -euo pipefail
export LC_ALL=C
export LANG=C

cd -P $(dirname $0)/

export HFI_NO_CPUAFFINITY=yes
export LAMMPS_ABT_NUM_XSTREAMS=$N_CORES
export LAMMPS_ABT_NUM_THREADS=$N_CORES

# export LAMMPS_MEASURE_BUSYTIME=1

N_REPEATS=1
# N_REPEATS=10

ANALYSIS_INTVLS=(1 2)
# ANALYSIS_THREADS=(55 110 220 440 880 1760 3520 7040 14080)
# SIZES=(8)
ANALYSIS_THREADS=($((N_CORES - 1)))
SIZES=(2 4 6 8 10 12)

# RESULT_APPEND=1
RESULT_APPEND=0

RESULT_DIR=${RESULT_DIR:-${PWD}/results}
if [[ $RESULT_APPEND -eq 1 ]]; then
  JOB_DIR=${RESULT_DIR}/latest
else
  DATETIME=${DATETIME:-$(date +%Y-%m-%d_%H-%M-%S)}
  JOB_DIR=${RESULT_DIR}/${DATETIME}
fi

mkdir -p $JOB_DIR

cd src

export I_MPI_CXX=icpc
make yes-kokkos
make kokkos_abt -j
make kokkos_omp -j

run_lammps_omp() {
  local NAME=$1
  for intvl in ${ANALYSIS_INTVLS[@]}; do
    for thread in ${ANALYSIS_THREADS[@]}; do
      for size in ${SIZES[@]}; do
        for i in $(seq 1 $N_REPEATS); do
          export LAMMPS_ANALYSIS_INTERVAL=$intvl
          export LAMMPS_NUM_ANALYSIS_THREADS=$thread

          export OMP_PROC_BIND=true
          export KMP_BLOCKTIME=0

          # local OUT_FILE=${JOB_DIR}/omp_${NAME}_intvl_${intvl}_thread_${thread}_size_${size}_${i}.out
          # local ERR_FILE=${JOB_DIR}/omp_${NAME}_intvl_${intvl}_thread_${thread}_size_${size}_${i}.err
          local OUT_FILE=${JOB_DIR}/omp_${NAME}_intvl_${intvl}_size_${size}_${i}.out
          local ERR_FILE=${JOB_DIR}/omp_${NAME}_intvl_${intvl}_size_${size}_${i}.err

          numactl -iall mpirun ./lmp_kokkos_omp -in ../bench/in.lj -var x $size -var y $size -var z $size -k on t $N_CORES -sf kk -pk kokkos newton off neigh full 2> $ERR_FILE | tee $OUT_FILE
        done
      done
    done
  done
}

run_lammps_abt() {
  local NAME=$1
  for intvl in ${ANALYSIS_INTVLS[@]}; do
    for thread in ${ANALYSIS_THREADS[@]}; do
      for size in ${SIZES[@]}; do
        for i in $(seq 1 $N_REPEATS); do
          export LAMMPS_ANALYSIS_INTERVAL=$intvl
          export LAMMPS_NUM_ANALYSIS_THREADS=$thread

          # local OUT_FILE=${JOB_DIR}/abt_${NAME}_intvl_${intvl}_thread_${thread}_size_${size}_${i}.out
          # local ERR_FILE=${JOB_DIR}/abt_${NAME}_intvl_${intvl}_thread_${thread}_size_${size}_${i}.err
          local OUT_FILE=${JOB_DIR}/abt_${NAME}_intvl_${intvl}_size_${size}_${i}.out
          local ERR_FILE=${JOB_DIR}/abt_${NAME}_intvl_${intvl}_size_${size}_${i}.err

          numactl -iall mpirun ./lmp_kokkos_abt -in ../bench/in.lj -var x $size -var y $size -var z $size -k on t $N_CORES -sf kk -pk kokkos newton off neigh full 2> $ERR_FILE | tee $OUT_FILE
        done
      done
    done
  done
}

# export LAMMPS_ENABLE_ANALYSIS=0
# export LAMMPS_ASYNC_ANALYSIS=0
# run_lammps_omp no_analysis

export LAMMPS_ENABLE_ANALYSIS=1
export LAMMPS_ASYNC_ANALYSIS=1
run_lammps_omp async

export LAMMPS_ENABLE_ANALYSIS=1
export LAMMPS_ASYNC_ANALYSIS=1
export LAMMPS_ANALYSIS_PTHREAD_NICE=19
run_lammps_omp async_nice
unset LAMMPS_ANALYSIS_PTHREAD_NICE

export LAMMPS_ENABLE_ANALYSIS=0
export LAMMPS_ASYNC_ANALYSIS=0
export LAMMPS_ABT_ENABLE_PREEMPTION=0
run_lammps_abt no_analysis

export LAMMPS_ENABLE_ANALYSIS=1
export LAMMPS_ASYNC_ANALYSIS=0
export LAMMPS_ABT_ENABLE_PREEMPTION=0
run_lammps_abt sync

# export LAMMPS_ENABLE_ANALYSIS=1
# export LAMMPS_ASYNC_ANALYSIS=1
# export LAMMPS_ABT_ENABLE_PREEMPTION=0
# run_lammps_abt async

export LAMMPS_ENABLE_ANALYSIS=1
export LAMMPS_ASYNC_ANALYSIS=1
export LAMMPS_ABT_ENABLE_PREEMPTION=1
export ABT_PREEMPTION_INTERVAL_USEC=1000
run_lammps_abt preemption

if [[ $RESULT_APPEND -eq 0 ]]; then
  ln -sfn ${JOB_DIR} ${RESULT_DIR}/latest
fi
