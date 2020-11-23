// icc -fopenmp -std=c99 -mkl matmul.c
// KMP_ABT_WORK_STEAL_FREQ=4 KMP_ABT_FORK_CUTOFF=4 KMP_ABT_FORK_NUM_WAYS=4 ABT_SET_AFFINITY=1 KMP_BLOCKTIME=0 KMP_HOT_TEAMS_MAX_LEVEL=2 KMP_HOT_TEAMS_MODE=1 NUM_THREADS_POTRF=8 LOOKAHEAD=7 NUM_THREADS_SYRK_BATCH=8 NUM_THREADS_SYRK_NEST=8 OMP_NUM_THREADS=8 OMP_PROC_BIND=close,unset KMP_ABT_NUM_ESS=56 MKL_NUM_THREADS=56 ABT_THREAD_STACKSIZE=150000 ABT_MEM_MAX_NUM_STACKS=80 ABT_MEM_LP_ALLOC=malloc OMP_NESTED=true MKL_DYNAMIC=false ./mkl_dgemm.out 16 10 2 2000 2000 2500

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <omp.h>
#include <mkl.h>

#define RAW_MATMUL 0

int main(int argc, char **argv) {
    if (argc != 7) {
        printf("Usage: ./mkl_dgemm NUM_OUTER_THREADS NUM_REPEATS NUM_WARMUPS M N K\n"
               "ex:    ./mkl_dgemm 16 10 2 2000 2000 2000\n");
        return -1;
    }

    const int num_outer_threads = atoi(argv[1]);
    const int num_repeats = atoi(argv[2]);
    const int num_warmups = atoi(argv[3]);
    const int m = atoi(argv[4]);
    const int k = atoi(argv[5]);
    const int n = atoi(argv[6]);

    const double alpha = 1.0;
    const double beta = 0.0;

    printf("=============================================================\n"
           "# of outer threads:  %d\n"
           "# of repeats:        %d\n"
           "# of warmups:        %d\n"
           "M:                   %d\n"
           "K:                   %d\n"
           "N:                   %d\n"
           "=============================================================\n\n",
           num_outer_threads, num_repeats, num_warmups, m, k, n);

#if RAW_MATMUL

    double *A = (double *)mkl_malloc(m * k * sizeof(double), 64);
    double *B = (double *)mkl_malloc(k * n * sizeof(double), 64);
    double *C = (double *)mkl_malloc(m * n * sizeof(double), 64);

    for (int i = 0; i < m * k; i++)
        A[i] = 1.0;
    for (int i = 0; i < k * n; i++)
        B[i] = 1.0;
    for (int i = 0; i < m * n; i++)
        C[i] = 0.0;

    double time = 0;
    for (int i = 0; i < num_warmups + num_repeats; i++) {
        printf("%d\n", i);
        if (i == num_warmups) {
            time = omp_get_wtime();
        }
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                    m, n, k, alpha, A, k, B, n, beta, C, n);
    }
    time = (omp_get_wtime() - time) / num_repeats;

    if (omp_get_thread_num() == 0) {
        int64_t num_flops = ((int64_t)2) * m * k * n;
        double flops = num_flops / time;
        printf("GFLOPS = %.2f (Elapsed %f [s])\n", flops * 1.0e-9, time);
    }

#else

    #pragma omp parallel num_threads(num_outer_threads)
    {
        double *A = (double *)mkl_malloc(m * k * sizeof(double), 64);
        double *B = (double *)mkl_malloc(k * n * sizeof(double), 64);
        double *C = (double *)mkl_malloc(m * n * sizeof(double), 64);

        int tid = omp_get_thread_num();

        for (int i = 0; i < m * k; i++)
            A[i] = 1.0;
        for (int i = 0; i < k * n; i++)
            B[i] = 1.0;
        for (int i = 0; i < m * n; i++)
            C[i] = 0.0;
        #pragma omp barrier

        double time = 0;
        for (int i = -num_warmups; i < num_repeats; i++) {
            if (i >= 0 && tid == 0) {
                time = omp_get_wtime();
            }
            cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                        m, n, k, alpha, A, k, B, n, beta, C, n);
            #pragma omp barrier
            if (i >= 0 && tid == 0) {
                double t = omp_get_wtime() - time;
                int64_t num_flops = ((int64_t)2) * m * k * n * omp_get_num_threads();
                double flops = num_flops / t;
                printf("[%d] GFLOPS = %.2f (Elapsed %f [s])\n", i, flops * 1.0e-9, t);
            }

            /* #pragma omp single */
            /* for (int i = 0; i < m * n; i++) */
            /*     if (fabs(C[i] - k) > 0.00001) */
            /*         printf("wrong answer\n"); */
        }

        mkl_free(A);
        mkl_free(B);
        mkl_free(C);
    }

#endif

    return 0;
}
