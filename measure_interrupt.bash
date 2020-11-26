#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)

source envs.bash

cd preemption_benchmarks

if "$RUN_MEASUREMENT"; then
  JOBNAME=timer_cost_naive   ./measure_jobs/job_common.bash ./measure_jobs/timer_measure.bash
  JOBNAME=timer_cost_managed ./measure_jobs/job_common.bash ./measure_jobs/timer_measure.bash
  JOBNAME=timer_cost_central ./measure_jobs/job_common.bash ./measure_jobs/timer_measure.bash
  JOBNAME=timer_cost_chain   ./measure_jobs/job_common.bash ./measure_jobs/timer_measure.bash
fi

if "$GENERATE_PLOT"; then
  # Plot
  ../plot.bash plot/timer_sync_plot.exs ../interrupt_plot.html
fi
