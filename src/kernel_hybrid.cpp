#include <immintrin.h>
#include <cstddef>

#ifndef HYBRID_AVX_UNROLL
#define HYBRID_AVX_UNROLL 1
#endif

#ifndef HYBRID_SCALAR_UNROLL
#define HYBRID_SCALAR_UNROLL 2
#endif

constexpr int AVX_STEP_SIZE = 8;
constexpr int TOTAL_STEP_SIZE = (HYBRID_AVX_UNROLL * AVX_STEP_SIZE) + HYBRID_SCALAR_UNROLL;

extern "C" void kernel_hybrid(const double *packA, const double *packB, double *C,
                              int N, int i0, int j0, int k0, int bs) {
    for (int ii = 0; ii < bs; ++ii) {
        int i = i0 + ii;
        int j_off = 0;

        // Main loop for full chunks that are guaranteed to be within bounds
        for (; j_off + TOTAL_STEP_SIZE <= bs; j_off += TOTAL_STEP_SIZE) {
            // --- AVX Part (Now safe to execute) ---
            for (int avx_idx = 0; avx_idx < HYBRID_AVX_UNROLL; ++avx_idx) {
                int current_j_avx = j_off + avx_idx * AVX_STEP_SIZE;
                __m512d cvec = _mm512_loadu_pd(&C[i * N + j0 + current_j_avx]);
                for (int kk = 0; kk < bs; ++kk) {
                    __m512d avec = _mm512_set1_pd(packA[ii * bs + kk]);
                    __m512d bvec = _mm512_loadu_pd(&packB[kk * bs + current_j_avx]);
                    cvec = _mm512_fmadd_pd(avec, bvec, cvec);
                }
                _mm512_storeu_pd(&C[i * N + j0 + current_j_avx], cvec);
            }

            // --- Scalar Part (Now safe to execute) ---
            int scalar_start_offset = HYBRID_AVX_UNROLL * AVX_STEP_SIZE;
            for (int scalar_idx = 0; scalar_idx < HYBRID_SCALAR_UNROLL; ++scalar_idx) {
                int current_j_scalar = j_off + scalar_start_offset + scalar_idx;
                double sum = C[i * N + j0 + current_j_scalar];
                for (int kk = 0; kk < bs; ++kk) {
                    sum += packA[ii * bs + kk] * packB[kk * bs + current_j_scalar];
                }
                C[i * N + j0 + current_j_scalar] = sum;
            }
        }

        // --- Cleanup Loop ---
        // Process any remaining columns one by one with a simple scalar loop.
        for (; j_off < bs; ++j_off) {
            double sum = C[i * N + j0 + j_off];
            for (int kk = 0; kk < bs; ++kk) {
                sum += packA[ii * bs + kk] * packB[kk * bs + j_off];
            }
            C[i * N + j0 + j_off] = sum;
        }
    }
}
