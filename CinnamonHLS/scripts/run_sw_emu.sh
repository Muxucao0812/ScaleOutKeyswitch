#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="sw_emu"
PLATFORM="${PLATFORM:-xilinx_u55c_gen3x16_xdma_3_202210_1}"
PACKAGE_NAME="${PACKAGE_NAME:-cinnamon_fpga}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/sw_emu}"
KERNEL_DIR="$ROOT_DIR/kernels"
KERNEL_FREQUENCY_MHZ="${KERNEL_FREQUENCY_MHZ:-200}"
COMPILE_FREQUENCY_MHZ="${COMPILE_FREQUENCY_MHZ:-$KERNEL_FREQUENCY_MHZ}"
LINK_FREQUENCY_MHZ="${LINK_FREQUENCY_MHZ:-$KERNEL_FREQUENCY_MHZ}"
CONNECTIVITY_CFG="${CONNECTIVITY_CFG:-$ROOT_DIR/config/hbm_memory_connectivity.cfg}"
MODE="${MODE:-full}" # full | build | run
PYTHON_BIN="${PYTHON_BIN:-python3.10}"
RUN_TARGET="${RUN_TARGET:-smoke}" # smoke | tutorial_rot

KERNEL_NAMES=(
  cinnamon_memory
  cinnamon_arithmetic
  cinnamon_modmul
  cinnamon_ntt
  cinnamon_base_conv
  cinnamon_automorphism
  cinnamon_transpose
  cinnamon_dispatch
)

if ! command -v v++ >/dev/null 2>&1; then
  echo "v++ not found in PATH. Please load Vitis 2024.1 first." >&2
  exit 1
fi

case "$MODE" in
  full|build|run) ;;
  *)
    echo "Invalid MODE: $MODE (expected: full|build|run)" >&2
    exit 1
    ;;
esac

case "$RUN_TARGET" in
  smoke|tutorial_rot) ;;
  *)
    echo "Invalid RUN_TARGET: $RUN_TARGET (expected: smoke|tutorial_rot)" >&2
    exit 1
    ;;
esac

mkdir -p "$BUILD_DIR"
XCLBIN_FILE="$BUILD_DIR/${PACKAGE_NAME}.${TARGET}.xclbin"

build_xclbin() {
  local xo_files=()
  local kernel_name src_file xo_file

  for kernel_name in "${KERNEL_NAMES[@]}"; do
    src_file="$KERNEL_DIR/${kernel_name}.cpp"
    xo_file="$BUILD_DIR/${kernel_name}.xo"
    xo_files+=("$xo_file")
    if [[ ! -f "$src_file" ]]; then
      echo "Kernel source not found: $src_file" >&2
      exit 1
    fi

    v++ -c -t "$TARGET" --platform "$PLATFORM" \
      -k "$kernel_name" \
      --kernel_frequency "$COMPILE_FREQUENCY_MHZ" \
      -I"$ROOT_DIR/include" \
      -I"$KERNEL_DIR" \
      --temp_dir "$BUILD_DIR/_tmp_compile_${kernel_name}" \
      -o "$xo_file" "$src_file"
  done

  local link_cfg_args=()
  if [[ -f "$CONNECTIVITY_CFG" ]]; then
    link_cfg_args+=(--config "$CONNECTIVITY_CFG")
  fi

  v++ -l -t "$TARGET" --platform "$PLATFORM" \
    --kernel_frequency "$LINK_FREQUENCY_MHZ" \
    --temp_dir "$BUILD_DIR/_tmp_link" \
    --save-temps \
    "${link_cfg_args[@]}" \
    -o "$XCLBIN_FILE" "${xo_files[@]}"

  if command -v emconfigutil >/dev/null 2>&1; then
    (cd "$BUILD_DIR" && emconfigutil --platform "$PLATFORM" --nd 1)
  fi

  echo "Built xclbin: $XCLBIN_FILE"
}

run_smoke() {
  if [[ ! -f "$XCLBIN_FILE" ]]; then
    echo "xclbin not found: $XCLBIN_FILE" >&2
    exit 1
  fi

  export XCL_EMULATION_MODE=sw_emu
  export EMCONFIG_PATH="$BUILD_DIR"
  export PYTHONPATH="$ROOT_DIR/../build/cinnamon_compiler_local/python:$ROOT_DIR/../build/cinnamon_emulator_local/python:$ROOT_DIR/python:/opt/xilinx/xrt/python:${PYTHONPATH:-}"

  if [[ "$RUN_TARGET" == "smoke" ]]; then
    "$PYTHON_BIN" -m cinnamon_fpga.smoke --xclbin "$XCLBIN_FILE"
    return
  fi

  "$PYTHON_BIN" "$ROOT_DIR/python/examples/tutorial_rot_fpga.py"
}

if [[ "$MODE" == "full" || "$MODE" == "build" ]]; then
  build_xclbin
fi

if [[ "$MODE" == "full" || "$MODE" == "run" ]]; then
  run_smoke
fi
