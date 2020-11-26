#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)

source envs.bash

if "$USE_GCC"; then
  echo "This measurement is not configured to run with gcc."
  echo "Please fix some Intel-specific variables in 'hpgmg/measure_mpi.sh' and remove this check to run with gcc."
  echo "Variables to be fix: I_MPI_PIN_DOMAIN, PMI_RANK, KMP_BLOCKTIME, KMP_AFFINITY"
  exit 1
fi

if "$RUN_MEASUREMENT"; then
  # We need to rebuild Argobots and BOLT after running other experiments because some scripts may rebuild them in different settings.
  ./build_argobots.bash
  CXXFLAGS="-DFAIR_SCHED=1" ./build_bolt.bash

  (
  cd hpgmg
  EXEC_TYPE=iomp ./measure_mpi.sh
  )

  (
  cd hpgmg
  EXEC_TYPE=bolt P_INTVL=1000 ./measure_mpi.sh
  )

  (
  cd hpgmg
  EXEC_TYPE=bolt P_INTVL=10000 ./measure_mpi.sh
  )

  # Rebuild BOLT to disable preemption
  CXXFLAGS="-DFAIR_SCHED=1 -DENABLE_PREEMPTION=0" ./build_bolt.bash

  (
  cd hpgmg
  EXEC_TYPE=bolt_no_preemption ./measure_mpi.sh
  )

  (
  cd hpgmg
  EXEC_TYPE=bolt_fixed ./measure_mpi.sh
  )
fi

if "$GENERATE_PLOT"; then
  # Plot
  (
  cd hpgmg
  ../plot.bash plot.exs ../hpgmg_plot.html
  )
fi
