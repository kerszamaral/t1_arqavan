#include <immintrin.h>
#include <cstddef>

extern "C" void kernel_avx(const double *packA, const double *packB, double *C,
                                    int N, int i0, int j0, int k0, int bs)
{
    // packA layout: packA[ii*bs + kk]  (ii in [0..bs), kk in [0..bs))
    // packB layout: packB[kk*bs + jj]  (kk in [0..bs), jj in [0..bs))
    // C indexing uses global N and offsets i0/j0.
    for (int ii = 0; ii < bs; ++ii) {
        int i = i0 + ii;
        for (int j_off = 0; j_off < bs; j_off += 8) {
            // load current C row (8 doubles)
            __m512d cvec = _mm512_loadu_pd(&C[i * N + (j0 + j_off)]);
            // reduction over k-block
            const double *packA_row = &packA[ii * bs]; // avals at packA_row[kk]
            for (int kk = 0; kk < bs; ++kk) {
                double aval = packA_row[kk];
                const double *brow = &packB[kk * bs + j_off]; // 8 contiguous doubles
                __m512d bvec = _mm512_loadu_pd(brow);
                __m512d avec = _mm512_set1_pd(aval);
                cvec = _mm512_fmadd_pd(avec, bvec, cvec);
            }
            _mm512_storeu_pd(&C[i * N + (j0 + j_off)], cvec);
        }
        // NOTE: assumes bs is multiple of 8. If not, need tail scalar handling.
    }
}

