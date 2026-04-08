#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build/cinnamon_compiler_local}"
PYTHON_BIN="${PYTHON_BIN:-python3.10}"

mkdir -p "$BUILD_DIR"

cmake -S "$ROOT_DIR/CinnamonCompiler" -B "$BUILD_DIR" \
  -DPython3_EXECUTABLE="$(command -v "$PYTHON_BIN")" \
  -DPYBIND11_FINDPYTHON=ON

cmake --build "$BUILD_DIR" -j "$(nproc)" --target all

echo "Compiler build complete."
echo "export PYTHONPATH=$BUILD_DIR/python:\${PYTHONPATH:-}"
