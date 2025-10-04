#!/usr/bin/env bash
set -euo pipefail

# scripts/run_and_measure.sh
# Runs and measures matrix multiplication kernels, including different tuning configurations
# and collects performance data using perf.

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
HYBRID_TUNINGS=("1_0" "1_8" "2_4")
INTERLEAVED_TUNINGS=("1_1" "1_4" "2_2")

# Check for perf command
if ! command -v perf &> /dev/null; then
    echo "Warning: 'perf' command not found. Skipping energy and frequency measurements."
    USE_PERF=0
else
    # Check for perf permissions (specifically for RAPL energy events)
    if perf stat -e "power/energy-pkg/" -a true &> /dev/null; then
        echo "[run] 'perf' is available and has permissions for energy events."
        USE_PERF=1
    else
        echo "Warning: 'perf' does not have permissions to access energy events (power/energy-pkg/)."
        echo "Try running with 'sudo' or adjusting /proc/sys/kernel/perf_event_paranoid."
        echo "Skipping energy and frequency measurements."
        USE_PERF=0
    fi
fi

echo "[run] Starting benchmark..."
mkdir -p "${RESULTS_DIR}"

# Set CPU governor
echo "[run] Setting CPU governor to 'performance' (may require sudo)..."
sudo sh -c 'for cpu in /sys/devices/system/cpu/cpu[0-9]*; do echo performance > $cpu/cpufreq/scaling_governor 2>/dev/null || true; done'

# Updated CSV header for new perf metrics
CSV="${RESULTS_DIR}/runs.csv"
echo "timestamp,mode,N,BS,run,elapsed_s,checksum,logfile,tuning,energy_J,avg_power_W,effective_freq_GHz" > "${CSV}"

# --- Main Experiment Loops ---
for N in "${N_VALUES[@]}"; do
  for BS in "${BLOCKS[@]}"; do
    if (( N % BS != 0 )); then
      echo "Skipping N=${N} BS=${BS} (not divisible)."
      continue
    fi

    for mode in "${MODES[@]}"; do
      CURRENT_TUNINGS=("NA")
      if [ "$mode" == "hybrid" ]; then
        CURRENT_TUNINGS=("${HYBRID_TUNINGS[@]}")
      elif [ "$mode" == "interleaved" ]; then
        CURRENT_TUNINGS=("${INTERLEAVED_TUNINGS[@]}")
      fi

      for tuning in "${CURRENT_TUNINGS[@]}"; do
        # Recompile if needed
        if [ "$tuning" != "NA" ]; then
          AVX_OPS=$(echo "$tuning" | cut -d'_' -f1)
          SCALAR_OPS=$(echo "$tuning" | cut -d'_' -f2)
          echo "--- Recompiling for ${mode} with tuning ${tuning} ---"
          (cd "${ROOT}" && make clean > /dev/null && make -j$(nproc) "HYBRID_AVX_UNROLL=${AVX_OPS}" "HYBRID_SCALAR_UNROLL=${SCALAR_OPS}" "INTERLEAVED_AVX_OPS=${AVX_OPS}" "INTERLEAVED_SCALAR_OPS=${SCALAR_OPS}")
        elif [ ! -x "${BIN}" ]; then
            (cd "${ROOT}" && make -j$(nproc))
        fi

        for run in $(seq 1 ${REPEATS}); do
          logfile="${RESULTS_DIR}/${mode}_${tuning}_N${N}_BS${BS}_run${run}.log"
          perf_logfile="${RESULTS_DIR}/${mode}_${tuning}_N${N}_BS${BS}_run${run}.perf"
          echo "=== RUN ${mode} (tuning: ${tuning}) N=${N} BS=${BS} run=${run} $(date +%FTT%z) ===" > "${logfile}"

          # Prepare the command to execute
          BENCH_CMD="taskset -c ${TASK_CPU} ${BIN} ${N} ${BS} ${mode} $((RANDOM & 0x7fffffff))"

          # Prepend perf stat if available and enabled
          if [ "$USE_PERF" -eq 1 ]; then
              PERF_EVENTS="power/energy-pkg/,cpu-cycles"
              EXEC_CMD="perf stat -e ${PERF_EVENTS} -o ${perf_logfile} -- ${BENCH_CMD}"
          else
              EXEC_CMD="${BENCH_CMD}"
          fi

          echo "[run] Running ${mode} (tuning: ${tuning}) N=${N} BS=${BS} run=${run}"
          # Execute the command and append its stdout/stderr to the main log file
          eval "${EXEC_CMD}" >> "${logfile}" 2>&1

          # --- PARSE RESULTS ---
          SUMMARY=$(grep "^SUMMARY" "${logfile}" | tail -n1 || echo "no-summary")
          elapsed=$(echo "${SUMMARY}" | sed -n 's/.*seconds=\([0-9.]*\).*/\1/p' || echo "NA")
          checksum=$(echo "${SUMMARY}" | sed -n 's/.*checksum=\([0-9.eE+-]*\).*/\1/p' || echo "NA")
          
          # Parse perf data and calculate metrics if available
          energy_J="NA"
          avg_power_W="NA"
          effective_freq_GHz="NA"
          if [ "$USE_PERF" -eq 1 ] && [ -f "${perf_logfile}" ]; then
              # Use awk to parse the perf output robustly
              perf_data=$(awk '
                  /power\/energy-pkg/ {energy=$1}
                  /cpu-cycles/ {cycles=$1}
                  END {printf "%.4f,%.0f", energy, cycles}
              ' "${perf_logfile}")
              
              energy_J=$(echo "$perf_data" | cut -d',' -f1)
              cycles=$(echo "$perf_data" | cut -d',' -f2)

              if [[ "$elapsed" != "NA" && "$elapsed" != "0" && "$energy_J" != "NA" ]]; then
                  avg_power_W=$(awk -v e="$energy_J" -v t="$elapsed" 'BEGIN{printf "%.2f", e/t}')
              fi
              if [[ "$elapsed" != "NA" && "$elapsed" != "0" && "$cycles" != "NA" ]]; then
                  effective_freq_GHz=$(awk -v c="$cycles" -v t="$elapsed" 'BEGIN{printf "%.2f", c/t/1e9}')
              fi
          fi
          
          # Write all results to CSV
          echo "$(date +%FTT%z),${mode},${N},${BS},${run},${elapsed},${checksum},${logfile},${tuning},${energy_J},${avg_power_W},${effective_freq_GHz}" >> "${CSV}"

          sleep 1
        done
      done
    done
  done
done

echo "[run] All experiments finished. Results are in ${RESULTS_DIR}"
