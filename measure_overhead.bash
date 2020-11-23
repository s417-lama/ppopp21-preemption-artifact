#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)

source envs.bash

# Build Argobots (with futex and local pool)
./build_argobots.bash

(
cd preemption_benchmarks
export EXEC_TYPE=no_preemption NWORKERS=$N_CORES
./measure_jobs/job_common.bash ./measure_jobs/noop.bash
)

(
cd preemption_benchmarks
export EXEC_TYPE=preemption PREEMPTION_TYPE=disabled NWORKERS=$N_CORES
./measure_jobs/job_common.bash ./measure_jobs/noop.bash
)

(
cd preemption_benchmarks
export EXEC_TYPE=preemption PREEMPTION_TYPE=yield NWORKERS=$N_CORES
./measure_jobs/job_common.bash ./measure_jobs/noop.bash
)

(
cd preemption_benchmarks
export EXEC_TYPE=preemption PREEMPTION_TYPE=klts_futex_local NWORKERS=$N_CORES
./measure_jobs/job_common.bash ./measure_jobs/noop.bash
)

# Build Argobots (without local pool)
CFLAGS="-DABT_SUB_XSTREAM_LOCAL_POOL=0" ./build_argobots.bash

(
cd preemption_benchmarks
export EXEC_TYPE=preemption PREEMPTION_TYPE=klts_futex NWORKERS=$N_CORES
./measure_jobs/job_common.bash ./measure_jobs/noop.bash
)

# Build Argobots (without local pool and futex)
CFLAGS="-DABT_SUB_XSTREAM_LOCAL_POOL=0 -DABT_USE_FUTEX_ON_SLEEP=0" ./build_argobots.bash

(
cd preemption_benchmarks
export EXEC_TYPE=preemption PREEMPTION_TYPE=klts NWORKERS=$N_CORES
./measure_jobs/job_common.bash ./measure_jobs/noop.bash
)

# Plot
(
cd preemption_benchmarks
../plot.bash ./plot/noop_plot.exs ../overhead_plot.html
)
