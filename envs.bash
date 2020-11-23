#!/bin/bash

ROOT_DIR=$(cd $(dirname $0) && pwd -P)
cd $ROOT_DIR

INSTALL_DIR=${ROOT_DIR}/opt

# load intel compilers
export INTEL_DIR=/soft/compilers/intel-2019/compilers_and_libraries_2019.4.243/linux
source ${INTEL_DIR}/bin/compilervars.sh intel64

# machine config
export N_CORES=56
export N_SOCKETS=2
