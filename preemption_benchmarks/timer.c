#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <sys/syscall.h>
#include <sched.h>
#include <pthread.h>

// sigev_notify_thread_id is not found... Because of glibc??
#define sigev_notify_thread_id _sigev_un._tid

#define N         4
#define TIMER_SIG SIGRTMIN
#define FREQ_NSEC 1000000000

void timer_handler(int sig, siginfo_t* si, void* uc) {
    int      rank = si->si_value.sival_int;
    uint64_t tid  = (uint64_t)syscall(SYS_gettid);
    printf("signal received; rank = %d, tid = %ld, sig = %d, cpu = %d\n", rank, tid, sig, sched_getcpu());
}

void bind_to_cpu(int cpu, uint64_t tid) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    sched_setaffinity(tid, sizeof(cpu_set_t), &cpuset);
}

void timer_start(int rank, uint64_t tid) {
    timer_t           timerid;
    struct sigevent   sev;
    struct itimerspec its;

    sev.sigev_notify           = SIGEV_THREAD_ID;
    sev.sigev_signo            = TIMER_SIG;
    sev.sigev_notify_thread_id = tid;
    sev.sigev_value.sival_int  = rank;
    if (timer_create(CLOCK_THREAD_CPUTIME_ID, &sev, &timerid) == -1) {
        perror("timer_create");
        exit(1);
    }

    uint64_t freq_nsec = FREQ_NSEC;
    /* uint64_t freq_nsec = FREQ_NSEC * (rank + 1); */
    its.it_value.tv_sec     = freq_nsec / 1000000000;
    its.it_value.tv_nsec    = freq_nsec % 1000000000;
    its.it_interval.tv_sec  = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;
    if (timer_settime(timerid, 0, &its, NULL) == -1) {
        perror("timer_settime");
        exit(1);
    }
}

void* thread_fn(void* arg) {
    int      rank = (int)((uint64_t)arg);
    uint64_t tid  = (uint64_t)syscall(SYS_gettid);

    printf("pthread created; rank = %d; tid = %ld\n", rank, tid);

    bind_to_cpu(rank, tid);
    timer_start(rank, tid);

    /* if (rank == 0) { */
    /*     sigset_t mask; */
    /*     sigemptyset(&mask); */
    /*     sigaddset(&mask, TIMER_SIG); */
    /*     pthread_sigmask(SIG_BLOCK, &mask, NULL); */
    /* } */

    for (uint64_t i = 0; i < 10000000000; i++) {
        /* if (rank == 3 && i % 10000 == 0) usleep(1); */
    }

    printf("pthread completed; rank = %d; tid = %ld\n", rank, tid);
    return NULL;
}

int main(int argc, char* argv[]) {
    struct sigaction sa;
    sigset_t         mask;
    pthread_t        threads[N];

    sa.sa_flags     = SA_SIGINFO;
    sa.sa_sigaction = timer_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(TIMER_SIG, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    for (uint64_t i = 0; i < N; i++) {
        pthread_create(&threads[i], NULL, thread_fn, (void*)i);
    }
    for (uint64_t i = 0; i < N; i++) {
        pthread_join(threads[i], NULL);
    }
    return 0;
}
