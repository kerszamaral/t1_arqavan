#include <immintrin.h>
#include <cstddef>

// Default interleave factors if not specified
#ifndef INTERLEAVED_AVX_OPS
#define INTERLEAVED_AVX_OPS 1
#endif

#ifndef INTERLEAVED_SCALAR_OPS
#define INTERLEAVED_SCALAR_OPS 1
#endif

// Each AVX instruction processes 8 doubles
constexpr int AVX_STEP_SIZE = 8;
constexpr int SCALAR_STEP_SIZE = 1;
constexpr int TOTAL_STEP_SIZE = (INTERLEAVED_AVX_OPS * AVX_STEP_SIZE) + (INTERLEAVED_SCALAR_OPS * SCALAR_STEP_SIZE);

extern "C" void kernel_interleaved(const double *packA, const double *packB, double *C,
                                         int N, int i0, int j0, int k0, int bs)
{
    for (int ii = 0; ii < bs; ++ii) {
        int i = i0 + ii;
        for (int j_off = 0; j_off < bs; j_off += TOTAL_STEP_SIZE) {
            // AVX accumulators
            __m512d cvecs[INTERLEAVED_AVX_OPS];
            for(int k=0; k<INTERLEAVED_AVX_OPS; ++k) {
                cvecs[k] = _mm512_loadu_pd(&C[i * N + j0 + j_off + k * AVX_STEP_SIZE]);
            }

            // Scalar accumulators
            double scalar_sums[INTERLEAVED_SCALAR_OPS] = {0.0};
            int scalar_start_offset = INTERLEAVED_AVX_OPS * AVX_STEP_SIZE;
            for(int k=0; k<INTERLEAVED_SCALAR_OPS; ++k) {
                scalar_sums[k] = C[i * N + j0 + j_off + scalar_start_offset + k];
            }

            // Interleaved k-loop
            for (int kk = 0; kk < bs; ++kk) {
                const double *packA_row_val = &packA[ii * bs + kk];
                double aval = *packA_row_val;
                __m512d avec = _mm512_set1_pd(aval);

                // AVX part
                #pragma unroll(INTERLEAVED_AVX_OPS)
                for(int k=0; k<INTERLEAVED_AVX_OPS; ++k) {
                    const double *brow = &packB[kk * bs + j_off + k * AVX_STEP_SIZE];
                    __m512d bvec = _mm512_loadu_pd(brow);
                    cvecs[k] = _mm512_fmadd_pd(avec, bvec, cvecs[k]);
                }

                // Scalar part
                #pragma unroll(INTERLEAVED_SCALAR_OPS)
                for(int k=0; k<INTERLEAVED_SCALAR_OPS; ++k) {
                    double bval = packB[kk * bs + j_off + scalar_start_offset + k];
                    scalar_sums[k] += aval * bval;
                }
            }

            // Store results
            for(int k=0; k<INTERLEAVED_AVX_OPS; ++k) {
                _mm512_storeu_pd(&C[i * N + j0 + j_off + k * AVX_STEP_SIZE], cvecs[k]);
            }
            for(int k=0; k<INTERLEAVED_SCALAR_OPS; ++k) {
                C[i * N + j0 + j_off + scalar_start_offset + k] = scalar_sums[k];
            }
        }
    }
}
