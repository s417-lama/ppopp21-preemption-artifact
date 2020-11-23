#pragma once
#ifndef MLOG_BASE_H_
#define MLOG_BASE_H_

#include "mlog/util.h"
#include "mlog/printf.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define MLOG_CACHELINE_SIZE 64

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mlog_freelist {
  struct mlog_freelist* next;
  void*                 mem;
  size_t                size;
}
mlog_freelist_t;

typedef struct mlog_buffer {
  void*            first;
  void*            last;
  size_t           size;
  mlog_freelist_t* freelist;
} __attribute__((aligned(MLOG_CACHELINE_SIZE)))
mlog_buffer_t;

typedef struct mlog_data {
  int               num_ranks;
  mlog_buffer_t*    begin_buf;
  mlog_buffer_t*    end_buf;
}
mlog_data_t;

typedef void* (*mlog_decoder_t)(FILE*, int, int, void*, void*);

/*
 * mlog_init
 */

static inline void mlog_buffer_init(mlog_buffer_t* buf, size_t buf_size) {
  buf->first    = (void*)malloc(buf_size);
  buf->last     = buf->first;
  buf->size     = buf_size;
  buf->freelist = NULL;
}

static inline void mlog_init(mlog_data_t* md, int num_ranks, size_t buf_size) {
  md->num_ranks = num_ranks;
  if (posix_memalign((void**)&md->begin_buf, MLOG_CACHELINE_SIZE, num_ranks * sizeof(mlog_buffer_t)) != 0) {
    perror("posix_memalign");
    exit(1);
  }
  if (posix_memalign((void**)&md->end_buf, MLOG_CACHELINE_SIZE, num_ranks * sizeof(mlog_buffer_t)) != 0) {
    perror("posix_memalign");
    exit(1);
  }
  for (int rank = 0; rank < num_ranks; rank++) {
    mlog_buffer_init(&md->begin_buf[rank], buf_size);
    mlog_buffer_init(&md->end_buf[rank], buf_size);
  }
}

/*
 * check begin buffer size
 */

static inline void _mlog_check_begin_buffer_size(mlog_buffer_t* buf, size_t write_size) {
  if ((char*)buf->first + buf->size < (char*)buf->last + write_size) {
#if MLOG_DISABLE_REALLOC_BUFFER
    fprintf(stderr, "MassiveLogger: realloc of begin buffer is not allowed (MLOG_DISABLE_REALLOC_BUFFER = 1)");
    abort();
#else
    size_t next_size = buf->size * 2;
    void*  next_buf  = malloc(next_size);
    if (next_buf == NULL) {
      perror("malloc");
      abort();
    }
    mlog_freelist_t* fl_node = (mlog_freelist_t*)malloc(sizeof(mlog_freelist_t));
    if (fl_node == NULL) {
      perror("malloc");
      abort();
    }
    fl_node->mem  = buf->first;
    fl_node->size = buf->size;
    fl_node->next = buf->freelist;
    buf->freelist = fl_node;
    buf->first    = next_buf;
    buf->last     = buf->first;
    buf->size     = next_size;
#endif
  }
}

#if MLOG_DISABLE_CHECK_BUFFER_SIZE
#define MLOG_CHECK_BEGIN_BUFFER_SIZE(buf, write_size) (void)0
#else
#define MLOG_CHECK_BEGIN_BUFFER_SIZE(buf, write_size) _mlog_check_begin_buffer_size((buf), (write_size))
#endif

/*
 * check end buffer size
 */

static inline void _mlog_check_end_buffer_size(mlog_buffer_t* buf, size_t write_size) {
  if ((char*)buf->first + buf->size < (char*)buf->last + write_size) {
#if MLOG_DISABLE_REALLOC_BUFFER
    fprintf(stderr, "MassiveLogger: realloc of end buffer is not allowed (MLOG_DISABLE_REALLOC_BUFFER = 1)");
    abort();
#else
    size_t next_size = buf->size * 2;
    size_t offset    = (char*)buf->last - (char*)buf->first;
    void*  next_buf  = realloc(buf->first, next_size);
    if (next_buf == NULL) {
      perror("realloc");
      abort();
    }
    buf->first = next_buf;
    buf->last  = (char*)next_buf + offset;
    buf->size  = next_size;
#endif
  }
}

