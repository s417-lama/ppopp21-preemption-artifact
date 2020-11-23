/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"
#include <signal.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#define sigev_notify_thread_id _sigev_un._tid

/* #define ABTD_SIGNAL_TIMER  SIGRTMIN */
/* #define ABTD_SIGNAL_WAKEUP (SIGRTMIN + 1) */

/* For debug; these signals are not caught by GDB */
#define ABTD_SIGNAL_TIMER  SIGURG
#define ABTD_SIGNAL_WAKEUP SIGIO

#define ABTD_SIGNAL_CHANGE_NUM_ES (SIGRTMIN + 2)

/* DEBUG utils */
#define PRINT_BACKTRACE_ON_SEGV 0

#if PRINT_BACKTRACE_ON_SEGV

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>

static void print_backtrace() {
    unw_cursor_t cursor;
    unw_context_t context;
    unw_word_t offset;
    unw_proc_info_t pinfo;
    Dl_info dli;
    char sname[256];
    void *addr;
    int count = 0;
    char *buf;
    size_t buf_size;
    FILE* mstream = open_memstream(&buf, &buf_size);
    ABTI_xstream *p_xstream = ABTI_local_get_xstream();

    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    fprintf(mstream, "[rank %d] Backtrace:\n", p_xstream->rank);
    while (unw_step(&cursor) > 0) {
        unw_get_proc_info(&cursor, &pinfo);
        unw_get_proc_name(&cursor, sname, sizeof(sname), &offset);
        addr = (char *)pinfo.start_ip + offset;
        dladdr(addr, &dli);
        fprintf(mstream, "  #%d %p in %s + 0x%lx from %s\n",
                count, addr, sname, offset, dli.dli_fname);
        count++;
    }
    fprintf(mstream, "\n");
    fflush(mstream);

    fwrite(buf, sizeof(char), buf_size, stdout);

    fclose(mstream);
    free(buf);
}

void ABTDI_signal_segv_handler(int sig)
{
    print_backtrace();
    sleep(100000000);
    exit(1);
}
#endif

ABTD_XSTREAM_LOCAL uint32_t ABTDI_signal_wakeup_flag = 0;

void ABTDI_signal_wakeup_handler(int sig)
{
    ABTDI_signal_wakeup_flag = 1;
}

void ABTDI_signal_change_num_xstreams_handler(int sig, siginfo_t *si, void *uc)
{
    // This signal handler should be called on the sub ES allocator

    int new_num_xstreams = si->si_int; // get a value from sigqueue()
    if (new_num_xstreams <= 0 || gp_ABTI_global->num_xstreams < new_num_xstreams) {
        // Increasing the number of ESs is not yet supported
        // TODO: implemente it
        exit(1);
    }
    int cur_num_xstreams = gp_ABTI_global->num_active_xstreams;
    if (new_num_xstreams > cur_num_xstreams) {
        // wake up
        for (int i = cur_num_xstreams; i < new_num_xstreams; i++) {
            ABTI_xstream *p_xstream = gp_ABTI_global->p_xstreams[i];
            while (p_xstream->should_sleep); // wait for sleeping
            ABTI_xstream_wakeup(p_xstream);
        }
    } else if (new_num_xstreams < cur_num_xstreams) {
        // sleep
        for (int i = new_num_xstreams; i < cur_num_xstreams; i++) {
            ABTI_xstream *p_xstream = gp_ABTI_global->p_xstreams[i];
            if (p_xstream->disable_sleep) continue;
            p_xstream->should_sleep = ABT_TRUE;
            /* ABTD_signal_timer_kill(p_xstream->p_cur_sub->ctx); */
        }
    }
    gp_ABTI_global->num_active_xstreams = new_num_xstreams;
}

