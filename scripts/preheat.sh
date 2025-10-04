#!/usr/bin/env bash
set -euo pipefail

# scripts/preheat.sh
# Lança N heaters (por padrão, bin/heater_avx_busy) e mantém-nos ativos até Ctrl+C.
# Uso: ./scripts/preheat.sh N
# Exemplo: ./scripts/preheat.sh 4   # lança 4 heaters nos cores 1..4

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HEATER_BIN="${ROOT}/bin/heater_avx"
RESULTS_DIR="${ROOT}/results"

if [ $# -lt 1 ]; then
  echo "Uso: $0 N"
  exit 1
fi
NUM_HEATERS=$1

if [ ! -x "${HEATER_BIN}" ]; then
  echo "[preheat] ${HEATER_BIN} não encontrado. Compile primeiro (make)."
  exit 1
fi

mkdir -p "${RESULTS_DIR}"
HEATER_PIDS=()

cleanup() {
  echo "[preheat] Encerrando heaters..."
  for pid in "${HEATER_PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" || true
      sleep 0.1
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  HEATER_PIDS=()
  rm -f "${RESULTS_DIR}"/heater_preheat_*.pid 2>/dev/null || true
  echo "[preheat] Todos os heaters foram mortos."
}

trap cleanup EXIT

start_heaters() {
  local n=$1
  local start_core=1  # evita core 0, usado pelo benchmark
  for ((i=0;i<n;i++)); do
    core=$((start_core + i))
    echo "[preheat] Lançando heater no core ${core}"
    nohup "${HEATER_BIN}" "${core}" >/dev/null 2>&1 &
    pid=$!
    HEATER_PIDS+=("${pid}")
    echo "${pid}" > "${RESULTS_DIR}/heater_preheat_${core}.pid"
    sleep 0.05
  done
}

start_heaters "${NUM_HEATERS}"

echo "[preheat] ${NUM_HEATERS} heaters ativos. Pressione Ctrl+C para encerrar."
while true; do
  sleep 1
done

