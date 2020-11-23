/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

ABTD_XSTREAM_LOCAL ABTI_sub_xstream *lp_ABTI_sub_xstream = NULL;

ABTI_sub_xstream *ABTI_sub_xstream_get_data()
{
    return lp_ABTI_sub_xstream;
}

ABTI_sub_xstream *(*gp_ABTI_get_sub_xstream)(void) = ABTI_sub_xstream_get_data;

static uint32_t g_sub_stream_id = 0;

static uint32_t ABTI_sub_xstream_get_id() {
    return ABTD_atomic_fetch_add_uint32(&g_sub_stream_id, 1);
}

static void ABTI_sub_xstream_on_initial_sleep(void *p_arg)
{
    ABTI_sub_xstream *p_sub_xstream = (ABTI_sub_xstream *)p_arg;
    ABTD_atomic_store_uint32(&p_sub_xstream->ready, 1);

    /* If this sub ES is allocated locally (per ES), it just sets `ready` flag
     * and the main ES will add it to the ES list. If this is globally
     * allocated, it should add itself to the global ES list. */
    if (p_sub_xstream->global_alloc) {
        ABTI_sub_xstream_allocator *p_allocator = gp_ABTI_global->p_sub_allocator;
        ABTI_sub_xstream_list_global_add(&p_allocator->sub_list, p_sub_xstream, p_sub_xstream);
        ABTD_atomic_fetch_sub_uint32(&p_allocator->create_count, 1);
    }
}

static void ABTI_sub_xstream_on_sleep_and_wakeup(void *p_arg)
{
    ABTI_sub_xstream *p_sub_xstream = (ABTI_sub_xstream *)p_arg;
    ABTI_xstream *p_xstream = ABTI_local_get_xstream();
    ABTI_sub_xstream_list_local *p_sub_list = &p_xstream->sub_xstream_list;

    /* Add itself to sub ES list. It is allowed because the addition is
     * serialized in the ES execution if we add it before waking up the next
     * sub ES here. */
    ABTI_sub_xstream_list_add(p_sub_list, p_sub_xstream);

    /* wake up the sub ES associated with the next thread */
    ABTI_thread *p_wakeup_thread = p_xstream->p_wakeup_thread;
    if (p_wakeup_thread) {
        p_xstream->p_wakeup_thread = NULL;
        ABTI_local_set_thread(p_wakeup_thread);
        ABTI_sub_xstream_wakeup(p_wakeup_thread->p_wakeup, p_xstream);
    }
}

