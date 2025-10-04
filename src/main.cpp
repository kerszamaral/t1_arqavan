#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iostream>
#include <string>

#include "papito.h"
#include "kernels.h"
#include "dispatch_kernels.h"
#include "runner.h"
#include "matrix_utils.h" // Include the new header

static void usage(const char *prg) {
    fprintf(stderr, "Usage: %s N BS mode seed\n", prg);
    fprintf(stderr, "Available modes: avx, scalar, hybrid, interleaved\n");
}

int main(int argc, char **argv) {
    if (argc < 5) { usage(argv[0]); return 1; }
    int N = atoi(argv[1]);
    int BS = atoi(argv[2]);
    const char *mode = argv[3];
    unsigned int seed = (unsigned int)atoi(argv[4]);

    if (N % 8 != 0 || BS <= 0 || N % BS != 0) {
        fprintf(stderr, "Error: N must be a multiple of 8, and BS must be a positive divisor of N.\n");
        return 1;
    }

    matmul_func_t kernel = get_kernel_for_mode(mode);
    if (kernel == nullptr) {
        fprintf(stderr, "Error: Unknown mode '%s'.\n", mode);
        usage(argv[0]);
        return 1;
    }

    // Use the new matrix_utils functions
    double *A = matrix_utils::alloc(N);
    double *B = matrix_utils::alloc(N);
    double *C = matrix_utils::alloc(N);
    if (!A || !B || !C) { perror("alloc"); return 1; }

    matrix_utils::fill(A, N);
    matrix_utils::fill(B, N);
    memset(C, 0, sizeof(double)*size_t(N)*size_t(N));

    papito_init();
    papito_start();
    auto t0 = std::chrono::high_resolution_clock::now();

    run_benchmark(A, B, C, N, BS, kernel);

    auto t1 = std::chrono::high_resolution_clock::now();
    papito_end();

    std::chrono::duration<double> elapsed = t1 - t0;
    double s = 0.0;
    for (long i=0;i<(long)N*N;++i) s += C[i];
    
    printf("done sum=%g\n", s);
    printf("SUMMARY\tN=%d\tBS=%d\tmode=%s\tseed=%u\tseconds=%g\tchecksum=%g\n",
           N, BS, mode, seed, elapsed.count(), s);

    free(A); free(B); free(C);
    return 0;
}
