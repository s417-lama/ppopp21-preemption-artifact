/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup ENV Init & Finalize
 * This group is for initialization and finalization of the Argobots
 * environment.
 */

/* Global Data */
ABTI_global *gp_ABTI_global = NULL;

/* To indicate how many times ABT_init is called. */
static uint32_t g_ABTI_num_inits = 0;
/* A global lock protecting the initialization/finalization process */
static uint8_t g_ABTI_init_lock = 0;
/* A flag whether Argobots has been initialized or not */
static uint32_t g_ABTI_initialized = 0;

static inline void ABTI_init_lock_acquire() {
    while (ABTD_atomic_test_and_set_uint8(&g_ABTI_init_lock)) {
        /* Busy-wait is allowed since this function may not be run in
         * ULT context. */
        while (ABTD_atomic_load_uint8(&g_ABTI_init_lock) != 0)
            ABTD_atomic_pause();
    }
}

static inline int ABTI_init_lock_is_locked() {
    return ABTD_atomic_load_uint8(&g_ABTI_init_lock) != 0;
}

static inline void ABTI_init_lock_release() {
    ABTD_atomic_clear_uint8(&g_ABTI_init_lock);
}


/**
 * @ingroup ENV
 * @brief   Initialize the Argobots execution environment.
 *
 * \c ABT_init() initializes the Argobots library and its execution environment.
 * It internally creates objects for the \a primary ES and the \a primary ULT.
 *
 * \c ABT_init() must be called by the primary ULT before using any other
 * Argobots functions. \c ABT_init() can be called again after
 * \c ABT_finalize() is called.
 *
 * @param[in] argc the number of arguments
 * @param[in] argv the argument vector
 * @return Error code
 * @retval ABT_SUCCESS on success
 */
