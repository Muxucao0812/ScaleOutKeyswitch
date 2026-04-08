#!/usr/bin/env bash
set -euo pipefail

echo "[v++]"
v++ --version | sed -n '1,4p'

echo
echo "[vitis_hls]"
vitis_hls -version | sed -n '1,4p'

echo
echo "[xrt-smi]"
XRT_SMI_BIN="$(command -v xrt-smi || true)"
if [[ -z "${XRT_SMI_BIN}" && -x "/opt/xilinx/xrt/bin/xrt-smi" ]]; then
  XRT_SMI_BIN="/opt/xilinx/xrt/bin/xrt-smi"
fi
if [[ -n "${XRT_SMI_BIN}" ]]; then
  "${XRT_SMI_BIN}" --version | sed -n '1,4p' || true
else
  echo "xrt-smi not found in PATH or /opt/xilinx/xrt/bin"
fi

echo
echo "[devices]"
if [[ -n "${XRT_SMI_BIN}" ]]; then
  "${XRT_SMI_BIN}" examine | sed -n '1,140p' || true
else
  echo "device query skipped (xrt-smi unavailable)"
fi
