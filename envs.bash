#!/bin/bash

# machine config: number of cores per node
export N_CORES=56

# machine config: number of sockets per node
export N_SOCKETS=2

# root directory, which must point to the top directory of the artifact
ROOT_DIR=$(cd $(dirname -- $0) && pwd -P)

# install directory (default: ROOT_DIR/opt)
export INSTALL_DIR=${ROOT_DIR}/opt

################################################################

# Parse options

show_help() {
  echo "Usage: ./measure_XXX [options]"
  echo "[options]"
  echo "  --only-plot    generates plots without running any measurement."
  echo "                 singularity or docker must be installed."
  echo "  --no-plot      runs measurements without generating plots."
  echo "                 Please set this option when singularity and docker are not avaliable."
  echo "  --use-gcc      runs measurements with gcc (default is the Intel compiler)."
  echo "                 Note that some experiments cannot run without Intel compilers."
  exit 1
}

# Whether or not to run measurement
RUN_MEASUREMENT=true

# Whether or not to generate plots
GENERATE_PLOT=true

# Whether or not to use gcc
USE_GCC=false

while getopts ":h-:" opt; do
  case "$opt" in
    -)
      case "${OPTARG}" in
        only-plot) RUN_MEASUREMENT=false ;;
        no-plot)   GENERATE_PLOT=false ;;
        use-gcc)   USE_GCC=true ;;
        *)         show_help ;;
      esac
      ;;
    *) show_help ;;
  esac
done

################################################################

# check root directory
if test ! -d $ROOT_DIR/argobots ; then
  echo "Error: incorrect root directory. Check ROOT_DIR=${ROOT_DIR}"
  exit 1
fi

# move to ROOT_DIR
cd $ROOT_DIR

if "$RUN_MEASUREMENT"; then
  if "$USE_GCC"; then
    # check gcc
    if test x"$(which gcc 2>/dev/null)" = 'x' ; then
      echo "Error: gcc is not found."
      exit 1
    fi
    export CC=gcc CXX=g++
  else
    # check gcc
    if test x"$(which icc 2>/dev/null)" = 'x' ; then
      echo "Error: icc is not found."
      echo "Please load environment variables for Intel compilers (e.g., 'source \${INTEL_DIR}/bin/compilervars.sh intel64')."
      echo "--use-gcc option might help if Intel compilers are not installed (see './measure_XXX --help')."
      exit 1
    fi
    export CC=icc CXX=icpc I_MPI_CC=icc I_MPI_CXX=icpc
  fi
fi
