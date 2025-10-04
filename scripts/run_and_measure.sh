#!/usr/bin/env bash
set -euo pipefail

# scripts/run_and_measure.sh
# Usage: ./scripts/run_and_measure.sh [num_heaters]
# or:    ./scripts/run_and_measure.sh --heaters N
#
# If num_heaters > 0, the script will launch N heater_avx processes on subsequent cores
# (cores 1..N, skipping TASK_CPU which is used for the main workload).
# The heaters are launched as the original user if the script is invoked with sudo.
#
# NOTE: heater_avx must be compiled (bin/heater_avx). Make sure Makefile builds it.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/bin/matmul_mixed"
HEATER_BIN="${ROOT}/bin/heater_avx"
RESULTS_DIR="${ROOT}/results"
BUILD_DIR="${ROOT}/build"
TASK_CPU=0

# defaults
NUM_HEATERS=0
REPEATS=3
MODES=(avx scalar mixed mixed_burst)
N_VALUES=(2048 4096)    # adjust as desired
BLOCKS=(64 128)         # adjust block sizes as desired

# parse optional arg
if [[ "${1-}" == "--heaters" ]]; then
  if [[ -n "${2-}" ]]; then
    NUM_HEATERS="$2"
    shift 2
  else
    echo "Usage: $0 [--heaters N]"; exit 1
  fi
elif [[ "${1-}" =~ ^[0-9]+$ ]]; then
  NUM_HEATERS="$1"
  shift 1
fi

# ensure integer
NUM_HEATERS=$((NUM_HEATERS + 0)) || NUM_HEATERS=0
if (( NUM_HEATERS < 0 )); then NUM_HEATERS=0; fi

echo "[run] NUM_HEATERS=${NUM_HEATERS}"

# build if missing
if [ ! -x "${BIN}" ]; then
  echo "[run] binary missing, building..."
  (cd "${ROOT}" && make -j$(nproc))
fi
# build heater if needed
if (( NUM_HEATERS > 0 )); then
  if [ ! -x "${HEATER_BIN}" ]; then
    echo "[run] heater binary missing, building..."
    (cd "${ROOT}" && make -j$(nproc))
  fi
fi

# prepare results dir; if running under sudo, ensure ownership set to original user
mkdir -p "${RESULTS_DIR}"
if [ -n "${SUDO_USER-}" ]; then
  # ensure owned by original user to avoid root-owned log files
  chown "${SUDO_USER}:${SUDO_USER}" "${RESULTS_DIR}" || true
fi

# Set performance governor (via sudo when necessary)
if [ "$(id -u)" -eq 0 ]; then
  echo "[run] running as root: setting governor = performance"
  for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
    echo performance > "${cpu}/cpufreq/scaling_governor" 2>/dev/null || true
  done
else
  echo "[run] will try to set governor to performance (may ask for password)..."
  sudo sh -c 'for cpu in /sys/devices/system/cpu/cpu[0-9]*; do echo performance > $cpu/cpufreq/scaling_governor 2>/dev/null || true; done'
fi

CSV="${RESULTS_DIR}/runs.csv"
echo "timestamp,mode,N,BS,run,elapsed_s,checksum,logfile,num_heaters" > "${CSV}"

# helper to start heaters and return their PIDs in array HEATER_PIDS
HEATER_PIDS=()
start_heaters() {
  local n=$1
  local start_core=2
  # prefer cores 1.. to avoid interfering with TASK_CPU=0
  for ((i=0;i<n;i++)); do
    core=$((start_core + i))
    echo "[run] starting heater on core ${core}"
    if [ -n "${SUDO_USER-}" ]; then
      # run heater as the original user so files created are owned by them
      sudo -u "${SUDO_USER}" bash -c "nohup ${HEATER_BIN} ${core} >/dev/null 2>&1 & echo \$!" > "${RESULTS_DIR}/heater_${core}.pid"
      pid=$(cat "${RESULTS_DIR}/heater_${core}.pid")
    else
      # run heater as current user
      nohup "${HEATER_BIN}" "${core}" >/dev/null 2>&1 & echo $! > "${RESULTS_DIR}/heater_${core}.pid"
      pid=$(cat "${RESULTS_DIR}/heater_${core}.pid")
    fi
    HEATER_PIDS+=("${pid}")
    # small pause so heaters spin up
    sleep 0.05
  done
}

stop_heaters() {
  for pid in "${HEATER_PIDS[@]}"; do
    if kill -0 "${pid}" 2>/dev/null; then
      echo "[run] killing heater pid ${pid}"
      kill "${pid}" || true
      sleep 0.02
      kill -9 "${pid}" 2>/dev/null || true
    fi
  done
  HEATER_PIDS=()
  # cleanup pid files
  rm -f "${RESULTS_DIR}"/heater_*.pid 2>/dev/null || true
}

# ensure heaters are stopped on exit
trap 'echo "[run] cleaning up..."; stop_heaters' EXIT

# main experiment loops
for N in "${N_VALUES[@]}"; do
  for BS in "${BLOCKS[@]}"; do
    if (( N % BS != 0 )); then
      echo "skipping N=${N} BS=${BS} (not divisible)"; continue
    fi

    for mode in "${MODES[@]}"; do
      for run in $(seq 1 ${REPEATS}); do
        logfile="${RESULTS_DIR}/${mode}_N${N}_BS${BS}_run${run}.log"
        echo "=== RUN ${mode} N=${N} BS=${BS} run=${run} $(date +%FT%T%z) heaters=${NUM_HEATERS} ===" > "${logfile}"

        # Start heater(s) only for mixed modes
        HEATER_PIDS=()
        if [[ "${mode}" == mixed* ]] && (( NUM_HEATERS > 0 )); then
          start_heaters "${NUM_HEATERS}"
          # small sleep to let heaters warm package
          sleep 0.5
        fi

        # set AVX burst env for mixed_burst runs
        if [ "${mode}" = "mixed_burst" ]; then
          export AVX_BURST=6
          export SCALAR_BURST=1
        else
          unset AVX_BURST
          unset SCALAR_BURST
        fi

        # run pinned to TASK_CPU
        echo "[run] running ${mode} N=${N} BS=${BS} run=${run} (heaters=${NUM_HEATERS})"
        taskset -c ${TASK_CPU} "${BIN}" "${N}" "${BS}" "${mode}" "$((RANDOM & 0x7fffffff))" >> "${logfile}" 2>&1

        # stop heaters after the run (if any)
        if (( ${#HEATER_PIDS[@]} > 0 )); then
          stop_heaters
        fi

        # extract summary
        SUMMARY=$(grep "^SUMMARY" "${logfile}" | tail -n1 || echo "no-summary")
        elapsed=$(echo "${SUMMARY}" | sed -n 's/.*seconds=\([0-9.]*\).*/\1/p' || echo "NA")
        checksum=$(echo "${SUMMARY}" | sed -n 's/.*checksum=\([0-9.eE+-]*\).*/\1/p' || echo "NA")
        echo "$(date +%FT%T%z),${mode},${N},${BS},${run},${elapsed},${checksum},${logfile},${NUM_HEATERS}" >> "${CSV}"

        sleep 1
      done
    done
  done
done

echo "[run] all experiments finished. Results in ${RESULTS_DIR}"