int ABTD_signal_timer_set_handler(void (*handler)(int))
{
    int abt_errno = ABT_SUCCESS;
    struct sigaction sa;

    sa.sa_flags   = SA_RESTART;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(ABTD_SIGNAL_TIMER, &sa, NULL) == -1) {
        HANDLE_ERROR("sigaction");
        abt_errno = ABT_ERR_OTHER;
    }

    /* TODO: move to other place */
    sa.sa_flags   = 0;
    sa.sa_handler = ABTDI_signal_wakeup_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(ABTD_SIGNAL_WAKEUP, &sa, NULL) == -1) {
        HANDLE_ERROR("sigaction");
        abt_errno = ABT_ERR_OTHER;
    }

    /* TODO: move to other place */
    sa.sa_flags     = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = ABTDI_signal_change_num_xstreams_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(ABTD_SIGNAL_CHANGE_NUM_ES, &sa, NULL) == -1) {
        HANDLE_ERROR("sigaction");
        abt_errno = ABT_ERR_OTHER;
    }

#if PRINT_BACKTRACE_ON_SEGV
    sa.sa_flags   = 0;
    sa.sa_handler = ABTDI_signal_segv_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        HANDLE_ERROR("sigaction");
        abt_errno = ABT_ERR_OTHER;
    }
    if (sigaction(SIGFPE, &sa, NULL) == -1) {
        HANDLE_ERROR("sigaction");
        abt_errno = ABT_ERR_OTHER;
    }
#endif

    return abt_errno;
}

int ABTD_signal_change_num_es_set_handler()
{
    int abt_errno = ABT_SUCCESS;
    struct sigaction sa;

    /* TODO: move to other place */
    sa.sa_flags     = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = ABTDI_signal_change_num_xstreams_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(ABTD_SIGNAL_CHANGE_NUM_ES, &sa, NULL) == -1) {
        HANDLE_ERROR("sigaction");
        abt_errno = ABT_ERR_OTHER;
    }

    return abt_errno;
}

static inline pid_t ABTDI_gettid(void)
{
    return syscall(SYS_gettid);
}

int ABTD_signal_timer_create(ABTD_signal_timer *timer)
{
    int abt_errno = ABT_SUCCESS;
    struct sigevent sev;

    sev.sigev_notify           = SIGEV_THREAD_ID;
    sev.sigev_signo            = ABTD_SIGNAL_TIMER;
    sev.sigev_notify_thread_id = ABTDI_gettid();
    if (timer_create(CLOCK_REALTIME, &sev, timer) == -1) {
        HANDLE_ERROR("timer_create");
        abt_errno = ABT_ERR_OTHER;
    }

    return abt_errno;
}

int ABTD_signal_timer_settime(uint64_t intvl_nsec, ABTD_signal_timer timer)
{
    int abt_errno = ABT_SUCCESS;
    struct itimerspec its;

    its.it_value.tv_sec     = intvl_nsec / 1000000000;
    its.it_value.tv_nsec    = intvl_nsec % 1000000000;
    its.it_interval.tv_sec  = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;
    if (timer_settime(timer, 0, &its, NULL) == -1) {
        HANDLE_ERROR("timer_settime");
        abt_errno = ABT_ERR_OTHER;
    }

    return abt_errno;
}

static inline uint64_t ABTDI_signal_timer_gettime()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
}

int ABTD_signal_timer_settime_abs(uint64_t offset_nsec, uint64_t intvl_nsec,
                                  ABTD_signal_timer timer)
{
    int abt_errno = ABT_SUCCESS;
    struct itimerspec its;

    uint64_t margin   = 1000;
    uint64_t cur_time = ABTDI_signal_timer_gettime() + margin;

    uint64_t ni = (cur_time - offset_nsec + intvl_nsec - 1) / intvl_nsec;
    uint64_t initial_time_nsec = ni * intvl_nsec + offset_nsec;

    its.it_value.tv_sec     = initial_time_nsec / 1000000000;
    its.it_value.tv_nsec    = initial_time_nsec % 1000000000;
    its.it_interval.tv_sec  = intvl_nsec / 1000000000;
    its.it_interval.tv_nsec = intvl_nsec % 1000000000;
    if (timer_settime(timer, TIMER_ABSTIME, &its, NULL) == -1) {
        HANDLE_ERROR("timer_settime");
        abt_errno = ABT_ERR_OTHER;
    }

    return abt_errno;
}

