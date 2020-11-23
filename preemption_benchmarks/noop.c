#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <immintrin.h>

#include <abt.h>

#include "logger.h"

#ifndef MLOG_RECORD
# define MLOG_RECORD 0
#endif

#ifndef USE_PTHREAD
# define USE_PTHREAD 0
#endif

#define PTHREAD_AFFINITY_RANGE 1
#define PTHREAD_AFFINITY_FIX   2
#define PTHREAD_AFFINITY_NONE  3

#ifndef PTHREAD_AFFINITY_TYPE
/* # define PTHREAD_AFFINITY_TYPE PTHREAD_AFFINITY_RANGE */
# define PTHREAD_AFFINITY_TYPE PTHREAD_AFFINITY_FIX
/* # define PTHREAD_AFFINITY_TYPE PTHREAD_AFFINITY_NONE */
#endif

#ifndef PREEMPTION_TYPE
/* # define PREEMPTION_TYPE ABT_PREEMPTION_YIELD */
# define PREEMPTION_TYPE ABT_PREEMPTION_NEW_ES
/* # define PREEMPTION_TYPE ABT_PREEMPTION_DISABLED */
#endif

#ifndef ENABLE_PREEMPTION
# define ENABLE_PREEMPTION 1
#endif

#define SCHED_TYPE ABT_SCHED_BASIC

#define HANDLE_ERROR(ret, msg)                        \
    if (ret != ABT_SUCCESS) {                         \
        fprintf(stderr, "ERROR[%d]: %s\n", ret, msg); \
        exit(EXIT_FAILURE);                           \
    }

typedef struct {
    int ult_id;
#if USE_PTHREAD
    int *p_ok;
    pthread_mutex_t *p_mutex;
    pthread_cond_t *p_cond;
#endif
} fn_args_t;

typedef struct {
    int worker_id;
#if !USE_PTHREAD
    ABT_pool pool;
#endif
} worker_args_t;

int num_xstreams          = 1;
int num_threads           = 1;
int num_repeats           = 1;
int num_pgroups           = 1;
uint64_t num_iter         = 10000000;
int preemption_intvl_usec = 1000;
ABT_preemption_group *preemption_groups;
uint64_t *g_times;
pthread_barrier_t g_barrier;

#if MLOG_RECORD
void* decoder_ult(FILE* stream, int rank0, int rank1, void* buf0, void* buf1) {
    uint64_t t0  = MLOG_READ_ARG(&buf0, uint64_t);
    int      cpu = MLOG_READ_ARG(&buf0, int);
    uint64_t t1  = MLOG_READ_ARG(&buf1, uint64_t);

    fprintf(stream, "%d,%lu,%d,%lu,0ult%04d\n", cpu, t0, cpu, t1, rank0);
    /* fprintf(stream, "%d,%lu,%d,%lu,ult\n", cpu, t0, cpu, t1); */
    return buf1;
}
#endif

/* int g_value = 0; */
/* double copy_value (double v) { */
/*     return v + g_value; */
/* } */
/* void consume_value (__m256d v) { */
/*     if (g_value != 0) */
/*         printf("%d", &v); */
/* } */

