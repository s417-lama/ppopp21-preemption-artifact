#!/bin/bash
# set -euo pipefail
export LC_ALL=C
export LANG=C

HOST=$1
TRACE_FILE=${2:-"\${HOME}/playground/results/latest.err"}

ssh -O forward -L 5006:localhost:5006 $HOST
ssh $HOST "./massivelogger/run_viewer.bash $TRACE_FILE" 2>&1 |
    tee /dev/stderr |
    stdbuf -oL grep "Bokeh app running at" |
    xargs -I{} xdg-open http://localhost:5006
ssh -O cancel -L 5006:localhost:5006 $HOST
