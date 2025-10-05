#include "kernels_whole.h"

extern "C" {
    void dgemm_(const char *TRANSA, const char *TRANSB, const int *M, const int *N, const int *K,
               const double *ALPHA, const double *A, const int *LDA, const double *B, const int *LDB,
               const double *BETA, double *C, const int *LDC);
}

// This kernel makes a single, efficient call to BLAS dgemm for the entire matrix.
extern "C" void kernel_blas_whole(const double *A, const double *B, double *C, int N) {
    char trans = 'N';
    double alpha = 1.0;
    double beta = 0.0; // Overwrite C with the result

    // For row-major C=A*B, we ask BLAS to compute C=alpha*B*A + beta*C 
    // since BLAS is column-major.
    dgemm_(&trans, &trans, &N, &N, &N, &alpha, B, &N, A, &N, &beta, C, &N);
}
