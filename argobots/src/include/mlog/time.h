#pragma once
#ifndef MLOG_TIME_H_
#define MLOG_TIME_H_

#include <sys/time.h>
#include <time.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint64_t mlog_gettimeofday_in_usec() {
  struct timeval tv;
  gettimeofday(&tv, 0);
  return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}

static inline uint64_t mlog_clock_gettime_in_nsec() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
}

static inline uint64_t mlog_rdtsc() {
  uint64_t r;
  __asm__ volatile ("rdtsc; shlq $32, %%rdx; orq %%rdx, %%rax" : "=a"(r) :: "%rdx");
  return r;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* MLOG_TIME_H_ */

/* vim: set ts=2 sw=2 tw=0: */
