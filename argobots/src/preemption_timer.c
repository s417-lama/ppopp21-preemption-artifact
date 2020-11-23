/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/* used for dedicated timer threads */
ABTD_XSTREAM_LOCAL ABTI_preemption_group *lp_ABTI_preemption_group = NULL;

#if ABT_ENABLE_PREEMPTION_PROFILE
int ABT_preemption_profile(int ult_id, int rank, int seq, int n_preemption)
{
    if (seq > 0) {
        ABTI_mlog_end("sched");
    }

    int cur_n_preemption = 0;

    ABTI_xstream *p_xstream = ABTI_local_get_xstream();
    p_xstream->t0 = mlog_clock_gettime_in_nsec();

    ABTI_sub_xstream *p_sub_xstream = ABTI_sub_xstream_get_data();
    p_sub_xstream->preempted = 0;

    while (cur_n_preemption < n_preemption) {
        if (p_sub_xstream->preempted) {
            p_sub_xstream->preempted = 0;
            cur_n_preemption++;
            p_xstream->t0 = mlog_clock_gettime_in_nsec();
            ABTI_mlog_end_t(p_xstream->t0, "leave a signal handler");
        }
        p_xstream->t0 = mlog_clock_gettime_in_nsec();
    }

    return 0;
}
#else
int ABT_preemption_profile(int ult_id, int rank, int seq, int n_preemption)
{
    fprintf(stderr, "Please set ABT_ENABLE_PREEMPTION_PROFILE\n");
    return 1;
}
#endif

static inline
void ABTI_preemption_timer_on_sleep(void *p_arg)
{
    ABTI_sub_xstream *p_next_sub = (ABTI_sub_xstream *)p_arg;
    ABTI_xstream *p_xstream = ABTI_local_get_xstream();
    ABTI_thread *p_thread = ABTI_local_get_thread();

    /* associate this sub ES with this ULT to be waken up */
    p_thread->preempted = ABT_TRUE;
    p_thread->p_wakeup = ABTI_sub_xstream_get_data();
    p_thread->state = ABT_THREAD_STATE_READY;

    /* wake up a sub ES and run sched on it */
    ABTI_sub_xstream_wakeup(p_next_sub, p_xstream);
}

