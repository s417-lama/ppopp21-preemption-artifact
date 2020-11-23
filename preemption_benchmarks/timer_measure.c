#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <sys/resource.h>
#include <sched.h>
#include <locale.h>
#include <errno.h>
#include <pthread.h>

#define LOGGER_ENABLE 0
#include "logger.h"

// sigev_notify_thread_id is not found... Because of glibc??
#define sigev_notify_thread_id _sigev_un._tid

#define CLOCKID        CLOCK_MONOTONIC
#define TIMER_SIG      SIGRTMIN
#define PREEMPTION_SIG (SIGRTMIN + 1)

#define BIND_CORE         1
#define BIND_TIMER_THREAD 0

#define KILL_ON_THREAD_TIMER 1

typedef struct tls {
    uint64_t prev_time;
    uint64_t preempted_time;
    uint64_t acc_in_time;
    uint64_t acc_out_time;
    uint64_t acc_interval_time;
    int      preempted;
    int      preempt_count;
    int      terminated;
    timer_t  timerid;
} __attribute__((aligned(64))) tls_t;

int               g_n_threads           = 1;
int               g_n_timers            = 1;
int               g_n_preemption        = 1000;
uint64_t          g_timer_intvl_in_nseq = 500000;
int               g_managed_timer       = 0;
int               g_sync_timer          = 0;
int               g_chain               = 0;
int               g_thread_timer        = 0;
int               g_timerfd             = 0;

__thread int      l_rank;
pthread_t*        g_threads;
tls_t*            g_tls;
struct timespec*  g_it_values;
pthread_barrier_t g_barrier;

static inline uint64_t gettime_in_nsec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
}

void* decoder_enter(FILE* stream, int rank0, int rank1, void* buf0, void* buf1) {
    uint64_t t0 = MLOG_READ_ARG(&buf0, uint64_t);
    uint64_t t1 = MLOG_READ_ARG(&buf1, uint64_t);

    fprintf(stream, "%d,%lu,%d,%lu,enter handler\n", rank0 - g_n_threads, t0, rank1 - g_n_threads, t1);
    return buf1;
}

void* decoder_leave(FILE* stream, int rank0, int rank1, void* buf0, void* buf1) {
    uint64_t t0 = MLOG_READ_ARG(&buf0, uint64_t);
    uint64_t t1 = MLOG_READ_ARG(&buf1, uint64_t);

    fprintf(stream, "%d,%lu,%d,%lu,leave handler\n", rank0, t0, rank1, t1);
    return buf1;
}

void create_timer(uint64_t tid) {
    int rank = l_rank;
    tls_t* tls = &g_tls[rank];
    timer_t* timerid = &tls->timerid;

    struct sigevent sev;
    sev.sigev_notify           = SIGEV_THREAD_ID;
    sev.sigev_notify_thread_id = tid;
    sev.sigev_signo            = TIMER_SIG;
    if (timer_create(CLOCKID, &sev, timerid) == -1) {
        perror("timer_create");
        exit(1);
    }
}

void start_timer() {
    int rank = l_rank;
    tls_t* tls = &g_tls[rank];
    timer_t timerid = tls->timerid;

    int nt = l_rank / (g_n_threads / g_n_timers);
    int flags = 0;
    struct itimerspec its;
    if (g_managed_timer || g_sync_timer) {
        its.it_value = g_it_values[nt];
        flags |= TIMER_ABSTIME;
    } else {
        its.it_value.tv_sec  = g_timer_intvl_in_nseq / 1000000000;
        its.it_value.tv_nsec = g_timer_intvl_in_nseq % 1000000000;
    }
    its.it_interval.tv_sec  = g_timer_intvl_in_nseq / 1000000000;
    its.it_interval.tv_nsec = g_timer_intvl_in_nseq % 1000000000;
    if (timer_settime(timerid, flags, &its, NULL) == -1) {
        perror("timer_settime");
        exit(1);
    }
}

