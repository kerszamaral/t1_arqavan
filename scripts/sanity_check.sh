#!/usr/bin/env bash
set -euo pipefail

# scripts/sanity_check.sh
# Verifies the correctness of all matrix multiplication kernels with robust comparison.

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

# --- Main ---
echo "--- Sanity Check for Matrix Multiplication Kernels ---"

# Build the project first
echo "[BUILD] Compiling the project..."
(cd "${ROOT}" && make -j$(nproc) > /dev/null)
echo "[BUILD] Done."

# Create a temporary directory for output files
TMP_DIR=$(mktemp -d)
trap 'rm -rf -- "$TMP_DIR"' EXIT

GOLDEN_FILE="${TMP_DIR}/golden.dat"

# 1. Generate the golden reference output
echo -n "[GOLDEN] Generating reference output with '${GOLDEN_MODE}' mode... "
"${BIN}" $N $BS "${GOLDEN_MODE}" $SEED --print-matrix > "${GOLDEN_FILE}" 2>/dev/null
echo "Done."
echo

# 2. Test all other modes against the golden reference
FAILURES=0
for mode in "${MODES_TO_TEST[@]}"; do
    echo -n "[TEST] Running mode '${mode}'... "
    CURRENT_FILE="${TMP_DIR}/${mode}.dat"
    
    # Run the benchmark and capture matrix output
    "${BIN}" $N $BS "${mode}" $SEED --print-matrix > "${CURRENT_FILE}" 2>/dev/null

    # 3. *** BUG FIX ***
    # Use 'paste' to combine files into two columns, then use a simpler 'awk'
    # script to compare column 1 (golden) and column 2 (current).
    if paste "${GOLDEN_FILE}" "${CURRENT_FILE}" | awk '
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
done

# 4. Final summary
echo
if [ "$FAILURES" -eq 0 ]; then
    echo -e "\033[0;32m✅ All checks passed!\033[0m"
    exit 0
else
    echo -e "\033[0;31m❌ ${FAILURES} check(s) failed.\033[0m"
    exit 1
fi
