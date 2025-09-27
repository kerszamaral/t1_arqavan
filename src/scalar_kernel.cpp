#include <cstddef>

extern "C" void matmul_block_scalar(const double *A, const double *B, double *C,
                         int N, int i0, int j0, int k0, int bs)
{
    for (int i = i0; i < i0 + bs; ++i) {
        for (int j = j0; j < j0 + bs; ++j) {
            double sum = C[i * N + j];
            for (int k = k0; k < k0 + bs; ++k) {
                sum += A[i * N + k] * B[k * N + j];
            }
            C[i * N + j] = sum;
        }
    }
}