void start_timerfd(int timerfd) {
    int rank = l_rank;
    tls_t* tls = &g_tls[rank];

    int nt = l_rank / (g_n_threads / g_n_timers);
    int flags = 0;
    struct itimerspec its;
    if (g_managed_timer || g_sync_timer) {
        its.it_value = g_it_values[nt];
        flags |= TFD_TIMER_ABSTIME;
    } else {
        its.it_value.tv_sec  = g_timer_intvl_in_nseq / 1000000000;
        its.it_value.tv_nsec = g_timer_intvl_in_nseq % 1000000000;
    }
    its.it_interval.tv_sec  = g_timer_intvl_in_nseq / 1000000000;
    its.it_interval.tv_nsec = g_timer_intvl_in_nseq % 1000000000;
    if (timerfd_settime(timerfd, flags, &its, NULL) == -1) {
        perror("timerfd_settime");
        exit(1);
    }
}

void cancel_timer() {
    int rank = l_rank;
    tls_t* tls = &g_tls[rank];
    timer_t timerid = tls->timerid;

    struct itimerspec its;
    its.it_value.tv_sec     = 0;
    its.it_value.tv_nsec    = 0;
    its.it_interval.tv_sec  = 0;
    its.it_interval.tv_nsec = 0;
    if (timer_settime(timerid, 0, &its, NULL) == -1) {
        perror("timer_settime");
        exit(1);
    }
}

void delete_timer() {
    int rank = l_rank;
    tls_t* tls = &g_tls[rank];
    timer_t timerid = tls->timerid;

    timer_delete(timerid);
}

void bind_to_cpu(int cpu, uint64_t tid) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    sched_setaffinity(tid, sizeof(cpu_set_t), &cpuset);
}

void preemption_handler() {
    int    rank = l_rank;
    tls_t* tls  = &g_tls[rank];

    if (g_chain) {
        int next = rank + 1;
        if (next < g_n_threads && next % (g_n_threads / g_n_timers) != 0) {
            pthread_kill(g_threads[next], PREEMPTION_SIG);
        }
    }

    if (!tls->terminated) {
        if (!g_thread_timer && tls->preempt_count >= g_n_preemption) {
            /* interruption is too frequent */
            cancel_timer();
            return;
        }
        tls->preempt_count++;

        uint64_t t = gettime_in_nsec();

        if (tls->prev_time > 0) {
            void* bp;
            if (tls->preempted_time > tls->prev_time) {
                /* interruption is too frequent */
                uint64_t dt = t - tls->preempted_time;
                tls->acc_in_time += dt;
                bp = LOGGER_BEGIN(rank + g_n_threads, tls->preempted_time);
            } else {
                uint64_t dt = t - tls->prev_time;
                tls->acc_in_time += dt;
                bp = LOGGER_BEGIN(rank + g_n_threads, tls->prev_time);
            }
            LOGGER_END(rank + g_n_threads, bp, decoder_enter, t);
        }
        if (tls->preempted_time > 0) {
            tls->acc_interval_time += t - tls->preempted_time;
        }
        tls->preempted_time = t;
        tls->preempted = 1;
    }
}

void timer_handler(int sig, siginfo_t* si, void* uc) {
    if (!g_chain) {
        int rank = l_rank;
        int end  = rank + g_n_threads / g_n_timers;
        if (end > g_n_threads) {
            end = g_n_threads;
        }
        for (int i = rank + 1; i < end; i++) {
            pthread_kill(g_threads[i], PREEMPTION_SIG);
        }
    }
    preemption_handler();
}

void* thread_timer_fn(void* arg) {
    int      rank    = (int)((uint64_t)arg);
    uint64_t tid     = (uint64_t)syscall(SYS_gettid);
    tls_t*   tls     = &g_tls[rank];
    int      timerfd = -1;

    l_rank = rank;

#if BIND_TIMER_THREAD
    bind_to_cpu(rank, tid);
    /* bind_to_cpu(rank + 56, tid); */
#endif

    struct timespec req, rem;
    req.tv_sec  = g_timer_intvl_in_nseq / 1000000000;
    req.tv_nsec = g_timer_intvl_in_nseq % 1000000000;

    if (g_timerfd) {
        timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
        start_timerfd(timerfd);
    }

    while (!tls->terminated) {
        if (g_timerfd) {
            uint64_t nticks;
            if (read(timerfd, &nticks, sizeof(nticks)) != sizeof(nticks)) {
                perror("read");
                exit(1);
            }
        } else {
            /* usleep(g_timer_intvl_in_nseq / 1000); */
            nanosleep(&req, &rem);
        }

#if KILL_ON_THREAD_TIMER
        pthread_kill(g_threads[rank], TIMER_SIG);
#else
        timer_handler(TIMER_SIG, NULL, NULL);
#endif
    }

    if (g_timerfd) {
        close(timerfd);
    }

    return NULL;
}