void thread_fn(void *args) {
    fn_args_t* fn_args = (fn_args_t*)args;
    int ult_id = fn_args->ult_id;
#if USE_PTHREAD
    volatile int *p_ok = fn_args->p_ok;
    pthread_mutex_t *p_mutex = fn_args->p_mutex;
    pthread_cond_t *p_cond = fn_args->p_cond;
    if (!(*p_ok)) {
        pthread_mutex_lock(p_mutex);
        if (!(*p_ok)) {
            pthread_cond_wait(p_cond, p_mutex);
        }
        pthread_mutex_unlock(p_mutex);
    }
#endif
#if MLOG_RECORD
    uint64_t t0 = mlog_clock_gettime_in_nsec();
    void* bp = LOGGER_BEGIN(ult_id, t0, sched_getcpu());
    for (uint64_t i = 0; i < num_iter; i++) {
        uint64_t t1 = mlog_clock_gettime_in_nsec();
        if (t1 - t0 > 1000000) {
            LOGGER_END(ult_id, bp, decoder_ult, t0);
            bp = LOGGER_BEGIN(ult_id, t1, sched_getcpu());
        }
        t0 = t1;
        __asm__ __volatile__("#noop");
    }
    LOGGER_END(ult_id, bp, decoder_ult, t0);
#else
    volatile float f = 0.000001f;
    for (uint64_t i = 0; i < num_iter; i++) {
        __asm__ __volatile__("#noop");
        f += 0.000001f;
    }
    if (fabs(f - 0.000001f * num_iter) < 0.000001) {
        printf("wrong answer.\n");
    }

    /* const __m256d a1  = _mm256_set1_pd(copy_value(0.0)); */
    /* const __m256d a2  = _mm256_set1_pd(copy_value(0.0)); */
    /* const __m256d a3  = _mm256_set1_pd(copy_value(0.0)); */
    /* __m256d c1  = _mm256_set1_pd(copy_value(1.0)); */
    /* __m256d c2  = _mm256_set1_pd(copy_value(2.0)); */
    /* __m256d c3  = _mm256_set1_pd(copy_value(3.0)); */
    /* __m256d c4  = _mm256_set1_pd(copy_value(4.0)); */
    /* __m256d c5  = _mm256_set1_pd(copy_value(5.0)); */
    /* __m256d c6  = _mm256_set1_pd(copy_value(6.0)); */
    /* __m256d c7  = _mm256_set1_pd(copy_value(7.0)); */
    /* __m256d c8  = _mm256_set1_pd(copy_value(8.0)); */
    /* __m256d c9  = _mm256_set1_pd(copy_value(9.0)); */
    /* __m256d c10 = _mm256_set1_pd(copy_value(10.0)); */
    /* for(int64_t i = 0; i < num_iter; i++) { */
    /*     c1  = _mm256_fmadd_pd(a1, a1, c1); */
    /*     c2  = _mm256_fmadd_pd(a1, a2, c2); */
    /*     c3  = _mm256_fmadd_pd(a1, a3, c3); */
    /*     c4  = _mm256_fmadd_pd(a2, a1, c4); */
    /*     c5  = _mm256_fmadd_pd(a2, a2, c5); */
    /*     c6  = _mm256_fmadd_pd(a2, a3, c6); */
    /*     c7  = _mm256_fmadd_pd(a3, a1, c7); */
    /*     c8  = _mm256_fmadd_pd(a3, a2, c8); */
    /*     c9  = _mm256_fmadd_pd(a3, a3, c9); */
    /*     c10 = _mm256_fmadd_pd(a1, a1, c10); */
    /* } */
    /* consume_value(c1); */
    /* consume_value(c2); */
    /* consume_value(c3); */
    /* consume_value(c4); */
    /* consume_value(c5); */
    /* consume_value(c6); */
    /* consume_value(c7); */
    /* consume_value(c8); */
    /* consume_value(c9); */
    /* consume_value(c10); */
#endif
}

