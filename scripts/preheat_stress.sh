#!/usr/bin/env bash
set -euo pipefail

# scripts/preheat_stress.sh
# Lança stress-ng ocupando todas as logical CPUs exceto as reservadas (por default 0 e 1).
# Fica rodando até Ctrl+C; ao receber SIGINT ou EXIT mata o stress-ng.
#
# Uso:
#   ./scripts/preheat_stress.sh            # usa RESERVED="0,1"
#   ./scripts/preheat_stress.sh 0,1,2     # usa lista personalizada de reserved cpus
#   ./scripts/preheat_stress.sh ""        # não reserva nenhum (usa todas)
#
# Requer: stress-ng instalado (sudo apt install -y stress-ng)
#
# Observação: este script cria carga alta na(s) CPU(s) permitida(s). Use com cuidado.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Default reserved logical CPUs (comma-separated). These will NOT be stressed.
# Default keeps cpu 0 and 1 free so you can run your program on them.
RESERVED_DEFAULT="0,1"

# parse optional arg
if [ "${1-}" = "" ]; then
  RESERVED="${RESERVED_DEFAULT}"
else
  RESERVED="$1"
fi

# Trim spaces
RESERVED="$(echo "${RESERVED}" | sed 's/[[:space:]]//g')"

# find all logical CPUs available, sorted
ALL_CPUS=()
for c in /sys/devices/system/cpu/cpu[0-9]*; do
  cpu=$(basename "$c")
  # ensure it is a cpu followed by number
  if [[ "$cpu" =~ ^cpu([0-9]+)$ ]]; then
    num=${BASH_REMATCH[1]}
    ALL_CPUS+=("$num")
  fi
done

if [ ${#ALL_CPUS[@]} -eq 0 ]; then
  echo "Não foi possível enumerar CPUs em /sys/devices/system/cpu. Abortando."
  exit 1
fi

# build set of reserved numbers (if empty, no reserved)
IFS=',' read -r -a RESERVED_ARR <<< "${RESERVED}"
declare -A RESERVED_MAP
for r in "${RESERVED_ARR[@]}"; do
  if [ -z "${r}" ]; then
    continue
  fi
  if [[ ! "$r" =~ ^[0-9]+$ ]]; then
    echo "Reserved CPU '$r' não é um número válido. Abortando."
    exit 1
  fi
  RESERVED_MAP["$r"]=1
done

# build allowed cpus list (strings)
ALLOWED_CPUS=()
for cpu in "${ALL_CPUS[@]}"; do
  if [ -n "${RESERVED_MAP[$cpu]-}" ]; then
    continue
  fi
  ALLOWED_CPUS+=("$cpu")
done

if [ ${#ALLOWED_CPUS[@]} -eq 0 ]; then
  echo "Nenhuma CPU disponível depois de reservar: '${RESERVED}'. Abortando."
  exit 1
fi

# create comma-separated CPU list for stress-ng --taskset
ALLOWED_LIST=$(IFS=,; echo "${ALLOWED_CPUS[*]}")

echo "CPUs totais detected: ${ALL_CPUS[*]}"
echo "Reserved CPUs: ${RESERVED_ARR[*]}"
echo "Will run stress-ng on CPUs: ${ALLOWED_LIST}"
NUM_WORKERS=${#ALLOWED_CPUS[@]}
echo "Number of stress-ng workers to spawn: ${NUM_WORKERS}"

# check stress-ng exists
if ! command -v stress-ng >/dev/null 2>&1; then
  echo "stress-ng não encontrado. Instale com: sudo apt install -y stress-ng"
  exit 1
fi

# pick a heavy cpu method (matrix) that tends to use SIMD
CPU_METHOD="matrixprod"

# trap to cleanup
STRESS_PID=0
cleanup() {
  echo
  echo "[preheat] cleanup: killing stress-ng (pid ${STRESS_PID})..."
  if [ "${STRESS_PID}" -ne 0 ]; then
    kill "${STRESS_PID}" 2>/dev/null || true
    sleep 0.2
    kill -9 "${STRESS_PID}" 2>/dev/null || true
  fi
  echo "[preheat] done."
}
trap cleanup INT TERM EXIT

echo "[preheat] Starting stress-ng. Press Ctrl+C to stop and kill stress-ng."
echo "[preheat] Command: stress-ng --cpu ${NUM_WORKERS} --cpu-method ${CPU_METHOD} --taskset \"${ALLOWED_LIST}\" --timeout 0s"

# Launch stress-ng pinned to allowed cpus. --taskset pins workers to mask.
# --cpu NUM_WORKERS creates that many workers; taskset ensures affinities.
# --timeout 0s => run until killed.
nohup stress-ng --cpu "${NUM_WORKERS}" --cpu-method "${CPU_METHOD}" --taskset "${ALLOWED_LIST}" --timeout 0s >/dev/null 2>&1 &
STRESS_PID=$!
echo "[preheat] stress-ng started with PID ${STRESS_PID}."

# Wait until killed by Ctrl+C
while true; do
  sleep 1
done

