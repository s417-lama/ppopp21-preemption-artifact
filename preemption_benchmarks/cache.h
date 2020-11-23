#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sched.h>

#ifndef RANDOM_ACCESS
# define RANDOM_ACCESS 0
#endif

#ifndef READ_WRITE
# define READ_WRITE 0
#endif

void cache_kernel(double *data, size_t data_num, size_t count, int ult_id);
