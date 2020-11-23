#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <pthread.h>

#include <abt.h>

#include "logger.h"

#ifndef MLOG_RECORD
# define MLOG_RECORD 0
#endif

#ifndef USE_PTHREAD
# define USE_PTHREAD 1
#endif

#define PTHREAD_AFFINITY_RANGE 1
#define PTHREAD_AFFINITY_FIX   2
#define PTHREAD_AFFINITY_NONE  3

#ifndef PTHREAD_AFFINITY_TYPE
/* # define PTHREAD_AFFINITY_TYPE PTHREAD_AFFINITY_RANGE */
# define PTHREAD_AFFINITY_TYPE PTHREAD_AFFINITY_FIX
/* # define PTHREAD_AFFINITY_TYPE PTHREAD_AFFINITY_NONE */
#endif

#define PTHREAD_BARRIER        1
#define BUSYWAIT_BARRIER       2
#define BUSYWAIT_YIELD_BARRIER 3
#define ABT_BARRIER            4
#define NO_BARRIER             5

#ifndef BARRIER_TYPE
# define BARRIER_TYPE PTHREAD_BARRIER
/* # define BARRIER_TYPE BUSYWAIT_BARRIER */
/* # define BARRIER_TYPE BUSYWAIT_YIELD_BARRIER */
/* # define BARRIER_TYPE ABT_BARRIER */
/* # define BARRIER_TYPE NO_BARRIER */
#endif

#ifndef PREEMPTION_TYPE
# define PREEMPTION_TYPE ABT_PREEMPTION_YIELD
/* # define PREEMPTION_TYPE ABT_PREEMPTION_NEW_ES */
/* # define PREEMPTION_TYPE ABT_PREEMPTION_DISABLED */
#endif

#ifndef ENABLE_PREEMPTION
# define ENABLE_PREEMPTION 1
#endif

#ifndef CENTRAL_POOL
# define CENTRAL_POOL 0
#endif

#define SCHED_TYPE ABT_SCHED_BASIC

#define HANDLE_ERROR(ret, msg)                        \
    if (ret != ABT_SUCCESS) {                         \
        fprintf(stderr, "ERROR[%d]: %s\n", ret, msg); \
        exit(EXIT_FAILURE);                           \
    }

#if BARRIER_TYPE == PTHREAD_BARRIER
pthread_barrier_t* barriers;
#elif BARRIER_TYPE == BUSYWAIT_BARRIER || BARRIER_TYPE == BUSYWAIT_YIELD_BARRIER
uint32_t* counters;
#elif BARRIER_TYPE == ABT_BARRIER
ABT_barrier* barriers;
#endif
int num_xstreams          = 1;
int num_threads           = 1;
int num_repeats           = 1;
int num_pgroups           = 1;
int num_barriers          = 10;
int preemption_intvl_usec = 1000;
ABT_preemption_group *preemption_groups;
pthread_barrier_t g_barrier;

typedef struct {
    int ult_id;
    int n_threads;
#if USE_PTHREAD
    int *p_ok;
    int *p_ncomplete;
    pthread_mutex_t *p_mutex, *p_mutex2;
    pthread_cond_t *p_cond, *p_cond2;
#endif
} fn_args_t;

typedef struct {
    int worker_id;
#if !USE_PTHREAD
    ABT_pool pool;
#endif
} worker_args_t;

void* decoder_ult(FILE* stream, int rank0, int rank1, void* buf0, void* buf1) {
    uint64_t t0  = MLOG_READ_ARG(&buf0, uint64_t);
    int      cpu = MLOG_READ_ARG(&buf0, int);
    uint64_t t1  = MLOG_READ_ARG(&buf1, uint64_t);

    fprintf(stream, "%d,%lu,%d,%lu,%04dult\n", cpu, t0, cpu, t1, rank0);
    /* fprintf(stream, "%d,%lu,%d,%lu,ult\n", cpu, t0, cpu, t1); */
    return buf1;
}

