#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)

cd bolt

rm -rf build
mkdir -p build
cd build
cmake ../ -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}/bolt -DCMAKE_BUILD_TYPE=Release -DLIBOMP_USE_ARGOBOTS=on -DLIBOMP_ARGOBOTS_INSTALL_DIR=${INSTALL_DIR}/argobots -DLIBOMP_REMOVE_FORKJOIN_LOCK=on -DLIBOMP_USE_ITT_NOTIFY=off -DCMAKE_CXX_FLAGS="-g $CXXFLAGS"
make -j install
