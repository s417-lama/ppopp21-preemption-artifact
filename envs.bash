#!/bin/bash

# location of Intel compiler package
export INTEL_DIR=/soft/compilers/intel-2019/compilers_and_libraries_2019.4.243/linux

# machine config: number of cores per node
export N_CORES=56

# machine config: number of sockets per node
export N_SOCKETS=2

# root directory, which must point to the top directory of the artifact
ROOT_DIR=$(cd $(dirname -- $0) && pwd -P)

# install directory (default: ROOT_DIR/opt)
INSTALL_DIR=${ROOT_DIR}/opt

################################################################

# check root directory
if test ! -d $ROOT_DIR/argobots ; then
  echo "Error: incorrect root directory. Check ROOT_DIR=${ROOT_DIR}"
  exit 1
fi

# move to ROOT_DIR
cd $ROOT_DIR

# load intel compilers
source ${INTEL_DIR}/bin/compilervars.sh intel64
if test x"$(which icc 2>/dev/null)" = 'x' ; then
  echo "Error: icc is not found.  Check INTEL_DIR=${INTEL_DIR}"
  exit 1
fi
