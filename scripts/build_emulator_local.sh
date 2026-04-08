#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build/cinnamon_emulator_local}"
PYTHON_BIN="${PYTHON_BIN:-python3.10}"
SEAL_DIR_DEFAULT="$HOME/.local/seal-4.1.1"
SEAL_DIR_EFFECTIVE="${SEAL_DIR:-$SEAL_DIR_DEFAULT}"

find_seal_config() {
  local root="$1"
  find "$root" -type f -name SEALConfig.cmake 2>/dev/null | head -n 1 || true
}

if [[ -z "$(find_seal_config "$SEAL_DIR_EFFECTIVE")" ]]; then
  if [[ -n "${CONDA_PREFIX:-}" ]] && [[ -n "$(find_seal_config "$CONDA_PREFIX")" ]]; then
    SEAL_DIR_EFFECTIVE="$CONDA_PREFIX"
  else
    cat >&2 <<MSG
SEALConfig.cmake not found.
Tried:
  - $SEAL_DIR_EFFECTIVE
  - active conda env (
    ${CONDA_PREFIX:-<none>}
    )
Please run: scripts/setup_seal.sh
MSG
    exit 1
  fi
fi

mkdir -p "$BUILD_DIR"

cmake -S "$ROOT_DIR/CinnamonEmulator" -B "$BUILD_DIR" \
  -DCMAKE_PREFIX_PATH="$SEAL_DIR_EFFECTIVE" \
  -DPython3_EXECUTABLE="$(command -v "$PYTHON_BIN")" \
  -DPYBIND11_FINDPYTHON=ON

cmake --build "$BUILD_DIR" -j "$(nproc)" --target all

PY_PKG_DIR="$BUILD_DIR/python"
if [[ ! -d "$PY_PKG_DIR" ]]; then
  echo "Build finished but python package directory not found: $PY_PKG_DIR" >&2
  exit 1
fi

echo "Emulator build complete."
echo "export SEAL_DIR=$SEAL_DIR_EFFECTIVE"
echo "export CMAKE_PREFIX_PATH=$SEAL_DIR_EFFECTIVE:\${CMAKE_PREFIX_PATH:-}"
echo "export PYTHONPATH=$PY_PKG_DIR:\${PYTHONPATH:-}"