static void *ABTI_sub_xstream_base(void *p_arg)
{
    int abt_errno = ABT_SUCCESS;
    ABTI_sub_xstream *p_sub_xstream = (ABTI_sub_xstream *)p_arg;
    ABTI_thread *p_basethread;
    ABTI_xstream *p_xstream;
    ABTI_sched *p_sched;

    ABTD_signal_change_num_es_block();

    lp_ABTI_sub_xstream = p_sub_xstream;

    abt_errno = ABTI_thread_create_internal(NULL, NULL, &p_basethread);
    ABTI_CHECK_ERROR(abt_errno);

    /* create a timer */
    abt_errno = ABTD_signal_timer_create(&p_sub_xstream->timer);
    ABTI_CHECK_ERROR(abt_errno);

    p_sub_xstream->p_base_thread = p_basethread;

    ABTI_sub_xstream_sleep(p_sub_xstream, &p_xstream,
                           ABTI_sub_xstream_on_initial_sleep, p_sub_xstream);
    ABTI_PREEMPTION_DEBUG("wakeup from pool");

    while (1) {
        if (p_sub_xstream->terminate_flag) {
            break;
        }

        p_sched = ABTI_xstream_get_top_sched(p_xstream);

#if ABT_ENABLE_PREEMPTION_PROFILE
        uint64_t t1 = mlog_clock_gettime_in_nsec();
        ABTI_mlog_end_t(t1, "wakeup a new sub ES");
        ABTI_mlog_begin_t(t1);
#endif

        ABTI_thread *p_wakeup_thread = p_xstream->p_wakeup_thread;
        if (p_wakeup_thread) {
            ABTI_PREEMPTION_DEBUG_1("switch to thread (%p)", p_wakeup_thread);
            /* neseccary only for ES join */
            p_xstream->p_wakeup_thread = NULL;
            ABTI_thread_context_switch_thread_to_thread(p_basethread,
                                                        p_wakeup_thread);
            ABTI_PREEMPTION_DEBUG_1("return from thread (%p)", p_wakeup_thread);
        } else {
            ABTI_PREEMPTION_DEBUG("switch to sched");
            /* switch to sched */
            ABTI_thread_context_switch_thread_to_sched(p_basethread, p_sched);
            ABTI_PREEMPTION_DEBUG("return from sched");
        }
        /* return from sched (or thread) */

        /* local variables can change; sub ESs are migratable to other cores */
        p_xstream = ABTI_local_get_xstream_explicit();

#if ABT_ENABLE_PREEMPTION_PROFILE
        uint64_t t3 = mlog_clock_gettime_in_nsec();
        ABTI_mlog_end_t(t3, "sched");
        ABTI_mlog_begin_t(t3);
#endif

        /* Stop a signal-based timer if it's started */
        if (p_sub_xstream->timer_started) {
            p_sub_xstream->timer_started = 0;

            /* stop a timer */
            if (gp_ABTI_global->preemption_timer_type == ABTI_PREEMPTION_TIMER_SIGNAL) {
                abt_errno = ABTD_signal_timer_stop(p_sub_xstream->timer);
                ABTI_CHECK_ERROR(abt_errno);
            }

#if ABT_ENABLE_PREEMPTION_PROFILE
            uint64_t t4 = mlog_clock_gettime_in_nsec();
            ABTI_mlog_end_t(t4, "stop a timer");
            ABTI_mlog_begin_t(t4);
#endif
        }

        ABTI_PREEMPTION_DEBUG("sleep in pool");
        /* wakeup the sub ES associated with the next thread and sleep again */
        ABTI_sub_xstream_sleep(p_sub_xstream, &p_xstream,
                               ABTI_sub_xstream_on_sleep_and_wakeup,
                               p_sub_xstream);
        ABTI_PREEMPTION_DEBUG("wakeup from pool");
    }
    ABTI_PREEMPTION_DEBUG("terminate");

    /* delete a timer */
    abt_errno = ABTD_signal_timer_delete(p_sub_xstream->timer);
    ABTI_CHECK_ERROR(abt_errno);

  fn_exit:
    return NULL;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

static void ABTI_xstream_trampoline_on_sleep(void *p_arg)
{
    ABTI_sub_xstream *p_sub_xstream = (ABTI_sub_xstream *)p_arg;
    ABTI_xstream *p_xstream = ABTI_local_get_xstream();

    ABTD_atomic_store_uint32(&p_sub_xstream->ready, 1);

    /* wake up the sub ES associated with the next thread */
    ABTI_thread *p_wakeup_thread = p_xstream->p_wakeup_thread;
    if (p_wakeup_thread) {
        p_xstream->p_wakeup_thread = NULL;
        ABTI_local_set_thread(p_wakeup_thread);
        ABTI_sub_xstream_wakeup(p_wakeup_thread->p_wakeup, p_xstream);
    }

    /* TODO: The main ES is not added to the sub ES list. It may be waste of
     * resources. */
}

static void ABTI_xstream_trampoline_thread(void *p_arg)
{
    ABTI_xstream *p_xstream;
    ABTI_sub_xstream *p_sub_xstream = ABTI_sub_xstream_get_data();
    ABTI_ASSERT(p_sub_xstream->is_main);

    while (1) {
        /* Stop a signal-based timer */
        if (gp_ABTI_global->preemption_timer_type == ABTI_PREEMPTION_TIMER_SIGNAL) {
            ABTD_signal_timer_stop(p_sub_xstream->timer);
        }

        ABTI_PREEMPTION_DEBUG("trampoline sleep");
        /* Now we only have to wake up the sub ES associated with the next
         * thread and sleep immediately. */
        ABTI_sub_xstream_sleep(p_sub_xstream, &p_xstream,
                               ABTI_xstream_trampoline_on_sleep, p_sub_xstream);
        /* TODO: It is woken up only when finalizing ESes. It may be waste of
         * resources. */
        ABTI_PREEMPTION_DEBUG("trampoline wakeup");

        /* switch to the specified thread (trampoline) */
        ABTI_thread *p_this_thread = p_sub_xstream->p_base_thread;
        ABTI_thread_context_switch_thread_to_thread(p_this_thread,
                p_sub_xstream->p_return_thread);
    }
}

int ABTI_sub_xstream_create(ABTI_sub_xstream **pp_sub_xstream, int global_alloc)
{
    int abt_errno = ABT_SUCCESS;

    ABTI_sub_xstream *p_sub_xstream = (ABTI_sub_xstream *)ABTU_malloc(
            sizeof(ABTI_sub_xstream));
    p_sub_xstream->id              = ABTI_sub_xstream_get_id();
    p_sub_xstream->is_main         = ABT_FALSE;
    p_sub_xstream->sleep_flag      = 0;
    p_sub_xstream->ready           = 0;
    p_sub_xstream->terminate_flag  = 0;
    p_sub_xstream->p_local         = NULL;
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    p_sub_xstream->p_local_log     = NULL;
#endif
    p_sub_xstream->affinity        = -1;
    p_sub_xstream->p_return_thread = NULL;
    p_sub_xstream->timer_started   = 0;
    p_sub_xstream->global_alloc    = global_alloc;

    abt_errno = ABTD_xstream_context_create(ABTI_sub_xstream_base,
                                            (void *)p_sub_xstream,
                                            &p_sub_xstream->ctx);
    ABTI_CHECK_ERROR(abt_errno);

    *pp_sub_xstream = p_sub_xstream;

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABTI_sub_xstream_sleep(ABTI_sub_xstream *p_sub_xstream,
                           ABTI_xstream **pp_xstream,
                           void (*callback_fn)(void *), void *p_arg)
{
    int abt_errno = ABT_SUCCESS;

#if ABT_USE_FUTEX_ON_SLEEP
    ABTD_futex_sleep(&p_sub_xstream->sleep_flag, callback_fn, p_arg);
#else
    ABTD_signal_sleep(callback_fn, p_arg);
#endif

    /* Now this sub ES is woken up. ES local variables can be changed, so we
     * have to reload them. ES local data must be passed through the sub ES
     * data structure. */
    lp_ABTI_local = p_sub_xstream->p_local;
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    lp_ABTI_log = (ABTI_log *)p_sub_xstream->p_local_log;
#endif

    /* reload ES local data */
    *pp_xstream = ABTI_local_get_xstream();

    /* update the current sub ES in ES local data */
    (*pp_xstream)->p_cur_sub = p_sub_xstream;

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABTI_sub_xstream_wakeup(ABTI_sub_xstream *p_sub_xstream,
                            ABTI_xstream *p_xstream)
{
    int abt_errno = ABT_SUCCESS;

    /* set affinity of the woken-up sub ES */
    if (gp_ABTI_global->set_affinity == ABT_TRUE &&
        p_sub_xstream->affinity != p_xstream->rank) {
        ABTD_affinity_set(p_sub_xstream->ctx, p_xstream->rank);
        p_sub_xstream->affinity = p_xstream->rank;
#if ABT_ENABLE_PREEMPTION_PROFILE
        uint64_t t = mlog_clock_gettime_in_nsec();
        ABTI_mlog_end_t(t, "set affinity");
        ABTI_mlog_begin_t(t);
#endif
    }

    /* pass ES local data to the woken-up sub ES */
    p_sub_xstream->p_local = lp_ABTI_local;
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    p_sub_xstream->p_local_log = (void *)lp_ABTI_log;
#endif

#if ABT_USE_FUTEX_ON_SLEEP
    abt_errno = ABTD_futex_wakeup(&p_sub_xstream->sleep_flag);
#else
    abt_errno = ABTD_signal_wakeup(p_sub_xstream->ctx);
#endif
    ABTI_CHECK_ERROR(abt_errno);

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABTI_sub_xstream_free(ABTI_sub_xstream *p_sub_xstream)
{
    int abt_errno = ABT_SUCCESS;
    ABTI_xstream *p_xstream = ABTI_local_get_xstream();

    p_sub_xstream->terminate_flag = 1;

    abt_errno = ABTI_sub_xstream_wakeup(p_sub_xstream, p_xstream);
    ABTI_CHECK_ERROR(abt_errno);

    abt_errno = ABTD_xstream_context_join(p_sub_xstream->ctx);
    ABTI_CHECK_ERROR(abt_errno);

    ABTU_free(p_sub_xstream);

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

ABT_bool ABTI_sub_xstream_exit_main(ABTI_thread *p_this_thread)
{
    ABTI_xstream *p_xstream = ABTI_local_get_xstream();
    ABTI_sub_xstream *p_main = p_xstream->p_main;
    ABTI_sub_xstream *p_local = ABTI_sub_xstream_get_data();

    /* If this sub ES is the main ES of another ES, it should exit this sub ES
     * and switches to another sub ES. */
    if (p_local->is_main && p_local != p_main) {
        if (p_local->is_main) {
            /* get an available sub ES. Here we assume that main ESes are not
             * included in the sub ES list. */
            ABTI_sub_xstream *p_sub_xstream = NULL;
            ABTI_sub_xstream_list_local *p_sub_list = &p_xstream->sub_xstream_list;
            ABTI_sub_xstream_list_pop(p_sub_list, &p_sub_xstream);
            if (p_sub_xstream == NULL) {
                /* If no sub ES is avaliable in the pool, it must not wait for
                 * new allocation because of a potential deadlock */
                return ABT_FALSE;
            }

            /* Return to the local base context (trampoline thread). We set
             * this thread to be woken up with the sub ES that was popped
             * above. */
            p_this_thread->p_wakeup = p_sub_xstream;
            p_xstream->p_wakeup_thread = p_this_thread;
            ABTI_thread_context_switch_thread_to_thread(p_this_thread,
                                                        p_local->p_base_thread);
        }
    }

    return ABT_TRUE;
}

int ABTI_sub_xstream_back_to_main(ABTI_thread *p_this_thread)
{
    int abt_errno = ABT_SUCCESS;

    /* First it must exit the main ES of another ES. We can wait for new
     * allocation of sub ESes here, assuming this function is not called
     * in scheduler context. */
    while (!ABTI_sub_xstream_exit_main(p_this_thread)) {
        sched_yield();
    };

    ABTI_xstream *p_xstream = ABTI_local_get_xstream();
    ABTI_sub_xstream *p_main = p_xstream->p_main;
    ABTI_sub_xstream *p_local = ABTI_sub_xstream_get_data();

    /* If the current local sub ES is not the main one, it must return to the
     * main one. */
    if (p_local != p_main) {
        /* Wait until the main ES surely sleeps */
        while (!ABTD_atomic_load_uint32(&p_main->ready)) {
            sched_yield();
        }
        p_main->ready = 0;

        /* Return to the local base context and wakeup the main ES sleeping in
         * the trampoline thread. The main ES should context switch to this
         * continuation. */
        p_main->p_return_thread = p_this_thread;
        p_xstream->p_wakeup_thread = p_main->p_base_thread;
        p_xstream->p_wakeup_thread->p_wakeup = p_main;
        ABTI_thread_context_switch_thread_to_thread(p_this_thread,
                p_local->p_base_thread);
        /* now switched from the trampoline thread */
    }
    ABTI_ASSERT(ABTI_sub_xstream_get_data_explicit() == p_main);

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABTI_sub_xstream_list_local_init(ABTI_sub_xstream_list_local *p_sub_list,
                                     int init_num)
{
    int abt_errno = ABT_SUCCESS;
    int i;

    ABTI_ASSERT(init_num > 0);

    ABTI_xstream *p_xstream = ABTI_local_get_xstream();

    /* TODO: temp for main ES */
    ABTI_sub_xstream *p_sub_xstream = (ABTI_sub_xstream *)ABTU_malloc(
            sizeof(ABTI_sub_xstream));
    p_sub_xstream->id              = ABTI_sub_xstream_get_id();
    p_sub_xstream->is_main         = ABT_TRUE;
    p_sub_xstream->sleep_flag      = 0;
    p_sub_xstream->ready           = 0;
    p_sub_xstream->terminate_flag  = 0;
    p_sub_xstream->p_local         = NULL;
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    p_sub_xstream->p_local_log     = NULL;
#endif
    p_sub_xstream->affinity        = ABTI_local_get_xstream()->rank;
    p_sub_xstream->p_return_thread = NULL;
    p_sub_xstream->timer_started   = 0;
    abt_errno = ABTI_thread_create_internal(
            ABTI_xstream_trampoline_thread, NULL,
            &p_sub_xstream->p_base_thread);
    ABTI_CHECK_ERROR(abt_errno);

    ABTD_xstream_context_self(&p_sub_xstream->ctx);

    p_xstream->p_main = p_sub_xstream;
    p_xstream->p_cur_sub = p_sub_xstream;
    lp_ABTI_sub_xstream = p_sub_xstream;

    p_sub_list->num = 0;
    p_sub_list->p_head = NULL;

    for (i = 0; i < init_num; i++) {
        ABTI_sub_xstream *p_sub_xstream;
        /* create a new sub ES */
        abt_errno = ABTI_sub_xstream_create(&p_sub_xstream, 0);
        ABTI_CHECK_ERROR(abt_errno);

        /* Wait until a newly-created sub ES sleeps */
        while (!ABTD_atomic_load_uint32(&p_sub_xstream->ready)) {
            sched_yield();
        }

        ABTI_sub_xstream_list_add(p_sub_list, p_sub_xstream);
    }

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABTI_sub_xstream_list_local_free(ABTI_sub_xstream_list_local *p_sub_list)
{
    int abt_errno = ABT_SUCCESS;

    /* TODO: temp */
    ABTU_free(lp_ABTI_sub_xstream);

    while (p_sub_list->num > 0) {
        ABTI_sub_xstream *p_sub_xstream;
        ABTI_sub_xstream_list_local_pop(p_sub_list, &p_sub_xstream);
        /* free sub ES */
        abt_errno = ABTI_sub_xstream_free(p_sub_xstream);
        ABTI_CHECK_ERROR(abt_errno);
    }

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABTI_sub_xstream_list_local_add(ABTI_sub_xstream_list_local *p_sub_list,
                                    ABTI_sub_xstream *p_sub_xstream)
{
    int abt_errno = ABT_SUCCESS;

    p_sub_xstream->p_next = p_sub_list->p_head;
    p_sub_list->p_head = p_sub_xstream;
    p_sub_list->num++;

    return abt_errno;
}

int ABTI_sub_xstream_list_local_pop(ABTI_sub_xstream_list_local *p_sub_list,
                                    ABTI_sub_xstream **pp_sub_xstream)
{
    int abt_errno = ABT_SUCCESS;

    ABTI_sub_xstream *p_sub_ret;

    if (p_sub_list->num > 0) {
        p_sub_ret = p_sub_list->p_head;
        p_sub_list->p_head = p_sub_ret->p_next;
        p_sub_list->num--;

        p_sub_ret->p_next = NULL;

        *pp_sub_xstream = p_sub_ret;
    } else {
        *pp_sub_xstream = NULL;
    }

    return abt_errno;
}

int ABTI_sub_xstream_list_global_init(ABTI_sub_xstream_list_global *p_sub_list)
{
    p_sub_list->p_head = NULL;
    return ABT_SUCCESS;
}

int ABTI_sub_xstream_list_global_add(ABTI_sub_xstream_list_global *p_sub_list,
                                     ABTI_sub_xstream *p_sub_head,
                                     ABTI_sub_xstream *p_sub_tail)
{
    int abt_errno = ABT_SUCCESS;

    void **ptr;
    void *old, *new;

    do {
        ABTI_sub_xstream *p_list_head = (ABTI_sub_xstream *)
            ABTD_atomic_load_ptr((void **)&p_sub_list->p_head);
        p_sub_tail->p_next = p_list_head;
        ptr = (void **)&p_sub_list->p_head;
        old = (void *)p_list_head;
        new = (void *)p_sub_head;
    } while (!ABTD_atomic_bool_cas_weak_ptr(ptr, old, new));

    return abt_errno;
}

int ABTI_sub_xstream_list_global_pop(ABTI_sub_xstream_list_global *p_sub_list,
                                     ABTI_sub_xstream **pp_sub_xstream)
{
    int abt_errno = ABT_SUCCESS;

    ABTI_sub_xstream *p_sub_ret;
    void **ptr;
    void *old, *new;

    do {
        p_sub_ret = (ABTI_sub_xstream *)
            ABTD_atomic_load_ptr((void **)&p_sub_list->p_head);
        if (p_sub_ret == NULL) break;
        ptr = (void **)&p_sub_list->p_head;
        old = (void *)p_sub_ret;
        new = (void *)p_sub_ret->p_next;
    } while (!ABTD_atomic_bool_cas_weak_ptr(ptr, old, new));

    *pp_sub_xstream = p_sub_ret;

    return abt_errno;
}

int ABTI_sub_xstream_list_global_pop_all(ABTI_sub_xstream_list_global *p_sub_list,
                                         ABTI_sub_xstream **pp_sub_xstream)
{
    int abt_errno = ABT_SUCCESS;

    ABTI_sub_xstream *p_sub_ret;
    void **ptr;
    void *old;

    do {
        p_sub_ret = (ABTI_sub_xstream *)
            ABTD_atomic_load_ptr((void **)&p_sub_list->p_head);
        ptr = (void **)&p_sub_list->p_head;
        old = (void *)p_sub_ret;
    } while (!ABTD_atomic_bool_cas_weak_ptr(ptr, old, NULL));

    *pp_sub_xstream = p_sub_ret;

    return abt_errno;
}

int ABTI_sub_xstream_list_add(ABTI_sub_xstream_list_local *p_sub_list,
                              ABTI_sub_xstream *p_sub_xstream)
{
    int abt_errno = ABT_SUCCESS;

#if ABT_SUB_XSTREAM_LOCAL_POOL
    if (p_sub_list->num < gp_ABTI_global->initial_num_sub_xstreams) {
        ABTI_sub_xstream_list_local_add(p_sub_list, p_sub_xstream);
    } else {
        ABTI_sub_xstream_list_global_add(&gp_ABTI_global->p_sub_allocator->sub_list,
                                         p_sub_xstream, p_sub_xstream);
    }
#else
    ABTI_sub_xstream_list_global_add(&gp_ABTI_global->p_sub_allocator->sub_list,
                                     p_sub_xstream, p_sub_xstream);
#endif

    return abt_errno;
}

int ABTI_sub_xstream_list_pop(ABTI_sub_xstream_list_local *p_sub_list,
                              ABTI_sub_xstream **pp_sub_xstream)
{
    int abt_errno = ABT_SUCCESS;

#if ABT_SUB_XSTREAM_LOCAL_POOL
    if (p_sub_list->num > 0) {
        ABTI_sub_xstream_list_local_pop(p_sub_list, pp_sub_xstream);
    } else {
        /* first pop all sub ESes from the global list */
        ABTI_sub_xstream_list_global_pop(&gp_ABTI_global->p_sub_allocator->sub_list,
                                         pp_sub_xstream);
        if (*pp_sub_xstream == NULL) {
            /* If there is no sub ES in the global list, request allocation */
            ABTI_sub_xstream_allocator_req(gp_ABTI_global->p_sub_allocator);
        } else {
            (*pp_sub_xstream)->p_next = NULL;
        }
    }
#else
    /* first pop all sub ESes from the global list */
    ABTI_sub_xstream_list_global_pop(&gp_ABTI_global->p_sub_allocator->sub_list,
                                     pp_sub_xstream);
    if (*pp_sub_xstream == NULL) {
        /* If there is no sub ES in the global list, request allocation */
        ABTI_sub_xstream_allocator_req(gp_ABTI_global->p_sub_allocator);
    } else {
        (*pp_sub_xstream)->p_next = NULL;
    }
#endif

    return abt_errno;
}

static void *ABTI_sub_xstream_allocator_fn(void *p_arg)
{
    int abt_errno = ABT_SUCCESS;
    int i;
    ABTI_sub_xstream_allocator *p_allocator = (ABTI_sub_xstream_allocator *)p_arg;

    while (1) {
        abt_errno = ABTD_futex_sleep(&p_allocator->sleep_flag, NULL, NULL);
        ABTI_CHECK_ERROR_MSG(abt_errno, "ABTD_futex_sleep");

        if (p_allocator->terminate_flag) {
            break;
        }

        ABTI_ASSERT(p_allocator->create_count == 0);

        int num_alloc = gp_ABTI_global->initial_num_sub_xstreams;
        p_allocator->create_count = num_alloc;

        for (i = 0; i < num_alloc; i++) {
            ABTI_sub_xstream *p_sub_xstream;
            abt_errno = ABTI_sub_xstream_create(&p_sub_xstream, 1);
            ABTI_CHECK_ERROR(abt_errno);
        }
    }

  fn_exit:
    return NULL;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABTI_sub_xstream_allocator_init(ABTI_sub_xstream_allocator **pp_allocator)
{
    int abt_errno = ABT_SUCCESS;

    ABTI_sub_xstream_allocator *p_allocator = (ABTI_sub_xstream_allocator *)
        ABTU_malloc(sizeof(ABTI_sub_xstream_allocator));

    p_allocator->create_count = 0;
    p_allocator->terminate_flag = 0;

    ABTI_sub_xstream_list_global_init(&p_allocator->sub_list);

    *pp_allocator = p_allocator;

    /* wrapper of pthread_create */
    abt_errno = ABTD_xstream_context_create(
            ABTI_sub_xstream_allocator_fn, (void *)p_allocator,
            &p_allocator->ctx);
    ABTI_CHECK_ERROR_MSG(abt_errno, "ABTD_xstream_context_create");

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABTI_sub_xstream_allocator_free(ABTI_sub_xstream_allocator *p_allocator)
{
    int abt_errno = ABT_SUCCESS;

    /* wait until all sub ESes previously allocated are added to the list */
    while (p_allocator->create_count > 0) {
        sched_yield();
    }

    /* wait until the allocator surely sleeps */
    while (!p_allocator->sleep_flag) {
        sched_yield();
    }

    ABTD_atomic_store_uint32(&p_allocator->terminate_flag, 1);

    abt_errno = ABTD_futex_wakeup(&p_allocator->sleep_flag);
    ABTI_CHECK_ERROR_MSG(abt_errno, "ABTD_futex_wakeup");

    ABTD_xstream_context_join(p_allocator->ctx);

    /* free all sub ESes in the global list */
    ABTI_sub_xstream *p_sub_xstream;
    ABTI_sub_xstream_list_global_pop_all(&p_allocator->sub_list, &p_sub_xstream);
    while (p_sub_xstream) {
        ABTI_sub_xstream *p_tmp = p_sub_xstream->p_next;

        abt_errno = ABTI_sub_xstream_free(p_sub_xstream);
        ABTI_CHECK_ERROR(abt_errno);

        p_sub_xstream = p_tmp;
    }

    ABTU_free(p_allocator);

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

int ABTI_sub_xstream_allocator_req(ABTI_sub_xstream_allocator *p_allocator)
{
    int abt_errno = ABT_SUCCESS;

    if (p_allocator->create_count == 0) {
        /* TODO: strictly this is not correct. Similar to ABA problem.
         * Use a version (tag). */
        abt_errno = ABTD_futex_wakeup(&p_allocator->sleep_flag);
        ABTI_CHECK_ERROR_MSG(abt_errno, "ABTD_futex_wakeup");
    }

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}
