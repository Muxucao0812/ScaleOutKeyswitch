#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$ROOT_DIR/.." && pwd)"
XCLBIN="${1:-$ROOT_DIR/build/sw_emu/cinnamon_fpga.sw_emu.xclbin}"
MATMUL_SIZES="${MATMUL_SIZES:-16,32,64,128}"
MATMUL_CASE="${MATMUL_CASE:-identity}"
STOP_ON_FAIL="${STOP_ON_FAIL:-1}"
RUN_ROT="${RUN_ROT:-1}"
RUN_MUL="${RUN_MUL:-1}"
RUN_MATMUL="${RUN_MATMUL:-1}"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
LOG_DIR="${LOG_DIR:-$ROOT_DIR/build/reports/sw_emu_tutorial_suite_$TIMESTAMP}"

if [[ ! -f "$XCLBIN" ]]; then
  echo "xclbin not found: $XCLBIN" >&2
  exit 1
fi

mkdir -p "$LOG_DIR"

export XCL_EMULATION_MODE=sw_emu
export EMCONFIG_PATH="$(dirname "$XCLBIN")"

PYTHONPATH_ENTRIES=(
  "$REPO_DIR/build/cinnamon_compiler_local/python"
  "$REPO_DIR/build/cinnamon_emulator_local/python"
  "$ROOT_DIR/python"
  "/opt/xilinx/xrt/python"
)

for entry in "${PYTHONPATH_ENTRIES[@]}"; do
  if [[ -d "$entry" ]]; then
    export PYTHONPATH="$entry:${PYTHONPATH:-}"
  fi
done

declare -A NAMES=()
declare -A LOGS=()
declare -A STATUSES=()

cleanup_running() {
  local pid
  for pid in "${!NAMES[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
}

trap cleanup_running EXIT INT TERM

start_job() {
  local name="$1"
  shift
  local log="$LOG_DIR/${name}.log"
  (
    set +e
    echo "[$(date '+%F %T')] START $name"
    "$@"
    status=$?
    echo "[$(date '+%F %T')] EXIT $name status=$status"
    exit "$status"
  ) >"$log" 2>&1 &
  local pid=$!
  NAMES["$pid"]="$name"
  LOGS["$pid"]="$log"
  echo "started $name pid=$pid log=$log"
}

print_log_tail() {
  local log="$1"
  echo "---- tail: $log ----"
  tail -n 40 "$log" || true
  echo "---------------------"
}

if [[ "$RUN_ROT" == "1" ]]; then
  start_job rot python3.10 "$ROOT_DIR/python/examples/tutorial_rot_fpga.py"
fi
if [[ "$RUN_MUL" == "1" ]]; then
  start_job mul python3.10 "$ROOT_DIR/python/examples/tutorial_mult_fpga.py"
fi
if [[ "$RUN_MATMUL" == "1" ]]; then
  MATMUL_ARGS=(
    python3.10 "$ROOT_DIR/python/examples/tutorial_matmul_fpga.py"
    --sizes "$MATMUL_SIZES"
    --case "$MATMUL_CASE"
  )
  if [[ "$STOP_ON_FAIL" == "1" ]]; then
    MATMUL_ARGS+=(--fail-fast)
  fi
  start_job matmul "${MATMUL_ARGS[@]}"
fi

while ((${#NAMES[@]} > 0)); do
  set +e
  wait -n -p finished_pid "${!NAMES[@]}"
  status=$?
  set -e

  name="${NAMES[$finished_pid]}"
  log="${LOGS[$finished_pid]}"
  unset 'NAMES[$finished_pid]'
  unset 'LOGS[$finished_pid]'
  STATUSES["$name"]="$status"

  if ((status != 0)); then
    echo "FAILED $name status=$status"
    print_log_tail "$log"
    if [[ "$STOP_ON_FAIL" == "1" ]]; then
      cleanup_running
      wait || true
      echo "suite_status=failed"
      echo "logs_dir=$LOG_DIR"
      exit "$status"
    fi
    continue
  fi

  echo "PASSED $name"
  print_log_tail "$log"
done

trap - EXIT INT TERM
overall_status=0
for name in "${!STATUSES[@]}"; do
  if [[ "${STATUSES[$name]}" != "0" ]]; then
    overall_status=1
  fi
done
if ((overall_status == 0)); then
  echo "suite_status=passed"
else
  echo "suite_status=completed_with_failures"
fi
echo "logs_dir=$LOG_DIR"
exit "$overall_status"
