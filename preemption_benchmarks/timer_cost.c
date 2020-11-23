#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/syscall.h>
#include <locale.h>
#include <pthread.h>

#define sigev_notify_thread_id _sigev_un._tid

#define N_THREADS       6
#define N_ITER_SETTIME  1000000
#define N_ITER_GETTIME  1000000
#define N_ITER_CLOCK    1000000
#define N_ITER_SIGMASK  1000000
#define N_ITER_BLOCKING 100000000
#define TIMER_FREQ      10000

#define SIG SIGRTMIN

#define BIND_CORE            1
#define TIMER_CREATE_CENTRAL 1

typedef struct tls {
    uint64_t tid;
    timer_t  timer;
    uint64_t settime_time;
    uint64_t gettime_time;
    uint64_t clock_time;
    uint64_t sigmask_time;
    uint64_t normal_time;
    uint64_t blocking_time;
    uint64_t side_effect_time;
} __attribute__((aligned(64))) tls_t;

tls_t             g_tls[N_THREADS];
pthread_barrier_t g_barrier;

static inline uint64_t gettime_in_nsec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
}

static inline void create_timer(timer_t *timerid, uint64_t tid) {
    struct sigevent sev;
    sev.sigev_notify           = SIGEV_THREAD_ID;
    sev.sigev_signo            = SIG;
    sev.sigev_notify_thread_id = tid;
    if (timer_create(CLOCK_MONOTONIC, &sev, timerid) == -1) {
        perror("timer_create");
        exit(1);
    }
}

static inline void set_timer(uint64_t freq_nsec, timer_t timerid) {
    struct itimerspec its;
    its.it_value.tv_sec     = freq_nsec / 1000000000;
    its.it_value.tv_nsec    = freq_nsec % 1000000000;
    its.it_interval.tv_sec  = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;
    if (timer_settime(timerid, 0, &its, NULL) == -1) {
        perror("timer_settime");
        exit(1);
    }
}

void bind_to_cpu(int cpu, uint64_t tid) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    sched_setaffinity(tid, sizeof(cpu_set_t), &cpuset);
}

void* thread_fn(void* arg) {
    sigset_t mask;
    uint64_t t1, t2;
    volatile uint64_t count = 0;
    int rank = (int)((uint64_t)arg);
    uint64_t tid = (uint64_t)syscall(SYS_gettid);
    tls_t *tls = &g_tls[rank];

    sigemptyset(&mask);
    sigaddset(&mask, SIG);

#if BIND_CORE
    bind_to_cpu(rank, tid);
#endif

#if TIMER_CREATE_CENTRAL
    tls->tid = tid;
    pthread_barrier_wait(&g_barrier);
    if (rank == 0) {
        for (int i = 0; i < N_THREADS; i++) {
            create_timer(&g_tls[i].timer, g_tls[i].tid);
        }
    }
#else
    create_timer(&tls->timer, tid);
#endif

    pthread_barrier_wait(&g_barrier);

    // timer_settime() time
    t1 = gettime_in_nsec();
    for (uint64_t i = 0; i < N_ITER_SETTIME; i++) {
        set_timer(i * 1000000000, tls->timer);
    }
    t2 = gettime_in_nsec();
    tls->settime_time = t2 - t1;

    // timer_gettime() time
    t1 = gettime_in_nsec();
    for (uint64_t i = 0; i < N_ITER_SETTIME; i++) {
        struct itimerspec its;
        timer_gettime(tls->timer, &its);
    }
    t2 = gettime_in_nsec();
    tls->gettime_time = t2 - t1;

    // clock_gettime() time
    t1 = gettime_in_nsec();
    for (uint64_t i = 0; i < N_ITER_SETTIME; i++) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
    }
    t2 = gettime_in_nsec();
    tls->clock_time = t2 - t1;

    // pthread_sigmask() time
    t1 = gettime_in_nsec();
    for (uint64_t i = 0; i < N_ITER_SIGMASK / 2; i++) {
        pthread_sigmask(SIG_BLOCK, &mask, NULL);
        pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
    }
    t2 = gettime_in_nsec();
    tls->sigmask_time = t2 - t1;

    // normal time
    t1 = gettime_in_nsec();
    for (uint64_t i = 0; i < N_ITER_BLOCKING; i++) {
        count++;
    }
    t2 = gettime_in_nsec();
    tls->normal_time = t2 - t1;

    // block the signal and set a timer
    pthread_sigmask(SIG_BLOCK, &mask, NULL);
    set_timer(TIMER_FREQ, tls->timer);

    // blocking time
    t1 = gettime_in_nsec();
    for (uint64_t i = 0; i < N_ITER_BLOCKING; i++) {
        count++;
    }
    t2 = gettime_in_nsec();
    tls->blocking_time = t2 - t1;

    // side effect
    pthread_barrier_wait(&g_barrier);
    if (rank % 2 == 0) {
        t1 = gettime_in_nsec();
        for (uint64_t i = 0; i < N_ITER_BLOCKING; i++) {
            count++;
        }
        t2 = gettime_in_nsec();
        tls->side_effect_time = t2 - t1;
        pthread_barrier_wait(&g_barrier);
    } else {
        pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
        pthread_barrier_wait(&g_barrier);
    }

    timer_delete(tls->timer);
    return NULL;
}

