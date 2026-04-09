#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$ROOT_DIR/.." && pwd)"

TARGET="sw_emu"
XCLBIN="${1:-$ROOT_DIR/build/sw_emu/cinnamon_fpga.sw_emu.xclbin}"
SAMPLE_ID="${2:-1}"
MISMATCH_DIR="${3:-$ROOT_DIR/build/reports/mismatch_swemu_debug}"

if [[ ! -f "$XCLBIN" ]]; then
  echo "xclbin not found: $XCLBIN" >&2
  exit 1
fi

export XILINX_XRT="${XILINX_XRT:-/opt/xilinx/xrt}"
export PATH="$XILINX_XRT/bin:${PATH}"

PYTHONPATH_ENTRIES=(
  "$ROOT_DIR/python"
  "$REPO_DIR/build/cinnamon_compiler_local/python"
  "$REPO_DIR/build/cinnamon_emulator_local/python"
  "$XILINX_XRT/python"
  "$REPO_DIR/CinnamonTutorial/notebook3"
)

for entry in "${PYTHONPATH_ENTRIES[@]}"; do
  if [[ -d "$entry" ]]; then
    export PYTHONPATH="$entry:${PYTHONPATH:-}"
  fi
done

python3.10 "$ROOT_DIR/python/examples/benchmark_tutorial3_inference.py" \
  --target "$TARGET" \
  --xclbin "$XCLBIN" \
  --chips "1" \
  --boards "0" \
  --warmup 0 \
  --runs 1 \
  --sample-id "$SAMPLE_ID" \
  --strict-golden-check \
  --archive-debug-artifacts \
  --mismatch-dump-dir "$MISMATCH_DIR"
