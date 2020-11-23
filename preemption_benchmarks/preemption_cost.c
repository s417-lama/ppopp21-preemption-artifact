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

#ifndef PREEMPTION_TYPE
/* # define PREEMPTION_TYPE ABT_PREEMPTION_YIELD */
# define PREEMPTION_TYPE ABT_PREEMPTION_NEW_ES
/* # define PREEMPTION_TYPE ABT_PREEMPTION_DISABLED */
#endif

#define SCHED_TYPE ABT_SCHED_BASIC

#define HANDLE_ERROR(ret, msg)                        \
    if (ret != ABT_SUCCESS) {                         \
        fprintf(stderr, "ERROR[%d]: %s\n", ret, msg); \
        exit(EXIT_FAILURE);                           \
    }

typedef struct {
    int ult_id;
    int rank;
    int seq;
    int n_preemption;
} fn_args_t;

void thread_fn(void *args) {
    fn_args_t* fn_args = (fn_args_t*)args;
    int ult_id         = fn_args->ult_id;
    int rank           = fn_args->rank;
    int seq            = fn_args->seq;
    int num_preemption = fn_args->n_preemption;
    ABT_preemption_profile(ult_id, rank, seq, num_preemption);
}

int main(int argc, char *argv[]) {
    int num_xstreams          = 1;
    int num_threads           = 2;
    int num_repeats           = 1;
    int num_pgroups           = 1;
    int preemption_intvl_usec = 1000;
    int num_preemption        = 100;

    /* read arguments. */
    int opt;
    while ((opt = getopt(argc, argv, "n:x:r:g:t:p:h")) != EOF) {
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
        case 'p':
            num_preemption = atoi(optarg);
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
                   "    -p : # of preemption for each thread\n");
            exit(1);
        }
    }

    setlocale(LC_NUMERIC, "");
    printf("=============================================================\n"
           "# of xstreams:                   %d\n"
           "# of threads:                    %d\n"
           "# of repeats:                    %d\n"
           "# of preemption groups:          %d\n"
           "# of preemption for each thread: %d\n"
           "Preemption timer interval:       %d usec\n"
           "Preemption type:                 " STR(PREEMPTION_TYPE) "\n"
           "=============================================================\n\n",
           num_xstreams, num_threads, num_repeats, num_pgroups, num_preemption,
           preemption_intvl_usec);

    char s[20];
    sprintf(s, "%d", preemption_intvl_usec);
    setenv("ABT_PREEMPTION_INTERVAL_USEC", s, 1);

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
    }

    ABT_preemption_group *preemption_groups = (ABT_preemption_group *)malloc(sizeof(ABT_preemption_group) * num_pgroups);
    ABT_preemption_timer_create_groups(num_pgroups, preemption_groups);
    for (int i = 0; i < num_pgroups; i++) {
        int begin = i * num_xstreams / num_pgroups;
        int end   = (i + 1) * num_xstreams / num_pgroups;
        if (end > num_xstreams) end = num_xstreams;
        ABT_preemption_timer_set_xstreams(preemption_groups[i], end - begin, &xstreams[begin]);
    }

    ABT_thread* threads = (ABT_thread *)malloc(sizeof(ABT_thread) * num_threads);
    fn_args_t* fn_args = (fn_args_t *)malloc(sizeof(fn_args_t) * num_threads);

    for (int it = 0; it < num_repeats; it++) {
        printf("Iteration %d started...  ", it);

        uint64_t t1 = mlog_clock_gettime_in_nsec();

        for (int i = 0; i < num_threads; i++) {
            fn_args[i].ult_id       = i;
            fn_args[i].rank         = i % num_xstreams;
            fn_args[i].seq          = i / num_xstreams;
            fn_args[i].n_preemption = num_preemption;

            ABT_thread_attr attr;
            ABT_thread_attr_create(&attr);
            ABT_thread_attr_set_preemption_type(attr, PREEMPTION_TYPE);
            ret = ABT_thread_create(pools[i % num_xstreams],
                                    thread_fn,
                                    &fn_args[i],
                                    attr,
                                    &threads[i]);
            HANDLE_ERROR(ret, "ABT_thread_create");
        }

        for (int i = 0; i < num_pgroups; i++) {
            ABT_preemption_timer_start(preemption_groups[i]);
        }

        for (int i = 0; i < num_threads; i++) {
            ret = ABT_thread_free(&threads[i]);
            HANDLE_ERROR(ret, "ABT_thread_free");
        }

        for (int i = 0; i < num_pgroups; i++) {
            ABT_preemption_timer_stop(preemption_groups[i]);
        }

        uint64_t t2 = mlog_clock_gettime_in_nsec();
        printf("[elapsed] %'ld nsec\n", t2 - t1);
    }

    free(threads);
    free(fn_args);

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

    printf("success!\n");

    logger_flush(stderr);

    return 0;
}
