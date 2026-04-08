#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$ROOT_DIR/.." && pwd)"

TARGET="${1:-sw_emu}"                     # sw_emu | hw_emu | hw
XCLBIN="${2:-$ROOT_DIR/build/${TARGET}/cinnamon_fpga.${TARGET}.xclbin}"
CHIPS="${3:-1,2,4}"
BOARDS="${4:-0,1,2,3}"
GOLDEN_FILE="${5:-$ROOT_DIR/golden/tutorial_add_kernel_outputs.json}"
ACTION="${6:-verify}"                     # verify | write-golden

if [[ ! -f "$XCLBIN" ]]; then
  echo "xclbin not found: $XCLBIN" >&2
  exit 1
fi

export XILINX_XRT="${XILINX_XRT:-/opt/xilinx/xrt}"
export PATH="$XILINX_XRT/bin:${PATH}"

PYTHONPATH_ENTRIES=(
  "$ROOT_DIR/python"
  "$XILINX_XRT/python"
)

if [[ -d "$REPO_DIR/build/cinnamon_compiler_local/python" ]]; then
  PYTHONPATH_ENTRIES+=("$REPO_DIR/build/cinnamon_compiler_local/python")
fi
if [[ -d "$REPO_DIR/build/cinnamon_emulator_local/python" ]]; then
  PYTHONPATH_ENTRIES+=("$REPO_DIR/build/cinnamon_emulator_local/python")
fi

for entry in "${PYTHONPATH_ENTRIES[@]}"; do
  export PYTHONPATH="$entry:${PYTHONPATH:-}"
done

if [[ "$TARGET" == "sw_emu" || "$TARGET" == "hw_emu" ]]; then
  export XCL_EMULATION_MODE="$TARGET"
  export EMCONFIG_PATH="$(dirname "$XCLBIN")"
fi

EXTRA_ARGS=()
if [[ "$ACTION" == "write-golden" ]]; then
  EXTRA_ARGS+=(--write-golden)
fi

python3.10 "$ROOT_DIR/python/examples/validate_fpga_backend.py" \
  --target "$TARGET" \
  --xclbin "$XCLBIN" \
  --chips "$CHIPS" \
  --boards "$BOARDS" \
  --golden "$GOLDEN_FILE" \
  "${EXTRA_ARGS[@]}"
