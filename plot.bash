#!/bin/bash
set -euo pipefail
export LC_ALL=C
export LANG=C

if type singularity > /dev/null 2>&1; then
  singularity run docker://s417lama/plotly_ex "$@"
elif type docker > /dev/null 2>&1; then
  docker run --rm -v $HOME:$HOME -w $PWD s417lama/plotly_ex "$@"
else
  echo "Note: singularity or docker must be installed to generate plots."
  echo ""
  echo "To generate plots locally, download the 'results' dir in each subdirectory (e.g., 'preemption_benchmarks/results') by 'scp -r' command and run './measure_XXX --only-plots' in your machine with singularity or docker installed."
fi
