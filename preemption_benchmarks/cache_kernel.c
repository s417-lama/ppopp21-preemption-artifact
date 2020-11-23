#define _GNU_SOURCE
#include <math.h>
#include <time.h>

#include "logger.h"
#include "cache.h"

static inline uint32_t rand32(uint32_t *p_seed) {
    uint32_t seed = *p_seed;
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    *p_seed = seed;
    return seed;
}

void cache_kernel(double *data, size_t data_num, size_t count, int ult_id) {
    double c = 0.0000001;

    for (size_t i = 0; i < data_num; i++) {
        /* data[i] = 0; */
        data[i] = 0.0000001;
    }

#if RANDOM_ACCESS
    uint32_t seed;
    do {
        seed = (uint32_t)time(NULL) + 64 + ult_id;
    } while (seed == 0);

    for (size_t it = 0; it < count; it++) {
        for (size_t i = 0; i < data_num; i++) {
            size_t idx = rand32(&seed) % data_num;
#if READ_WRITE
            data[idx] += c;
#else
            c += data[idx];
#endif
        }
    }
#else
    void *bp = logger_begin_tl(sched_getcpu());
    for (size_t it = 0; it < count; it++) {
        asm volatile("#loop begin");
        /* void *bp = logger_begin_tl(ult_id); */
        for (size_t i = 0; i < data_num; i++) {
#if READ_WRITE
            data[i] += c;
#else
            c += data[i];
#endif
        }
        /* logger_end_tl(ult_id, bp, "loop"); */
        asm volatile("#loop end");
    }
    logger_end_tl(sched_getcpu(), bp, "loop");

    /* for (size_t i = 0; i < data_num; i++) { */
    /*     if (fabs(c * count - data[i]) > 0.000001) { */
    /*         printf("error! data[%ld] = %f\n", i, data[i]); */
    /*         exit(1); */
    /*     } */
    /* } */
#endif
    if (c == 0) {
        printf("error\n");
    }
}
