#include <immintrin.h>
#include <cstddef>

#ifndef INTERLEAVED_AVX_OPS
#define INTERLEAVED_AVX_OPS 1
#endif

#ifndef INTERLEAVED_SCALAR_OPS
#define INTERLEAVED_SCALAR_OPS 1
#endif

constexpr int AVX_STEP_SIZE = 8;
constexpr int SCALAR_STEP_SIZE = 1;
constexpr int TOTAL_STEP_SIZE = (INTERLEAVED_AVX_OPS * AVX_STEP_SIZE) + (INTERLEAVED_SCALAR_OPS * SCALAR_STEP_SIZE);

extern "C" void kernel_interleaved(const double *packA, const double *packB, double *C,
                                   int N, int i0, int j0, int k0, int bs) {
    for (int ii = 0; ii < bs; ++ii) {
        int i = i0 + ii;
        int j_off = 0;
        
        // --- Main Loop ---
        // Process full interleaved chunks that fit within the block size.
        for (; j_off + TOTAL_STEP_SIZE <= bs; j_off += TOTAL_STEP_SIZE) {
            __m512d cvecs[INTERLEAVED_AVX_OPS];
            double scalar_sums[INTERLEAVED_SCALAR_OPS];
            int scalar_start_offset = INTERLEAVED_AVX_OPS * AVX_STEP_SIZE;

            // Load initial values from C
            for(int k=0; k<INTERLEAVED_AVX_OPS; ++k) {
                cvecs[k] = _mm512_loadu_pd(&C[i * N + j0 + j_off + k * AVX_STEP_SIZE]);
            }
            for(int k=0; k<INTERLEAVED_SCALAR_OPS; ++k) {
                scalar_sums[k] = C[i * N + j0 + j_off + scalar_start_offset + k];
            }

            // Interleaved accumulation loop over k
            for (int kk = 0; kk < bs; ++kk) {
                __m512d avec = _mm512_set1_pd(packA[ii * bs + kk]);
                double aval = packA[ii * bs + kk];
                
                // AVX part
                for(int k=0; k<INTERLEAVED_AVX_OPS; ++k) {
                    __m512d bvec = _mm512_loadu_pd(&packB[kk * bs + j_off + k * AVX_STEP_SIZE]);
                    cvecs[k] = _mm512_fmadd_pd(avec, bvec, cvecs[k]);
                }
                // Scalar part
                for(int k=0; k<INTERLEAVED_SCALAR_OPS; ++k) {
                    scalar_sums[k] += aval * packB[kk * bs + j_off + scalar_start_offset + k];
                }
            }

            // Store results back to C
            for(int k=0; k<INTERLEAVED_AVX_OPS; ++k) {
                _mm512_storeu_pd(&C[i * N + j0 + j_off + k * AVX_STEP_SIZE], cvecs[k]);
            }
            for(int k=0; k<INTERLEAVED_SCALAR_OPS; ++k) {
                C[i * N + j0 + j_off + scalar_start_offset + k] = scalar_sums[k];
            }
        }

        // --- Cleanup Loop ---
        // Process the remainder with a safe scalar loop.
        for (; j_off < bs; ++j_off) {
            double sum = C[i * N + j0 + j_off];
            for (int kk = 0; kk < bs; ++kk) {
                sum += packA[ii * bs + kk] * packB[kk * bs + j_off];
            }
            C[i * N + j0 + j_off] = sum;
        }
    }
}
