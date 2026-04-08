#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-sw_emu}"   # sw_emu | hw_emu | hw
PLATFORM="${PLATFORM:-xilinx_u55c_gen3x16_xdma_3_202210_1}"
PACKAGE_NAME="${PACKAGE_NAME:-cinnamon_fpga}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/${TARGET}}"
KERNEL_DIR="${ROOT_DIR}/kernels"
KERNEL_FREQUENCY_MHZ="${KERNEL_FREQUENCY_MHZ:-200}"
CONNECTIVITY_CFG="${CONNECTIVITY_CFG:-$ROOT_DIR/config/hbm_memory_connectivity.cfg}"

if ! command -v v++ >/dev/null 2>&1; then
  echo "v++ not found in PATH. Please load Vitis 2024.1 first." >&2
  exit 1
fi

mkdir -p "$BUILD_DIR"
XCLBIN_FILE="$BUILD_DIR/${PACKAGE_NAME}.${TARGET}.xclbin"

KERNEL_NAMES=(
  cinnamon_memory
  cinnamon_arithmetic
  cinnamon_montgomery
  cinnamon_ntt
  cinnamon_base_conv
  cinnamon_automorphism
  cinnamon_transpose
  cinnamon_dispatch
)

XO_FILES=()
for KERNEL_NAME in "${KERNEL_NAMES[@]}"; do
  SRC_FILE="$KERNEL_DIR/${KERNEL_NAME}.cpp"
  if [[ ! -f "$SRC_FILE" ]]; then
    echo "Kernel source not found: $SRC_FILE" >&2
    exit 1
  fi
  XO_FILE="$BUILD_DIR/${KERNEL_NAME}.xo"
  XO_FILES+=("$XO_FILE")
  v++ -c -t "$TARGET" --platform "$PLATFORM" \
    -k "$KERNEL_NAME" \
    --kernel_frequency "$KERNEL_FREQUENCY_MHZ" \
    -I"$ROOT_DIR/include" \
    -I"$KERNEL_DIR" \
    --temp_dir "$BUILD_DIR/_tmp_compile_${KERNEL_NAME}" \
    -o "$XO_FILE" "$SRC_FILE"
done

LINK_CFG_ARGS=()
if [[ -f "$CONNECTIVITY_CFG" ]]; then
  LINK_CFG_ARGS+=(--config "$CONNECTIVITY_CFG")
fi

v++ -l -t "$TARGET" --platform "$PLATFORM" \
  --kernel_frequency "$KERNEL_FREQUENCY_MHZ" \
  --temp_dir "$BUILD_DIR/_tmp_link" \
  --save-temps \
  "${LINK_CFG_ARGS[@]}" \
  -o "$XCLBIN_FILE" "${XO_FILES[@]}"

if [[ "$TARGET" == "sw_emu" || "$TARGET" == "hw_emu" ]]; then
  if command -v emconfigutil >/dev/null 2>&1; then
    (cd "$BUILD_DIR" && emconfigutil --platform "$PLATFORM" --nd 1)
  fi
fi

echo "Built xclbin: $XCLBIN_FILE"
echo "Kernel frequency target: ${KERNEL_FREQUENCY_MHZ} MHz"
