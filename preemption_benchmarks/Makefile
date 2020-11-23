ABT_PATH=$(abspath ../opt/argobots)
BOLT_PATH=$(abspath ../opt/bolt)
MKL_PATH=/opt/intel/mkl

ifeq ($(origin CC), default)
CC := gcc
# CC := icc
endif
CFLAGS  := -std=gnu11 -O3 -g $(CFLAGS)
LDFLAGS := -lm -ldl

# CFLAGS += -O0 -g

# Argobots
CFLAGS      += -I$(ABT_PATH)/include
ABT_LDFLAGS := -L$(ABT_PATH)/lib -Wl,-R$(ABT_PATH)/lib -labt

# BOLT
BOLT_LDFLAGS := -L$(BOLT_PATH)/lib -Wl,-R${BOLT_PATH}/lib -liomp5

# MKL
CFLAGS      += -I$(MKL_PATH)/include
MKL_LDFLAGS := -L$(MKL_PATH)/lib/intel64 -Wl,-R${MKL_PATH}/lib/intel64 -lmkl_intel_lp64 -lmkl_intel_thread -lmkl_core

# MassiveLogger
# CFLAGS += -I../opt/massivelogger/include
CFLAGS += -I.

APPS := fib timer timer_measure barrier timer_cost noop cache preemption_cost preemption_cost2 mkl_dgemm change_num_es

all: $(APPS)

fib: fib.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(ABT_LDFLAGS)

timer: timer.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lpthread -lrt

timer_measure: timer_measure.c logger.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lpthread -lrt

barrier: barrier.c logger.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(ABT_LDFLAGS) -lpthread

timer_cost: timer_cost.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lpthread -lrt

noop: noop.c logger.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(ABT_LDFLAGS) -lpthread

cache: cache.c cache_kernel.c cache.h logger.o
	$(CC) $(CFLAGS) -c cache_kernel.c $(LDFLAGS)
	$(CC) $(CFLAGS) -o $@ cache.c cache_kernel.o logger.o $(LDFLAGS) $(ABT_LDFLAGS) -lpthread

preemption_cost: preemption_cost.c logger.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(ABT_LDFLAGS) -lpthread

preemption_cost2: preemption_cost2.c logger.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(ABT_LDFLAGS) -lpthread

mkl_dgemm: mkl_dgemm.c logger.o
	$(CC) $(CFLAGS) -fopenmp -o $@ $^ $(LDFLAGS) $(MKL_LDFLAGS) $(BOLT_LDFLAGS)

change_num_es: change_num_es.c
	$(CC) $(CFLAGS) -fopenmp -o $@ $^ $(LDFLAGS) $(MKL_LDFLAGS) $(BOLT_LDFLAGS)

logger.o: logger.c logger.h
	$(CC) $(CFLAGS) -o $@ -c $< $(LDFLAGS)

patch: mkl_patch/mkl_yield.c mkl_patch/pool.h
	$(CC) -shared -fPIC -o libmklyield_skylake.so mkl_patch/mkl_yield.c -ldl -g -std=gnu99 -DOVERWRITE_MKL_SERV_ALLOC=0 -DUSE_SCHED_YIELD=0 -I$(ABT_PATH)/include
	$(CC) -shared -fPIC -o libmklyield_skylake_sched.so mkl_patch/mkl_yield.c -ldl -g -std=gnu99 -DOVERWRITE_MKL_SERV_ALLOC=0 -DUSE_SCHED_YIELD=1 -I$(ABT_PATH)/include
	$(CC) -shared -fPIC -o libmklyield_knl.so mkl_patch/mkl_yield.c -ldl -g -std=gnu99 -DOVERWRITE_MKL_SERV_ALLOC=1 -DUSE_SCHED_YIELD=0 -I$(ABT_PATH)/include
	$(CC) -shared -fPIC -o libmklyield_knl_sched.so mkl_patch/mkl_yield.c -ldl -g -std=gnu99 -DOVERWRITE_MKL_SERV_ALLOC=1 -DUSE_SCHED_YIELD=1 -I$(ABT_PATH)/include

install: $(APPS)
ifndef INSTALL_PATH
	$(error INSTALL_PATH is undefined)
endif
	mkdir -p $(INSTALL_PATH)
	cp -f $^ $(INSTALL_PATH)

clean:
	rm -f *.o *.so $(APPS)
