#include "kernels.h"

// BLAS dgemm is often implemented in Fortran, so we declare it with C linkage
// to handle potential name mangling (e.g., dgemm -> dgemm_).
extern "C" {
    void dgemm_(const char *TRANSA, const char *TRANSB, const int *M, const int *N, const int *K,
               const double *ALPHA, const double *A, const int *LDA, const double *B, const int *LDB,
               const double *BETA, double *C, const int *LDC);
}

// Our C++ wrapper that calls the BLAS dgemm function.
extern "C" void kernel_blas(const double *packA, const double *packB, double *C,
                                    int N, int i0, int j0, int k0, int bs) {
    char trans = 'N';
    double alpha = 1.0;
    double beta = 1.0; // Accumulate onto existing C values

    // We are multiplying two bs x bs packed matrices.
    // The leading dimension (LDA/LDB) of the packed blocks is 'bs'.
    // The result is written into a sub-block of C, which has a leading dimension of 'N'.
    dgemm_(&trans, &trans, &bs, &bs, &bs, &alpha, packB, &bs, packA, &bs, &beta, &C[i0 * N + j0], &N);
}
