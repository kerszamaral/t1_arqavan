#include "kernels_whole.h"

// A simple O(N^3) matrix multiplication kernel operating on the whole matrix.
extern "C" void kernel_scalar_whole(const double *A, const double *B, double *C, int N) {
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            double sum = 0.0;
            for (int k = 0; k < N; ++k) {
                sum += A[i * N + k] * B[k * N + j];
            }
            C[i * N + j] = sum;
        }
    }
}
