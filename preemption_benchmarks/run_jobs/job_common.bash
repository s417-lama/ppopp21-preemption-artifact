#!/bin/bash
# set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)/..

JOBFILE=$1

if [[ -n $CPUFREQ ]]; then
    sudo pstate-frequency -S --turbo 1
    sudo cpupower -c all frequency-set -g performance -d $CPUFREQ -u $CPUFREQ
fi

hostname
lscpu
numactl -H
nproc

# export INTEL_DIR=/soft/compilers/intel/compilers_and_libraries_2016.2.181/linux
# export INTEL_DIR=/soft/compilers/intel/compilers_and_libraries_2016.3.210/linux
# export INTEL_DIR=/soft/compilers/intel/compilers_and_libraries_2016.3.223/linux
# export INTEL_DIR=/soft/compilers/intel/compilers_and_libraries_2017.0.098/linux
# export INTEL_DIR=/soft/compilers/intel/compilers_and_libraries_2017.1.132/linux
# export INTEL_DIR=/soft/compilers/intel/compilers_and_libraries_2017.2.174/linux
# export INTEL_DIR=/soft/compilers/intel/compilers_and_libraries_2017.4.196/linux
# export INTEL_DIR=/soft/compilers/intel/compilers_and_libraries_2018.0.128/linux
# export INTEL_DIR=/soft/compilers/intel/compilers_and_libraries_2018.1.163/linux
# export INTEL_DIR=/soft/compilers/intel-2019/compilers_and_libraries_2019.0.117/linux
# export INTEL_DIR=/soft/compilers/intel-2019/compilers_and_libraries_2019.3.199/linux
export INTEL_DIR=/soft/compilers/intel-2019/compilers_and_libraries_2019.4.243/linux

source ${INTEL_DIR}/bin/compilervars.sh intel64
icc --version

$JOBFILE
