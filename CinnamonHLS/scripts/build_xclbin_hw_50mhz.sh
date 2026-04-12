#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILE_FREQUENCY_MHZ="${COMPILE_FREQUENCY_MHZ:-50}"
LINK_FREQUENCY_MHZ="${LINK_FREQUENCY_MHZ:-50}"
EXTRA_LINK_CFG="${EXTRA_LINK_CFG:-$ROOT_DIR/config/hw_impl_high_effort.cfg}"
BUILD_STAGE="${BUILD_STAGE:-full}"

if [[ "$#" -ne 0 ]]; then
  echo "This wrapper does not accept positional arguments." >&2
  echo "Override behavior with environment variables such as BUILD_STAGE or BUILD_DIR." >&2
  exit 1
fi

echo "Launching hw build with:"
echo "  COMPILE_FREQUENCY_MHZ=${COMPILE_FREQUENCY_MHZ}"
echo "  LINK_FREQUENCY_MHZ=${LINK_FREQUENCY_MHZ}"
echo "  EXTRA_LINK_CFG=${EXTRA_LINK_CFG}"
echo "  BUILD_STAGE=${BUILD_STAGE}"

exec env \
  COMPILE_FREQUENCY_MHZ="${COMPILE_FREQUENCY_MHZ}" \
  LINK_FREQUENCY_MHZ="${LINK_FREQUENCY_MHZ}" \
  EXTRA_LINK_CFG="${EXTRA_LINK_CFG}" \
  BUILD_STAGE="${BUILD_STAGE}" \
  "${ROOT_DIR}/scripts/build_xclbin.sh" hw
