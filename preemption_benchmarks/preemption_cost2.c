#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <pthread.h>

#include <abt.h>

#include "logger.h"

#define STR_EXPAND(x) #x
#define STR(x) STR_EXPAND(x)

#ifndef USE_PTHREAD
# define USE_PTHREAD 0
#endif

#ifndef SET_PTHREAD_AFFINITY
# define SET_PTHREAD_AFFINITY 0
/* # define SET_PTHREAD_AFFINITY 1 */
#endif

#ifndef PREEMPTION_TYPE
/* # define PREEMPTION_TYPE ABT_PREEMPTION_YIELD */
# define PREEMPTION_TYPE ABT_PREEMPTION_NEW_ES
#endif

#define SCHED_TYPE ABT_SCHED_BASIC

#define HANDLE_ERROR(ret, msg)                        \
    if (ret != ABT_SUCCESS) {                         \
        fprintf(stderr, "ERROR[%d]: %s\n", ret, msg); \
        exit(EXIT_FAILURE);                           \
    }

typedef struct {
    int ult_id;
    uint64_t* p_prev_ts;
    uint64_t* p_ts;
    int* p_count;
    int* p_terminate;
} fn_args_t;

typedef struct {
    int worker_id;
#if !USE_PTHREAD
    ABT_pool pool;
#endif
} worker_args_t;

int num_xstreams          = 1;
int num_repeats           = 1;
int num_pgroups           = 1;
int num_preemption        = 1000;
int preemption_intvl_usec = 1000;
ABT_preemption_group *preemption_groups;
pthread_barrier_t g_barrier;

int compare(const void *arg1, const void *arg2) { return (*((int*)arg1) - *((int*)arg2)); }

void thread_fn(void *args) {
    fn_args_t* fn_args = (fn_args_t*)args;
    int ult_id                   = fn_args->ult_id;
    volatile uint64_t* p_my_t    = &fn_args->p_prev_ts[ult_id % 2];
    volatile uint64_t* p_other_t = &fn_args->p_prev_ts[(ult_id + 1) % 2];
    volatile uint64_t* p_ts      = fn_args->p_ts;
    volatile int* p_count        = fn_args->p_count;
    volatile int* p_terminate    = fn_args->p_terminate;
    int first = 1;
    while (!(*p_terminate)) {
        uint64_t t = mlog_clock_gettime_in_nsec();
        if (t - *p_my_t > 100000) {
            if (first) {
                first = 0;
            } else {
                if (*p_terminate) break;
                p_ts[*p_count] = t - *p_other_t;
                /* printf("[%d] %ld\n", ult_id, t - *p_other_t); */
                (*p_count)++;
                if (*p_count >= num_preemption) {
                    *p_terminate = 1;
                    break;
                }
            }
        }
        *p_my_t = t;
    }
}