void worker_fn(void *args) {
    int ret;
    worker_args_t* worker_args = (worker_args_t*)args;
    int worker_id = worker_args->worker_id;
    int n_threads = num_threads / num_xstreams;

#if USE_PTHREAD
    pthread_t *pthreads = (pthread_t *)malloc(sizeof(pthread_t) * n_threads);
    int ok;
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_t cond;
    pthread_cond_init(&cond, NULL);
#else
    ABT_thread* threads = (ABT_thread *)malloc(sizeof(ABT_thread) * n_threads);
    ABT_pool pool = worker_args->pool;
#endif

    fn_args_t* fn_args = (fn_args_t *)malloc(sizeof(fn_args_t) * n_threads);

    for (int it = 0; it < num_repeats; it++) {
        pthread_barrier_wait(&g_barrier);
#if USE_PTHREAD
        ok = 0;
#endif

        for (int i = 0; i < n_threads; i++) {
            fn_args[i].ult_id = i + worker_id * n_threads;

#if USE_PTHREAD
            fn_args[i].p_ok = &ok;
            fn_args[i].p_mutex = &mutex;
            fn_args[i].p_cond = &cond;
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

#if USE_PTHREAD
        pthread_mutex_lock(&mutex);
        uint64_t t1 = mlog_clock_gettime_in_nsec();
        ok = 1;
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&mutex);
        for (int i = 0; i < n_threads; i++) {
            pthread_join(pthreads[i], NULL);
        }
#else
        uint64_t t1 = mlog_clock_gettime_in_nsec();

        for (int i = 0; i < n_threads; i++) {
            ret = ABT_thread_free(&threads[i]);
            HANDLE_ERROR(ret, "ABT_thread_free");
        }
#endif

        uint64_t t2 = mlog_clock_gettime_in_nsec();
        g_times[worker_id] = t2 - t1;

        pthread_barrier_wait(&g_barrier);

        if (worker_id == 0) {
#if !USE_PTHREAD && ENABLE_PREEMPTION
            for (int i = 0; i < num_pgroups; i++) {
                ABT_preemption_timer_stop(preemption_groups[i]);
            }
#endif
            uint64_t sum_t = 0;
            for (int i = 0; i < num_xstreams; i++) {
                sum_t += g_times[i];
            }
            printf("[elapsed] %'ld nsec\n", sum_t / num_xstreams);
        }
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
    while ((opt = getopt(argc, argv, "n:x:r:g:t:i:h")) != EOF) {
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
        case 't':
            preemption_intvl_usec = atoi(optarg);
            break;
        case 'i':
            num_iter = atol(optarg);
            break;
        case 'h':
        default:
            printf("Usage: ./barrier [args]\n"
                   "  Parameters:\n"
                   "    -n : # of ULTs (int)\n"
                   "    -x : # of ESs (int)\n"
                   "    -r : # of repeats (int)\n"
                   "    -g : # of preemption groups (int)\n"
                   "    -t : Preemption timer interval in usec (int)\n"
                   "    -i : # of iterations for each ULT (uint64_t)\n");
            exit(1);
        }
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
#elif ENABLE_PREEMPTION
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
           "# of xstreams:             %d\n"
           "# of threads:              %d\n"
           "# of repeats:              %d\n"
#if USE_PTHREAD
           "Use pthread:               true\n"
           "Pthread affinity type:     %s\n"
#elif ENABLE_PREEMPTION
           "Use pthread:               false\n"
           "Preemption:                On\n"
           "# of preemption groups:    %d\n"
           "Preemption timer interval: %d usec\n"
           "Preemption type:           %s\n"
#else
           "Use pthread:               false\n"
           "Preemption:                Off\n"
#endif
           "# of iterations:           %ld\n"
           "=============================================================\n\n",
           num_xstreams, num_threads, num_repeats,
#if USE_PTHREAD
           pat,
#elif ENABLE_PREEMPTION
           num_pgroups, preemption_intvl_usec, pt,
#endif
           num_iter);

    logger_init(num_threads);

#if ENABLE_PREEMPTION
    char s[20];
    sprintf(s, "%d", preemption_intvl_usec);
    setenv("ABT_PREEMPTION_INTERVAL_USEC", s, 1);
#endif

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
        ret = ABT_xstream_create_basic(SCHED_TYPE, 1, &pools[i], ABT_SCHED_CONFIG_NULL, &xstreams[i]);
        HANDLE_ERROR(ret, "ABT_xstream_create");
        ret = ABT_xstream_start(xstreams[i]);
        HANDLE_ERROR(ret, "ABT_xstream_start");
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

    pthread_barrier_init(&g_barrier, NULL, num_xstreams);
    g_times = (uint64_t *)malloc(sizeof(uint64_t) * num_xstreams);
    worker_args_t* worker_args = (worker_args_t *)malloc(sizeof(worker_args_t) * num_xstreams);

    for (int i = 0; i < num_xstreams; i++) {
        worker_args[i].worker_id = i;

#if USE_PTHREAD
#if PTHREAD_AFFINITY_TYPE == PTHREAD_AFFINITY_FIX
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        CPU_SET(i, &cpus);
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
        ret = ABT_thread_create(pools[i],
                                worker_fn,
                                &worker_args[i],
                                attr,
                                &threads[i]);
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