int ABTD_signal_timer_stop(ABTD_signal_timer timer)
{
    int abt_errno = ABT_SUCCESS;
    struct itimerspec its;

    its.it_value.tv_sec     = 0;
    its.it_value.tv_nsec    = 0;
    its.it_interval.tv_sec  = 0;
    its.it_interval.tv_nsec = 0;
    if (timer_settime(timer, 0, &its, NULL) == -1) {
        HANDLE_ERROR("timer_settime");
        abt_errno = ABT_ERR_OTHER;
    }

    return abt_errno;
}

int ABTD_signal_timer_delete(ABTD_signal_timer timer)
{
    int abt_errno = ABT_SUCCESS;
    if (timer_delete(timer) == -1) {
        HANDLE_ERROR("timer_delete");
        abt_errno = ABT_ERR_OTHER;
    }
    return abt_errno;
}

int ABTD_signal_timer_kill(ABTD_xstream_context ctx)
{
    int abt_errno = ABT_SUCCESS;
    if (pthread_kill(ctx, ABTD_SIGNAL_TIMER) != 0) {
        HANDLE_ERROR("pthread_kill");
        abt_errno = ABT_ERR_OTHER;
    }
    return abt_errno;
}

static inline
int ABTDI_signal_mask(int how, sigset_t *p_mask, sigset_t *p_old_mask)
{
    int abt_errno = ABT_SUCCESS;
    if (pthread_sigmask(how, p_mask, p_old_mask) != 0) {
        HANDLE_ERROR("pthread_sigmask");
        abt_errno = ABT_ERR_OTHER;
    }
    return abt_errno;
}

int ABTD_signal_timer_block(void)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, ABTD_SIGNAL_TIMER);
    return ABTDI_signal_mask(SIG_BLOCK, &mask, NULL);
}

int ABTD_signal_timer_unblock(void)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, ABTD_SIGNAL_TIMER);
    return ABTDI_signal_mask(SIG_UNBLOCK, &mask, NULL);
}

int ABTD_signal_change_num_es_block(void)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, ABTD_SIGNAL_CHANGE_NUM_ES);
    return ABTDI_signal_mask(SIG_BLOCK, &mask, NULL);
}

int ABTD_signal_wakeup(ABTD_xstream_context ctx)
{
    int abt_errno = ABT_SUCCESS;
    if (pthread_kill(ctx, ABTD_SIGNAL_WAKEUP) != 0) {
        HANDLE_ERROR("pthread_kill");
        abt_errno = ABT_ERR_OTHER;
    }
    return abt_errno;
}

/* async-signal-safe */
int ABTD_signal_sleep(void (*callback_fn)(void *), void *p_arg)
{
    int abt_errno = ABT_SUCCESS;

    sigset_t old_mask;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, ABTD_SIGNAL_WAKEUP);

    ABTDI_signal_mask(SIG_BLOCK, &mask, &old_mask);
    if (callback_fn) {
        callback_fn(p_arg);
    }

    while (!ABTDI_signal_wakeup_flag) {
        sigsuspend(&old_mask);
    }
    ABTDI_signal_wakeup_flag = 0;

    ABTDI_signal_mask(SIG_UNBLOCK, &mask, NULL);

    return abt_errno;
}

int ABTD_futex_sleep(int *p_sleep_flag,
                     void (*callback_fn)(void *), void *p_arg)
{
    int abt_errno = ABT_SUCCESS;

    ABTD_atomic_store_int32(p_sleep_flag, 1);
    if (callback_fn) {
        callback_fn(p_arg);
    }
    syscall(SYS_futex, p_sleep_flag, FUTEX_WAIT, 1, NULL, NULL, 0);

    return abt_errno;
}

int ABTD_futex_wakeup(int *p_sleep_flag)
{
    int abt_errno = ABT_SUCCESS;
    int tmp;

    int val3 = FUTEX_OP(FUTEX_OP_SET, 0, FUTEX_OP_CMP_EQ, 1);
    syscall(SYS_futex, &tmp, FUTEX_WAKE_OP, 0, 1, p_sleep_flag, val3);

    return abt_errno;
}
