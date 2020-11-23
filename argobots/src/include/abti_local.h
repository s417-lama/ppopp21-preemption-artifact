/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef LOCAL_H_INCLUDED
#define LOCAL_H_INCLUDED

/* Inlined functions for ES Local Data */

static inline
ABTI_local *ABTI_local_get_explicit(void)
{
    return gp_ABTI_local_get();
}

static inline
ABTI_xstream *ABTI_local_get_xstream(void) {
    return lp_ABTI_local->p_xstream;
}

static inline
ABTI_xstream *ABTI_local_get_xstream_explicit(void) {
    return ABTI_local_get_explicit()->p_xstream;
}

static inline
void ABTI_local_set_xstream(ABTI_xstream *p_xstream) {
    lp_ABTI_local->p_xstream = p_xstream;
}

static inline
ABTI_thread *ABTI_local_get_thread(void) {
    return lp_ABTI_local->p_thread;
}

static inline
ABTI_thread *ABTI_local_get_thread_explicit(void) {
    return ABTI_local_get_explicit()->p_thread;
}

static inline
void ABTI_local_set_thread(ABTI_thread *p_thread) {
    lp_ABTI_local->p_thread = p_thread;
}

static inline
ABTI_task *ABTI_local_get_task(void) {
    return lp_ABTI_local->p_task;
}

static inline
void ABTI_local_set_task(ABTI_task *p_task) {
    lp_ABTI_local->p_task = p_task;
}

static inline
int *ABTI_local_get_entry_count(void)
{
    return gp_ABTI_local_entry_count_get();
}

static inline
ABTI_sub_xstream *ABTI_sub_xstream_get_data_explicit(void) {
    return gp_ABTI_get_sub_xstream();
}

#endif /* LOCAL_H_INCLUDED */

