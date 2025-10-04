#!/usr/bin/env bash
set -euo pipefail

# scripts/run_and_measure.sh
# Runs and measures matrix multiplication kernels, including different tuning configurations.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/bin/matmul_mixed"
RESULTS_DIR="${ROOT}/results"
TASK_CPU=0

# --- Configuration ---
REPEATS=3
MODES=(avx scalar hybrid interleaved)
N_VALUES=(2048 4096)
BLOCKS=(64 128)

# --- Define Tuning Configurations ---
# Format: "AVX_OPS_SCALAR_OPS". Example: "1_2" means 1 AVX op, 2 scalar ops.
HYBRID_TUNINGS=("1_2" "1_8" "2_4")
INTERLEAVED_TUNINGS=("1_1" "1_4" "2_2")

# ... rest of the script remains the same

echo "[run] Starting benchmark with tunings..."

# Prepare results directory
mkdir -p "${RESULTS_DIR}"

# Set CPU governor to performance
echo "[run] Setting CPU governor to 'performance' (may require sudo)..."
sudo sh -c 'for cpu in /sys/devices/system/cpu/cpu[0-9]*; do echo performance > $cpu/cpufreq/scaling_governor 2>/dev/null || true; done'

# Updated CSV header to include a 'tuning' column
CSV="${RESULTS_DIR}/runs.csv"
echo "timestamp,mode,N,BS,run,elapsed_s,checksum,logfile,tuning" > "${CSV}"

# --- Main Experiment Loops ---
for N in "${N_VALUES[@]}"; do
  for BS in "${BLOCKS[@]}"; do
    if (( N % BS != 0 )); then
      echo "Skipping N=${N} BS=${BS} (not divisible)."
      continue
    fi

    for mode in "${MODES[@]}"; do
      # Determine which tunings to use for the current mode
      CURRENT_TUNINGS=("NA") # Default for modes without specific tunings
      if [ "$mode" == "hybrid" ]; then
        CURRENT_TUNINGS=("${HYBRID_TUNINGS[@]}")
      elif [ "$mode" == "interleaved" ]; then
        CURRENT_TUNINGS=("${INTERLEAVED_TUNINGS[@]}")
      fi

      for tuning in "${CURRENT_TUNINGS[@]}"; do
        # Recompile with specific tuning settings if needed
        if [ "$tuning" != "NA" ]; then
          AVX_OPS=$(echo "$tuning" | cut -d'_' -f1)
          SCALAR_OPS=$(echo "$tuning" | cut -d'_' -f2)
          
          echo "--- Recompiling for ${mode} with tuning ${tuning} (AVX=${AVX_OPS}, SCALAR=${SCALAR_OPS}) ---"
          (cd "${ROOT}" && make clean > /dev/null) # Clean previous build
          if [ "$mode" == "hybrid" ]; then
            (cd "${ROOT}" && make -j$(nproc) HYBRID_AVX_UNROLL=${AVX_OPS} HYBRID_SCALAR_UNROLL=${SCALAR_OPS})
          elif [ "$mode" == "interleaved" ]; then
            (cd "${ROOT}" && make -j$(nproc) INTERLEAVED_AVX_OPS=${AVX_OPS} INTERLEAVED_SCALAR_OPS=${SCALAR_OPS})
          fi
          echo "--- Recompilation finished ---"
        elif [ ! -x "${BIN}" ]; then
            # Build once for standard modes if binary is missing
            (cd "${ROOT}" && make -j$(nproc))
        fi

        for run in $(seq 1 ${REPEATS}); do
          logfile="${RESULTS_DIR}/${mode}_${tuning}_N${N}_BS${BS}_run${run}.log"
          echo "=== RUN ${mode} (tuning: ${tuning}) N=${N} BS=${BS} run=${run} $(date +%FT%T%z) ===" > "${logfile}"

          echo "[run] Running ${mode} (tuning: ${tuning}) N=${N} BS=${BS} run=${run}"
          taskset -c ${TASK_CPU} "${BIN}" "${N}" "${BS}" "${mode}" "$((RANDOM & 0x7fffffff))" >> "${logfile}" 2>&1

          # Extract summary
          SUMMARY=$(grep "^SUMMARY" "${logfile}" | tail -n1 || echo "no-summary")
          elapsed=$(echo "${SUMMARY}" | sed -n 's/.*seconds=\([0-9.]*\).*/\1/p' || echo "NA")
          checksum=$(echo "${SUMMARY}" | sed -n 's/.*checksum=\([0-9.eE+-]*\).*/\1/p' || echo "NA")
          
          # Write results to CSV, including the tuning value
          echo "$(date +%FT%T%z),${mode},${N},${BS},${run},${elapsed},${checksum},${logfile},${tuning}" >> "${CSV}"

          sleep 1
        done
      done
    done
  done
done

echo "[run] All experiments finished. Results are in ${RESULTS_DIR}"
