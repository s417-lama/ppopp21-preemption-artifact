
#ifndef SLATE_MATRIX_HH
#define SLATE_MATRIX_HH

#include "Slate_Tile.hh"

#include <utility>
#include <map>
#include <vector>

#include <omp.h>

#define SYRK_TASK  1
#define SYRK_NEST  2
#define SYRK_BATCH 3

#ifndef SYRK_NEST_TYPE
#define SYRK_NEST_TYPE SYRK_NEST
#endif

extern int lookahead;
extern int num_threads_syrk_nest;
extern int num_threads_syrk_batch;
extern int num_threads_potrf;

namespace Slate {

//------------------------------------------------------------------------------
template<class FloatType>
class Matrix {
public:
    int64_t it_; ///< first row of tiles
    int64_t jt_; ///< first column of tiles
    int64_t mt_; ///< number of tile rows
    int64_t nt_; ///< number of tile columns

    // TODO: replace by unordered_map
    std::map<std::pair<int64_t, int64_t>, Tile<FloatType>*> *tiles_;

    void copyTo(int64_t m, int64_t n, FloatType *a, int64_t lda,
                int64_t mb, int64_t nb);

    void copyFrom(int64_t m, int64_t n, FloatType *a, int64_t lda,
                  int64_t mb, int64_t nb); 

    Matrix(int64_t m, int64_t n, double *a, int64_t lda,
           int64_t mb, int64_t nb)
    {
        tiles_ = new std::map<std::pair<int64_t, int64_t>, Tile<FloatType>*>;
        it_ = 0;
        jt_ = 0;
        mt_ = m % mb == 0 ? m/mb : m/mb+1;
        nt_ = n % nb == 0 ? n/nb : n/nb+1;
        copyTo(m, n, a, lda, mb, nb);
    }
    Matrix(const Matrix &a, int64_t it, int64_t jt, int64_t mt, int64_t nt)
    {
        assert(it+mt <= a.mt_);
        assert(jt+nt <= a.nt_);
        *this = a;
        it_ += it;
        jt_ += jt;
        mt_ = mt;
        nt_ = nt;
    }

    Tile<FloatType>* &operator()(int64_t m, int64_t n) {
        return (*tiles_)[std::pair<int64_t, int64_t>(it_+m, jt_+n)];
    }
    Tile<FloatType>* &operator()(int64_t m, int64_t n) const {
        return (*tiles_)[std::pair<int64_t, int64_t>(it_+m, jt_+n)];
    }

    void syrk_task(Ccblas::Uplo uplo, Ccblas::Tran trans,
                   FloatType alpha, const Matrix &a, FloatType beta);

    void syrk_nest(Ccblas::Uplo uplo, Ccblas::Tran trans,
                   FloatType alpha, const Matrix &a, FloatType beta);

    void syrk_batch(Ccblas::Uplo uplo, Ccblas::Tran trans,
                    FloatType alpha, const Matrix &a, FloatType beta);

    void trsm(Ccblas::Side side, Ccblas::Uplo uplo,
              Ccblas::Tran trans, Ccblas::Diag diag,
              FloatType alpha, const Matrix &a);

