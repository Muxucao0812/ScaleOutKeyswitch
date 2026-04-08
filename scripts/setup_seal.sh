#!/usr/bin/env bash
set -euo pipefail

SEAL_VERSION="4.1.1"
SEAL_PREFIX_DEFAULT="$HOME/.local/seal-${SEAL_VERSION}"
SEAL_PREFIX="${SEAL_DIR:-$SEAL_PREFIX_DEFAULT}"
FORCE_SOURCE=0

usage() {
  cat <<USAGE
Usage: $0 [--prefix <path>] [--force-source]

Installs Microsoft SEAL ${SEAL_VERSION} without sudo.
Installation order:
1) Current conda environment (if active)
2) Source build into --prefix (default: ${SEAL_PREFIX_DEFAULT})

Environment variables respected:
  SEAL_DIR      Install prefix (same as --prefix)
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      SEAL_PREFIX="$2"
      shift 2
      ;;
    --force-source)
      FORCE_SOURCE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

find_seal_config() {
  local root="$1"
  find "$root" -type f -name SEALConfig.cmake 2>/dev/null | head -n 1 || true
}

if [[ -n "$(find_seal_config "$SEAL_PREFIX")" ]]; then
  echo "SEAL already installed at: $SEAL_PREFIX"
  echo "export SEAL_DIR=$SEAL_PREFIX"
  echo "export CMAKE_PREFIX_PATH=$SEAL_PREFIX:\${CMAKE_PREFIX_PATH:-}"
  exit 0
fi

if [[ "$FORCE_SOURCE" -eq 0 ]] && command -v conda >/dev/null 2>&1; then
  # shellcheck disable=SC1091
  source "$(conda info --base)/etc/profile.d/conda.sh"
  if [[ -n "${CONDA_PREFIX:-}" ]]; then
    echo "Trying conda install in active env: ${CONDA_PREFIX}"
    if conda install -y -c conda-forge "seal=${SEAL_VERSION}" >/tmp/cinnamon_conda_seal_install.log 2>&1; then
      CONDA_SEAL_CONFIG="$(find_seal_config "$CONDA_PREFIX")"
      if [[ -n "$CONDA_SEAL_CONFIG" ]]; then
        echo "SEAL installed via conda at: $(dirname "$(dirname "$CONDA_SEAL_CONFIG")")"
        echo "export CMAKE_PREFIX_PATH=$CONDA_PREFIX:\${CMAKE_PREFIX_PATH:-}"
        echo "export SEAL_DIR=$CONDA_PREFIX"
        exit 0
      fi
      echo "Conda install finished but SEALConfig.cmake not found; falling back to source build" >&2
    else
      echo "Conda install failed; falling back to source build" >&2
    fi
  else
    echo "No active conda environment detected; skipping conda install path" >&2
  fi
fi

echo "Building SEAL ${SEAL_VERSION} from source into ${SEAL_PREFIX}"
SEAL_SRC_ROOT="${HOME}/.cache/cinnamon/seal-${SEAL_VERSION}"
SEAL_SRC_DIR="${SEAL_SRC_ROOT}/src"
SEAL_BUILD_DIR="${SEAL_SRC_ROOT}/build"

mkdir -p "${SEAL_SRC_ROOT}"
if [[ ! -d "${SEAL_SRC_DIR}/.git" ]]; then
  rm -rf "${SEAL_SRC_DIR}"
  git clone --depth 1 --branch "v${SEAL_VERSION}" https://github.com/microsoft/SEAL.git "${SEAL_SRC_DIR}"
fi

cmake -S "${SEAL_SRC_DIR}" -B "${SEAL_BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${SEAL_PREFIX}" \
  -DSEAL_BUILD_DEPS=ON \
  -DSEAL_BUILD_SEAL_C=OFF \
  -DSEAL_BUILD_EXAMPLES=OFF \
  -DSEAL_BUILD_TESTS=OFF

cmake --build "${SEAL_BUILD_DIR}" -j "$(nproc)"
cmake --install "${SEAL_BUILD_DIR}"

SEAL_CONFIG="$(find_seal_config "$SEAL_PREFIX")"
if [[ -z "$SEAL_CONFIG" ]]; then
  echo "SEAL install completed, but SEALConfig.cmake not found under $SEAL_PREFIX" >&2
  exit 1
fi

echo "SEAL source install complete: ${SEAL_PREFIX}"
echo "export SEAL_DIR=$SEAL_PREFIX"
echo "export CMAKE_PREFIX_PATH=$SEAL_PREFIX:\${CMAKE_PREFIX_PATH:-}"
