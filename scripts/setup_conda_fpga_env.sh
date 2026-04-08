#!/usr/bin/env bash
set -euo pipefail

ENV_NAME="${1:-cinnamon-fpga-py310}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v conda >/dev/null 2>&1; then
  echo "conda is required but was not found in PATH" >&2
  exit 1
fi

# shellcheck disable=SC1091
source "$(conda info --base)/etc/profile.d/conda.sh"

if conda env list | awk '{print $1}' | grep -Fxq "$ENV_NAME"; then
  echo "Updating existing conda env: $ENV_NAME"
  conda install -y -n "$ENV_NAME" -c conda-forge \
    python=3.10 pip numpy pytest cmake ninja jupyterlab ipykernel
else
  echo "Creating conda env: $ENV_NAME"
  conda create -y -n "$ENV_NAME" -c conda-forge \
    python=3.10 pip numpy pytest cmake ninja jupyterlab ipykernel
fi

conda run -n "$ENV_NAME" python -m pip install --upgrade pip
conda run -n "$ENV_NAME" python -m pip install -e "$ROOT_DIR/CinnamonHLS/python"

echo
echo "Environment ready: $ENV_NAME"
echo "Activate with: conda activate $ENV_NAME"
echo "Then export:"
echo "  export PYTHONPATH=/opt/xilinx/xrt/python:\${PYTHONPATH:-}"
echo "  export XILINX_XRT=/opt/xilinx/xrt"

echo "Optional emulator path (after running scripts/build_emulator_local.sh):"
echo "  export PYTHONPATH=$ROOT_DIR/build/cinnamon_emulator_local/python:\${PYTHONPATH:-}"
