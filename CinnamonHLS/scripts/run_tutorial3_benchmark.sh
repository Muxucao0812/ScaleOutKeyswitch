#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$ROOT_DIR/.." && pwd)"

TARGET="${1:-hw}"
XCLBIN="${2:-$ROOT_DIR/build/hw/cinnamon_fpga.hw.xclbin}"
CHIPS="${3:-1,2,4}"
BOARDS="${4:-0,1,2,3}"
WARMUP="${5:-2}"
RUNS="${6:-5}"
SAMPLE_ID="${7:-1}"
MODE="${8:-verify}" # verify | write-golden

if [[ "$TARGET" != "hw" ]]; then
  echo "Only hw target is supported by this benchmark script." >&2
  exit 1
fi
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

EXTRA_ARGS=()
if [[ "$MODE" == "write-golden" ]]; then
  EXTRA_ARGS+=(--write-golden)
fi

python3.10 "$ROOT_DIR/python/examples/benchmark_tutorial3_inference.py" \
  --target "$TARGET" \
  --xclbin "$XCLBIN" \
  --chips "$CHIPS" \
  --boards "$BOARDS" \
  --warmup "$WARMUP" \
  --runs "$RUNS" \
  --sample-id "$SAMPLE_ID" \
  "${EXTRA_ARGS[@]}"
