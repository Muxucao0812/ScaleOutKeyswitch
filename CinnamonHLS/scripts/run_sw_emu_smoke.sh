#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
XCLBIN="${1:-$ROOT_DIR/build/sw_emu/cinnamon_fpga.sw_emu.xclbin}"

if [[ ! -f "$XCLBIN" ]]; then
  echo "xclbin not found: $XCLBIN" >&2
  exit 1
fi

export XCL_EMULATION_MODE=sw_emu
export EMCONFIG_PATH="$(dirname "$XCLBIN")"
export PYTHONPATH="$ROOT_DIR/python:/opt/xilinx/xrt/python:${PYTHONPATH:-}"

python3.10 -m cinnamon_fpga.smoke --xclbin "$XCLBIN"