static inline
int ABTI_preemption_timer_preempt(void)
{
    int abt_errno = ABT_SUCCESS;
    ABTI_thread *p_cur_thread = ABTI_local_get_thread();
    ABTI_thread_attr *p_attr = &p_cur_thread->attr;
    ABTI_xstream *p_xstream = ABTI_local_get_xstream();

#if ABT_ENABLE_NORMAL_PROFILE
    void* bp = ABTI_mlog_begin_2();
#endif

#if ABT_ENABLE_PREEMPTION_PROFILE
    ABTI_mlog_begin_t(p_xstream->t0);

    uint64_t t1 = mlog_clock_gettime_in_nsec();
    ABTI_mlog_end_t(t1, "enter a signal handler");
    ABTI_mlog_begin_t(t1);
#endif

    ABTI_PREEMPTION_DEBUG("preemption tick");

    if (p_attr->preemption_type == ABT_PREEMPTION_YIELD) {
        ABTI_ENTER;
        ABTD_signal_timer_unblock();

#if ABT_ENABLE_NORMAL_PROFILE
        ABTI_mlog_end_2(bp, "preemption yield");
#endif

        ABTI_thread_yield(p_cur_thread);

        ABTI_LEAVE;
    } else if (p_attr->preemption_type == ABT_PREEMPTION_NEW_ES) {
        ABTI_sub_xstream *p_sub_xstream = ABTI_sub_xstream_get_data();

        /* get an available sub ES */
        ABTI_sub_xstream_list_local *p_sub_list = &p_xstream->sub_xstream_list;
        ABTI_sub_xstream *p_next_sub;
        ABTI_sub_xstream_list_pop(p_sub_list, &p_next_sub);

        if (p_next_sub) {
#if ABT_ENABLE_PREEMPTION_PROFILE
            p_sub_xstream->preempted = 1;
#endif
            /* stop a timer */
            if (gp_ABTI_global->preemption_timer_type == ABTI_PREEMPTION_TIMER_SIGNAL) {
                ABTD_signal_timer_stop(p_sub_xstream->timer);
#if ABT_ENABLE_PREEMPTION_PROFILE
                uint64_t t2 = mlog_clock_gettime_in_nsec();
                ABTI_mlog_end_t(t2, "stop a timer");
                ABTI_mlog_begin_t(t2);
#endif
            }

#if ABT_ENABLE_NORMAL_PROFILE
            ABTI_mlog_end_2(bp, "preemption new_es");
#endif
            ABTI_PREEMPTION_DEBUG("sleep (preempted)");
            /* This sub ES is preempted. sleep. */
            ABTI_sub_xstream_sleep(p_sub_xstream, &p_xstream,
                                   ABTI_preemption_timer_on_sleep, p_next_sub);
            ABTI_PREEMPTION_DEBUG("wakeup (preempted)");

            /* Now the current thread is woken up again */
            p_cur_thread->preempted = ABT_FALSE;

#if ABT_ENABLE_PREEMPTION_PROFILE
            uint64_t t3 = mlog_clock_gettime_in_nsec();
            ABTI_mlog_end_t(t3, "wakeup a preempted sub ES");
            ABTI_mlog_begin_t(t3);
#endif

            /* restart a timer */
            if (gp_ABTI_global->preemption_timer_type == ABTI_PREEMPTION_TIMER_SIGNAL) {
                ABTI_preemption_timer *p_ptimer = p_xstream->p_preemption_timer;
                ABTD_signal_timer_settime_abs(p_ptimer->offset, p_ptimer->intvl,
                                              p_sub_xstream->timer);
#if ABT_ENABLE_PREEMPTION_PROFILE
                uint64_t t4 = mlog_clock_gettime_in_nsec();
                ABTI_mlog_end_t(t4, "restart a timer");
                ABTI_mlog_begin_t(t4);
#endif
            }
        }
    } else {
#if ABT_ENABLE_NORMAL_PROFILE
        ABTI_mlog_end_2(bp, "preemption disabled");
#endif
    }

    return abt_errno;
}

static inline
ABT_bool ABTI_preemption_timer_check_preemptive(ABTI_xstream *p_xstream)
{
    return p_xstream->preemptive;
}

static inline
ABT_bool ABTI_preemption_timer_preempt_another(void)
{
    int next;
    ABTI_xstream *p_xstream = ABTI_local_get_xstream();

    if (!p_xstream || !p_xstream->ptimer_on) {
        return ABT_FALSE;
    }

    ABTI_preemption_timer *p_ptimer = p_xstream->p_preemption_timer;

    ABTI_preemption_group *p_group = p_ptimer->p_group;

    if (p_group->index_next == -1) {
        p_group->index_next = p_ptimer->preemption_index;
    }

    /* check correctness */
    if (p_group->index_next != p_ptimer->preemption_index) {
        /* ignore unintended signals */
        p_group->index_next = -1;
        p_group->in_chain   = ABT_FALSE;
        return ABT_FALSE;
    }

    /* start chain preemption */
    if (p_group->in_chain == ABT_FALSE) {
        p_group->in_chain = ABT_TRUE;
        p_group->index_until = p_ptimer->preemption_index;
    }

    next = (p_group->index_next + 1) % p_group->num_xstreams;
    while (next != p_group->index_until) {
        ABTI_xstream *p_next_xstream = p_group->p_xstreams[next];
        if (ABTI_preemption_timer_check_preemptive(p_next_xstream)) {
            p_group->index_next = next;
            ABTD_signal_timer_kill(p_next_xstream->p_cur_sub->ctx);
            return ABT_TRUE;
        }
        next = (next + 1) % p_group->num_xstreams;
    }
    p_group->in_chain = ABT_FALSE;
    p_group->index_next = -1;

    return ABT_TRUE;
}

static inline
void ABTI_preemption_timer_preempt_first(ABTI_preemption_group *p_group)
{
    int i;

    if (p_group->in_chain) return;

    for (i = 0; i < p_group->num_xstreams; i++) {
        ABTI_xstream *p_xstream = p_group->p_xstreams[i];
        if (ABTI_preemption_timer_check_preemptive(p_xstream)) {
            p_group->index_next  = i;
            p_group->index_until = 0;
            p_group->in_chain    = ABT_TRUE;
            ABTD_signal_timer_kill(p_xstream->p_cur_sub->ctx);
            return;
        }
    }
}

