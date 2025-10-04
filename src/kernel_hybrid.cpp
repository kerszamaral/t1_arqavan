#include <immintrin.h>
#include <cstddef>

// Default unroll factors if not specified in the Makefile
#ifndef HYBRID_AVX_UNROLL
#define HYBRID_AVX_UNROLL 1 // Corresponds to 8 columns
#endif

#ifndef HYBRID_SCALAR_UNROLL
#define HYBRID_SCALAR_UNROLL 2 // Corresponds to 2 columns
#endif

// Each AVX instruction processes 8 doubles
constexpr int AVX_STEP_SIZE = 8;
// Total step size for one iteration of the unrolled loop
constexpr int TOTAL_STEP_SIZE = (HYBRID_AVX_UNROLL * AVX_STEP_SIZE) + HYBRID_SCALAR_UNROLL;

extern "C" void kernel_hybrid(const double *packA, const double *packB, double *C,
                                    int N, int i0, int j0, int k0, int bs)
{
    for (int ii = 0; ii < bs; ++ii) {
        int i = i0 + ii;
        for (int j_off = 0; j_off < bs; j_off += TOTAL_STEP_SIZE) {

            // Unrolled AVX part
            #pragma unroll(HYBRID_AVX_UNROLL)
            for (int avx_idx = 0; avx_idx < HYBRID_AVX_UNROLL; ++avx_idx) {
                int current_j_avx = j_off + avx_idx * AVX_STEP_SIZE;
                __m512d cvec = _mm512_loadu_pd(&C[i * N + j0 + current_j_avx]);
                const double *packA_row = &packA[ii * bs];
                for (int kk = 0; kk < bs; ++kk) {
                    double aval = packA_row[kk];
                    const double *brow = &packB[kk * bs + current_j_avx];
                    __m512d bvec = _mm512_loadu_pd(brow);
                    __m512d avec = _mm512_set1_pd(aval);
                    cvec = _mm512_fmadd_pd(avec, bvec, cvec);
                }
                _mm512_storeu_pd(&C[i * N + j0 + current_j_avx], cvec);
            }

            // Unrolled Scalar part
            int scalar_start_offset = HYBRID_AVX_UNROLL * AVX_STEP_SIZE;
            #pragma unroll(HYBRID_SCALAR_UNROLL)
            for (int scalar_idx = 0; scalar_idx < HYBRID_SCALAR_UNROLL; ++scalar_idx) {
                int current_j_scalar = j_off + scalar_start_offset + scalar_idx;
                if (current_j_scalar < bs) { // Bounds check
                    double sum = C[i * N + j0 + current_j_scalar];
                    for (int kk = 0; kk < bs; ++kk) {
                        double a = packA[ii * bs + kk];
                        double b = packB[kk * bs + current_j_scalar];
                        sum += a * b;
                    }
                    C[i * N + j0 + current_j_scalar] = sum;
                }
            }
        }
    }
}
