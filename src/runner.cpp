#include "kernels.h"
#include "matrix_utils.h" // Include the matrix utilities
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

// Utility functions for packing matrices
static void pack_A_block(const double *A, double *packA, int N, int i0, int k0, int bs) {
    for (int ii = 0; ii < bs; ++ii) {
        const double *arow = &A[(i0 + ii) * N + k0];
        double *prow = &packA[ii * bs];
        for (int kk = 0; kk < bs; ++kk) prow[kk] = arow[kk];
    }
}

static void pack_B_block(const double *B, double *packB, int N, int k0, int j0, int bs) {
    for (int kk = 0; kk < bs; ++kk) {
        const double *brow = &B[(k0 + kk) * N + j0];
        double *prow = &packB[kk * bs];
        for (int jj = 0; jj < bs; ++jj) prow[jj] = brow[jj];
    }
}

// The main benchmark loop
void run_benchmark(const double *A, const double *B, double *C, int N, int BS, matmul_func_t kernel) {
    // Use the utility to allocate aligned packing buffers
    double *packA = matrix_utils::alloc(BS);
    double *packB = matrix_utils::alloc(BS);

    if (!packA || !packB) {
        perror("Failed to allocate packing buffers");
        return;
    }

    for (int i0 = 0; i0 < N; i0 += BS) {
        for (int k0 = 0; k0 < N; k0 += BS) {
            pack_A_block(A, packA, N, i0, k0, BS);
            for (int j0 = 0; j0 < N; j0 += BS) {
                pack_B_block(B, packB, N, k0, j0, BS);
                kernel(packA, packB, C, N, i0, j0, k0, BS);
            }
        }
    }

    free(packA);
    free(packB);
}
