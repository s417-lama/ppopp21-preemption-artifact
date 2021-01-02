#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)/..

JOBFILE=$1
. $JOBFILE

JOBNAME=$(job_name)

DATETIME=${DATETIME:-$(date +%Y-%m-%d_%H-%M-%S)}
RESULT_DIR=${RESULT_DIR:-${PWD}/results/${JOBNAME}}
# EXEDIR=${PWD}/tmp/$(uuidgen)
EXEDIR=$(mktemp -d)
JOBDIR=${RESULT_DIR}/${DATETIME}

mkdir -p $EXEDIR
trap "rm -rf $EXEDIR" EXIT

mkdir -p $JOBDIR

run_job

ln -sfn ${JOBDIR} ${RESULT_DIR}/latest
