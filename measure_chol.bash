#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)

source envs.bash

if "$USE_GCC"; then
  echo "This measurement cannot run without Intel compiler, OpenMP, and MKL."
  exit 1
fi

if "$RUN_MEASUREMENT"; then
  # We need to rebuild Argobots and BOLT after running other experiments because some scripts may rebuild them in different settings.
  ./build_argobots.bash
  ./build_bolt.bash

  (
  cd chol
  ./run.bash
  )
fi

if "$GENERATE_PLOT"; then
  # Plot
  (
  cd chol
  ../plot.bash plot/chol_plot.exs ../chol_plot.html
  )
fi
