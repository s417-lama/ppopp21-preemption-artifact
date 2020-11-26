#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)

source envs.bash

if "$RUN_MEASUREMENT"; then
  # We need to rebuild Argobots after running other experiments because some scripts may rebuild them in different settings.
  ./build_argobots.bash

  cd preemption_benchmarks
  ./run_jobs/preemption_cost2.bash
fi
