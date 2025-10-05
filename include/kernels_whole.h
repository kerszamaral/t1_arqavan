#pragma once

// Define a function pointer type for whole-matrix kernels
using matmul_whole_func_t = void (*)(const double *A, const double *B, double *C, int N);

// Declare the new whole-matrix kernel functions
extern "C" void kernel_scalar_whole(const double *A, const double *B, double *C, int N);
extern "C" void kernel_blas_whole(const double *A, const double *B, double *C, int N);

