#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <sched.h>
#include <string.h>
#include <stdlib.h>
#include <omp.h>
#include "pool.h"

#ifndef INTEL_DEBUG
#define INTEL_DEBUG 0
#endif

#ifndef OVERWRITE_MKL_SERV_ALLOC
#define OVERWRITE_MKL_SERV_ALLOC 0
#endif

#ifndef USE_SCHED_YIELD
#define USE_SCHED_YIELD 0
#endif

#if !INTEL_DEBUG
#include <abt.h>
#endif

// MKL override
#define MAX_POOL_SIZE_LOG (4 + 10 + 10) // 2^4 MB
#define MAX_POOL_SIZE     (1 << MAX_POOL_SIZE_LOG)

static void mkl_key_destructor(void *value) {
  return;
}

typedef struct mkl_global_t {
  int init;
#if !INTEL_DEBUG
  int max_mkl_threads_default; // 0 (uninitialized) -> -1 (being initialized) -> (n > 0) (initialized)
  ABT_key mkl_thread_key;
#endif
  __attribute__((aligned(CACHELINE_SIZE)))
  pool_t memory_pools[MAX_POOL_SIZE_LOG - 10 + 1]; // 2^10 = 1024

} __attribute__((aligned(CACHELINE_SIZE))) mkl_global;
__attribute__((aligned(CACHELINE_SIZE))) mkl_global g_mkl;

static void mkl_global_init();

void mkl_serv_thread_yield() {
#if USE_SCHED_YIELD
  sched_yield();
#else
#if !INTEL_DEBUG
  ABT_thread_yield();
#else
  sched_yield();
#endif
#endif
}

#if OVERWRITE_MKL_SERV_ALLOC

static int get_entry_index(size_t size) {
  return -1;
  if (size < 1024) {
    return 0;
  } else if (size < MAX_POOL_SIZE) {
    // __builtin_clz(n) = 21 (1024 <= n < 2048)
    //                  = 22 (2048 <= n < 4096)
    //                  = 23 (4096 <= n < 8192)
    return __builtin_clz(1024) - __builtin_clz((unsigned int)size) + 1;
  } else {
    return -1;
  }
  return size;
}

static size_t get_element_size(int entry_index) {
  return 1024 << entry_index;
}

static pool_t get_pool_handle(int entry_index) {
  mkl_global_init();
  return g_mkl.memory_pools[entry_index];
}

void *mkl_serv_allocate(size_t size, int alignment) {
  const int entry_index = get_entry_index(size + CACHELINE_SIZE);
  void *ptr;
  if (entry_index == -1) {
    // Use posix_memalign.
    int ret = posix_memalign(&ptr, CACHELINE_SIZE, size + CACHELINE_SIZE);
  } else {
    // Use ES-local pool.
    pool_t handle = get_pool_handle(entry_index);
    pool_alloc(handle, &ptr);
  }
  ((size_t *)ptr)[0] = size;
  return ((char *)ptr) + CACHELINE_SIZE;
}

void mkl_serv_deallocate(void *_ptr) {
  void *ptr = (void *)(((char *)_ptr) - CACHELINE_SIZE);
  size_t size = ((size_t *)ptr)[0];
  const int entry_index = get_entry_index(size + CACHELINE_SIZE);
  if (entry_index == -1) {
    // Use free.
    free(ptr);
  } else {
    // Use ES-local pool.
    pool_t handle = get_pool_handle(entry_index);
    pool_free(handle, ptr);
  }
}

#endif

static void mkl_global_init() {
  // Initialize global_init
  if (unlikely(__atomic_load_n(&g_mkl.init, __ATOMIC_ACQUIRE) != 2)) {
    if (__atomic_load_n(&g_mkl.init, __ATOMIC_ACQUIRE) == 0) {
      int expected = 0;
      if (__atomic_compare_exchange_n(&g_mkl.init, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
#if OVERWRITE_MKL_SERV_ALLOC
        // Initialize memory pools.
        for (int i = 0; i < MAX_POOL_SIZE_LOG - 10 + 1; i++) {
          size_t element_size = get_element_size(i);
          pool_create(element_size, &g_mkl.memory_pools[i]);
        }
#endif
#if !INTEL_DEBUG
        // Initialize the number of threads.
        {
          char *env = getenv("MKL_NUM_THREADS");
          if (env) {
            g_mkl.max_mkl_threads_default = atoi(env);
          } else {
            int num_xstreams = 0;
            ABT_xstream_get_num(&num_xstreams);
            g_mkl.max_mkl_threads_default = num_xstreams;
          }
          ABT_key_create(mkl_key_destructor, &g_mkl.mkl_thread_key);
          // ABT_key_free(&g_mkl.mkl_thread_key);
        }
#endif
        __atomic_store_n(&g_mkl.init, 2, __ATOMIC_RELEASE);
        return;
      }
    }
    while (__atomic_load_n(&g_mkl.init, __ATOMIC_ACQUIRE) != 2) {
#if defined(__GNUC__) && defined(__x86_64__)
      __asm__ __volatile__ ("pause");
#endif
    }
  }
}

#if INTEL_DEBUG

int mkl_serv_domain_get_max_threads() {
  typedef int (*original_f)();
  original_f f= dlsym(RTLD_NEXT, "mkl_serv_domain_get_max_threads");
  int val = (*f)();
  printf("[%d-%d] mkl_serv_domain_get_max_threads = %d\n", omp_get_level(), omp_get_thread_num(), val);
  return val;
}

int mkl_serv_set_num_threads(int arg) {
  typedef int (*original_f)(int);
  original_f f= dlsym(RTLD_NEXT, "mkl_serv_set_num_threads");
  int val = (*f)(arg);
  printf("[%d-%d] mkl_serv_set_num_threads(%d)\n", omp_get_level(), omp_get_thread_num(), arg);
  return val;
}

int mkl_serv_set_num_threads_local(int arg) {
  typedef int (*original_f)(int);
  original_f f= dlsym(RTLD_NEXT, "mkl_serv_set_num_threads_local");
  int val = (*f)(arg);
  printf("[%d-%d] mkl_serv_set_num_threads_local(%d)\n", omp_get_level(), omp_get_thread_num(), arg);
  return val;
}

#else

int mkl_serv_set_num_threads(int arg) {
  mkl_global_init();
  g_mkl.max_mkl_threads_default = arg;
  return arg;
}

int mkl_serv_set_num_threads_local(int arg) {
  mkl_global_init();
  int64_t local_num_threads = arg;
  ABT_key_set(g_mkl.mkl_thread_key, (void *)((intptr_t)local_num_threads));
  return arg;
}

int mkl_serv_domain_get_max_threads() {
  mkl_global_init();
  void *p_local_value;
  ABT_key_get(g_mkl.mkl_thread_key, &p_local_value);
  int local_num_threads = (int)((intptr_t)p_local_value);
  if (local_num_threads <= 0) {
    return g_mkl.max_mkl_threads_default;
  } else {
    return local_num_threads;
  }
}

#endif
