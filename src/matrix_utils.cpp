#include "matrix_utils.h"
#include <cstdlib> // For posix_memalign and free

namespace matrix_utils {

double* alloc(int N) {
    void* p = nullptr;
    // Align memory to a 64-byte boundary for AVX-512 compatibility
    if (posix_memalign(&p, 64, sizeof(double) * size_t(N) * size_t(N)) != 0) {
        return nullptr;
    }
    return static_cast<double*>(p);
}

void fill(double *matrix, int N) {
    for (long i = 0; i < (long)N * N; ++i) {
        // Simple pseudo-random filling
        matrix[i] = static_cast<double>((i * 33 + 7) % 100) + 1.0;
    }
}

} // namespace matrix_utils
