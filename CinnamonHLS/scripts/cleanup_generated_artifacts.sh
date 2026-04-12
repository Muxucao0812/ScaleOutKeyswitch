#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$ROOT_DIR/.." && pwd)"
DRY_RUN=0

if [[ "${1:-}" == "--dry-run" ]]; then
  DRY_RUN=1
fi

delete_path() {
  local path="$1"
  if [[ ! -e "$path" ]]; then
    return 0
  fi
  if [[ "$DRY_RUN" -eq 1 ]]; then
    printf '[DRY] rm -rf %s\n' "$path"
  else
    if rm -rf "$path"; then
      printf '[DEL] %s\n' "$path"
    else
      printf '[WARN] Failed to delete (likely in use): %s\n' "$path" >&2
    fi
  fi
}

delete_find_matches() {
  local base="$1"
  local maxdepth="$2"
  local pattern="$3"
  if [[ ! -d "$base" ]]; then
    return 0
  fi
  while IFS= read -r -d '' path; do
    delete_path "$path"
  done < <(find "$base" -maxdepth "$maxdepth" -mindepth 1 -name "$pattern" -print0)
}

delete_build_subdirs_except() {
  local base="$1"
  shift
  if [[ ! -d "$base" ]]; then
    return 0
  fi

  local -a keep_names=("$@")
  while IFS= read -r -d '' path; do
    local name
    name="$(basename "$path")"
    local keep=0
    local entry
    for entry in "${keep_names[@]}"; do
      if [[ "$name" == "$entry" ]]; then
        keep=1
        break
      fi
    done
    if [[ "$keep" -eq 0 ]]; then
      delete_path "$path"
    fi
  done < <(find "$base" -maxdepth 1 -mindepth 1 -type d -print0)
}

echo "[INFO] Cleaning generated logs and temporary artifacts..."
echo "[INFO] ROOT_DIR=$ROOT_DIR"
echo "[INFO] REPO_DIR=$REPO_DIR"
echo "[INFO] DRY_RUN=$DRY_RUN"

# Top-level repository logs.
delete_find_matches "$REPO_DIR" 1 "v++_*.log"
delete_find_matches "$REPO_DIR" 1 "*.backup.log"
delete_find_matches "$REPO_DIR" 1 "vitis_hls.log"
delete_find_matches "$REPO_DIR" 1 "xcd.log"

# Top-level generated cache dirs.
delete_path "$REPO_DIR/.ipcache"
delete_path "$REPO_DIR/.run"
delete_path "$REPO_DIR/.Xil"

# CinnamonHLS logs.
delete_find_matches "$ROOT_DIR" 1 "v++_*.log"
delete_find_matches "$ROOT_DIR" 1 "*.backup.log"
delete_find_matches "$ROOT_DIR" 1 "vitis_hls.log"
delete_find_matches "$ROOT_DIR" 1 "xcd.log"

# CinnamonHLS generated projects and caches.
delete_find_matches "$ROOT_DIR" 1 ".kernel_validation_prj_*"
delete_find_matches "$ROOT_DIR" 1 ".auto_transpose_prj_*"
delete_path "$ROOT_DIR/.ipcache"
delete_path "$ROOT_DIR/.run"
delete_path "$ROOT_DIR/.Xil"
delete_path "$ROOT_DIR/prj_mem_dbg7"

# Build-only temporary products; keep reports and final xclbin outputs.
if [[ -d "$ROOT_DIR/build" ]]; then
  while IFS= read -r -d '' path; do
    delete_path "$path"
  done < <(find "$ROOT_DIR/build" -type d \( -name "_tmp_compile_*" -o -name "_tmp_link" -o -name "hls_tmp" \) -print0)
fi

# Generated validation/example outputs under CinnamonHLS/build; keep reusable tool builds.
delete_path "$ROOT_DIR/build/logs"
delete_path "$ROOT_DIR/build/detached"
delete_path "$ROOT_DIR/build/reports"
delete_find_matches "$ROOT_DIR/build" 1 "tmp_*"
delete_find_matches "$ROOT_DIR/build" 1 "cinnamon_*"
delete_find_matches "$ROOT_DIR/build" 1 "bsgs_matmul_benchmark"
delete_build_subdirs_except "$ROOT_DIR/build" "cpu" "hw" "sw_emu" "CMakeFiles"

echo "[INFO] Cleanup complete."
