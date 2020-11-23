#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)

source envs.bash

# We need to rebuild Argobots and BOLT after running other experiments because some scripts may rebuild them in different settings.
./build_argobots.bash
./build_bolt.bash

cd chol
./run.bash

# Plot
../plot.bash plot/chol_plot.exs ../chol_plot.html
