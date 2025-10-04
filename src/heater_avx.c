// src/heater_avx.c
// Small AVX-512 "heater" that runs heavy FMAs in a tight loop pinned to a core.
// Usage: heater_avx <core-index>
// Note: requires an x86 CPU with AVX-512 (or change compile flags to -mavx2 for AVX2).
#include <immintrin.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

int main(int argc, char **argv) {
    int core = 1; // default core to pin heater
    if (argc > 1) core = atoi(argv[1]);
    if (core < 0) core = 1;

    // set affinity to requested core
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) != 0) {
        perror("sched_setaffinity");
        // continue even if affinity failed
    }

    // warmup vectors
    __m512d a = _mm512_set1_pd(1.23456789);
    __m512d b = _mm512_set1_pd(2.34567891);
    __m512d c = _mm512_set1_pd(0.0);

    // run forever; user is expected to kill the process when done
    while (1) {
        // inner unrolled FMA loop to keep the core busy and power high
        for (int i = 0; i < 20000; ++i) {
            // multiple fmadd to maintain throughput
            c = _mm512_fmadd_pd(a, b, c);
            c = _mm512_fmadd_pd(b, a, c);
            c = _mm512_fmadd_pd(a, c, b);
            c = _mm512_fmadd_pd(b, c, a);
        }
        // Prevent the compiler from optimizing the loop away
        volatile double sink = ((double*)&c)[0];
        (void)sink;
        // tiny sleep to yield occasionally (keeps heater effective but responsive)
        //usleep(1000); // 1 ms
    }
    return 0;
}