    void potrf(Ccblas::Uplo uplo, int64_t lookahead=0);
};

//------------------------------------------------------------------------------
template<class FloatType>
void Matrix<FloatType>::copyTo(int64_t m, int64_t n, FloatType *a,
                               int64_t lda, int64_t mb, int64_t nb)
{
    for (int64_t i = 0; i < m; i += mb)
        for (int64_t j = 0; j < n; j += nb)
            if (j <= i)
                (*this)(i/mb, j/nb) =
                    new Tile<FloatType>(std::min(mb, m-i), std::min(nb, n-j),
                                        &a[(size_t)lda*j+i], lda);
}

//------------------------------------------------------------------------------
template<class FloatType>
void Matrix<FloatType>::copyFrom(int64_t m, int64_t n, FloatType *a,
                                 int64_t lda, int64_t mb, int64_t nb)
{
    for (int64_t i = 0; i < m; i += mb)
        for (int64_t j = 0; j < n; j += nb)
            if (j <= i)
                (*this)(i/mb, j/nb)->copyFrom(&a[(size_t)lda*j+i], lda);
}

//------------------------------------------------------------------------------
template<class FloatType>
void Matrix<FloatType>::syrk_task(Ccblas::Uplo uplo, Ccblas::Tran trans,
                                  FloatType alpha, const Matrix &a,
                                  FloatType beta)
{
    using namespace Ccblas;

    Matrix<FloatType> c = *this;

    // Lower, NoTrans
    for (int64_t n = 0; n < nt_; ++n) {

        for (int64_t k = 0; k < a.nt_; ++k)
            #pragma omp task
            c(n, n)->syrk(uplo, trans, -1.0, a(n, k), k == 0 ? beta : 1.0);

        for (int64_t m = n+1; m < mt_; ++m)
            for (int64_t k = 0; k < a.nt_; ++k)
                #pragma omp task
                c(m, n)->gemm(trans, Tran::Trans,
                              alpha, a(m, k), a(n, k), k == 0 ? beta : 1.0);
    }
    #pragma omp taskwait
}

//------------------------------------------------------------------------------
template<class FloatType>
void Matrix<FloatType>::syrk_nest(Ccblas::Uplo uplo, Ccblas::Tran trans,
                                  FloatType alpha, const Matrix &a,
                                  FloatType beta)
{
    using namespace Ccblas;

    Matrix<FloatType> c = *this;

    for (int64_t n = 0; n < nt_; ++n) {
        for (int64_t k = 0; k < a.nt_; ++k)
            #pragma omp task
            c(n, n)->syrk(uplo, trans, -1.0, a(n, k), k == 0 ? beta : 1.0);
    }

    #pragma omp parallel for collapse(3) schedule(dynamic, 1) num_threads(num_threads_syrk_nest)
    for (int64_t n = 0; n < nt_; ++n) {
        for (int64_t m = 0; m < mt_; ++m)
            for (int64_t k = 0; k < a.nt_; ++k)
                if (m >= n+1)
                    c(m, n)->gemm(trans, Tran::Trans,
                                  alpha, a(m, k), a(n, k), k == 0 ? beta : 1.0);
    }
    #pragma omp taskwait
}

//------------------------------------------------------------------------------
template<class FloatType>
void Matrix<FloatType>::syrk_batch(Ccblas::Uplo uplo, Ccblas::Tran trans,
                                   FloatType alpha, const Matrix &a,
                                   FloatType beta)
{
    using namespace Ccblas;

    Matrix<FloatType> c = *this;

    // Lower, NoTrans
    for (int64_t n = 0; n < nt_; ++n) {
        for (int64_t k = 0; k < a.nt_; ++k)
            #pragma omp task
            c(n, n)->syrk(uplo, trans, -1.0, a(n, k), k == 0 ? beta : 1.0);
    }

    CBLAS_TRANSPOSE transa_array[1];
    CBLAS_TRANSPOSE transb_array[1];
    int m_array[1];
    int n_array[1];
    int k_array[1];
    double alpha_array[1];
    const double **a_array;
    int lda_array[1];
    const double **b_array;
    int ldb_array[1];
    double beta_array[1];
    double **c_array;
    int ldc_array[1];

    int nb = c(0, 0)->nb_;
    transa_array[0] = CblasNoTrans;
    transb_array[0] = CblasTrans;
    m_array[0] = nb;
    n_array[0] = nb;
    k_array[0] = nb;
    alpha_array[0] = alpha;
    lda_array[0] = nb;
    ldb_array[0] = nb;
    beta_array[0] = beta;
    ldc_array[0] = nb;
    int group_size = (nt_*(nt_-1))/2;

    a_array = (const double**)malloc(sizeof(double*)*group_size);
    b_array = (const double**)malloc(sizeof(double*)*group_size);
    c_array = (double**)malloc(sizeof(double*)*group_size);
    assert(a_array != nullptr);
    assert(b_array != nullptr);
    assert(c_array != nullptr);

    int i;
    for (int64_t n = 0; n < nt_; ++n) {
        for (int64_t m = n+1; m < mt_; ++m) {
            for (int64_t k = 0; k < a.nt_; ++k) {
                a_array[i] = a(m, k)->data_;
                b_array[i] = a(n, k)->data_;
                c_array[i] = c(m, n)->data_;
                ++i;
            }
        }
    }
    trace_cpu_start();
    mkl_set_num_threads_local(num_threads_syrk_batch);
    cblas_dgemm_batch(CblasColMajor, transa_array, transb_array,
                      m_array, n_array, k_array, alpha_array,
                      a_array, lda_array, b_array, ldb_array, beta_array,
                      c_array, ldc_array, 1, &group_size);
    mkl_set_num_threads_local(1);
    trace_cpu_stop("DarkGreen");

    free(a_array);
    free(b_array);
    free(c_array);

    #pragma omp taskwait
}

//------------------------------------------------------------------------------
template<class FloatType>
void Matrix<FloatType>::trsm(Ccblas::Side side, Ccblas::Uplo uplo,
                             Ccblas::Tran trans, Ccblas::Diag diag,
                             FloatType alpha, const Matrix &a)
{
    using namespace Ccblas;

    Matrix<FloatType> b = *this;

    // Right, Lower, Trans
    for (int64_t k = 0; k < nt_; ++k) {

        for (int64_t m = 0; m < mt_; ++m) {
            #pragma omp task
            b(m, k)->trsm(side, uplo, trans, diag, 1.0, a(k, k)); 

            for (int64_t n = k+1; n < nt_; ++n)
                #pragma omp task
                b(m, n)->gemm(Tran::NoTrans, trans,
                              -1.0/alpha, b(m, k), a(n, k), 1.0);
        }
    }
    #pragma omp taskwait
}

//------------------------------------------------------------------------------
template<class FloatType>
void Matrix<FloatType>::potrf(Ccblas::Uplo uplo, int64_t lookahead)
{
    using namespace Ccblas;

    Matrix<FloatType> a = *this;

    std::vector<uint8_t> column_vector(nt_);
    uint8_t *column = column_vector.data();

    #pragma omp parallel num_threads(num_threads_potrf)
    {
    #pragma omp master
    for (int64_t k = 0; k < nt_; ++k) {
        #pragma omp task depend(inout:column[k])
        {
            a(k, k)->potrf(uplo);

            for (int64_t m = k+1; m < nt_; ++m)
//              #pragma omp task priority(1)
                #pragma omp task
                {
                    a(m, k)->trsm(Side::Right, Uplo::Lower,
                                  Tran::Trans, Diag::NonUnit,
                                  1.0, a(k, k));

                    if (m-k-1 > 0)
                        a(m, k)->pack_a(m-k-1);

                    if (nt_-m-1 > 0)
                        a(m, k)->pack_b(nt_-m-1);
                }

            #pragma omp taskwait
        }
        for (int64_t n = k+1; n < k+1+lookahead && n < nt_; ++n) {
            #pragma omp task depend(in:column[k]) \
                             depend(inout:column[n])
            {
                #pragma omp task
                a(n, n)->syrk(Uplo::Lower, Tran::NoTrans,
                              -1.0, a(n, k), 1.0);            

                for (int64_t m = n+1; m < nt_; ++m)
                    #pragma omp task
                    a(m, n)->gemm(Tran::NoTrans, Tran::Trans,
                                  -1.0, a(m, k), a(n, k), 1.0);

                #pragma omp taskwait
            }
        }
        if (k+1+lookahead < nt_)
            #pragma omp task depend(in:column[k]) \
                             depend(inout:column[k+1+lookahead]) \
                             depend(inout:column[nt_-1])
            Matrix(a, k+1+lookahead, k+1+lookahead,
#if SYRK_NEST_TYPE == SYRK_TASK
                   nt_-1-k-lookahead, nt_-1-k-lookahead).syrk_task(
#elif SYRK_NEST_TYPE == SYRK_NEST
                   nt_-1-k-lookahead, nt_-1-k-lookahead).syrk_nest(
#elif SYRK_NEST_TYPE == SYRK_BATCH
                   nt_-1-k-lookahead, nt_-1-k-lookahead).syrk_batch(
#else
#error "Please specify SYRK_NEST_TYPE"
#endif
                Uplo::Lower, Tran::NoTrans,
                -1.0, Matrix(a, k+1+lookahead, k, nt_-1-k-lookahead, 1), 1.0);
    }
        #pragma omp barrier // workaround
    }
}

} // namespace Slate

#endif // SLATE_MATRIX_HH