void thread_fn(void *args) {
    fn_args_t* fn_args = (fn_args_t*)args;
#if USE_PTHREAD
    volatile int *p_ok = fn_args->p_ok;
    pthread_mutex_t *p_mutex = fn_args->p_mutex;
    pthread_mutex_t *p_mutex2 = fn_args->p_mutex2;
    pthread_cond_t *p_cond = fn_args->p_cond;
    pthread_cond_t *p_cond2 = fn_args->p_cond2;
    if (!(*p_ok)) {
        pthread_mutex_lock(p_mutex);
        if (!(*p_ok)) {
            pthread_cond_wait(p_cond, p_mutex);
        }
        pthread_mutex_unlock(p_mutex);
    }
#endif
    for (int i = 0; i < num_barriers; i++) {
#if BARRIER_TYPE == PTHREAD_BARRIER
        pthread_barrier_wait(&barriers[i]);
#elif BARRIER_TYPE == BUSYWAIT_BARRIER
        int ult_id = fn_args->ult_id;
        volatile uint32_t* p_counter = &counters[i];

        __sync_fetch_and_add(p_counter, 1);

#if MLOG_RECORD
        uint64_t t0 = mlog_clock_gettime_in_nsec();
        void* bp = LOGGER_BEGIN(ult_id, t0, sched_getcpu());
        while (*p_counter != num_threads) {
            uint64_t t1 = mlog_clock_gettime_in_nsec();
            if (t1 - t0 > 100000) {
                LOGGER_END(ult_id, bp, decoder_ult, t0);
                bp = LOGGER_BEGIN(ult_id, t1, sched_getcpu());
            }
            t0 = t1;
        };
        LOGGER_END(ult_id, bp, decoder_ult, t0);
#else
        while (*p_counter != num_threads) {
            /* char* ptr = malloc(1000000); */
            /* fprintf(stderr, "%p\n", ptr); */
            /* free(ptr); */
        };
#endif
#elif BARRIER_TYPE == BUSYWAIT_YIELD_BARRIER
        volatile uint32_t* p_counter = &counters[i];

        __sync_fetch_and_add(p_counter, 1);

        while (*p_counter != num_threads) {
            sched_yield();
        }
#elif BARRIER_TYPE == ABT_BARRIER
        ABT_barrier_wait(barriers[i]);
#endif
    }
#if USE_PTHREAD
    volatile int *p_ncomplete = fn_args->p_ncomplete;
    pthread_mutex_lock(p_mutex2);
    (*p_ncomplete)++;
    if (*p_ncomplete == fn_args->n_threads) {
        pthread_cond_signal(p_cond2);
    }
    pthread_mutex_unlock(p_mutex2);
#endif
}

