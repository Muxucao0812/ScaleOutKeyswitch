#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HLS_ROOT="$REPO_ROOT/CinnamonHLS"
QUEUE_SCRIPT="$REPO_ROOT/scripts/queue_hw_run.sh"
PYTHON_BIN="${PYTHON_BIN:-python3.10}"

TARGET="${TARGET:-hw}"
XCLBIN="${XCLBIN:-$HLS_ROOT/build/hw_50mhz/cinnamon_fpga.hw.xclbin}"
CHIPS="${CHIPS:-1}"
BOARDS="${BOARDS:-0}"
SAMPLE_START="${SAMPLE_START:-1}"
SAMPLE_END="${SAMPLE_END:-1}"
REGISTER_FILE_SIZE="${REGISTER_FILE_SIZE:-1024}"
RUNTIME_TIMEOUT_SEC="${RUNTIME_TIMEOUT_SEC:-28800}" # 8h
DISPATCH_WAIT_TIMEOUT_MS="${DISPATCH_WAIT_TIMEOUT_MS:-300000}" # 5min per kernel dispatch
POLL_SECONDS="${POLL_SECONDS:-30}"
MAX_RETRIES="${MAX_RETRIES:-0}"
WAIT_PID="${WAIT_PID:-}"
REPORT_TAG="${REPORT_TAG:-hw_inference}"
FORCE_RECOMPILE="${FORCE_RECOMPILE:-0}"

if [[ ! -x "$QUEUE_SCRIPT" ]]; then
  echo "queue script not found or not executable: $QUEUE_SCRIPT" >&2
  exit 1
fi

if [[ ! -f "$XCLBIN" ]]; then
  echo "xclbin not found: $XCLBIN" >&2
  exit 1
fi

DEFAULT_PYTHONPATH="$REPO_ROOT/build/cinnamon_compiler_local/python:$REPO_ROOT/build/cinnamon_emulator_local/python:$HLS_ROOT/python:/opt/xilinx/xrt/python"
export PYTHONPATH="${PYTHONPATH:-$DEFAULT_PYTHONPATH}"
export CINNAMON_FPGA_DISPATCH_WAIT_TIMEOUT_MS="$DISPATCH_WAIT_TIMEOUT_MS"

QUEUE_ARGS=(--poll-seconds "$POLL_SECONDS" --max-retries "$MAX_RETRIES")
if [[ -n "$WAIT_PID" ]]; then
  QUEUE_ARGS+=(--wait-pid "$WAIT_PID")
fi

RUN_PREFIX=()
if [[ "$RUNTIME_TIMEOUT_SEC" =~ ^[0-9]+$ ]] && (( RUNTIME_TIMEOUT_SEC > 0 )); then
  if command -v timeout >/dev/null 2>&1; then
    RUN_PREFIX=(timeout --foreground --signal=TERM --kill-after=30s "${RUNTIME_TIMEOUT_SEC}s")
  else
    echo "warning: timeout command not found; running without runtime timeout guard" >&2
  fi
fi

CMD=(
  "$PYTHON_BIN"
  "$HLS_ROOT/python/examples/benchmark_tutorial3_inference.py"
  --target "$TARGET"
  --xclbin "$XCLBIN"
  --chips "$CHIPS"
  --boards "$BOARDS"
  --sample-start "$SAMPLE_START"
  --sample-end "$SAMPLE_END"
  --register-file-size "$REGISTER_FILE_SIZE"
  --report-tag "$REPORT_TAG"
)

if [[ "$FORCE_RECOMPILE" == "1" ]]; then
  CMD+=(--force-recompile)
fi

echo "target=$TARGET xclbin=$XCLBIN chips=$CHIPS boards=$BOARDS sample_range=$SAMPLE_START..$SAMPLE_END"
if (( ${#RUN_PREFIX[@]} > 0 )); then
  echo "runtime timeout: ${RUNTIME_TIMEOUT_SEC}s"
else
  echo "runtime timeout: disabled"
fi
echo "dispatch wait timeout: ${DISPATCH_WAIT_TIMEOUT_MS}ms"

"$QUEUE_SCRIPT" "${QUEUE_ARGS[@]}" -- "${RUN_PREFIX[@]}" "${CMD[@]}"
