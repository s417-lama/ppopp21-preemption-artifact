#!/bin/bash
set -euo pipefail
export LC_ALL=C
export LANG=C

singularity run docker://s417lama/plotly_ex "$@"
