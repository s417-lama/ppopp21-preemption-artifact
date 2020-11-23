#define _GNU_SOURCE
#include <sys/mman.h>
#include <locale.h>
#include <pthread.h>

#include <abt.h>

#include "logger.h"
#include "cache.h"

#define STR_EXPAND(x) #x
#define STR(x) STR_EXPAND(x)

enum pthread_affinity_type {
    PTHREAD_AFFINITY_RANGE,
    PTHREAD_AFFINITY_FIX,
    PTHREAD_AFFINITY_NONE,
};

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

#ifndef P_INTERVALS
# define P_INTERVALS {1000, 1200, 1500, 2000, 5000, 10000}
#endif

#define HANDLE_ERROR(ret, msg)                        \
    if (ret != ABT_SUCCESS) {                         \
        fprintf(stderr, "ERROR[%d]: %s\n", ret, msg); \
        exit(EXIT_FAILURE);                           \
    }

typedef struct {
    int ult_id;
    uint64_t count;
    size_t data_num;
    double *data;
} fn_args_t;

void thread_fn(void *args) {
    fn_args_t* fn_args = (fn_args_t*)args;
    int ult_id = fn_args->ult_id;
    uint64_t count = fn_args->count;
    size_t data_num = fn_args->data_num;
    double *data = fn_args->data;

    cache_kernel(data, data_num, count, ult_id);
}

