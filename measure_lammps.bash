#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)

source envs.bash

# We need to rebuild Argobots after running other experiments because some scripts may rebuild them in different settings.
./build_argobots.bash

cd lammps
./measure.bash

# Plot
../plot.bash plot_size.exs ../lammps_plot.html
