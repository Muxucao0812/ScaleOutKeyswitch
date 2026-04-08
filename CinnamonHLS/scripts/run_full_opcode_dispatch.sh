#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-sw_emu}"  # sw_emu | hw_emu | hw
XCLBIN="${2:-$ROOT_DIR/build/${TARGET}/cinnamon_fpga.${TARGET}.xclbin}"
BOARD="${3:-0}"

if [[ ! -f "$XCLBIN" ]]; then
  echo "xclbin not found: $XCLBIN" >&2
  exit 1
fi

export XILINX_XRT="${XILINX_XRT:-/opt/xilinx/xrt}"
export PATH="$XILINX_XRT/bin:${PATH}"
export PYTHONPATH="$ROOT_DIR/python:$XILINX_XRT/python:${PYTHONPATH:-}"

if [[ "$TARGET" == "sw_emu" || "$TARGET" == "hw_emu" ]]; then
  export XCL_EMULATION_MODE="$TARGET"
  export EMCONFIG_PATH="$(dirname "$XCLBIN")"
fi

python3.10 "$ROOT_DIR/python/examples/validate_full_opcode_dispatch.py" \
  --target "$TARGET" \
  --xclbin "$XCLBIN" \
  --board "$BOARD"
