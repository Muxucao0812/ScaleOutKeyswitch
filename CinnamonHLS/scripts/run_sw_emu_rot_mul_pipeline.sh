#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="${LOG_DIR:-$ROOT_DIR/build/sw_emu/nohup_logs}"
PYTHON_BIN="${PYTHON_BIN:-python3.10}"

mkdir -p "$LOG_DIR"

BUILD_LOG="$LOG_DIR/sw_emu_build.log"
ROT_LOG="$LOG_DIR/sw_emu_tutorial_rot.log"
MUL_LOG="$LOG_DIR/sw_emu_tutorial_mul.log"
STATUS_LOG="$LOG_DIR/sw_emu_pipeline.status"

echo "[PIPELINE] start $(date '+%F %T')"
echo "RUNNING" > "$STATUS_LOG"

echo "[PIPELINE] build sw_emu xclbin"
MODE=build PYTHON_BIN="$PYTHON_BIN" "$ROOT_DIR/scripts/run_sw_emu.sh" \
  >"$BUILD_LOG" 2>&1

export XCL_EMULATION_MODE=sw_emu
export EMCONFIG_PATH="$ROOT_DIR/build/sw_emu"
export PYTHONPATH="$ROOT_DIR/../build/cinnamon_compiler_local/python:$ROOT_DIR/../build/cinnamon_emulator_local/python:$ROOT_DIR/python:/opt/xilinx/xrt/python:${PYTHONPATH:-}"

echo "[PIPELINE] run tutorial_rot_fpga.py"
"$PYTHON_BIN" "$ROOT_DIR/python/examples/tutorial_rot_fpga.py" \
  >"$ROT_LOG" 2>&1

echo "[PIPELINE] run tutorial_mult_fpga.py"
"$PYTHON_BIN" "$ROOT_DIR/python/examples/tutorial_mult_fpga.py" \
  >"$MUL_LOG" 2>&1

echo "PASS" > "$STATUS_LOG"
echo "[PIPELINE] done $(date '+%F %T')"