void timer_handler(int sig, siginfo_t* si, void* uc) {
    (void)sig;
    (void)si;
    (void)uc;
}

int main(int argc, char* argv[]) {
    struct sigaction sa;
    sigset_t         mask;
    pthread_t        threads[N_THREADS];

    sa.sa_flags     = SA_SIGINFO;
    sa.sa_sigaction = timer_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIG, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    setlocale(LC_NUMERIC, "");
    printf("=============================================================\n"
           "timer cost measurement\n"
           "  # of threads:                             %d\n"
           "  # of iterations for timer_settime():      %d\n"
           "  # of iterations for timer_gettime():      %d\n"
           "  # of iterations for clock_gettime():      %d\n"
           "  # of iterations for pthread_sigmask():    %d\n"
           "  # of iterations for blocking measurement: %d\n"
           "  Timer interval:                           %d nsec\n"
           "=============================================================\n\n",
           N_THREADS, N_ITER_SETTIME, N_ITER_GETTIME, N_ITER_CLOCK,
           N_ITER_SIGMASK, N_ITER_BLOCKING, TIMER_FREQ);

    pthread_barrier_init(&g_barrier, NULL, N_THREADS);

    for (uint64_t i = 0; i < N_THREADS; i++) {
        pthread_create(&threads[i], NULL, thread_fn, (void*)i);
    }
    for (uint64_t i = 0; i < N_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("timer_settime() cost ----------------------------------------\n");
    for (int i = 0; i < N_THREADS; i++) {
        printf("  [rank %2d] %'14ld nsec, ave: %'14ld nsec\n",
               i, g_tls[i].settime_time, g_tls[i].settime_time / N_ITER_SETTIME);
    }
    printf("timer_gettime() cost ----------------------------------------\n");
    for (int i = 0; i < N_THREADS; i++) {
        printf("  [rank %2d] %'14ld nsec, ave: %'14ld nsec\n",
               i, g_tls[i].gettime_time, g_tls[i].gettime_time / N_ITER_GETTIME);
    }
    printf("clock_gettime() cost ----------------------------------------\n");
    for (int i = 0; i < N_THREADS; i++) {
        printf("  [rank %2d] %'14ld nsec, ave: %'14ld nsec\n",
               i, g_tls[i].clock_time, g_tls[i].clock_time / N_ITER_CLOCK);
    }
    printf("pthread_sigmask() cost --------------------------------------\n");
    for (int i = 0; i < N_THREADS; i++) {
        printf("  [rank %2d] %'14ld nsec, ave: %'14ld nsec\n",
               i, g_tls[i].sigmask_time, g_tls[i].sigmask_time / N_ITER_SIGMASK);
    }
    printf("blocking time -----------------------------------------------\n");
    for (int i = 0; i < N_THREADS; i++) {
        printf("  [rank %2d] %+'14ld nsec, normal: %'14ld nsec\n",
               i, (int64_t)g_tls[i].blocking_time - g_tls[i].normal_time, g_tls[i].normal_time);
    }
    printf("side effects ------------------------------------------------\n");
    for (int i = 0; i < N_THREADS; i++) {
        if (i % 2 == 0) {
            printf("  [rank %2d] %+'14ld nsec, normal: %'14ld nsec\n",
                   i, (int64_t)g_tls[i].side_effect_time - g_tls[i].normal_time, g_tls[i].normal_time);
        }
    }

    return 0;
}