void worker_fn(void *args) {
    int ret;
    worker_args_t* worker_args = (worker_args_t*)args;
    int worker_id = worker_args->worker_id;
    int n_threads = num_threads / num_xstreams;

#if USE_PTHREAD
    pthread_t *pthreads = (pthread_t *)malloc(sizeof(pthread_t) * n_threads);
    int ok, ncomplete;
    pthread_mutex_t mutex, mutex2;
    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_init(&mutex2, NULL);
    pthread_cond_t cond, cond2;
    pthread_cond_init(&cond, NULL);
    pthread_cond_init(&cond2, NULL);
#else
    ABT_thread* threads = (ABT_thread *)malloc(sizeof(ABT_thread) * n_threads);
    ABT_pool pool = worker_args->pool;
#endif

    fn_args_t* fn_args = (fn_args_t *)malloc(sizeof(fn_args_t) * n_threads);

    for (int it = 0; it < num_repeats; it++) {
        pthread_barrier_wait(&g_barrier);

        if (worker_id == 0) {
#if BARRIER_TYPE == PTHREAD_BARRIER
            for (int i = 0; i < num_barriers; i++) {
                pthread_barrier_init(&barriers[i], NULL, num_threads);
            }
#elif BARRIER_TYPE == BUSYWAIT_BARRIER || BARRIER_TYPE == BUSYWAIT_YIELD_BARRIER
            for (int i = 0; i < num_barriers; i++) {
                counters[i] = 0;
            }
#elif BARRIER_TYPE == ABT_BARRIER
            for (int i = 0; i < num_barriers; i++) {
                ABT_barrier_create(num_threads, &barriers[i]);
            }
#endif
        }

#if USE_PTHREAD
        ok = 0;
        ncomplete = 0;
#endif

        pthread_barrier_wait(&g_barrier);

        for (int i = 0; i < n_threads; i++) {
            fn_args[i].ult_id = i + worker_id * n_threads;
            fn_args[i].n_threads = n_threads;
#if USE_PTHREAD
            fn_args[i].p_ok = &ok;
            fn_args[i].p_ncomplete = &ncomplete;
            fn_args[i].p_mutex = &mutex;
            fn_args[i].p_cond = &cond;
            fn_args[i].p_mutex2 = &mutex2;
            fn_args[i].p_cond2 = &cond2;
            pthread_create(&pthreads[i], 0, (void*)thread_fn, &fn_args[i]);
#else
            ABT_thread_attr attr;
            ABT_thread_attr_create(&attr);
            ABT_thread_attr_set_preemption_type(attr, PREEMPTION_TYPE);
            ret = ABT_thread_create(pool,
                                    thread_fn,
                                    &fn_args[i],
                                    attr,
                                    &threads[i]);
            HANDLE_ERROR(ret, "ABT_thread_create");
#endif
        }

        pthread_barrier_wait(&g_barrier);

        if (worker_id == 0) {
            printf("Iteration %d started...  ", it);
#if !USE_PTHREAD && ENABLE_PREEMPTION
            for (int i = 0; i < num_pgroups; i++) {
                ABT_preemption_timer_start(preemption_groups[i]);
            }
#endif
        }

        pthread_barrier_wait(&g_barrier);

        uint64_t t1, t2;
        if (worker_id == 0) {
            t1 = mlog_clock_gettime_in_nsec();
        }

#if USE_PTHREAD
        // start
        pthread_mutex_lock(&mutex);
        ok = 1;
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&mutex);

        // wait
        pthread_mutex_lock(&mutex2);
        pthread_cond_wait(&cond2, &mutex2);
        pthread_mutex_unlock(&mutex2);

        if (worker_id == 0) {
            t2 = mlog_clock_gettime_in_nsec();
            printf("[elapsed] %'ld nsec\n", t2 - t1);
        }

        for (int i = 0; i < n_threads; i++) {
            pthread_join(pthreads[i], NULL);
        }
#else
        for (int i = 0; i < n_threads; i++) {
            ret = ABT_thread_free(&threads[i]);
            HANDLE_ERROR(ret, "ABT_thread_free");
        }

        if (worker_id == 0) {
            t2 = mlog_clock_gettime_in_nsec();
            printf("[elapsed] %'ld nsec\n", t2 - t1);
        }

        if (worker_id == 0) {
#if ENABLE_PREEMPTION
            for (int i = 0; i < num_pgroups; i++) {
                ABT_preemption_timer_stop(preemption_groups[i]);
            }
#endif
        }
#endif

    }

#if USE_PTHREAD
    free(pthreads);
#else
    free(threads);
    free(fn_args);
#endif
}

