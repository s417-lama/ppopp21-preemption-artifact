#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)

source envs.bash

if "$USE_GCC"; then
  echo "This measurement is not configured to run with gcc."
  echo "Please fix some Intel-specific variables in 'lammps/measure.sh' and remove this check to run with gcc."
  echo "Variables to be fix: KMP_BLOCKTIME"
  exit 1
fi

if "$RUN_MEASUREMENT"; then
  # We need to rebuild Argobots after running other experiments because some scripts may rebuild them in different settings.
  ./build_argobots.bash

  (
  cd lammps
  ./measure.bash
  )
fi

if "$GENERATE_PLOT"; then
  # Plot
  (
  cd lammps
  ../plot.bash plot_size.exs ../lammps_plot.html
  )
fi
