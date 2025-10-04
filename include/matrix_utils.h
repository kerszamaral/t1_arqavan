#pragma once

namespace matrix_utils {

// Allocates an N x N matrix with 64-byte alignment suitable for AVX512
double* alloc(int N);

// Fills an N x N matrix with random double-precision values
void fill(double *matrix, int N);

} // namespace matrix_utils