void work_loop() {
    int rank = l_rank;
    tls_t* tls = &g_tls[rank];

    while (tls->preempt_count < g_n_preemption) {
        if (tls->preempted == 1) {
            tls->preempted = 0;
            tls->prev_time = gettime_in_nsec();
            uint64_t dt = tls->prev_time - tls->preempted_time;
            tls->acc_out_time += dt;

            void* bp = LOGGER_BEGIN(rank, tls->preempted_time);
            LOGGER_END(rank, bp, decoder_leave, tls->prev_time);
        }
        tls->prev_time = gettime_in_nsec();
    }
    tls->terminated = 1;
}

void tls_init(int rank) {
    l_rank = rank;
    tls_t* tls = &g_tls[rank];

    tls->prev_time         = 0;
    tls->preempted_time    = 0;
    tls->acc_in_time       = 0;
    tls->acc_out_time      = 0;
    tls->acc_interval_time = 0;
    tls->preempted         = 0;
    tls->preempt_count     = 0;
    tls->terminated        = 0;
}

void print_result() {
    int      count_tot             = 0;
    uint64_t ave_in_time_tot       = 0;
    uint64_t ave_out_time_tot      = 0;
    uint64_t ave_interval_time_tot = 0;
    for (int i = 0; i < g_n_threads; i++) {
        tls_t* tls = &g_tls[i];
        int count = tls->preempt_count;
        uint64_t ave_in_time       = tls->acc_in_time       / count;
        uint64_t ave_out_time      = tls->acc_out_time      / count;
        uint64_t ave_interval_time = tls->acc_interval_time / count;
        printf("[rank %d]\n"
               "  # of preemption:                            %d\n"
               "  Average interval time of preemption:        %'ld nsec\n"
               "  Average time to call a signal handler:      %ld nsec\n"
               "  Average time to return to a normal context: %ld nsec\n\n",
               i, count, ave_interval_time, ave_in_time, ave_out_time);
        count_tot             += count;
        ave_in_time_tot       += ave_in_time;
        ave_out_time_tot      += ave_out_time;
        ave_interval_time_tot += ave_interval_time;
    }
    printf("[Total]\n"
           "  # of preemption:                            %d\n"
           "  Average interval time of preemption:        %'ld nsec\n"
           "  Average time to call a signal handler:      %ld nsec\n"
           "  Average time to return to a normal context: %ld nsec\n\n",
           count_tot, ave_interval_time_tot / g_n_threads,
           ave_in_time_tot / g_n_threads, ave_out_time_tot / g_n_threads);
}

void* thread_fn(void* arg) {
    int      rank = (int)((uint64_t)arg);
    uint64_t tid  = (uint64_t)syscall(SYS_gettid);
    pthread_t timer_th;

    tls_init(rank);

    logger_warmup(rank);
    logger_warmup(rank + g_n_threads);

    if (g_thread_timer && rank % (g_n_threads / g_n_timers) == 0) {
        pthread_create(&timer_th, NULL, thread_timer_fn, (void*)((uint64_t)rank));
    }

#if BIND_CORE
    bind_to_cpu(rank, tid);
#endif

    if (!g_thread_timer && rank % (g_n_threads / g_n_timers) == 0) {
        create_timer(tid);
    }

    pthread_barrier_wait(&g_barrier);

    if (!g_thread_timer && rank % (g_n_threads / g_n_timers) == 0) {
        start_timer();
    }

    work_loop();

    if (g_thread_timer && rank % (g_n_threads / g_n_timers) == 0) {
        pthread_join(timer_th, NULL);
    }

    pthread_barrier_wait(&g_barrier);

    if (!g_thread_timer && rank % (g_n_threads / g_n_timers) == 0) {
        delete_timer();
    }

    pthread_barrier_wait(&g_barrier);

    return NULL;
}

