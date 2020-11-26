#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

make clean
CFLAGS="-DUSE_PTHREAD=1" make preemption_cost2
echo ""
echo "## Pthread"
taskset -c 0 ./preemption_cost2 -x 1 -g 1 -r 1 -p 1000

make clean
CFLAGS="-DPREEMPTION_TYPE=ABT_PREEMPTION_YIELD" make preemption_cost2
echo ""
echo "## Argobots (signal-yield)"
taskset -c 0 ./preemption_cost2 -x 1 -g 1 -r 1 -p 1000 -t 10000

make clean
CFLAGS="-DPREEMPTION_TYPE=ABT_PREEMPTION_NEW_ES" make preemption_cost2
echo ""
echo "## Argobots (KLT-switching)"
taskset -c 0 ./preemption_cost2 -x 1 -g 1 -r 1 -p 1000 -t 10000
