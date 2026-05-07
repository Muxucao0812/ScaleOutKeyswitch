#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TIMESTAMP="${CINNAMON_CSIM_TS:-$(date +%Y%m%d_%H%M%S)}"
BUILD_ROOT="${CINNAMON_CSIM_ROOT:-$ROOT_DIR/build/csim}"
LOG_ROOT="$BUILD_ROOT/logs/$TIMESTAMP"
PROJECT_ROOT="${CINNAMON_CSIM_PROJECT_ROOT:-$ROOT_DIR}"
LATEST_LINK="$BUILD_ROOT/latest"
SUMMARY_MD="$LOG_ROOT/summary.md"
INDEX_MD="$LOG_ROOT/log_index.md"
PART_NAME="${CINNAMON_HLS_PART:-xcu55c-fsvh2892-2L-e}"
CLOCK_PERIOD_NS="${CINNAMON_HLS_CLOCK_NS:-5.0}"
KERNELS_CSV="${CINNAMON_CSIM_KERNELS:-memory,arithmetic,modmul,ntt,base_conv,automorphism,transpose}"

mkdir -p "$LOG_ROOT" "$PROJECT_ROOT"

if [[ -L "$LATEST_LINK" || -e "$LATEST_LINK" ]]; then
  rm -rf "$LATEST_LINK"
fi
ln -s "$LOG_ROOT" "$LATEST_LINK"

declare -A STEP_STATUS
declare -A STEP_LOG
declare -a RUN_KEYS=()
overall_rc=0

resolve_kernel_info() {
  local kernel="$1"
  case "$kernel" in
    memory)
      printf 'cinnamon_memory|kernels/cinnamon_memory.cpp|tests/csim/tb_cinnamon_memory.cpp\n'
      ;;
    arithmetic)
      printf 'cinnamon_arithmetic|kernels/cinnamon_arithmetic.cpp|tests/csim/tb_cinnamon_arithmetic.cpp\n'
      ;;
    modmul)
      printf 'cinnamon_modmul|kernels/cinnamon_modmul.cpp|tests/csim/tb_cinnamon_modmul.cpp\n'
      ;;
    ntt)
      printf 'cinnamon_ntt|kernels/cinnamon_ntt.cpp|tests/csim/tb_cinnamon_ntt.cpp\n'
      ;;
    base_conv)
      printf 'cinnamon_base_conv|kernels/cinnamon_base_conv.cpp|tests/csim/tb_cinnamon_base_conv.cpp\n'
      ;;
    automorphism)
      printf 'cinnamon_automorphism|kernels/cinnamon_automorphism.cpp|tests/csim/tb_cinnamon_automorphism.cpp\n'
      ;;
    transpose)
      printf 'cinnamon_transpose|kernels/cinnamon_transpose.cpp|tests/csim/tb_cinnamon_transpose.cpp\n'
      ;;
    *)
      echo "unsupported kernel: $kernel" >&2
      return 1
      ;;
  esac
}

run_hls_csim_kernel() {
  local kernel="$1"
  local info
  info="$(resolve_kernel_info "$kernel")"

  local top src tb
  IFS='|' read -r top src tb <<< "$info"

  local key="csim_${kernel}"
  local work_dir="$LOG_ROOT/$key"
  local prj_dir="$PROJECT_ROOT/.csim_prj_${kernel}_${TIMESTAMP}"
  local log_path="$LOG_ROOT/${key}.log"
  local tcl="$work_dir/run_${kernel}.tcl"

  mkdir -p "$work_dir"

  cat > "$tcl" <<EOT
open_project -reset $prj_dir
set_top $top
add_files $src -cflags "-std=c++17 -I$ROOT_DIR/include -I$ROOT_DIR/kernels"
add_files -tb $tb -cflags "-std=c++17 -I$ROOT_DIR/include -I$ROOT_DIR/kernels"
open_solution -reset sol1 -flow_target vivado
set_part {$PART_NAME}
create_clock -period $CLOCK_PERIOD_NS
csim_design
exit
EOT

  echo "[RUN] $key"
  echo "[$(date '+%F %T')] cd $ROOT_DIR && vitis_hls -f $tcl" > "$log_path"
  if (
    cd "$ROOT_DIR"
    vitis_hls -f "$tcl"
  ) >> "$log_path" 2>&1; then
    STEP_STATUS["$key"]="PASS"
  else
    local rc=$?
    STEP_STATUS["$key"]="FAIL($rc)"
    overall_rc=1
  fi
  STEP_LOG["$key"]="$log_path"
  RUN_KEYS+=("$key")
}

IFS=',' read -r -a kernels <<< "$KERNELS_CSV"
for kernel in "${kernels[@]}"; do
  kernel="${kernel// /}"
  [[ -n "$kernel" ]] || continue
  run_hls_csim_kernel "$kernel"
done

{
  echo "# HLS CSim Summary"
  echo
  echo "Generated on: $(date '+%Y-%m-%d %H:%M:%S %Z')"
  echo
  echo "| Kernel | Status | Log |"
  echo "|---|---|---|"
  for key in "${RUN_KEYS[@]}"; do
    echo "| ${key#csim_} | ${STEP_STATUS[$key]:-NOT_RUN} | ${STEP_LOG[$key]:-N/A} |"
  done
} > "$SUMMARY_MD"

{
  echo "# HLS CSim Log Index"
  echo
  echo "Timestamp: $TIMESTAMP"
  echo "Project root: $PROJECT_ROOT"
  echo
  for key in "${RUN_KEYS[@]}"; do
    echo "- $key: ${STEP_STATUS[$key]:-NOT_RUN}"
    echo "  log: ${STEP_LOG[$key]:-N/A}"
  done
} > "$INDEX_MD"

printf 'CSim summary: %s\n' "$SUMMARY_MD"
printf 'Log index: %s\n' "$INDEX_MD"
printf 'Project root: %s\n' "$PROJECT_ROOT"
printf 'Latest symlink: %s -> %s\n' "$LATEST_LINK" "$LOG_ROOT"

exit "$overall_rc"
