
#ifndef CCBLAS_HH
#define CCBLAS_HH

#include <complex>

#include "mkl_cblas.h"

namespace Ccblas {

enum class Order {ColMajor, RowMajor};
enum class Side {Left, Right};
enum class Uplo {Upper, Lower};
enum class Tran {Trans, NoTrans, ConjTrans};
enum class Diag {Unit, NonUnit};

//------------------------------------------------------------------------------
inline
void trsm(Ccblas::Order order, Ccblas::Side side, Ccblas::Uplo uplo,
          Ccblas::Tran transa, Ccblas::Diag diag, int64_t m, int64_t n,
          float alpha, float *a, int64_t lda, float *b, int64_t ldb)
{
    cblas_strsm(CblasColMajor, CblasRight, CblasLower, CblasTrans, CblasNonUnit,
                m, n, alpha, a, lda, b, ldb);
}

inline
void trsm(Ccblas::Order order, Ccblas::Side side, Ccblas::Uplo uplo,
          Ccblas::Tran transa, Ccblas::Diag diag, int64_t m, int64_t n,
          double alpha, double *a, int64_t lda, double *b, int64_t ldb)
{
    cblas_dtrsm(CblasColMajor, CblasRight, CblasLower, CblasTrans, CblasNonUnit,
                m, n, alpha, a, lda, b, ldb);
}

inline
void trsm(Ccblas::Order order, Ccblas::Side side, Ccblas::Uplo uplo,
          Ccblas::Tran transa, Ccblas::Diag diag, int64_t m, int64_t n,
          std::complex<float> alpha, std::complex<float> *a, int64_t lda,
          std::complex<float> *b, int64_t ldb)
{
    cblas_ctrsm(CblasColMajor, CblasRight, CblasLower, CblasConjTrans,
                CblasNonUnit, m, n, (const void*)&alpha, a, lda, b, ldb);
}

inline
void trsm(Ccblas::Order order, Ccblas::Side side, Ccblas::Uplo uplo,
          Ccblas::Tran transa, Ccblas::Diag diag, int64_t m, int64_t n,
          std::complex<double> alpha, std::complex<double> *a, int64_t lda,
          std::complex<double> *b, int64_t ldb)
{
    cblas_ztrsm(CblasColMajor, CblasRight, CblasLower, CblasConjTrans,
                CblasNonUnit, m, n, (const void*)&alpha, a, lda, b, ldb);
}

//------------------------------------------------------------------------------
inline
void syrk(Ccblas::Order order, Ccblas::Uplo uplo, Ccblas::Tran trans,
          int64_t n, int64_t k, float alpha, float *a, int64_t lda,
          float beta, float *c, int64_t ldc)
{
    cblas_ssyrk(CblasColMajor, CblasLower, CblasNoTrans,
                n, k, alpha, a, lda, beta, c, ldc);
}

inline
void syrk(Ccblas::Order order, Ccblas::Uplo uplo, Ccblas::Tran trans,
          int64_t n, int64_t k, double alpha, double *a, int64_t lda,
          double beta, double *c, int64_t ldc)
{
    cblas_dsyrk(CblasColMajor, CblasLower, CblasNoTrans,
                n, k, alpha, a, lda, beta, c, ldc);
}

inline
void syrk(Ccblas::Order order, Ccblas::Uplo uplo, Ccblas::Tran trans,
          int64_t n, int64_t k, float alpha, std::complex<float> *a,
          int64_t lda, float beta, std::complex<float> *c, int64_t ldc)
{
    cblas_cherk(CblasColMajor, CblasLower, CblasNoTrans,
                n, k, alpha, a, lda, beta, c, ldc);
}

inline
void syrk(Ccblas::Order order, Ccblas::Uplo uplo, Ccblas::Tran trans,
          int64_t n, int64_t k, double alpha, std::complex<double> *a,
          int64_t lda, double beta, std::complex<double> *c, int64_t ldc)
{
    cblas_zherk(CblasColMajor, CblasLower, CblasNoTrans,
                n, k, alpha, a, lda, beta, c, ldc);
}

//------------------------------------------------------------------------------
inline
void gemm(Ccblas::Order order, Ccblas::Tran transa, Ccblas::Tran transb,
          int64_t m, int64_t n, int64_t k, float alpha, float *a, int64_t lda,
          float *b, int64_t ldb, float beta, float *c, int64_t ldc)
{
    cblas_sgemm(CblasColMajor, CblasNoTrans, CblasTrans, m, n, k, 
                alpha, a, lda, b, ldb, beta, c, ldc);
}

inline
void gemm(Ccblas::Order order, Ccblas::Tran transa, Ccblas::Tran transb,
          int64_t m, int64_t n, int64_t k, double alpha, double *a, int64_t lda,
          double *b, int64_t ldb, double beta, double *c, int64_t ldc)
{
    cblas_dgemm(CblasColMajor, CblasNoTrans, CblasTrans, m, n, k, 
                alpha, a, lda, b, ldb, beta, c, ldc);
}

inline
void gemm(Ccblas::Order order, Ccblas::Tran transa, Ccblas::Tran transb,
          int64_t m, int64_t n, int64_t k, std::complex<float> alpha,
          std::complex<float> *a, int64_t lda, std::complex<float> *b,
          int64_t ldb, std::complex<float> beta,
          std::complex<float> *c, int64_t ldc)
{
    cblas_cgemm(CblasColMajor, CblasNoTrans, CblasTrans, m, n, k, 
                (const void*)&alpha, a, lda, b, ldb, (const void*)&beta,
                c, ldc);
}

inline
void gemm(Ccblas::Order order, Ccblas::Tran transa, Ccblas::Tran transb,
          int64_t m, int64_t n, int64_t k, std::complex<double> alpha,
          std::complex<double> *a, int64_t lda, std::complex<double> *b,
          int64_t ldb, std::complex<double> beta,
          std::complex<double> *c, int64_t ldc)
{
    cblas_zgemm(CblasColMajor, CblasNoTrans, CblasTrans, m, n, k, 
                (const void*)&alpha, a, lda, b, ldb, (const void*)&beta,
                c, ldc);
}

} // namespace CCBLAS_HH

#endif // CCBLAS_HH