#if MLOG_DISABLE_CHECK_BUFFER_SIZE
#define MLOG_CHECK_END_BUFFER_SIZE(buf, write_size) (void)0
#else
#define MLOG_CHECK_END_BUFFER_SIZE(buf, write_size) _mlog_check_end_buffer_size((buf), (write_size))
#endif

/*
 * MLOG_BEGIN
 */

#define MLOG_BUFFER_WRITE_VALUE(buf, arg) \
  *((MLOG_TYPEOF(arg)*)((buf)->last)) = (arg); \
  (buf)->last = ((char*)(buf)->last) + MLOG_SIZEOF(arg);

#define MLOG_BEGIN_WRITE_ARG(arg) MLOG_BUFFER_WRITE_VALUE(_mlog_buf, arg)

#define MLOG_BEGIN(md, rank, ...) \
  (MLOG_CHECK_BEGIN_BUFFER_SIZE(&((md)->begin_buf[rank]), MLOG_SUM_SIZEOF(__VA_ARGS__)), ((md)->begin_buf[rank].last)); \
  { \
    mlog_buffer_t* _mlog_buf = &((md)->begin_buf[rank]); \
    MLOG_FOREACH(MLOG_BEGIN_WRITE_ARG, __VA_ARGS__); \
  }

/*
 * MLOG_END
 */

#define MLOG_END_WRITE_ARG(arg) MLOG_BUFFER_WRITE_VALUE(_mlog_buf, arg)

#define MLOG_END(md, rank, begin_ptr, decoder, ...) { \
    mlog_buffer_t* _mlog_buf = &((md)->end_buf[rank]); \
    MLOG_CHECK_END_BUFFER_SIZE(_mlog_buf, MLOG_SUM_SIZEOF((begin_ptr), (decoder), __VA_ARGS__)); \
    _mlog_write_begin_ptr_to_end_buffer(_mlog_buf, (begin_ptr)); \
    _mlog_write_decoder_to_end_buffer(_mlog_buf, (decoder)); \
    MLOG_FOREACH(MLOG_END_WRITE_ARG, __VA_ARGS__); \
  }

static inline void _mlog_write_begin_ptr_to_end_buffer(mlog_buffer_t* end_buf, void* begin_ptr) {
  MLOG_BUFFER_WRITE_VALUE(end_buf, begin_ptr);
}

static inline void _mlog_write_decoder_to_end_buffer(mlog_buffer_t* end_buf, mlog_decoder_t decoder) {
  MLOG_BUFFER_WRITE_VALUE(end_buf, decoder);
}

/*
 * MLOG_PRINTF
 */

#define MLOG_PRINTF(md, rank, ...) { \
    mlog_buffer_t* _mlog_buf = &((md)->end_buf[rank]); \
    MLOG_CHECK_END_BUFFER_SIZE(_mlog_buf, MLOG_SUM_SIZEOF(NULL, __VA_ARGS__)); \
    _mlog_write_begin_ptr_to_end_buffer(_mlog_buf, NULL); \
    MLOG_FOREACH(MLOG_END_WRITE_ARG, __VA_ARGS__); \
  }

/*
 * mlog_clear
 */

static inline void mlog_free_buffers_in_freelist(mlog_freelist_t** freelist) {
  mlog_freelist_t* fl_node = *freelist;
  while (fl_node != NULL) {
    free(fl_node->mem);
    mlog_freelist_t* tmp = fl_node;
    fl_node = fl_node->next;
    free(tmp);
  }
  *freelist = NULL;
}

static inline void mlog_clear_buffer(mlog_buffer_t* buf) {
  mlog_free_buffers_in_freelist(&buf->freelist);
  buf->last = buf->first;
}

static inline void mlog_clear_begin_buffer(mlog_data_t* md, int rank) {
  mlog_clear_buffer(&md->begin_buf[rank]);
}

