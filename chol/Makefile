CC  = icc
CPP = icpc

ABT_PATH = ../opt/argobots
CFLAGS  := -std=c99 -O3 -DVERSION=$(version) $(CFLAGS)
CCFLAGS := -std=c++11 -fopenmp -O3 -DVERSION=$(version) $(CCFLAGS)

LIBS_TBB = -lmkl_intel_lp64 -lmkl_tbb_thread -lmkl_core -ltbb -lstdc++ -lpthread -lm -ldl
LIBS_OMP = -mkl=parallel -lstdc++ -lpthread -lm -ldl

all:
	$(CC) $(CFLAGS) -c trace/trace.c -o trace/trace.o
	$(CPP) $(CCFLAGS) -o app_tbb app.cc trace/trace.o $(LIBS_TBB)
	$(CPP) $(CCFLAGS) -o app_omp app.cc trace/trace.o $(LIBS_OMP)
	$(CC) -shared -fPIC -o libmklyield_skylake.so mkl_patch/mkl_yield.c -ldl -g -std=gnu99 -DOVERWRITE_MKL_SERV_ALLOC=0 -DUSE_SCHED_YIELD=0 -I$(ABT_PATH)/include
	$(CC) -shared -fPIC -o libmklyield_skylake_sched.so mkl_patch/mkl_yield.c -ldl -g -std=gnu99 -DOVERWRITE_MKL_SERV_ALLOC=0 -DUSE_SCHED_YIELD=1 -I$(ABT_PATH)/include
	$(CC) -shared -fPIC -o libmklyield_knl.so mkl_patch/mkl_yield.c -ldl -g -std=gnu99 -DOVERWRITE_MKL_SERV_ALLOC=1 -DUSE_SCHED_YIELD=0 -I$(ABT_PATH)/include
	$(CC) -shared -fPIC -o libmklyield_knl_sched.so mkl_patch/mkl_yield.c -ldl -g -std=gnu99 -DOVERWRITE_MKL_SERV_ALLOC=1 -DUSE_SCHED_YIELD=1 -I$(ABT_PATH)/include

clean:
	rm -f trace/trace.o app_tbb app_omp *.so