int ABT_init(int argc, char **argv)
{
    ABTI_UNUSED(argc); ABTI_UNUSED(argv);
    int abt_errno = ABT_SUCCESS;

    ABTI_mlog_init();

    /* First, take a global lock protecting the initialization/finalization
     * process. Don't go to fn_exit before taking a lock */
    ABTI_init_lock_acquire();

    /* If Argobots has already been initialized, just return */
    if (g_ABTI_num_inits++ > 0)
        goto fn_exit;

    gp_ABTI_global = (ABTI_global *)ABTU_malloc(sizeof(ABTI_global));

    /* Initialize the system environment */
    ABTD_env_init(gp_ABTI_global);

    /* Initialize memory pool */
    ABTI_mem_init(gp_ABTI_global);

    /* Initialize the event environment */
    ABTI_event_init();

    /* Initialize IDs */
    ABTI_thread_reset_id();
    ABTI_task_reset_id();
    ABTI_sched_reset_id();
    ABTI_pool_reset_id();

    /* Initialize the ES array */
    gp_ABTI_global->p_xstreams = (ABTI_xstream **)ABTU_calloc(
            gp_ABTI_global->max_xstreams, sizeof(ABTI_xstream *));
    gp_ABTI_global->num_xstreams = 0;
    gp_ABTI_global->num_active_xstreams = 0;

    /* Create a spinlock */
    ABTI_spinlock_create(&gp_ABTI_global->xstreams_lock);

    /* Init the ES local data */
    abt_errno = ABTI_local_init();
    ABTI_CHECK_ERROR_MSG(abt_errno, "ABTI_local_init");

    /* Create the primary ES */
    ABTI_xstream *p_newxstream;
    abt_errno = ABTI_xstream_create_primary(&p_newxstream);
    ABTI_CHECK_ERROR_MSG(abt_errno, "ABTI_xstream_create_primary");
    ABTI_local_set_xstream(p_newxstream);

    ABTI_mlog_warmup();

    /* Create the primary ULT, i.e., the main thread */
    ABTI_thread *p_main_thread;
    abt_errno = ABTI_thread_create_main(p_newxstream, &p_main_thread);
    /* Set as if p_newxstream is currently running the main thread. */
    p_main_thread->state = ABT_THREAD_STATE_RUNNING;
    p_main_thread->p_last_xstream = p_newxstream;
    ABTI_CHECK_ERROR_MSG(abt_errno, "ABTI_thread_create_main");
    gp_ABTI_global->p_thread_main = p_main_thread;
    ABTI_local_set_thread(p_main_thread);

    /* Set a signal handler for the signal to change # of ESs */
    ABTD_signal_change_num_es_set_handler();

    /* Initialize a global sub ES allocator */
    ABTI_sub_xstream_allocator_init(&gp_ABTI_global->p_sub_allocator);

    /* Block the signal for changing # of ESs (only the sub ES allocator handles it) */
    ABTD_signal_change_num_es_block();

    /* Create sub ESs */
    int init_num_sub_xstreams = ABTI_global_get_initial_num_sub_xstreams();
    ABTI_sub_xstream_list_local_init(&p_newxstream->sub_xstream_list,
                                     init_num_sub_xstreams);

    /* Create a timer */
    ABTI_preemption_timer_create(p_newxstream);

    /* Start the primary ES */
    abt_errno = ABTI_xstream_start_primary(p_newxstream, p_main_thread);
    ABTI_CHECK_ERROR_MSG(abt_errno, "ABTI_xstream_start_primary");

    if (gp_ABTI_global->print_config == ABT_TRUE) {
        ABT_info_print_config(stdout);
    }
    ABTD_atomic_store_uint32(&g_ABTI_initialized, 1);

  fn_exit:
    /* Unlock a global lock */
    ABTI_init_lock_release();
    ABTI_ENTRY_COUNT_SET(0);
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

/**
 * @ingroup ENV
 * @brief   Terminate the Argobots execution environment.
 *
 * \c ABT_finalize() terminates the Argobots execution environment and
 * deallocates memory internally used in Argobots. This function also contains
 * deallocation of objects for the primary ES and the primary ULT.
 *
 * \c ABT_finalize() must be called by the primary ULT. Invoking the Argobots
 * functions after \c ABT_finalize() is not allowed. To use the Argobots
 * functions after calling \c ABT_finalize(), \c ABT_init() needs to be called
 * again.
 *
 * @return Error code
 * @retval ABT_SUCCESS on success
 */
int ABT_finalize(void)
{
    ABTI_ENTER;
    int abt_errno = ABT_SUCCESS;

    /* First, take a global lock protecting the initialization/finalization
     * process. Don't go to fn_exit before taking a lock */
    ABTI_init_lock_acquire();

    /* If Argobots is not initialized, just return */
    if (g_ABTI_num_inits == 0) {
        abt_errno = ABT_ERR_UNINITIALIZED;
        goto fn_exit;
    }
    /* If Argobots is still referenced by others, just return */
    if (--g_ABTI_num_inits != 0)
        goto fn_exit;

    /* If called by an external thread, return an error. */
    ABTI_CHECK_TRUE(lp_ABTI_local != NULL, ABT_ERR_INV_XSTREAM);

    ABTI_xstream *p_xstream = ABTI_local_get_xstream();
    ABTI_CHECK_TRUE_MSG(p_xstream->type == ABTI_XSTREAM_TYPE_PRIMARY,
                        ABT_ERR_INV_XSTREAM,
                        "ABT_finalize must be called by the primary ES.");

    ABTI_thread *p_thread = ABTI_local_get_thread();
    ABTI_CHECK_TRUE_MSG(p_thread->type == ABTI_THREAD_TYPE_MAIN,
                        ABT_ERR_INV_THREAD,
                        "ABT_finalize must be called by the primary ULT.");

    /* Return to the main ES if executed in a sub ES */
    ABTI_sub_xstream_back_to_main(p_thread);

    /* Set the join request */
    ABTI_xstream_set_request(p_xstream, ABTI_XSTREAM_REQ_JOIN);

    /* We wait for the remaining jobs */
    if (p_xstream->state != ABT_XSTREAM_STATE_TERMINATED) {
        /* Set the orphan request for the primary ULT */
        ABTI_thread_set_request(p_thread, ABTI_THREAD_REQ_ORPHAN);

        LOG_EVENT("[U%" PRIu64 ":E%d] yield to scheduler\n",
                  ABTI_thread_get_id(p_thread), p_thread->p_last_xstream->rank);

        /* Switch to the top scheduler */
        ABTI_sched *p_sched = ABTI_xstream_get_top_sched(p_thread->p_last_xstream);
        ABTI_thread_context_switch_thread_to_sched(p_thread, p_sched);

        /* Back to the original thread */
        LOG_EVENT("[U%" PRIu64 ":E%d] resume after yield\n",
                  ABTI_thread_get_id(p_thread), p_thread->p_last_xstream->rank);
    }

    /* Remove the primary ULT */
    ABTI_thread_free_main(p_thread);

    /* Delete a timer */
    ABTI_preemption_timer_delete(p_xstream);

    /* Free sub ESs */
    ABTI_sub_xstream_list_local_free(&p_xstream->sub_xstream_list);

    /* Free a global sub ES allocator */
    ABTI_sub_xstream_allocator_free(gp_ABTI_global->p_sub_allocator);

    /* Free the primary ES */
    abt_errno = ABTI_xstream_free(p_xstream);
    ABTI_CHECK_ERROR(abt_errno);

    /* Finalize the ES local data */
    abt_errno = ABTI_local_finalize();
    ABTI_CHECK_ERROR(abt_errno);

    /* Finalize the event environment */
    ABTI_event_finalize();

    /* Free the ES array */
    ABTU_free(gp_ABTI_global->p_xstreams);

    /* Finalize the memory pool */
    ABTI_mem_finalize(gp_ABTI_global);

    /* Free the spinlock */
    ABTI_spinlock_free(&gp_ABTI_global->xstreams_lock);

    if (gp_ABTI_global->set_affinity == ABT_TRUE) {
        ABTD_affinity_finalize();
    }

    /* Free the ABTI_global structure */
    ABTU_free(gp_ABTI_global);
    gp_ABTI_global = NULL;
    ABTD_atomic_store_uint32(&g_ABTI_initialized, 0);

    ABTI_mlog_flush(stderr);

  fn_exit:
    /* Unlock a global lock */
    ABTI_init_lock_release();
    ABTI_LEAVE;
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

/**
 * @ingroup ENV
 * @brief   Check whether \c ABT_init() has been called.
 *
 * \c ABT_initialized() returns \c ABT_SUCCESS if the Argobots execution
 * environment has been initialized. Otherwise, it returns
 * \c ABT_ERR_UNINITIALIZED.
 *
 * @return Error code
 * @retval ABT_SUCCESS           if the environment has been initialized.
 * @retval ABT_ERR_UNINITIALIZED if the environment has not been initialized.
 */
int ABT_initialized(void)
{
    ABTI_ENTER;
    int abt_errno = ABT_SUCCESS;

    if (ABTD_atomic_load_uint32(&g_ABTI_initialized) == 0) {
        abt_errno = ABT_ERR_UNINITIALIZED;
    }

    ABTI_LEAVE;
    return abt_errno;
}

/* If new_size is equal to zero, we double max_xstreams.
 * NOTE: This function currently cannot decrease max_xstreams.
 */
void ABTI_global_update_max_xstreams(int new_size)
{
    int i;

    if (new_size != 0 && new_size < gp_ABTI_global->max_xstreams) return;

    static int max_xstreams_warning_once = 0;
    if (max_xstreams_warning_once == 0) {
        /* Because some Argobots functionalities depend on the runtime value
         * ABT_MAX_NUM_XSTREAMS (or gp_ABTI_global->max_xstreams), changing this
         * value at run-time can cause an error.  For example, using ABT_mutex
         * created before updating max_xstreams causes an error since
         * ABTI_thread_htable's array size depends on ABT_MAX_NUM_XSTREAMS.
         * To fix this issue, please set a larger number to ABT_MAX_NUM_XSTREAMS
         * in advance. */
        char *warning_message = (char *)malloc(sizeof(char) * 1024);
        snprintf(warning_message, 1024,
                 "Warning: the number of execution streams exceeds "
                 "ABT_MAX_NUM_XSTREAMS (=%d), which may cause an unexpected "
                 "error.",
                 gp_ABTI_global->max_xstreams);
        HANDLE_WARNING(warning_message);
        free(warning_message);
        max_xstreams_warning_once = 1;
    }

    new_size = (new_size > 0) ? new_size : gp_ABTI_global->max_xstreams * 2;
    gp_ABTI_global->max_xstreams = new_size;
    gp_ABTI_global->p_xstreams = (ABTI_xstream **)ABTU_realloc(
            gp_ABTI_global->p_xstreams, new_size * sizeof(ABTI_xstream *));

    for (i = gp_ABTI_global->num_xstreams; i < new_size; i++) {
        gp_ABTI_global->p_xstreams[i] = NULL;
    }
}