static inline void mlog_clear_begin_buffer_all(mlog_data_t* md) {
  for (int rank = 0; rank < md->num_ranks; rank++) {
    mlog_clear_begin_buffer(md, rank);
  }
}

static inline void mlog_clear_end_buffer(mlog_data_t* md, int rank) {
  mlog_clear_buffer(&md->end_buf[rank]);
}

static inline void mlog_clear_end_buffer_all(mlog_data_t* md) {
  for (int rank = 0; rank < md->num_ranks; rank++) {
    mlog_clear_end_buffer(md, rank);
  }
}

static inline void mlog_clear(mlog_data_t* md, int rank) {
  mlog_clear_begin_buffer(md, rank);
  mlog_clear_end_buffer(md, rank);
}

static inline void mlog_clear_all(mlog_data_t* md) {
  for (int rank = 0; rank < md->num_ranks; rank++) {
    mlog_clear(md, rank);
  }
}

/*
 * mlog_flush
 */

static inline int _mlog_buffers_in_freelist_include(mlog_freelist_t* freelist, void* p) {
  mlog_freelist_t* fl_node = freelist;
  while (fl_node != NULL) {
    if (fl_node->mem <= p && p < (void*)((char*)fl_node->mem + fl_node->size)) return 1;
    fl_node = fl_node->next;
  }
  return 0;
}

static inline int _mlog_buffer_includes(mlog_buffer_t* buf, void* p) {
  return (buf->first <= p && p < buf->last) || _mlog_buffers_in_freelist_include(buf->freelist, p);
}

static inline int _mlog_get_rank_from_begin_ptr(mlog_data_t* md, void* begin_ptr) {
  for (int rank = 0; rank < md->num_ranks; rank++) {
    if (_mlog_buffer_includes(&md->begin_buf[rank], begin_ptr)) {
      return rank;
    }
  }
  return -1;
}

static inline void mlog_flush(mlog_data_t* md, int rank, FILE* stream) {
  void* cur_end_buffer = md->end_buf[rank].first;
  while (cur_end_buffer < md->end_buf[rank].last) {
    void* begin_ptr = MLOG_READ_ARG(&cur_end_buffer, void*);
    if (begin_ptr) {
      mlog_decoder_t decoder = MLOG_READ_ARG(&cur_end_buffer, mlog_decoder_t);
      void*          buf0    = begin_ptr;
      void*          buf1    = cur_end_buffer;
      int            rank0   = _mlog_get_rank_from_begin_ptr(md, begin_ptr);
      int            rank1   = rank;
      cur_end_buffer = decoder(stream, rank0, rank1, buf0, buf1);
    } else {
      char* format = MLOG_READ_ARG(&cur_end_buffer, char*);
      cur_end_buffer = mlog_decode_printf(stream, format, cur_end_buffer);
    }
  }
  mlog_clear_end_buffer(md, rank);
  fflush(stream);
}

static inline void mlog_flush_all(mlog_data_t* md, FILE* stream) {
  for (int rank = 0; rank < md->num_ranks; rank++) {
    mlog_flush(md, rank, stream);
  }
  mlog_clear_begin_buffer_all(md);
}

/*
 * mlog_warmup
 */

static inline void _mlog_warmup_impl(mlog_buffer_t* buf) {
  if (buf->first != buf->last) {
    fprintf(stderr, "MassiveLogger: mlog_warmup() is called while buffers are not cleared");
    abort();
  }
  /* write a single word per page to cause pagefault */
  int pagesize = sysconf(_SC_PAGESIZE);
  for (size_t i = 0; i < buf->size; i += pagesize) {
    ((char*)(buf->first))[i] = 0;
  }
}

static inline void mlog_warmup(mlog_data_t* md, int rank) {
  _mlog_warmup_impl(&md->begin_buf[rank]);
  _mlog_warmup_impl(&md->end_buf[rank]);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* MLOG_BASE_H_ */

/* vim: set ts=2 sw=2 tw=0: */
