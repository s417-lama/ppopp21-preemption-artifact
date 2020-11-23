#ifndef MLOG_H_INCLUDED
#define MLOG_H_INCLUDED

#include "abti.h"

#define ABTI_MLOG_ENABLE 1

/* #define MLOG_DISABLE_CHECK_BUFFER_SIZE 1 */
/* #define MLOG_DISABLE_REALLOC_BUFFER    1 */
#include <mlog/mlog.h>
#include <unistd.h>

#if ABTI_MLOG_ENABLE

extern mlog_data_t g_md;

static void *ABTI_mlog_decoder(FILE* stream, int rank0, int rank1, void* buf0, void* buf1)
{
    uint64_t t0         = MLOG_READ_ARG(&buf0, uint64_t);
    uint64_t t1         = MLOG_READ_ARG(&buf1, uint64_t);
    char*    event_name = MLOG_READ_ARG(&buf1, char*);

    fprintf(stream, "%d,%lu,%d,%lu,%s\n", rank0, t0, rank1, t1, event_name);
    return buf1;
}

static void *ABTI_mlog_decoder_ult(FILE* stream, int rank0, int rank1, void* buf0, void* buf1)
{
    uint64_t      t0        = MLOG_READ_ARG(&buf0, uint64_t);
    uint64_t      t1        = MLOG_READ_ARG(&buf1, uint64_t);
    ABT_thread_id thread_id = MLOG_READ_ARG(&buf1, ABT_thread_id);

    fprintf(stream, "%d,%lu,%d,%lu,thread_%03ld\n", rank0, t0, rank1, t1, thread_id);
    /* fprintf(stream, "%d,%lu,%d,%lu,thread\n", rank0, t0, rank1, t1); */
    return buf1;
}

static inline void ABTI_mlog_init()
{
    mlog_init(&g_md, sysconf(_SC_NPROCESSORS_ONLN), (2 << 20));
}

static inline void ABTI_mlog_begin()
{
    lp_ABTI_local->p_xstream->p_mlog_data = MLOG_BEGIN(&g_md, lp_ABTI_local->p_xstream->rank, mlog_clock_gettime_in_nsec());
}

static inline void ABTI_mlog_end(char* event_name)
{
    if (lp_ABTI_local->p_xstream->p_mlog_data) {
        MLOG_END(&g_md, lp_ABTI_local->p_xstream->rank, lp_ABTI_local->p_xstream->p_mlog_data, ABTI_mlog_decoder, mlog_clock_gettime_in_nsec(), event_name);
        lp_ABTI_local->p_xstream->p_mlog_data = NULL;
    } else {
        printf("%d: end %s (something is wrong)\n", lp_ABTI_local->p_xstream->rank, event_name);
    }
}

static inline void *ABTI_mlog_begin_2()
{
    void* bp = MLOG_BEGIN(&g_md, lp_ABTI_local->p_xstream->rank, mlog_clock_gettime_in_nsec());
    return bp;
}

static inline void ABTI_mlog_end_2(void* bp, char* event_name)
{
    MLOG_END(&g_md, lp_ABTI_local->p_xstream->rank, bp, ABTI_mlog_decoder, mlog_clock_gettime_in_nsec(), event_name);
}

static inline void ABTI_mlog_begin_t(uint64_t t)
{
    lp_ABTI_local->p_xstream->p_mlog_data = MLOG_BEGIN(&g_md, lp_ABTI_local->p_xstream->rank, t);
}

static inline void ABTI_mlog_end_t(uint64_t t, char* event_name)
{
    if (lp_ABTI_local->p_xstream->p_mlog_data) {
        MLOG_END(&g_md, lp_ABTI_local->p_xstream->rank, lp_ABTI_local->p_xstream->p_mlog_data, ABTI_mlog_decoder, t, event_name);
        lp_ABTI_local->p_xstream->p_mlog_data = NULL;
    } else {
        printf("%d: end %s (something is wrong)\n", lp_ABTI_local->p_xstream->rank, event_name);
    }
}

static inline void ABTI_mlog_end_thread(ABT_thread_id thread_id)
{
    if (lp_ABTI_local->p_xstream->p_mlog_data) {
        MLOG_END(&g_md, lp_ABTI_local->p_xstream->rank, lp_ABTI_local->p_xstream->p_mlog_data, ABTI_mlog_decoder_ult, mlog_clock_gettime_in_nsec(), thread_id);
        lp_ABTI_local->p_xstream->p_mlog_data = NULL;
    } else {
        printf("%d: end thread (something is wrong)\n", lp_ABTI_local->p_xstream->rank);
    }
}

static inline void ABTI_mlog_flush(FILE* stream)
{
    mlog_flush_all(&g_md, stream);
}

static inline void ABTI_mlog_clear()
{
    mlog_clear_all(&g_md);
}

static inline void ABTI_mlog_warmup()
{
    mlog_warmup(&g_md, lp_ABTI_local->p_xstream->rank);
}

#else

static inline void ABTI_mlog_init() {}
static inline void ABTI_mlog_begin() {}
static inline void ABTI_mlog_end(char* event_name) {}
static inline void *ABTI_mlog_begin_2() { return NULL; }
static inline void ABTI_mlog_end_2(void* bp, char* event_name) {}
static inline void ABTI_mlog_begin_t(uint64_t t) {}
static inline void ABTI_mlog_end_t(uint64_t t, char* event_name) {}
static inline void ABTI_mlog_flush(FILE* stream) {}
static inline void ABTI_mlog_clear() {}
static inline void ABTI_mlog_warmup() {}
static inline void ABTI_mlog_end_thread(ABT_thread_id thread_id) {}

#endif

#endif /* MLOG_H_INCLUDED */