int main(int argc, char *argv[]) {
    /* read arguments. */
    int opt;
    extern char *optarg;
    while ((opt = getopt(argc, argv, "n:x:r:g:b:t:h")) != EOF) {
        switch (opt) {
        case 'n':
            num_threads = atoi(optarg);
            break;
        case 'x':
            num_xstreams = atoi(optarg);
            break;
        case 'r':
            num_repeats = atoi(optarg);
            break;
        case 'g':
            num_pgroups = atoi(optarg);
            break;
        case 'b':
            num_barriers = atoi(optarg);
            break;
        case 't':
            preemption_intvl_usec = atoi(optarg);
            break;
        case 'h':
        default:
            printf("Usage: ./barrier [args]\n"
                   "  Parameters:\n"
                   "    -n : # of ULTs (int)\n"
                   "    -x : # of ESs (int)\n"
                   "    -r : # of repeats (int)\n"
                   "    -g : # of preemption groups (int)\n"
                   "    -t : Preemption timer interval in usec (int)\n");
            exit(1);
        }
    }

    char* bt;
    switch (BARRIER_TYPE) {
        case PTHREAD_BARRIER:
            bt = "PTHREAD_BARRIER";
            break;
        case BUSYWAIT_BARRIER:
            bt = "BUSYWAIT_BARRIER";
            break;
        case BUSYWAIT_YIELD_BARRIER:
            bt = "BUSYWAIT_YIELD_BARRIER";
            break;
        case ABT_BARRIER:
            bt = "ABT_BARRIER";
            break;
        case NO_BARRIER:
            bt = "NO_BARRIER";
            break;
    }

#if USE_PTHREAD
    char* pat;
    switch (PTHREAD_AFFINITY_TYPE) {
        case PTHREAD_AFFINITY_RANGE:
            pat = "PTHREAD_AFFINITY_RANGE";
            break;
        case PTHREAD_AFFINITY_FIX:
            pat = "PTHREAD_AFFINITY_FIX";
            break;
        case PTHREAD_AFFINITY_NONE:
            pat = "PTHREAD_AFFINITY_NONE";
            break;
    }
#else
    char* pt;
    switch (PREEMPTION_TYPE) {
        case ABT_PREEMPTION_YIELD:
            pt = "ABT_PREEMPTION_YIELD";
            break;
        case ABT_PREEMPTION_NEW_ES:
            pt = "ABT_PREEMPTION_NEW_ES";
            break;
        case ABT_PREEMPTION_DISABLED:
            pt = "ABT_PREEMPTION_DISABLED";
            break;
    }
#endif

    setlocale(LC_NUMERIC, "");
    printf("=============================================================\n"
           "Use pthread:               %s\n"
           "Barrier type:              %s\n"
           "# of xstreams:             %d\n"
           "# of threads:              %d\n"
           "# of repeats:              %d\n"
           "# of barriers:             %d\n"
#if USE_PTHREAD
           "Pthread affinity type:     %s\n"
#else
           "# of preemption groups:    %d\n"
           "Preemption timer interval: %d usec\n"
           "Preemption type:           %s\n"
           "Central Pool:              %s\n"
#endif
           "=============================================================\n\n",
           (USE_PTHREAD ? "true" : "false"), bt, num_xstreams, num_threads, num_repeats, num_barriers,
#if USE_PTHREAD
           pat);
#else
           num_pgroups, preemption_intvl_usec, pt, (CENTRAL_POOL ? "true" : "false"));
#endif

    logger_init(num_threads);

    char s[20];
    sprintf(s, "%d", preemption_intvl_usec);
    setenv("ABT_PREEMPTION_INTERVAL_USEC", s, 1);

#if USE_PTHREAD
    pthread_t *pthreads = (pthread_t *)malloc(sizeof(pthread_t) * num_xstreams);

#if PTHREAD_AFFINITY_TYPE == PTHREAD_AFFINITY_RANGE
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    for (int i = 0; i < num_xstreams; i++) {
        CPU_SET(i, &cpus);
    }
    sched_setaffinity(getpid(), sizeof(cpus), &cpus);
#endif
#else
    int ret;
    ABT_init(0, NULL);

    /* Create pools */
    ABT_pool* pools = (ABT_pool *)malloc(sizeof(ABT_pool) * num_xstreams);
    for (int i = 0; i < num_xstreams; i++) {
        ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &pools[i]);
    }

    /* Primary ES creation */
    ABT_xstream *xstreams = (ABT_xstream *)malloc(sizeof(ABT_xstream) * num_xstreams);
    ABT_xstream_self(&xstreams[0]);
    ABT_sched sched;
    ABT_sched_create_basic(SCHED_TYPE, 1, &pools[0], ABT_SCHED_CONFIG_NULL, &sched);
    ABT_xstream_set_main_sched(xstreams[0], sched);

    /* Secondary ES creation */
    for (int i = 1; i < num_xstreams; i++) {
#if CENTRAL_POOL
        ret = ABT_xstream_create_basic(SCHED_TYPE, 1, &pools[0], ABT_SCHED_CONFIG_NULL, &xstreams[i]);
#else
        ret = ABT_xstream_create_basic(SCHED_TYPE, 1, &pools[i], ABT_SCHED_CONFIG_NULL, &xstreams[i]);
#endif
        HANDLE_ERROR(ret, "ABT_xstream_create");
    }

