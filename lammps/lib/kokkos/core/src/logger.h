#pragma once

#include <stdio.h>

/* #define MLOG_DISABLE_CHECK_BUFFER_SIZE 1 */
/* #define MLOG_DISABLE_REALLOC_BUFFER    1 */
#include "mlog/mlog.h"

#ifndef LOGGER_ENABLE
# define LOGGER_ENABLE 0
#endif

namespace Kokkos {

extern mlog_data_t g_md;

static inline void logger_init(int num_workers) {
  mlog_init(&g_md, num_workers, (2 << 20));
}

static inline void logger_flush(FILE* stream) {
  mlog_flush_all(&g_md, stream);
}

static inline void logger_warmup(int rank) {
  mlog_warmup(&g_md, rank);
}

#if LOGGER_ENABLE

static inline void* logger_begin_tl(int rank) {
  return mlog_begin_tl(&g_md, rank);
}

static inline void logger_end_tl(int rank, void* bp, char* event) {
  mlog_end_tl(&g_md, rank, bp, event);
}

#define LOGGER_BEGIN(rank, ...)                   MLOG_BEGIN(&g_md, rank, __VA_ARGS__);
#define LOGGER_END(rank, begin_ptr, decoder, ...) MLOG_END(&g_md, rank, begin_ptr, decoder, __VA_ARGS__);

#else

static inline void* logger_begin_tl(int rank) { return NULL; };
static inline void logger_end_tl(int rank, void* bp, char* event) {};

#define LOGGER_BEGIN(...) (void*)0
#define LOGGER_END(...)   (void)0

#endif

static inline void logger_clear() {
  mlog_clear_all(&g_md);
}

}
