#!/usr/bin/env bash
set -euo pipefail

# project-root/scripts/run_and_measure.sh
# Executa o binário bin/matmul_mixed em vários modos e BS, coletando a saída do papito (PAPI wrapper)
# Uso: sudo ./run_and_measure.sh    # sudo recomendado para ajustar governor

# Config
ROOT_DIR="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"   # project-root
BIN="${ROOT_DIR}/bin/matmul_mixed"
MAKE_CMD="make -C ${ROOT_DIR}"
RESULTS_DIR="${ROOT_DIR}/results"
SCRIPT_NAME="$(basename "$0")"
TASK_CPU=0
REPEATS=3
MODES=(avx scalar mixed periodic)
BLOCKS=(64 128 256)
N_VALUES=(1024 2048)   # ajustar conforme memória/tempo
SLEEP_BETWEEN=1

# Ensure results dir exists
mkdir -p "${RESULTS_DIR}"

# Build if missing
if [ ! -x "${BIN}" ]; then
  echo "[run] binary ${BIN} not found — building..."
  ${MAKE_CMD}
fi

# Optionally set governor to performance for reproducibility (requires root)
if [ "$(id -u)" -eq 0 ]; then
  echo "[run] setting scaling governor to 'performance' for all CPUs"
  for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
    echo performance > "${cpu}/cpufreq/scaling_governor" 2>/dev/null || true
  done
else
  echo "[run] not running as root: governor not changed (results may vary)"
fi

# CSV header
CSV="${RESULTS_DIR}/runs.csv"
echo "timestamp,mode,N,BS,run,elapsed_s,checksum,logfile" > "${CSV}"

run_once() {
  local mode=$1; local N=$2; local BS=$3; local runid=$4
  local log="${RESULTS_DIR}/${mode}_N${N}_BS${BS}_run${runid}.log"
  echo "[run] mode=${mode} N=${N} BS=${BS} run=${runid} -> ${log}"
  echo "=== RUN ${mode} N=${N} BS=${BS} run=${runid} $(date +%FT%T%z) ===" > "${log}"

  # Snapshot frequency before run (if available)
  if [ -r "/sys/devices/system/cpu/cpu${TASK_CPU}/cpufreq/scaling_cur_freq" ]; then
    echo "scaling_cur_freq_before:" >> "${log}"
    cat "/sys/devices/system/cpu/cpu${TASK_CPU}/cpufreq/scaling_cur_freq" >> "${log}" 2>/dev/null || true
  fi

  # Run pinned to TASK_CPU, papito is expected to print counters during program execution
  # NOTE: the program takes args: N BS mode seed
  # We redirect both stdout+stderr to the log and also show progress in console.
  taskset -c ${TASK_CPU} "${BIN}" "${N}" "${BS}" "${mode}" $((RANDOM & 0x7fffffff)) >> "${log}" 2>&1

  # Snapshot frequency after run (if available)
  if [ -r "/sys/devices/system/cpu/cpu${TASK_CPU}/cpufreq/scaling_cur_freq" ]; then
    echo "scaling_cur_freq_after:" >> "${log}"
    cat "/sys/devices/system/cpu/cpu${TASK_CPU}/cpufreq/scaling_cur_freq" >> "${log}" 2>/dev/null || true
  fi

  # Extract SUMMARY line from log (the program prints SUMMARY at the end)
  local summary
  summary=$(grep "^SUMMARY" "${log}" | tail -n1 || true)
  if [ -z "${summary}" ]; then
    elapsed="NA"; checksum="NA"
  else
    # parse: SUMMARY\tN=... \tBS=... \tmode=... \tseed=...\tseconds=... \tchecksum=...
    elapsed=$(echo "${summary}" | sed -n 's/.*seconds=\([0-9.]*\).*/\1/p' || echo "NA")
    checksum=$(echo "${summary}" | sed -n 's/.*checksum=\([0-9.eE+-]*\).*/\1/p' || echo "NA")
  fi

  # Append to csv
  echo "$(date +%FT%T%z),${mode},${N},${BS},${runid},${elapsed},${checksum},${log}" >> "${CSV}"
}

# Loop experiments
for N in "${N_VALUES[@]}"; do
  for BS in "${BLOCKS[@]}"; do
    if (( N % BS != 0 )); then
      echo "[warn] skipping BS=${BS} since it doesn't divide N=${N}"
      continue
    fi
    for mode in "${MODES[@]}"; do
      for runid in $(seq 1 ${REPEATS}); do
        run_once "${mode}" "${N}" "${BS}" "${runid}"
        sleep "${SLEEP_BETWEEN}"
      done
    done
  done
done

echo "[run] all experiments finished. Results folder: ${RESULTS_DIR}"