#if ENABLE_PREEMPTION
    preemption_groups = (ABT_preemption_group *)malloc(sizeof(ABT_preemption_group) * num_pgroups);
    ABT_preemption_timer_create_groups(num_pgroups, preemption_groups);
    for (int i = 0; i < num_pgroups; i++) {
        int begin = i * num_xstreams / num_pgroups;
        int end   = (i + 1) * num_xstreams / num_pgroups;
        if (end > num_xstreams) end = num_xstreams;
        ABT_preemption_timer_set_xstreams(preemption_groups[i], end - begin, &xstreams[begin]);
    }
#endif

    ABT_thread* threads = (ABT_thread *)malloc(sizeof(ABT_thread) * num_xstreams);
#endif

#if BARRIER_TYPE == PTHREAD_BARRIER
    barriers = (pthread_barrier_t *)malloc(sizeof(pthread_barrier_t) * num_barriers);
#elif BARRIER_TYPE == BUSYWAIT_BARRIER || BARRIER_TYPE == BUSYWAIT_YIELD_BARRIER
    counters = (uint32_t *)malloc(sizeof(uint32_t) * num_barriers);
#elif BARRIER_TYPE == ABT_BARRIER
    barriers = (ABT_barrier *)malloc(sizeof(ABT_barrier) * num_barriers);
#endif

    pthread_barrier_init(&g_barrier, NULL, num_xstreams);
    worker_args_t* worker_args = (worker_args_t *)malloc(sizeof(worker_args_t) * num_xstreams);

    for (int i = 0; i < num_xstreams; i++) {
        worker_args[i].worker_id = i;

#if USE_PTHREAD
#if PTHREAD_AFFINITY_TYPE == PTHREAD_AFFINITY_FIX
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        CPU_SET(i % num_xstreams, &cpus);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
        pthread_create(&pthreads[i], &attr, (void*)worker_fn, &worker_args[i]);
#else
        pthread_create(&pthreads[i], 0, (void*)worker_fn, &worker_args[i]);
#endif
#else
        worker_args[i].pool = pools[i];

        ABT_thread_attr attr;
        ABT_thread_attr_create(&attr);
        ABT_thread_attr_set_preemption_type(attr, ABT_PREEMPTION_DISABLED);
#if CENTRAL_POOL
        ret = ABT_thread_create(pools[0],
                                worker_fn,
                                &worker_args[i],
                                attr,
                                &threads[i]);
#else
        ret = ABT_thread_create(pools[i % num_xstreams],
                                worker_fn,
                                &worker_args[i],
                                attr,
                                &threads[i]);
#endif
        HANDLE_ERROR(ret, "ABT_thread_create");
#endif
    }

#if USE_PTHREAD
    for (int i = 0; i < num_xstreams; i++) {
        pthread_join(pthreads[i], NULL);
    }
#else
    for (int i = 0; i < num_xstreams; i++) {
        ret = ABT_thread_free(&threads[i]);
        HANDLE_ERROR(ret, "ABT_thread_free");
    }
#endif

#if USE_PTHREAD
    free(pthreads);
#else
    free(threads);
    free(worker_args);

#if ENABLE_PREEMPTION
    for (int i = 0; i < num_pgroups; i++) {
        ABT_preemption_timer_delete(preemption_groups[i]);
    }
    free(preemption_groups);
#endif

    /* Join and free Execution Streams */
    for (int i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_free(&xstreams[i]);
        HANDLE_ERROR(ret, "ABT_xstream_free");
    }

    ret = ABT_finalize();
    HANDLE_ERROR(ret, "ABT_finalize");

    free(xstreams);
    free(pools);
#endif

    printf("success!\n");

    logger_flush(stderr);

    return 0;
}
