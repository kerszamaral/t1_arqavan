#include <cstddef>

extern "C" void matmul_block_scalar(const double *packA, const double *packB, double *C,
                                    int N, int i0, int j0, int k0, int bs)
{
    // packA layout: packA[ii*bs + kk]
    // packB layout: packB[kk*bs + jj]
    for (int ii = 0; ii < bs; ++ii) {
        int i = i0 + ii;
        for (int jj = 0; jj < bs; ++jj) {
            int j = j0 + jj;
            // single accumulator -> dependent chain
            double sum = C[i * N + j];
            for (int kk = 0; kk < bs; ++kk) {
                double a = packA[ii * bs + kk];
                double b = packB[kk * bs + jj];
                sum = sum + a * b;
            }
            C[i * N + j] = sum;
        }
    }
}

