#pragma once

#include "kernels.h"

void run_benchmark(const double *A, const double *B, double *C, int N, int BS, matmul_func_t kernel);

