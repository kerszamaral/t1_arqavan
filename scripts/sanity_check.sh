#!/usr/bin/env bash
set -euo pipefail

# scripts/sanity_check.sh
# Verifies the correctness of all matrix multiplication kernels, including
# the different tuning configurations for hybrid and interleaved modes.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/bin/matmul_mixed"
RESULTS_DIR="${ROOT}/results"

# --- Configuration ---
N=128
BS=64
SEED=42
GOLDEN_MODE="blas_whole"
MODES_TO_TEST=(
    "scalar_whole"
    "scalar"
    "avx"
    "blas"
    "hybrid"
    "interleaved"
)

# --- Tunings for Hybrid and Interleaved Kernels ---
# (Copied from run_and_measure.sh to ensure consistency)
HYBRID_TUNINGS=(
    "1_0"    # Purely Vector: Baseline for the hybrid structure.
    "0_8"    # Purely Scalar: How does it compare to the dedicated scalar kernel?
    "1_8"    # Balanced Workload: 1 AVX op (8 columns) and 8 scalar ops (8 columns).
    "2_4"    # Vector Heavy: 2 AVX ops (16 cols) for every 4 scalar ops.
    "1_16"   # Scalar Heavy: Good for testing latency hiding.
)

# --- INTERLEAVED_TUNINGS: AVX and scalar ops mixed in the inner loop ---
# Goal: Test the CPU's out-of-order and superscalar capabilities at a fine-grained level.
INTERLEAVED_TUNINGS=(
    "1_1"    # Tightly Interleaved: A fine-grained, constant mix of work.
    "1_4"    # Scalar-Biased Mix: Can the CPU hide scalar latency behind vector throughput?
    "2_1"    # Vector-Biased Mix: Can a scalar op be hidden among vector ops?
    "2_8"    # Balanced and Unrolled: 2 AVX ops (16 cols) and 8 scalar ops (8 cols).
    "4_4"    # Highly Parallel: Gives the scheduler a large window of independent instructions.
)

# --- Main ---
echo "--- Sanity Check for Matrix Multiplication Kernels ---"

# Create a temporary directory for output files
TMP_DIR=$(mktemp -d)
trap 'rm -rf -- "$TMP_DIR"' EXIT

# 1. Generate the golden reference output using the default build
echo "[BUILD] Compiling default version for golden reference..."
(cd "${ROOT}" && make -j$(nproc) > /dev/null 2>&1)
echo "[BUILD] Done."

GOLDEN_FILE="${TMP_DIR}/golden.dat"
echo -n "[GOLDEN] Generating reference output with '${GOLDEN_MODE}'... "
"${BIN}" $N $BS "${GOLDEN_MODE}" $SEED --print-matrix > "${GOLDEN_FILE}" 2>/dev/null
echo "Done."
echo

# 2. Test all modes against the golden reference
FAILURES=0
for mode in "${MODES_TO_TEST[@]}"; do
    
    # --- Test Logic with support for Tunings ---
    test_kernel() {
        local mode_name=$1
        local tuning_name=${2:-"default"}
        local current_file="${TMP_DIR}/${mode_name}_${tuning_name}.dat"
        
        echo -n "[TEST] Running mode '${mode_name}' (tuning: ${tuning_name})... "
        
        # Run the benchmark and capture matrix output
        "${BIN}" $N $BS "${mode_name}" $SEED --print-matrix > "${current_file}" 2>/dev/null

        # Compare the output with the golden file
        if paste "${GOLDEN_FILE}" "${current_file}" | awk '
            BEGIN {
                eps = 1e-6; # Epsilon for floating point comparison
            }
            {
                # Compare the absolute difference against the epsilon
                if (($1 - $2) > eps || ($2 - $1) > eps) {
                    printf "Mismatch at element %d: golden=%.6f, current=%.6f\n", NR-1, $1, $2;
                    exit 1; # Mismatch found
                }
            }
        '; then
            echo -e "\033[0;32mPASS\033[0m"
        else
            echo -e "\033[0;31mFAIL\033[0m"
            FAILURES=$((FAILURES + 1))
        fi
    }
    # --- End of Test Logic ---

    if [ "$mode" == "hybrid" ]; then
        for tuning in "${HYBRID_TUNINGS[@]}"; do
            AVX_OPS=$(echo "$tuning" | cut -d'_' -f1)
            SCALAR_OPS=$(echo "$tuning" | cut -d'_' -f2)
            echo "[BUILD] Recompiling for hybrid tuning: ${tuning}..."
            (cd "${ROOT}" && make clean > /dev/null && make -j$(nproc) "HYBRID_AVX_UNROLL=${AVX_OPS}" "HYBRID_SCALAR_UNROLL=${SCALAR_OPS}" > /dev/null 2>&1)
            test_kernel "$mode" "$tuning"
        done
    elif [ "$mode" == "interleaved" ]; then
        for tuning in "${INTERLEAVED_TUNINGS[@]}"; do
            AVX_OPS=$(echo "$tuning" | cut -d'_' -f1)
            SCALAR_OPS=$(echo "$tuning" | cut -d'_' -f2)
            echo "[BUILD] Recompiling for interleaved tuning: ${tuning}..."
            (cd "${ROOT}" && make clean > /dev/null && make -j$(nproc) "INTERLEAVED_AVX_OPS=${AVX_OPS}" "INTERLEAVED_SCALAR_OPS=${SCALAR_OPS}" > /dev/null 2>&1)
            test_kernel "$mode" "$tuning"
        done
    else
        # For non-tunable modes, compile with default settings and test once
        (cd "${ROOT}" && make clean > /dev/null && make -j$(nproc) > /dev/null 2>&1)
        test_kernel "$mode"
    fi
done

# 3. Final summary
echo
if [ "$FAILURES" -eq 0 ]; then
    echo -e "\033[0;32m✅ All checks passed!\033[0m"
    exit 0
else
    echo -e "\033[0;31m❌ ${FAILURES} check(s) failed.\033[0m"
    exit 1
fi