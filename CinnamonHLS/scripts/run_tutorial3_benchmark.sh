#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$ROOT_DIR/.." && pwd)"

default_hw_xclbin() {
  local primary="$ROOT_DIR/build/hw/cinnamon_fpga.hw.xclbin"
  local detached_50="$ROOT_DIR/build/detached/hw50_link/runs/20260410_230801/cinnamon_fpga.hw.xclbin"
  if [[ -f "$primary" ]]; then
    printf '%s\n' "$primary"
    return
  fi
  if [[ -f "$detached_50" ]]; then
    printf '%s\n' "$detached_50"
    return
  fi
  printf '%s\n' "$primary"
}

TARGET="${1:-hw}"
XCLBIN="${2:-}"
CHIPS="${3:-1,2,4}"
BOARDS="${4:-0,1,2,3}"
SAMPLE_START="${5:-1}"
SAMPLE_END="${6:-20}"
REPORT_TAG="${7:-hw_s1_20}"
MODE="${8:-accuracy}" # accuracy | pure
WORK_ROOT="${9:-$ROOT_DIR/build/cinnamon_tutorial3_fpga_only_${REPORT_TAG}}"
REPORT_DIR="${10:-$ROOT_DIR/build/reports}"

if [[ -z "$XCLBIN" ]]; then
  if [[ "$TARGET" == "hw" ]]; then
    XCLBIN="$(default_hw_xclbin)"
  else
    XCLBIN="$ROOT_DIR/build/$TARGET/cinnamon_fpga.$TARGET.xclbin"
  fi
fi

if [[ ! -f "$XCLBIN" ]]; then
  echo "xclbin not found: $XCLBIN" >&2
  exit 1
fi

case "$MODE" in
  accuracy|pure)
    ;;
  *)
    echo "unsupported mode: $MODE (expected: accuracy | pure)" >&2
    exit 1
    ;;
esac

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
if [[ "$MODE" == "pure" ]]; then
  EXTRA_ARGS+=(--disable-emulator-mirror)
else
  cat >&2 <<'EOF'
Running in accuracy mode.
Note: current benchmark gets pred/label by materializing outputs through the emulator mirror path.
The FPGA timing breakdown in the JSON is still the FPGA run_program timing.
EOF
fi

python3.10 "$ROOT_DIR/python/examples/benchmark_tutorial3_inference.py" \
  --target "$TARGET" \
  --xclbin "$XCLBIN" \
  --chips "$CHIPS" \
  --boards "$BOARDS" \
  --sample-start "$SAMPLE_START" \
  --sample-end "$SAMPLE_END" \
  --work-root "$WORK_ROOT" \
  --report-dir "$REPORT_DIR" \
  --report-tag "$REPORT_TAG" \
  "${EXTRA_ARGS[@]}"
