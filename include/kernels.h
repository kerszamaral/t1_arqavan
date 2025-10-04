#pragma once

// Define a function pointer type for all matmul kernels
using matmul_func_t = void (*)(const double *packA, const double *packB, double *C,
                              int N, int i0, int j0, int k0, int bs);

// Declare all kernel functions
extern "C" void kernel_avx(const double *packA, const double *packB, double *C,
                           int N, int i0, int j0, int k0, int bs);
extern "C" void kernel_scalar(const double *packA, const double *packB, double *C,
                              int N, int i0, int j0, int k0, int bs);
extern "C" void kernel_hybrid(const double *packA, const double *packB, double *C,
                              int N, int i0, int j0, int k0, int bs);
extern "C" void kernel_interleaved(const double *packA, const double *packB, double *C,
                                   int N, int i0, int j0, int k0, int bs);


