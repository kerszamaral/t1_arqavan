#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iostream>
#include <string>
#include <vector> // Required for argument parsing

#include "papito.h"
#include "matrix_utils.h"
#include "dispatch_kernels.h"
#include "dispatch_kernels_whole.h"
#include "runner.h"
#include "runner_whole.h"

// Function to print the matrix to stdout
void print_matrix(const double* M, int N) {
    for (int i = 0; i < N * N; ++i) {
        printf("%.10f\n", M[i]);
    }
}

static void usage(const char *prg) {
    fprintf(stderr, "Usage: %s N BS mode seed [--print-matrix]\n", prg);
    fprintf(stderr, "Block modes: avx, scalar, hybrid, interleaved, blas\n");
    fprintf(stderr, "Whole modes: scalar_whole, blas_whole\n");
}

int main(int argc, char **argv) {
    if (argc < 5) { usage(argv[0]); return 1; }
    
    // Simple argument parsing for the optional flag
    std::vector<std::string> args(argv, argv + argc);
    bool print_output_matrix = false;
    if (args.size() > 5 && args[5] == "--print-matrix") {
        print_output_matrix = true;
    }

    int N = std::stoi(args[1]);
    int BS = std::stoi(args[2]);
    std::string mode = args[3];
    unsigned int seed = std::stoul(args[4]);

    if (N % 8 != 0) {
        fprintf(stderr, "Error: N must be a multiple of 8.\n");
        return 1;
    }

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

    // --- Dispatch Logic ---
    matmul_whole_func_t whole_kernel = get_kernel_for_mode_whole(mode);
    if (whole_kernel) {
        run_benchmark_whole_matrix(A, B, C, N, whole_kernel);
    } else {
        matmul_func_t block_kernel = get_kernel_for_mode(mode);
        if (block_kernel) {
            if (BS <= 0 || N % BS != 0) {
                fprintf(stderr, "Error: For block modes, BS must be a positive divisor of N.\n");
                return 1;
            }
            run_benchmark(A, B, C, N, BS, block_kernel);
        } else {
            fprintf(stderr, "Error: Unknown mode '%s'.\n", mode.c_str());
            usage(argv[0]);
            return 1;
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    papito_end();
    
    // All logging and summary info goes to stderr
    std::chrono::duration<double> elapsed = t1 - t0;
    double s = 0.0;
    for (long i=0;i<(long)N*N;++i) s += C[i];
    
    fprintf(stderr, "done sum=%g\n", s);
    fprintf(stderr, "SUMMARY\tN=%d\tBS=%d\tmode=%s\tseed=%u\tseconds=%g\tchecksum=%g\n",
           N, BS, mode.c_str(), seed, elapsed.count(), s);

    // If requested, print the final matrix to stdout
    if (print_output_matrix) {
        print_matrix(C, N);
    }

    free(A); free(B); free(C);
    return 0;
}
