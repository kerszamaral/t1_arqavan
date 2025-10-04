#!/usr/bin/env bash
set -euo pipefail

# scripts/run_and_measure.sh
# Simplified script to run and measure matrix multiplication kernels.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/bin/matmul_mixed"
RESULTS_DIR="${ROOT}/results"
TASK_CPU=0

# --- Configuration ---
REPEATS=3
# Updated modes to include all kernels
MODES=(avx scalar hybrid interleaved)
N_VALUES=(2048 4096)
BLOCKS=(64 128)

echo "[run] Starting benchmark..."

# Build if the main binary is missing
if [ ! -x "${BIN}" ]; then
  echo "[run] Binary missing, building..."
  (cd "${ROOT}" && make -j$(nproc))
fi

# Prepare results directory
mkdir -p "${RESULTS_DIR}"

# Set CPU governor to performance for stable measurements
echo "[run] Setting CPU governor to 'performance' (may require sudo)..."
sudo sh -c 'for cpu in /sys/devices/system/cpu/cpu[0-9]*; do echo performance > $cpu/cpufreq/scaling_governor 2>/dev/null || true; done'

# Updated CSV header (removed num_heaters)
CSV="${RESULTS_DIR}/runs.csv"
echo "timestamp,mode,N,BS,run,elapsed_s,checksum,logfile" > "${CSV}"

# --- Main Experiment Loops ---
for N in "${N_VALUES[@]}"; do
  for BS in "${BLOCKS[@]}"; do
    if (( N % BS != 0 )); then
      echo "Skipping N=${N} BS=${BS} (not divisible)."
      continue
    fi

    for mode in "${MODES[@]}"; do
      for run in $(seq 1 ${REPEATS}); do
        logfile="${RESULTS_DIR}/${mode}_N${N}_BS${BS}_run${run}.log"
        echo "=== RUN ${mode} N=${N} BS=${BS} run=${run} $(date +%FT%T%z) ===" > "${logfile}"

        # Unset burst-related environment variables to ensure a clean run
        unset AVX_BURST
        unset SCALAR_BURST

        # Run the benchmark pinned to a specific CPU core
        echo "[run] Running ${mode} N=${N} BS=${BS} run=${run}"
        taskset -c ${TASK_CPU} "${BIN}" "${N}" "${BS}" "${mode}" "$((RANDOM & 0x7fffffff))" >> "${logfile}" 2>&1

        # Extract summary from the log file
        SUMMARY=$(grep "^SUMMARY" "${logfile}" | tail -n1 || echo "no-summary")
        elapsed=$(echo "${SUMMARY}" | sed -n 's/.*seconds=\([0-9.]*\).*/\1/p' || echo "NA")
        checksum=$(echo "${SUMMARY}" | sed -n 's/.*checksum=\([0-9.eE+-]*\).*/\1/p' || echo "NA")
        
        # Write results to CSV (removed num_heaters)
        echo "$(date +%FT%T%z),${mode},${N},${BS},${run},${elapsed},${checksum},${logfile}" >> "${CSV}"

        sleep 1 # Pause between runs
      done
    done
  done
done

echo "[run] All experiments finished. Results are in ${RESULTS_DIR}"