static void ABTI_preemption_timer_on_tick(int sig)
{
    ABTI_UNUSED(sig);

    if (lp_ABTI_preemption_group) {
        /* Handler called in a dedicated timer */
        ABTI_preemption_timer_preempt_first(lp_ABTI_preemption_group);
    } else {
        /* Handler in an ES */
        if (!ABTI_preemption_timer_preempt_another()) {
            return;
        }

        if (l_ABTI_entry_count == 0) {
            ABTI_preemption_timer_preempt();
        }
    }
}

static void* ABTI_preemption_timer_dedicated_fn(void *p_arg)
{
    int abt_errno = ABT_SUCCESS;
    ABTI_preemption_timer *p_ptimer = (ABTI_preemption_timer *)p_arg;
    ABTI_preemption_group *p_group = p_ptimer->p_group;

    lp_ABTI_preemption_group = p_group;

    ABTD_affinity_set(p_ptimer->ctx, p_ptimer->rank);

    ABTD_signal_timer timer;
    abt_errno = ABTD_signal_timer_create(&timer);
    ABTI_CHECK_ERROR(abt_errno);

    while (1) {
        abt_errno = ABTD_futex_sleep(&p_ptimer->sleep_flag, NULL, NULL);
        ABTI_CHECK_ERROR_MSG(abt_errno, "ABTD_futex_sleep");

        if (p_ptimer->terminate_flag) {
            break;
        }

        if (p_ptimer->start_flag) {
            p_ptimer->start_flag = 0;
            abt_errno =
                ABTD_signal_timer_settime_abs(p_ptimer->offset, p_ptimer->intvl,
                                              timer);
            ABTI_CHECK_ERROR(abt_errno);
        }

        if (p_ptimer->stop_flag) {
            p_ptimer->stop_flag = 0;
            abt_errno = ABTD_signal_timer_stop(timer);
            ABTI_CHECK_ERROR(abt_errno);
        }
    }

    abt_errno = ABTD_signal_timer_delete(timer);
    ABTI_CHECK_ERROR(abt_errno);

  fn_exit:
    return NULL;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABT_preemption_timer_create_groups(int num_groups,
                                       ABT_preemption_group *new_groups)
{
    ABTI_ENTER;
    int abt_errno = ABT_SUCCESS;
    int i;

    /* Set a signal handler for timers */
    abt_errno = ABTD_signal_timer_set_handler(ABTI_preemption_timer_on_tick);
    ABTI_CHECK_ERROR(abt_errno);

    uint64_t base_intvl = gp_ABTI_global->preemption_intvl_usec * 1000;
    /* settings for each preemption group */
    for (i = 0; i < num_groups; i++) {
        ABTI_preemption_group *p_group = (ABTI_preemption_group *)ABTU_malloc(
                sizeof(ABTI_preemption_group));
        /* TODO: use get_handle */
        new_groups[i] = (ABT_preemption_group)p_group;

        uint64_t duration = base_intvl / num_groups;
        p_group->offset_nsec = 1 + i * duration;

        p_group->delete_counter = 0;
    }

  fn_exit:
    ABTI_LEAVE;
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABT_preemption_timer_set_xstreams(ABT_preemption_group group,
                                      int num_xstreams, ABT_xstream *xstreams)
{
    ABTI_ENTER;
    int abt_errno = ABT_SUCCESS;
    int i;

    /* TODO: use get_ptr */
    ABTI_preemption_group *p_group = (ABTI_preemption_group *)group;
    p_group->num_xstreams = num_xstreams;
    p_group->p_xstreams = (ABTI_xstream **)ABTU_malloc(
            sizeof(ABTI_xstream *) * num_xstreams);

    /* Wait until timers are created */
    for (i = 0; i < num_xstreams; i++) {
        ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstreams[i]);
        p_group->p_xstreams[i] = p_xstream;
        ABTI_CHECK_TRUE_MSG(ABTD_atomic_load_int32((int32_t *)&p_xstream->state)
                            != ABT_XSTREAM_STATE_CREATED,
                            ABT_ERR_XSTREAM,
                            "This function must be called after ESs start.");

        while (ABTD_atomic_load_ptr((void *)&p_xstream->p_preemption_timer)
               == NULL);
    }

    uint64_t base_intvl = gp_ABTI_global->preemption_intvl_usec * 1000;
    /* settings for timers of each ES in preemption groups */
    for (i = 0; i < num_xstreams; i++) {
        ABTI_xstream *p_xstream = p_group->p_xstreams[i];
        ABTI_preemption_timer *p_ptimer = p_xstream->p_preemption_timer;
        p_ptimer->preemption_index = i;
        p_ptimer->p_group          = p_group;
        p_ptimer->intvl            = base_intvl * p_group->num_xstreams;
        p_ptimer->offset           = p_group->offset_nsec + i * base_intvl;

        if (gp_ABTI_global->preemption_timer_type == ABTI_PREEMPTION_TIMER_DEDICATED) {
            /* Create dedicated kernel-level thread timers */
            /* TODO: fix # of HW threads */
            int n_physical_cores = gp_ABTI_global->num_cores / 2;
            p_ptimer->rank           = p_xstream->rank + n_physical_cores;
            p_ptimer->sleep_flag     = 0;
            p_ptimer->terminate_flag = 0;
            p_ptimer->start_flag     = 0;
            p_ptimer->stop_flag      = 0;
            abt_errno = ABTD_xstream_context_create(
                    ABTI_preemption_timer_dedicated_fn, (void *)p_ptimer,
                    &p_ptimer->ctx);
            ABTI_CHECK_ERROR_MSG(abt_errno, "ABTD_xstream_context_create");
        }
    }

  fn_exit:
    ABTI_LEAVE;
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABT_preemption_timer_start(ABT_preemption_group group)
{
    ABTI_ENTER;
    int abt_errno = ABT_SUCCESS;
    int i;

    /* TODO: use get_ptr */
    ABTI_preemption_group *p_group = (ABTI_preemption_group *)group;
    p_group->index_next = -1;
    p_group->in_chain   = ABT_FALSE;

    /* start timers */
    for (i = 0; i < p_group->num_xstreams; i++) {
        ABTI_xstream *p_xstream = p_group->p_xstreams[i];
        p_xstream->ptimer_on = ABT_TRUE;
        ABTI_preemption_timer *p_ptimer = p_xstream->p_preemption_timer;

        if (gp_ABTI_global->preemption_timer_type == ABTI_PREEMPTION_TIMER_SIGNAL) {
            abt_errno =
                ABTD_signal_timer_settime_abs(p_ptimer->offset, p_ptimer->intvl,
                                              p_xstream->p_cur_sub->timer);
            ABTI_CHECK_ERROR(abt_errno);
        } else {
            while (!ABTD_atomic_load_int32(&p_ptimer->sleep_flag)) {
                sched_yield();
            }
            ABTD_atomic_store_uint32(&p_ptimer->start_flag, 1);
            abt_errno = ABTD_futex_wakeup(&p_ptimer->sleep_flag);
            ABTI_CHECK_ERROR_MSG(abt_errno, "ABTD_futex_wakeup");
        }
    }

  fn_exit:
    ABTI_LEAVE;
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABT_preemption_timer_stop(ABT_preemption_group group)
{
    ABTI_ENTER;
    int abt_errno = ABT_SUCCESS;
    int i;

    /* TODO: use get_ptr */
    ABTI_preemption_group *p_group = (ABTI_preemption_group *)group;

    for (i = 0; i < p_group->num_xstreams; i++) {
        ABTI_xstream *p_xstream = p_group->p_xstreams[i];
        p_xstream->ptimer_on = ABT_FALSE;
        ABTI_preemption_timer *p_ptimer = p_xstream->p_preemption_timer;

        if (gp_ABTI_global->preemption_timer_type == ABTI_PREEMPTION_TIMER_SIGNAL) {
            abt_errno = ABTD_signal_timer_stop(p_xstream->p_cur_sub->timer);
            ABTI_CHECK_ERROR(abt_errno);
        } else {
            while (!ABTD_atomic_load_int32(&p_ptimer->sleep_flag)) {
                sched_yield();
            }
            ABTD_atomic_store_uint32(&p_ptimer->stop_flag, 1);
            abt_errno = ABTD_futex_wakeup(&p_ptimer->sleep_flag);
            ABTI_CHECK_ERROR_MSG(abt_errno, "ABTD_futex_wakeup");
        }
    }

  fn_exit:
    ABTI_LEAVE;
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABT_preemption_timer_delete(ABT_preemption_group group)
{
    ABTI_ENTER;
    int abt_errno = ABT_SUCCESS;
    int i;

    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream();

    abt_errno = ABT_preemption_timer_stop(group);
    ABTI_CHECK_ERROR(abt_errno);

    /* TODO: use get_ptr */
    ABTI_preemption_group *p_group = (ABTI_preemption_group *)group;

    if (gp_ABTI_global->preemption_timer_type == ABTI_PREEMPTION_TIMER_SIGNAL) {
        /* To make sure the preemption group data is not accessed in each ES's
         * signal handler (i.e., p_ptimer->disabled is set in every ES), it uses
         * an atomic variable (delete_counter). */
        for (i = 0; i < p_group->num_xstreams; i++) {
            ABTI_xstream *p_xstream = p_group->p_xstreams[i];
            if (p_xstream->rank == p_local_xstream->rank) {
                ABTD_atomic_fetch_add_uint32(&p_group->delete_counter, 1);
            } else {
                ABTI_xstream_set_request(p_xstream, ABTI_XSTREAM_REQ_FREE_TIMER);
                // Wakeup sleeping ES
                // TODO: fix workaround
                p_xstream->disable_sleep = ABT_TRUE;
                if (p_xstream->should_sleep) {
                    while (p_xstream->should_sleep);
                    ABTI_xstream_wakeup(p_xstream);
                } else if (p_xstream->sleep_flag) {
                    ABTI_xstream_wakeup(p_xstream);
                }
            }
        }

        /* wait until it is ready to free */
        while (ABTD_atomic_load_uint32(&p_group->delete_counter)
               != p_group->num_xstreams);

    } else {
        for (i = 0; i < p_group->num_xstreams; i++) {
            ABTI_xstream *p_xstream = p_group->p_xstreams[i];
            ABTI_preemption_timer *p_ptimer = p_xstream->p_preemption_timer;
            while (!ABTD_atomic_load_int32(&p_ptimer->sleep_flag)) {
                sched_yield();
            }
            ABTD_atomic_store_uint32(&p_ptimer->terminate_flag, 1);
            abt_errno = ABTD_futex_wakeup(&p_ptimer->sleep_flag);
            ABTI_CHECK_ERROR_MSG(abt_errno, "ABTD_futex_wakeup");

            ABTD_xstream_context_join(p_ptimer->ctx);
        }
    }

    ABTU_free(p_group->p_xstreams);
    ABTU_free(p_group);

  fn_exit:
    ABTI_LEAVE;
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABTI_preemption_timer_create(ABTI_xstream *p_xstream)
{
    int abt_errno = ABT_SUCCESS;

    ABTI_preemption_timer *p_ptimer = (ABTI_preemption_timer *)ABTU_malloc(
            sizeof(ABTI_preemption_timer));

    if (gp_ABTI_global->preemption_timer_type == ABTI_PREEMPTION_TIMER_SIGNAL) {
        abt_errno = ABTD_signal_timer_create(&p_xstream->p_cur_sub->timer);
        ABTI_CHECK_ERROR(abt_errno);
    }

    ABTD_atomic_store_ptr((void **)&p_xstream->p_preemption_timer, p_ptimer);

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABTI_preemption_timer_delete(ABTI_xstream *p_xstream)
{
    int abt_errno = ABT_SUCCESS;

    ABTI_ASSERT(!p_xstream->ptimer_on);

    ABTI_preemption_timer *p_ptimer = p_xstream->p_preemption_timer;

    if (gp_ABTI_global->preemption_timer_type == ABTI_PREEMPTION_TIMER_SIGNAL) {
        abt_errno = ABTD_signal_timer_delete(p_xstream->p_cur_sub->timer);
        ABTI_CHECK_ERROR(abt_errno);
    }

    ABTU_free(p_ptimer);

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}