int main(int argc, char *argv[]) {
    int num_xstreams  = 1;
    int num_threads   = 1;
    int num_repeats   = 1;
    int num_pgroups   = 1;
    size_t data_size  = 1000000;
    uint64_t num_iter = 1000;

    /* read arguments. */
    int opt;
    extern char *optarg;
    while ((opt = getopt(argc, argv, "n:x:r:g:i:d:h")) != EOF) {
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
        case 'i':
            num_iter = atol(optarg);
            break;
        case 'd':
            data_size = atol(optarg);
            break;
        case 'h':
        default:
            printf("Usage: ./cache [args]\n"
                   "  Parameters:\n"
                   "    -n : # of ULTs (int)\n"
                   "    -x : # of ESs (int)\n"
                   "    -r : # of repeats (int)\n"
                   "    -g : # of preemption groups (int)\n"
                   "    -i : # of iterations for each ULT (uint64_t)\n"
                   "    -d : Data size per thread (size_t)\n");
            exit(1);
        }
    }

    setlocale(LC_NUMERIC, "");

    logger_init(num_threads);

    fn_args_t* fn_args = (fn_args_t *)malloc(sizeof(fn_args_t) * num_threads);

    size_t data_num = data_size / sizeof(double);

    for (int i = 0; i < num_threads; i++) {
        fn_args[i].ult_id   = i;
        fn_args[i].count    = num_iter;
        fn_args[i].data_num = data_num;
        fn_args[i].data     = NULL;
        fn_args[i].data     = malloc(data_num * sizeof(double));
    }

    /*
     * pthread
     */

    printf("\n"
           "=============================================================\n"
           "Random Access:              " STR(RANDOM_ACCESS) "\n"
           "Read/Write:                 " STR(READ_WRITE) "\n"
           "Use pthread:                Yes \n"
           "Pthread affinity type:      " STR(PTHREAD_AFFINITY_TYPE) "\n"
           "# of xstreams:              %d\n"
           "# of threads:               %d\n"
           "# of repeats:               %d\n"
           "# of iterations per thread: %ld\n"
           "Data size per thread:       %ld\n"
           "=============================================================\n\n",
           num_xstreams, num_threads, num_repeats, num_iter, data_size);

    pthread_t *pthreads = (pthread_t *)malloc(sizeof(pthread_t) * num_threads);

    if (PTHREAD_AFFINITY_TYPE == PTHREAD_AFFINITY_RANGE) {
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        for (int i = 0; i < num_xstreams; i++) {
            CPU_SET(i, &cpus);
        }
        sched_setaffinity(getpid(), sizeof(cpus), &cpus);
    }

    for (int it = 0; it < num_repeats; it++) {
        printf("pthread (%d/%d) : ", it, num_repeats);

        uint64_t t1 = mlog_clock_gettime_in_nsec();

        for (int i = 0; i < num_threads; i++) {
            if (PTHREAD_AFFINITY_TYPE == PTHREAD_AFFINITY_FIX) {
                pthread_attr_t attr;
                pthread_attr_init(&attr);
                cpu_set_t cpus;
                CPU_ZERO(&cpus);
                CPU_SET(i % num_xstreams, &cpus);
                pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
                pthread_create(&pthreads[i], &attr, (void*)thread_fn, &fn_args[i]);
            } else {
                pthread_create(&pthreads[i], 0, (void*)thread_fn, &fn_args[i]);
            }
        }

        for (int i = 0; i < num_threads; i++) {
            pthread_join(pthreads[i], NULL);
        }

        uint64_t t2 = mlog_clock_gettime_in_nsec();
        printf("%'ld nsec\n", t2 - t1);
    }

    free(pthreads);

    int ret;
    ABT_pool* pools = (ABT_pool *)malloc(sizeof(ABT_pool) * num_xstreams);
    ABT_sched* scheds = (ABT_sched *)malloc(num_xstreams * sizeof(ABT_sched));
    ABT_xstream *xstreams = (ABT_xstream *)malloc(sizeof(ABT_xstream) * num_xstreams);
    ABT_thread* threads = (ABT_thread *)malloc(sizeof(ABT_thread) * num_threads);

    /*
     * no preemption
     */

    printf("\n"
           "=============================================================\n"
           "Random Access:              " STR(RANDOM_ACCESS) "\n"
           "Read/Write:                 " STR(READ_WRITE) "\n"
           "Use pthread:                No \n"
           "Enable preemption:          No \n"
           "# of xstreams:              %d\n"
           "# of threads:               %d\n"
           "# of repeats:               %d\n"
           "# of iterations per thread: %ld\n"
           "Data size per thread:       %ld\n"
           "=============================================================\n\n",
           num_xstreams, num_threads, num_repeats, num_iter, data_size);

    ABT_init(0, NULL);

    /* Create pools */
    for (int i = 0; i < num_xstreams; i++) {
        ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &pools[i]);
        /* ABT_pool_create_basic(ABT_POOL_FIFO_LOCKFREE, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &pools[i]); */
    }

    /* create schedulers */
    for (int i = 0; i < num_xstreams; i++) {
        ret = ABT_sched_create_basic(ABT_SCHED_BASIC, 1, &pools[i],
                                     ABT_SCHED_CONFIG_NULL, &scheds[i]);
        HANDLE_ERROR(ret, "ABT_sched_create_basic");
    }

    /* Primary ES creation */
    ABT_xstream_self(&xstreams[0]);
    ABT_xstream_set_main_sched(xstreams[0], scheds[0]);

    /* Secondary ES creation */
    for (int i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_create(scheds[i], &xstreams[i]);
        HANDLE_ERROR(ret, "ABT_xstream_create");
        ret = ABT_xstream_start(xstreams[i]);
        HANDLE_ERROR(ret, "ABT_xstream_start");
    }

    for (int it = 0; it < num_repeats; it++) {
        printf("no preemption (%d/%d) : ", it, num_repeats);

        uint64_t t1 = mlog_clock_gettime_in_nsec();

        for (int i = 0; i < num_threads; i++) {
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

        for (int i = 0; i < num_threads; i++) {
            ret = ABT_thread_free(&threads[i]);
            HANDLE_ERROR(ret, "ABT_thread_free");
        }

        uint64_t t2 = mlog_clock_gettime_in_nsec();
        printf("%'ld nsec\n", t2 - t1);
    }

    /* Join and free Execution Streams */
    for (int i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_free(&xstreams[i]);
        HANDLE_ERROR(ret, "ABT_xstream_free");
    }

    ret = ABT_finalize();
    HANDLE_ERROR(ret, "ABT_finalize");

    /*
     * preemption
     */

    int p_intervals[] = P_INTERVALS;

    for (int ip = 0; ip < sizeof(p_intervals) / sizeof(int); ip++) {
        int preemption_intvl_usec = p_intervals[ip];

        printf("\n"
               "=============================================================\n"
               "Random Access:              " STR(RANDOM_ACCESS) "\n"
               "Read/Write:                 " STR(READ_WRITE) "\n"
               "Use pthread:                No \n"
               "Enable preemption:          Yes \n"
               "# of preemption groups:     %d\n"
               "Preemption timer interval:  %d usec\n"
               "# of xstreams:              %d\n"
               "# of threads:               %d\n"
               "# of repeats:               %d\n"
               "# of iterations per thread: %ld\n"
               "Data size per thread:       %ld\n"
               "=============================================================\n\n",
               num_pgroups, preemption_intvl_usec,
               num_xstreams, num_threads, num_repeats, num_iter, data_size);

        char s[20];
        sprintf(s, "%d", preemption_intvl_usec);
        setenv("ABT_PREEMPTION_INTERVAL_USEC", s, 1);

        ABT_init(0, NULL);

        /* Create pools */
        for (int i = 0; i < num_xstreams; i++) {
            ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &pools[i]);
            /* ABT_pool_create_basic(ABT_POOL_FIFO_LOCKFREE, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &pools[i]); */
        }

        /* create schedulers */
        for (int i = 0; i < num_xstreams; i++) {
            ret = ABT_sched_create_basic(ABT_SCHED_BASIC, 1, &pools[i],
                                         ABT_SCHED_CONFIG_NULL, &scheds[i]);
            HANDLE_ERROR(ret, "ABT_sched_create_basic");
        }

        /* Primary ES creation */
        ABT_xstream_self(&xstreams[0]);
        ABT_xstream_set_main_sched(xstreams[0], scheds[0]);

        /* Secondary ES creation */
        for (int i = 1; i < num_xstreams; i++) {
            ret = ABT_xstream_create(scheds[i], &xstreams[i]);
            HANDLE_ERROR(ret, "ABT_xstream_create");
            ret = ABT_xstream_start(xstreams[i]);
            HANDLE_ERROR(ret, "ABT_xstream_start");
        }

        ABT_preemption_group *preemption_groups = (ABT_preemption_group *)malloc(sizeof(ABT_preemption_group) * num_pgroups);
        ABT_preemption_timer_create_groups(num_pgroups, preemption_groups);
        for (int i = 0; i < num_pgroups; i++) {
            int begin = i * num_xstreams / num_pgroups;
            int end   = (i + 1) * num_xstreams / num_pgroups;
            if (end > num_xstreams) end = num_xstreams;
            ABT_preemption_timer_set_xstreams(preemption_groups[i], end - begin, &xstreams[begin]);
        }

        for (int it = 0; it < num_repeats; it++) {
            printf("preemption intvl = %d usec (%d/%d) : ", preemption_intvl_usec, it, num_repeats);

            uint64_t t1 = mlog_clock_gettime_in_nsec();

            for (int i = 0; i < num_threads; i++) {
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
            printf("%'ld nsec\n", t2 - t1);
        }

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
    }

    free(pools);
    free(scheds);
    free(xstreams);
    free(threads);

    printf("success!\n");

    for (int i = 0; i < num_threads; i++) {
        free(fn_args[i].data);
    }
    free(fn_args);

    logger_flush(stderr);

    return 0;
}
