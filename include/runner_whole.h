#pragma once

#include "kernels_whole.h"

void run_benchmark_whole_matrix(const double *A, const double *B, double *C, int N, matmul_whole_func_t kernel);