int main(int argc, char* argv[]) {
    int timer_intvl_in_usec = 500;

    /* read arguments. */
    int opt;
    while ((opt = getopt(argc, argv, "n:t:i:p:m:s:c:d:f:h")) != EOF) {
        switch (opt) {
        case 'n':
            g_n_threads = atoi(optarg);
            break;
        case 't':
            g_n_timers = atoi(optarg);
            break;
        case 'i':
            timer_intvl_in_usec = atoi(optarg);
            break;
        case 'p':
            g_n_preemption = atoi(optarg);
            break;
        case 'm':
            g_managed_timer = atoi(optarg);
            break;
        case 's':
            g_sync_timer = atoi(optarg);
            break;
        case 'c':
            g_chain = atoi(optarg);
            break;
        case 'd':
            g_thread_timer = atoi(optarg);
            break;
        case 'f':
            g_timerfd = atoi(optarg);
            break;
        case 'h':
        default:
            printf("Usage: ./timer_measure [args]\n"
                   "  Parameters:\n"
                   "    -n : # of threads (int)\n"
                   "    -t : # of timers (int)\n"
                   "    -i : Interval time of preemption in usec (int)\n"
                   "    -p : # of preemption for each thread\n"
                   "    -m : Managed timer (0 or 1)\n"
                   "    -s : Sync timer (0 or 1)\n"
                   "    -c : Chain wake-up (0 or 1)\n"
                   "    -d : Thread timer (0 or 1)\n"
                   "    -f : Use timerfd (0 or 1)\n");
            exit(1);
        }
    }

    g_timer_intvl_in_nseq = timer_intvl_in_usec * 1000;

    setlocale(LC_NUMERIC, "");
    printf("=============================================================\n"
           "# of threads:                    %d\n"
           "# of timers:                     %d\n"
           "# of preemption for each thread: %d\n"
           "Interval time of preemption:     %'ld nsec\n"
           "Managed timer:                   %s\n"
           "Sync timer:                      %s\n"
           "Chain wake-up:                   %s\n"
           "Thread timer:                    %s\n"
           "Use timerfd:                     %s\n"
           "=============================================================\n\n",
           g_n_threads, g_n_timers, g_n_preemption, g_timer_intvl_in_nseq,
           (g_managed_timer ? "true" : "false"), (g_sync_timer ? "true" : "false"), (g_chain ? "true" : "false"),
           (g_thread_timer ? "true" : "false"), (g_timerfd ? "true" : "false"));

    logger_init(g_n_threads * 2);

    /* timer signal */
    struct sigaction sa;
    sa.sa_flags     = SA_SIGINFO;
    sa.sa_sigaction = timer_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(TIMER_SIG, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    /* preemption signal */
    sa.sa_flags     = 0;
    sa.sa_sigaction = preemption_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(PREEMPTION_SIG, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    pthread_barrier_init(&g_barrier, NULL, g_n_threads);

    g_threads = (pthread_t*)malloc(sizeof(pthread_t) * g_n_threads);
    g_tls = (tls_t*)aligned_alloc(64, sizeof(tls_t) * g_n_threads);
    for (uint64_t i = 1; i < g_n_threads; i++) {
        pthread_create(&g_threads[i], NULL, thread_fn, (void*)i);
    }
    g_threads[0] = pthread_self();

    if (g_managed_timer || g_sync_timer) {
        g_it_values = (struct timespec*)malloc(sizeof(struct timespec) * g_n_timers);
        /* set start time of timer */
        uint64_t cur_t = gettime_in_nsec();
        for (uint64_t i = 0; i < g_n_timers; i++) {
            uint64_t t;
            if (g_sync_timer) {
                t = cur_t;
            } else {
                t = cur_t + i * g_timer_intvl_in_nseq / g_n_timers;
            }
            g_it_values[i].tv_sec  = t / 1000000000;
            g_it_values[i].tv_nsec = t % 1000000000;
        }
    }

    thread_fn((void*)0);

    for (uint64_t i = 1; i < g_n_threads; i++) {
        pthread_join(g_threads[i], NULL);
    }

    logger_flush(stderr);

    print_result();

    free(g_threads);
    free(g_tls);
    if (g_managed_timer || g_sync_timer) {
        free(g_it_values);
    }

    return 0;
}
