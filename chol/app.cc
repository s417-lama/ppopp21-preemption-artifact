
#include "Slate_Matrix.hh"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <omp.h>
#include <mkl_cblas.h>
#include <mkl_lapacke.h>

int lookahead;
int num_threads_syrk_nest;
int num_threads_syrk_batch;
int num_threads_potrf;

extern "C" void trace_off();
extern "C" void trace_on();
void print_lapack_matrix(int m, int n, double *a, int lda, int mb, int nb);

//------------------------------------------------------------------------------
int main (int argc, char *argv[])
{
    /* assert(argc == 3); */
    int nb = atoi(argv[1]);
    int nt = atoi(argv[2]);
    int nrepeat = (argc > 3) ? atoi(argv[3]) : 1;
    int nwarmup = (argc > 4) ? atoi(argv[4]) : 1;
    int n = nb*nt;
    int lda = n;

    {
        char *env;
        env = getenv("LOOKAHEAD");
        if (env) {
            lookahead = atoi(env);
        } else {
            lookahead = 0;
        }
        env = getenv("NUM_THREADS_SYRK_NEST");
        if (env) {
            num_threads_syrk_nest = atoi(env);
        } else {
            num_threads_syrk_nest = 60;
        }
        env = getenv("NUM_THREADS_SYRK_BATCH");
        if (env) {
            num_threads_syrk_batch = atoi(env);
        } else {
            num_threads_syrk_batch = 60;
        }
        env = getenv("NUM_THREADS_POTRF");
        if (env) {
            num_threads_potrf = atoi(env);
        } else {
            num_threads_potrf = 8;
        }
    }

    //------------------------------------------------------
    double *a1 = (double*)malloc(sizeof(double)*nb*nb*nt*nt);
    assert(a1 != nullptr);

    int seed[] = {0, 0, 0, 1};
    int retval;
    retval = LAPACKE_dlarnv(1, seed, (size_t)lda*n, a1);
    assert(retval == 0);

    for (int i = 0; i < n; ++i)
        a1[(size_t)i*lda+i] += sqrt(n);

    //------------------------------------------------------

    double *a2 = (double*)malloc(sizeof(double)*nb*nb*nt*nt);
    assert(a2 != nullptr);

    memcpy(a2, a1, sizeof(double)*lda*n);

    //------------------------------------------------------

    trace_off();
    for (int i = 0; i < nwarmup; i++) {
        Slate::Matrix<double> temp(n, n, a1, lda, nb, nb);
        temp.potrf(Ccblas::Uplo::Lower, lookahead);
    }
    trace_on();

    Slate::Matrix<double> a(n, n, a1, lda, nb, nb);

    for (int i = 0; i < nrepeat; i++) {
        a.copyTo(n, n, a1, lda, nb, nb);

        double start = omp_get_wtime();
        /* double start = 0; */
        a.potrf(Ccblas::Uplo::Lower, lookahead);
        double time = omp_get_wtime()-start;

        double gflops = (double)nb*nb*nb*nt*nt*nt/3.0/time/1000000000.0;
        printf("\t%.0lf GFLOPS\n", gflops);
    }

    a.copyFrom(n, n, a1, lda, nb, nb);

    retval = LAPACKE_dpotrf(LAPACK_COL_MAJOR, 'L', n, a2, lda);
    assert(retval == 0);

    cblas_daxpy((size_t)lda*n, -1.0, a1, 1, a2, 1);

    double norm = LAPACKE_dlansy(LAPACK_COL_MAJOR, 'F', 'L', n, a1, lda);
    double error = LAPACKE_dlange(LAPACK_COL_MAJOR, 'F', n, n, a2, lda);
    if (norm != 0)
        error /= norm;
    printf("\t%le\n", error);

    free(a1);
    free(a2);
    return EXIT_SUCCESS;
}

//------------------------------------------------------------------------------
void print_lapack_matrix(int m, int n, double *a, int lda, int mb, int nb)
{
    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < n; ++j) {
            printf("%8.2lf", a[(size_t)lda*j+i]);
            if ((j+1)%nb == 0)
                printf(" |");
        }
        printf("\n");
        if ((i+1)%mb == 0) {
            for (int j = 0; j < (n+1)*8; ++j) {
                printf("-");
            }
            printf("\n");        
        }
    }
    printf("\n");
}
