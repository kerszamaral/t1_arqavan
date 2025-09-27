#include <immintrin.h>
#include <cstddef>

extern "C" void matmul_block_avx512(const double *A, const double *B, double *C,
                         int N, int i0, int j0, int k0, int bs)
{
    for (int i = i0; i < i0 + bs; ++i) {
        for (int j = j0; j < j0 + bs; j += 8) { // 8 doubles per 512-bit vector
            __m512d cvec = _mm512_loadu_pd(&C[i * N + j]);
            for (int k = k0; k < k0 + bs; ++k) {
                double aval = A[i * N + k];
                __m512d bvec = _mm512_loadu_pd(&B[k * N + j]);
                __m512d avec = _mm512_set1_pd(aval);
                cvec = _mm512_fmadd_pd(avec, bvec, cvec);
            }
            _mm512_storeu_pd(&C[i * N + j], cvec);
        }
    }
}
