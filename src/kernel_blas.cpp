#include "kernels.h"

// BLAS dgemm is often implemented in Fortran, so we declare it with C linkage
// to handle potential name mangling (e.g., dgemm -> dgemm_).
extern "C" {
    void dgemm_(const char *TRANSA, const char *TRANSB, const int *M, const int *N, const int *K,
               const double *ALPHA, const double *A, const int *LDA, const double *B, const int *LDB,
               const double *BETA, double *C, const int *LDC);
}

// Our C++ wrapper that calls the BLAS dgemm function.
// Note: This kernel ignores the packed layout and operates directly on the original matrices.
extern "C" void kernel_blas(const double *A, const double *B, double *C,
                            int N, int i0, int j0, int k0, int bs) {
    // BLAS expects column-major layout, but C/C++ uses row-major.
    // To multiply A * B, we can ask BLAS to compute B^T * A^T, which results in (A*B)^T.
    // Since our matrices are row-major, this effectively gives us the correct C = A*B.
    char transa = 'N'; // No transpose for A (becomes B^T)
    char transb = 'N'; // No transpose for B (becomes A^T)
    
    double alpha = 1.0;
    double beta = 1.0; // Add to existing values in C

    // Note: In this context, M, N, and K all correspond to the block size (BS)
    // because we are calling this for each block.
    dgemm_(&transa, &transb, &bs, &bs, &bs, &alpha, B, &N, A, &N, &beta, C, &N);
}
