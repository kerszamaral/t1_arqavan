#!/usr/bin/env bash
set -euo pipefail

# scripts/run_and_measure.sh
# Optimized to run _whole modes only once per matrix size.
export OPENBLAS_NUM_THREADS=1

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/bin/matmul_mixed"
RESULTS_DIR="${ROOT}/results"
TASK_CPU=0

# --- Separate modes into two categories ---
WHOLE_MODES=(scalar_whole blas_whole)
BLOCK_MODES=(avx scalar hybrid interleaved blas)

# --- Configuration ---
REPEATS=15 # Keep repeats low for a broad test, can be increased later

# --- Matrix Sizes (N_VALUES) ---
# A wide range from small (L2 cache) to very large (memory-bound).
# Includes powers of two and a non-power-of-two size.
N_VALUES=(512 1024 2048 4096)

# --- Block Sizes (BLOCKS) ---
# A variety of block sizes to test cache blocking effectiveness.
# Small sizes test loop overhead, large sizes test cache capacity.
BLOCKS=(64)

# --- Define Tuning Configurations ---

# --- HYBRID_TUNINGS: Sequential AVX block, then sequential scalar block ---
# Goal: Test the impact of switching between sustained periods of vector and scalar work.
HYBRID_TUNINGS=(
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

# Check for perf command
USE_PERF=0
if command -v perf &> /dev/null; then
    if perf stat -e "power/energy-pkg/" -a true &> /dev/null; then
        echo "[run] 'perf' is available and has permissions for energy events."
        USE_PERF=1
    else
        echo "Warning: 'perf' does not have permissions for energy events. Skipping advanced metrics."
    fi
fi

echo "[run] Starting benchmark..."
mkdir -p "${RESULTS_DIR}"

# Set CPU governor
echo "[run] Setting CPU governor to 'performance' (may require sudo)..."
sudo sh -c 'for cpu in /sys/devices/system/cpu/cpu[0-9]*; do echo performance > $cpu/cpufreq/scaling_governor 2>/dev/null || true; done'

CSV="${RESULTS_DIR}/runs.csv"
echo "timestamp,mode,N,BS,run,elapsed_s,checksum,logfile,tuning,energy_J,avg_power_W,effective_freq_GHz" > "${CSV}"

# --- Part 1: Run Whole-Matrix Modes ---
echo
echo "--- Running Whole-Matrix Modes (once per matrix size) ---"
(cd "${ROOT}" && make -j$(nproc)) # Ensure project is built once for these modes
for N in "${N_VALUES[@]}"; do
    for mode in "${WHOLE_MODES[@]}"; do
        # BS is not applicable, use 0 as a placeholder. Run is always 1.
        BS=0
        run=1
        tuning="NA"

        logfile="${RESULTS_DIR}/${mode}_N${N}.log"
        perf_logfile="${RESULTS_DIR}/${mode}_N${N}.perf"
        echo "=== RUN ${mode} N=${N} ===" > "${logfile}"
        
        BENCH_CMD="taskset -c ${TASK_CPU} ${BIN} ${N} ${BS} ${mode} $((RANDOM & 0x7fffffff))"
        EXEC_CMD="${BENCH_CMD}"
        if [ "$USE_PERF" -eq 1 ]; then
            PERF_EVENTS="power/energy-pkg/"
            EXEC_CMD="perf stat -e ${PERF_EVENTS} -o ${perf_logfile} -- ${BENCH_CMD}"
        fi

        echo "[run] Running ${mode} N=${N}"
        eval "${EXEC_CMD}" >> "${logfile}" 2>&1

        SUMMARY=$(grep "^SUMMARY" "${logfile}" | tail -n1 || echo "no-summary")
        elapsed=$(echo "${SUMMARY}" | sed -n 's/.*seconds=\([0-9.]*\).*/\1/p' || echo "NA")
        checksum=$(echo "${SUMMARY}" | sed -n 's/.*checksum=\([0-g.eE+-]*\).*/\1/p' || echo "NA")
        
        energy_J="NA"; avg_power_W="NA"; effective_freq_GHz="NA"
        if [ "$USE_PERF" -eq 1 ] && [ -f "${perf_logfile}" ]; then
            perf_data=$(awk '/power\/energy-pkg/ {e=$1} /cpu-cycles/ {c=$1} END {printf "%.4f,%.0f", e, c}' "${perf_logfile}")
            energy_J=$(echo "$perf_data" | cut -d',' -f1); cycles=$(echo "$perf_data" | cut -d',' -f2)
            if [[ "$elapsed" != "NA" && "$elapsed" != "0" ]]; then
                avg_power_W=$(awk -v e="$energy_J" -v t="$elapsed" 'BEGIN{printf "%.2f", e/t}')
                effective_freq_GHz=$(awk -v c="$cycles" -v t="$elapsed" 'BEGIN{printf "%.2f", c/t/1e9}')
            fi
        fi
        
        echo "$(date +%FTT%z),${mode},${N},${BS},${run},${elapsed},${checksum},${logfile},${tuning},${energy_J},${avg_power_W},${effective_freq_GHz}" >> "${CSV}"
        sleep 1
    done
done

# --- Part 2: Run Block-Based Modes ---
echo
echo "--- Running Block-Based Modes (with tunings, blocks, and repeats) ---"
for N in "${N_VALUES[@]}"; do
  for BS in "${BLOCKS[@]}"; do
    if (( N % BS != 0 )); then continue; fi

    for mode in "${BLOCK_MODES[@]}"; do
      CURRENT_TUNINGS=("NA")
      if [ "$mode" == "hybrid" ]; then CURRENT_TUNINGS=("${HYBRID_TUNINGS[@]}"); fi
      if [ "$mode" == "interleaved" ]; then CURRENT_TUNINGS=("${INTERLEAVED_TUNINGS[@]}"); fi

      for tuning in "${CURRENT_TUNINGS[@]}"; do
        if [ "$tuning" != "NA" ]; then
          AVX_OPS=$(echo "$tuning" | cut -d'_' -f1); SCALAR_OPS=$(echo "$tuning" | cut -d'_' -f2)
          echo "--- Recompiling for ${mode} with tuning ${tuning} ---"
          (cd "${ROOT}" && make clean > /dev/null && make -j$(nproc) "HYBRID_AVX_UNROLL=${AVX_OPS}" "HYBRID_SCALAR_UNROLL=${SCALAR_OPS}" "INTERLEAVED_AVX_OPS=${AVX_OPS}" "INTERLEAVED_SCALAR_OPS=${SCALAR_OPS}")
        fi

        for run in $(seq 1 ${REPEATS}); do
          logfile="${RESULTS_DIR}/${mode}_${tuning}_N${N}_BS${BS}_run${run}.log"
          perf_logfile="${RESULTS_DIR}/${mode}_${tuning}_N${N}_BS${BS}_run${run}.perf"
          echo "=== RUN ${mode} (tuning: ${tuning}) N=${N} BS=${BS} run=${run} ===" > "${logfile}"

          BENCH_CMD="taskset -c ${TASK_CPU} ${BIN} ${N} ${BS} ${mode} $((RANDOM & 0x7fffffff))"
          EXEC_CMD="${BENCH_CMD}"
          if [ "$USE_PERF" -eq 1 ]; then
              PERF_EVENTS="power/energy-pkg/"
              EXEC_CMD="perf stat -e ${PERF_EVENTS} -o ${perf_logfile} -- ${BENCH_CMD}"
          fi

          echo "[run] Running ${mode} (tuning: ${tuning}) N=${N} BS=${BS} run=${run}"
          eval "${EXEC_CMD}" >> "${logfile}" 2>&1

          SUMMARY=$(grep "^SUMMARY" "${logfile}" | tail -n1 || echo "no-summary")
          elapsed=$(echo "${SUMMARY}" | sed -n 's/.*seconds=\([0-9.]*\).*/\1/p' || echo "NA")
          checksum=$(echo "${SUMMARY}" | sed -n 's/.*checksum=\([0-9.eE+-]*\).*/\1/p' || echo "NA")
          
          energy_J="NA"; avg_power_W="NA"; effective_freq_GHz="NA"
          if [ "$USE_PERF" -eq 1 ] && [ -f "${perf_logfile}" ]; then
              perf_data=$(awk '/power\/energy-pkg/ {e=$1} /cpu-cycles/ {c=$1} END {printf "%.4f,%.0f", e, c}' "${perf_logfile}")
              energy_J=$(echo "$perf_data" | cut -d',' -f1); cycles=$(echo "$perf_data" | cut -d',' -f2)
              if [[ "$elapsed" != "NA" && "$elapsed" != "0" ]]; then
                  avg_power_W=$(awk -v e="$energy_J" -v t="$elapsed" 'BEGIN{printf "%.2f", e/t}')
                  effective_freq_GHz=$(awk -v c="$cycles" -v t="$elapsed" 'BEGIN{printf "%.2f", c/t/1e9}')
              fi
          fi
          
          echo "$(date +%FTT%z),${mode},${N},${BS},${run},${elapsed},${checksum},${logfile},${tuning},${energy_J},${avg_power_W},${effective_freq_GHz}" >> "${CSV}"
          sleep 1
        done
      done
    done
  done
done

echo "[run] All experiments finished. Results are in ${RESULTS_DIR}"