void worker_fn(void *args) {
    int ret;
    worker_args_t* worker_args = (worker_args_t*)args;
    int worker_id = worker_args->worker_id;

#if USE_PTHREAD
    pthread_t pthreads[2];
#else
    ABT_thread threads[2];
    ABT_pool pool = worker_args->pool;
#endif

    fn_args_t fn_args[2];
    uint64_t* ts = (uint64_t*)malloc(num_preemption * sizeof(uint64_t));

    for (int it = 0; it < num_repeats; it++) {
        pthread_barrier_wait(&g_barrier);

        uint64_t prev_ts[2] = {0, 0};
        int count = 0;
        int terminate = 0;

        for (int i = 0; i < 2; i++) {
            fn_args[i].ult_id      = i + worker_id * 2;
            fn_args[i].p_prev_ts   = prev_ts;
            fn_args[i].p_ts        = ts;
            fn_args[i].p_count     = &count;
            fn_args[i].p_terminate = &terminate;
#if USE_PTHREAD
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

        if (worker_id == 0) {
            printf("Iteration %d started...\n", it);
        }

#if !USE_PTHREAD
        pthread_barrier_wait(&g_barrier);

        if (worker_id == 0) {
            for (int i = 0; i < num_pgroups; i++) {
                ABT_preemption_timer_start(preemption_groups[i]);
            }
        }

        pthread_barrier_wait(&g_barrier);
#endif

#if USE_PTHREAD
        for (int i = 0; i < 2; i++) {
            pthread_join(pthreads[i], NULL);
        }
#else
        for (int i = 0; i < 2; i++) {
            ret = ABT_thread_free(&threads[i]);
            HANDLE_ERROR(ret, "ABT_thread_free");
        }
#endif

#if !USE_PTHREAD
        pthread_barrier_wait(&g_barrier);

        if (worker_id == 0) {
            for (int i = 0; i < num_pgroups; i++) {
                ABT_preemption_timer_stop(preemption_groups[i]);
            }
        }
#endif
        // median
        qsort(ts, num_preemption, sizeof(uint64_t), compare);
        printf("[%d] median = %ld ns; count = %d\n", worker_id, ts[num_preemption / 2], count);
        // average
        /* uint64_t sum = 0; */
        /* for (int i = 0; i < num_preemption; i++) sum += ts[i]; */
        /* printf("[%d] average = %ld ns; count = %d\n", worker_id, sum / count, count); */
    }
}

int main(int argc, char *argv[]) {
    /* read arguments. */
    int opt;
    extern char *optarg;
    while ((opt = getopt(argc, argv, "x:r:g:t:p:h")) != EOF) {
        switch (opt) {
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
        case 'p':
            num_preemption = atoi(optarg);
            break;
        case 'h':
        default:
            printf("Usage: ./preemption_cost2 [args]\n"
                   "  Parameters:\n"
                   "    -x : # of ESs (int)\n"
                   "    -r : # of repeats (int)\n"
                   "    -g : # of preemption groups (int)\n"
                   "    -t : Preemption timer interval in usec (int)\n"
                   "    -p : # of preemption for each thread\n");
            exit(1);
        }
    }

    setlocale(LC_NUMERIC, "");
    printf("=============================================================\n"
           "# of xstreams:                   %d\n"
           "# of repeats:                    %d\n"
#if USE_PTHREAD
           "Use Pthread:                     true\n"
#else
           "Use Pthread:                     false\n"
           "# of preemption groups:          %d\n"
           "Preemption timer interval:       %d usec\n"
           "Preemption type:                 " STR(PREEMPTION_TYPE) "\n"
#endif
           "# of preemption for each thread: %d\n"
           "=============================================================\n\n",
           num_xstreams, num_repeats,
#if !USE_PTHREAD
           num_pgroups, preemption_intvl_usec,
#endif
           num_preemption);

    char s[20];
    sprintf(s, "%d", preemption_intvl_usec);
    setenv("ABT_PREEMPTION_INTERVAL_USEC", s, 1);

#if USE_PTHREAD
    pthread_t *pthreads = (pthread_t *)malloc(sizeof(pthread_t) * num_xstreams);
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

    preemption_groups = (ABT_preemption_group *)malloc(sizeof(ABT_preemption_group) * num_pgroups);
    ABT_preemption_timer_create_groups(num_pgroups, preemption_groups);
    for (int i = 0; i < num_pgroups; i++) {
        int begin = i * num_xstreams / num_pgroups;
        int end   = (i + 1) * num_xstreams / num_pgroups;
        if (end > num_xstreams) end = num_xstreams;
        ABT_preemption_timer_set_xstreams(preemption_groups[i], end - begin, &xstreams[begin]);
    }

    ABT_thread* threads = (ABT_thread *)malloc(sizeof(ABT_thread) * num_xstreams);
#endif

    pthread_barrier_init(&g_barrier, NULL, num_xstreams);
    worker_args_t* worker_args = (worker_args_t *)malloc(sizeof(worker_args_t) * num_xstreams);

    for (int i = 0; i < num_xstreams; i++) {
        worker_args[i].worker_id = i;

#if USE_PTHREAD
        pthread_attr_t attr;
        pthread_attr_init(&attr);
#if SET_PTHREAD_AFFINITY
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        CPU_SET(i, &cpus);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
#endif
        pthread_create(&pthreads[i], &attr, (void*)worker_fn, &worker_args[i]);
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

    for (int i = 0; i < num_pgroups; i++) {
        ABT_preemption_timer_delete(preemption_groups[i]);
    }
    free(preemption_groups);

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

    return 0;
}

