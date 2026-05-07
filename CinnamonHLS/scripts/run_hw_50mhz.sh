#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="hw"
PLATFORM="${PLATFORM:-xilinx_u55c_gen3x16_xdma_3_202210_1}"
PACKAGE_NAME="${PACKAGE_NAME:-cinnamon_fpga}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/hw_50mhz}"
KERNEL_DIR="$ROOT_DIR/kernels"
COMPILE_FREQUENCY_MHZ="${COMPILE_FREQUENCY_MHZ:-50}"
LINK_FREQUENCY_MHZ="${LINK_FREQUENCY_MHZ:-50}"
CONNECTIVITY_CFG="${CONNECTIVITY_CFG:-$ROOT_DIR/config/hbm_memory_connectivity.cfg}"
EXTRA_LINK_CFG="${EXTRA_LINK_CFG:-$ROOT_DIR/config/hw_impl_high_effort.cfg}"
BUILD_STAGE="${BUILD_STAGE:-full}" # full | compile | link

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

case "$BUILD_STAGE" in
  full|compile|link) ;;
  *)
    echo "Invalid BUILD_STAGE: $BUILD_STAGE (expected: full|compile|link)" >&2
    exit 1
    ;;
esac

mkdir -p "$BUILD_DIR"
XCLBIN_FILE="$BUILD_DIR/${PACKAGE_NAME}.${TARGET}.xclbin"

XO_FILES=()
for kernel_name in "${KERNEL_NAMES[@]}"; do
  src_file="$KERNEL_DIR/${kernel_name}.cpp"
  xo_file="$BUILD_DIR/${kernel_name}.xo"
  XO_FILES+=("$xo_file")

  if [[ ! -f "$src_file" ]]; then
    echo "Kernel source not found: $src_file" >&2
    exit 1
  fi

  if [[ "$BUILD_STAGE" != "link" ]]; then
    v++ -c -t "$TARGET" --platform "$PLATFORM" \
      -k "$kernel_name" \
      --kernel_frequency "$COMPILE_FREQUENCY_MHZ" \
      -I"$ROOT_DIR/include" \
      -I"$KERNEL_DIR" \
      --temp_dir "$BUILD_DIR/_tmp_compile_${kernel_name}" \
      -o "$xo_file" "$src_file"
  elif [[ ! -f "$xo_file" ]]; then
    echo "Missing xo for link stage: $xo_file" >&2
    exit 1
  fi
done

if [[ "$BUILD_STAGE" == "compile" ]]; then
  echo "Compile only done: $BUILD_DIR"
  echo "Compile frequency target: ${COMPILE_FREQUENCY_MHZ} MHz"
  exit 0
fi

LINK_CFG_ARGS=()
if [[ -f "$CONNECTIVITY_CFG" ]]; then
  LINK_CFG_ARGS+=(--config "$CONNECTIVITY_CFG")
fi
if [[ -n "$EXTRA_LINK_CFG" ]]; then
  if [[ ! -f "$EXTRA_LINK_CFG" ]]; then
    echo "Extra link config not found: $EXTRA_LINK_CFG" >&2
    exit 1
  fi
  LINK_CFG_ARGS+=(--config "$EXTRA_LINK_CFG")
fi

v++ -l -t "$TARGET" --platform "$PLATFORM" \
  --kernel_frequency "$LINK_FREQUENCY_MHZ" \
  --temp_dir "$BUILD_DIR/_tmp_link" \
  --save-temps \
  "${LINK_CFG_ARGS[@]}" \
  -o "$XCLBIN_FILE" "${XO_FILES[@]}"

echo "Built xclbin: $XCLBIN_FILE"
echo "Compile frequency target: ${COMPILE_FREQUENCY_MHZ} MHz"
echo "Link frequency target: ${LINK_FREQUENCY_MHZ} MHz"
