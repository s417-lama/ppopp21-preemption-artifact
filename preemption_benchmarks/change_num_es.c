#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include <locale.h>

#include "logger.h"

uint64_t num_iter         = 10000000;
int num_repeats           = 10;
int preemption_intvl_usec = 1000;

void work_fn() {
    volatile float f = 0.000001f;
    for (uint64_t i = 0; i < num_iter; i++) {
        __asm__ __volatile__("#noop");
        f += 0.000001f;
    }
    if (fabs(f - 0.000001f * num_iter) < 0.000001) {
        printf("wrong answer.\n");
    }
}

int main(int argc, char *argv[]) {
    /* read arguments. */
    int opt;
    extern char *optarg;
    while ((opt = getopt(argc, argv, "r:t:i:h")) != EOF) {
        switch (opt) {
        case 'r':
            num_repeats = atoi(optarg);
            break;
        case 't':
            preemption_intvl_usec = atoi(optarg);
            break;
        case 'i':
            num_iter = atol(optarg);
            break;
        case 'h':
        default:
            printf("Usage: ./change_num_es [args]\n"
                   "  Parameters:\n"
                   "    -r : # of repeats (int)\n"
                   "    -t : Preemption timer interval in usec (int)\n"
                   "    -i : # of iterations for each ULT (uint64_t)\n");
            exit(1);
        }
    }

    setlocale(LC_NUMERIC, "");
    printf("=============================================================\n"
           "# of repeats:              %d\n"
           "# of iterations:           %ld\n"
           "Preemption timer interval: %d usec\n"
           "=============================================================\n\n",
           num_repeats, num_iter, preemption_intvl_usec);

    char s[20];
    sprintf(s, "%d", preemption_intvl_usec);
    setenv("ABT_PREEMPTION_INTERVAL_USEC", s, 1);

    for (int it = 0; it < num_repeats; it++) {
        uint64_t t1 = mlog_clock_gettime_in_nsec();
#pragma omp parallel
        {
            work_fn();
        }
        uint64_t t2 = mlog_clock_gettime_in_nsec();
        printf("[elapsed] %'ld nsec\n", t2 - t1);
    }

    return 0;
}
