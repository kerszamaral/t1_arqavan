#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <string>
#include <unistd.h>

#include "papito.h"

// kernels (implemented in separate files)
extern "C" void matmul_block_avx512(const double *packA, const double *packB, double *C,
                                    int N, int i0, int j0, int k0, int bs);
extern "C" void matmul_block_scalar(const double *packA, const double *packB, double *C,
                                    int N, int i0, int j0, int k0, int bs);

static uint32_t rng_state = 123456789u;
static inline uint32_t xorshift32() {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return (rng_state = x);
}

static double* aligned_alloc_matrix(int N) {
    void* p = nullptr;
    if (posix_memalign(&p, 64, sizeof(double)*size_t(N)*size_t(N)) != 0) return nullptr;
    return (double*)p;
}

static void fill_rand(double *M, int N) {
    for (long i=0;i<(long)N*N;++i) M[i] = (double)( (i*33 + 7) % 100 ) + 1.0;
}

// pack A block: packA[ii*bs + kk] = A[(i0+ii)*N + (k0+kk)]
static void pack_A_block(const double *A, double *packA, int N, int i0, int k0, int bs) {
    for (int ii = 0; ii < bs; ++ii) {
        const double *arow = &A[(i0 + ii) * N + k0];
        double *prow = &packA[ii * bs];
        for (int kk = 0; kk < bs; ++kk) prow[kk] = arow[kk];
    }
}

// pack B block: packB[kk*bs + jj] = B[(k0+kk)*N + (j0+jj)]
static void pack_B_block(const double *B, double *packB, int N, int k0, int j0, int bs) {
    for (int kk = 0; kk < bs; ++kk) {
        const double *brow = &B[(k0 + kk) * N + j0];
        double *prow = &packB[kk * bs];
        for (int jj = 0; jj < bs; ++jj) prow[jj] = brow[jj];
    }
}

static void usage(const char *prg) {
    fprintf(stderr, "Usage: %s N BS mode seed\n modes: avx | scalar | mixed | mixed_burst | periodic\n", prg);
}

int main(int argc, char **argv) {
    if (argc < 5) { usage(argv[0]); return 1; }
    int N = atoi(argv[1]);
    int BS = atoi(argv[2]);
    const char *mode = argv[3];
    unsigned int seed = (unsigned int)atoi(argv[4]);
    rng_state = seed ? seed : (unsigned int)time(NULL);

    if (N % 8 != 0) {
        fprintf(stderr, "Require N multiple of 8 for simplicity. N=%d\n", N);
        return 1;
    }
    if (BS <= 0 || N % BS != 0) {
        fprintf(stderr, "BS must divide N exactly. N=%d BS=%d\n", N, BS);
        return 1;
    }

    double *A = aligned_alloc_matrix(N);
    double *B = aligned_alloc_matrix(N);
    double *C = aligned_alloc_matrix(N);
    if (!A || !B || !C) { perror("alloc"); return 1; }
    fill_rand(A, N);
    fill_rand(B, N);
    memset(C, 0, sizeof(double)*size_t(N)*size_t(N));

    // pack buffers (one per block, reused)
    double *packA = nullptr;
    double *packB = nullptr;
    if (posix_memalign((void**)&packA, 64, sizeof(double)*size_t(BS)*size_t(BS)) != 0) { perror("packA alloc"); return 1; }
    if (posix_memalign((void**)&packB, 64, sizeof(double)*size_t(BS)*size_t(BS)) != 0) { perror("packB alloc"); return 1; }

    // burst parameters for mixed_burst (env vars)
    int AVX_BURST = 4;
    int SCALAR_BURST = 2;
    {
        const char *ev;
        if ((ev = getenv("AVX_BURST")) != nullptr) AVX_BURST = atoi(ev);
        if ((ev = getenv("SCALAR_BURST")) != nullptr) SCALAR_BURST = atoi(ev);
        if (AVX_BURST <= 0) AVX_BURST = 4;
        if (SCALAR_BURST <= 0) SCALAR_BURST = 2;
    }

    papito_init();
    papito_start();
    auto t0 = std::chrono::high_resolution_clock::now();

    bool in_avx = true;
    int avx_rem = AVX_BURST;
    int scalar_rem = SCALAR_BURST;

    for (int i0 = 0; i0 < N; i0 += BS) {
        for (int k0 = 0; k0 < N; k0 += BS) {
            // pack A for this (i0,k0), reuse across all j0
            pack_A_block(A, packA, N, i0, k0, BS);

            for (int j0 = 0; j0 < N; j0 += BS) {
                // pack B for this (k0,j0)
                pack_B_block(B, packB, N, k0, j0, BS);

                // decide kernel to use for this block
                int use_avx = 0;
                if (strcmp(mode, "avx") == 0) use_avx = 1;
                else if (strcmp(mode, "scalar") == 0) use_avx = 0;
                else if (strcmp(mode, "mixed") == 0) use_avx = (xorshift32() & 1);
                else if (strcmp(mode, "mixed_burst") == 0) {
                    if (in_avx) {
                        use_avx = 1;
                        avx_rem--;
                        if (avx_rem <= 0) { in_avx = false; scalar_rem = SCALAR_BURST; }
                    } else {
                        use_avx = 0;
                        scalar_rem--;
                        if (scalar_rem <= 0) { in_avx = true; avx_rem = AVX_BURST; }
                    }
                } else if (strcmp(mode, "periodic") == 0) {
                    int block_index = (i0/BS)*(N/BS)*(N/BS) + (k0/BS)*(N/BS) + (j0/BS);
                    use_avx = ((block_index / 4) % 2 == 0);
                } else {
                    // default to mixed
                    use_avx = (xorshift32() & 1);
                }

                if (use_avx) {
                    matmul_block_avx512(packA, packB, C, N, i0, j0, k0, BS);
                } else {
                    matmul_block_scalar(packA, packB, C, N, i0, j0, k0, BS);
                }
            } // j0
        } // k0
    } // i0

    auto t1 = std::chrono::high_resolution_clock::now();
    papito_end();

    std::chrono::duration<double> elapsed = t1 - t0;
    double s = 0.0;
    for (long i=0;i<(long)N*N;++i) s += C[i];
    printf("done sum=%g\n", s);
    printf("SUMMARY\tN=%d\tBS=%d\tmode=%s\tseed=%u\tseconds=%g\tchecksum=%g\n",
           N, BS, mode, seed, elapsed.count(), s);

    free(A); free(B); free(C); free(packA); free(packB);
    return 0;
}

