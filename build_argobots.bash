#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

cd $(dirname $0)

cd argobots

CFLAGS="-g -std=gnu99 $CFLAGS"
# CFLAGS="$CFLAGS -DABT_SUB_XSTREAM_LOCAL_POOL=0" # disable using local pools in KLT-switching
# CFLAGS="$CFLAGS -DABT_USE_FUTEX_ON_SLEEP=0" # disable futex in KLT-switching

if [[ ! -f configure ]]; then
    ./autogen.sh
fi
CFLAGS=$CFLAGS ./configure --enable-fast=O3,ndebug --enable-tls-model=initial-exec --enable-affinity --disable-checks --prefix=${INSTALL_DIR}/argobots
# CFLAGS=$CFLAGS ./configure --enable-debug=most --enable-fast=O0 --enable-valgrind --prefix=${INSTALL_DIR}/argobots # for debug
make clean
make -j install
