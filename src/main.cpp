#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <cassert>
#include <unistd.h>
#include "papito.h"

extern "C" void matmul_block_avx512(const double*, const double*, double*, int,int,int,int,int);
extern "C" void matmul_block_scalar(const double*, const double*, double*, int,int,int,int,int);

static unsigned int rng_state = 123456789u;
static inline unsigned int xorshift32() {
    unsigned int x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng_state = x;
}

static double* aligned_alloc_matrix(int N) {
    void* p = nullptr;
    if (posix_memalign(&p, 64, sizeof(double)*size_t(N)*size_t(N)) != 0) return nullptr;
    return (double*)p;
}

static void fill_rand(double *M, int N) {
    for (long i=0;i<(long)N*N;++i) M[i] = (double)( (i*33 + 7) % 100 ) + 1.0;
}

void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " N BS mode seed\n"
              << "  mode: avx | scalar | mixed | periodic\n"
              << "  periodic: alternates AVX/scalar every 'period_blocks' blocks in inner loop\n";
}

int main(int argc, char** argv) {
    if (argc < 5) {
        usage(argv[0]);
        return 1;
    }
    int N = atoi(argv[1]);
    int BS = atoi(argv[2]);
    const char* mode = argv[3];
    unsigned int seed = (unsigned int)atoi(argv[4]);
    rng_state = seed ? seed : (unsigned int)time(nullptr);

    if (N % 8 != 0) {
        std::cerr << "N must be multiple of 8 (for simplicity). N=" << N << std::endl;
        return 1;
    }
    if (BS <= 0 || N % BS != 0) {
        std::cerr << "BS must divide N exactly. N=" << N << " BS=" << BS << std::endl;
        return 1;
    }

    double *A = aligned_alloc_matrix(N);
    double *B = aligned_alloc_matrix(N);
    double *C = aligned_alloc_matrix(N);
    if (!A || !B || !C) { perror("posix_memalign"); return 1; }

    fill_rand(A,N);
    fill_rand(B,N);
    std::memset(C, 0, sizeof(double)*size_t(N)*size_t(N));

    papito_init();
    papito_start();

    auto t0 = std::chrono::high_resolution_clock::now();

    int period_blocks = 4; // used if mode == periodic
    for (int i0 = 0; i0 < N; i0 += BS) {
        for (int j0 = 0; j0 < N; j0 += BS) {
            for (int k0 = 0; k0 < N; k0 += BS) {
                bool use_avx = false;
                if (strcmp(mode,"avx") == 0) use_avx = true;
                else if (strcmp(mode,"scalar") == 0) use_avx = false;
                else if (strcmp(mode,"mixed") == 0) use_avx = (xorshift32() & 1);
                else if (strcmp(mode,"periodic") == 0) {
                    // periodic pattern: use avx for period_blocks blocks then scalar for period_blocks
                    int block_index = ( (i0/BS)*(N/BS)*(N/BS) + (j0/BS)*(N/BS) + (k0/BS) );
                    use_avx = ((block_index / period_blocks) % 2 == 0);
                } else {
                    std::cerr << "Unknown mode: " << mode << std::endl;
                    papito_end();
                    return 1;
                }
                if (use_avx) {
                    matmul_block_avx512(A,B,C,N,i0,j0,k0,BS);
                } else {
                    matmul_block_scalar(A,B,C,N,i0,j0,k0,BS);
                }
            }
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    papito_end();

    std::chrono::duration<double> elapsed = t1 - t0;
    // simple checksum to prevent optimizing away
    double s = 0.0;
    for (long i=0;i<(long)N*N;++i) s += C[i];

    std::cout << "SUMMARY\tN="<<N<<"\tBS="<<BS<<"\tmode="<<mode<<"\tseed="<<seed
              <<"\tseconds="<<elapsed.count()<<"\tchecksum="<<s<<std::endl;

    free(A); free(B); free(C);
    return 0;
}
