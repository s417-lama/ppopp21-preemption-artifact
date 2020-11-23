#!/bin/bash
set -eo pipefail
export LC_ALL=C
export LANG=C

DATETIME=${DATETIME:-$(TZ='America/Chicago' date +%Y-%m-%d_%H-%M-%S)}
RESULT_DIR=${RESULT_DIR:-${PWD}/results}
OUT_FILE=${RESULT_DIR}/cache_${DATETIME}.out
ERR_FILE=${RESULT_DIR}/cache_${DATETIME}.err

make clean
CC=icc make cache
# CFLAGS="-DLOGGER_ENABLE=1" CC=icc make cache

# Skylake 8180 (28 cores/socket, 2 sockets)
# L1: 32 KB/core
# L2: 1 MB/core
# L3: 38.5 MB/socket
# total: L2 * 56 + L3 * 2 = 133 MB

export ABT_INITIAL_NUM_SUB_XSTREAMS=10
# export ABT_MEM_LP_ALLOC=mmap_rp

# export LD_PRELOAD=${HOME}/opt/libhugetlbfs/lib64/libhugetlbfs.so HUGETLB_MORECORE=yes

./cache -x 56 -n 56 -g 56 -r 10 -i 10000 -d 1000000

# 1 MB * 112 = 112 MB
./cache -x 56 -n 112 -g 56 -r 10 -i 10000 -d 1000000

# 1 MB * 224 = 224 MB
./cache -x 56 -n 224 -g 56 -r 10 -i 10000 -d 1000000

# 1 MB * 448 = 448 MB
./cache -x 56 -n 448 -g 56 -r 10 -i 10000 -d 1000000

# 1 MB * 896 = 896 MB
./cache -x 56 -n 896 -g 56 -r 10 -i 10000 -d 1000000

# perf stat -e cs ./cache -x 56 -n 896 -g 56 -r 10 -i 10000 -d 1000000

ln -sf $OUT_FILE ${RESULT_DIR}/latest.out
ln -sf $ERR_FILE ${RESULT_DIR}/latest.err
