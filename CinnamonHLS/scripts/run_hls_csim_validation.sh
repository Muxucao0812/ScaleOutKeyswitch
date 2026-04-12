#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TIMESTAMP="${CINNAMON_HLS_CSIM_TS:-$(date +%Y%m%d_%H%M%S)}"
LOG_ROOT="${CINNAMON_HLS_CSIM_LOG_ROOT:-$ROOT_DIR/build/logs/hls_csim/$TIMESTAMP}"
PART_NAME="${CINNAMON_HLS_PART:-xcu55c-fsvh2892-2L-e}"
CLOCK_PERIOD_NS="${CINNAMON_HLS_CLOCK_NS:-5.0}"
LATEST_LINK="$ROOT_DIR/build/logs/hls_csim/latest"
SUMMARY_MD="$LOG_ROOT/summary.md"
INDEX_MD="$LOG_ROOT/log_index.md"

mkdir -p "$LOG_ROOT"

if [[ -L "$LATEST_LINK" || -e "$LATEST_LINK" ]]; then
  rm -rf "$LATEST_LINK"
fi
ln -s "$LOG_ROOT" "$LATEST_LINK"

declare -A STEP_STATUS
declare -A STEP_LOG
overall_rc=0

run_hls_csim_kernel() {
  local kernel="$1"
  local top="$2"
  local src="$3"
  local tb="$4"

  local key="csim_${kernel}"
  local work_dir="$LOG_ROOT/$key"
  local prj_dir="$ROOT_DIR/.kernel_validation_prj_${kernel}_${TIMESTAMP}"
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
}

run_hls_csim_kernel "memory" "cinnamon_memory" "kernels/cinnamon_memory.cpp" "hls/tb/tb_cinnamon_memory.cpp"
run_hls_csim_kernel "arithmetic" "cinnamon_arithmetic" "kernels/cinnamon_arithmetic.cpp" "hls/tb/tb_cinnamon_arithmetic.cpp"
run_hls_csim_kernel "montgomery" "cinnamon_montgomery" "kernels/cinnamon_montgomery.cpp" "hls/tb/tb_cinnamon_montgomery.cpp"
run_hls_csim_kernel "ntt" "cinnamon_ntt" "kernels/cinnamon_ntt.cpp" "hls/tb/tb_cinnamon_ntt.cpp"
run_hls_csim_kernel "base_conv" "cinnamon_base_conv" "kernels/cinnamon_base_conv.cpp" "hls/tb/tb_cinnamon_base_conv.cpp"
run_hls_csim_kernel "automorphism" "cinnamon_automorphism" "kernels/cinnamon_automorphism.cpp" "hls/tb/tb_cinnamon_automorphism.cpp"
run_hls_csim_kernel "transpose" "cinnamon_transpose" "kernels/cinnamon_transpose.cpp" "hls/tb/tb_cinnamon_transpose.cpp"

{
  echo "# HLS CSim Summary"
  echo
  echo "Generated on: $(date '+%Y-%m-%d %H:%M:%S %Z')"
  echo
  echo "| Kernel | Status | Log |"
  echo "|---|---|---|"
  for key in csim_memory csim_arithmetic csim_montgomery csim_ntt csim_base_conv csim_automorphism csim_transpose; do
    echo "| ${key#csim_} | ${STEP_STATUS[$key]:-NOT_RUN} | ${STEP_LOG[$key]:-N/A} |"
  done
} > "$SUMMARY_MD"

{
  echo "# HLS CSim Log Index"
  echo
  echo "Timestamp: $TIMESTAMP"
  echo
  for key in csim_memory csim_arithmetic csim_montgomery csim_ntt csim_base_conv csim_automorphism csim_transpose; do
    echo "- $key: ${STEP_STATUS[$key]:-NOT_RUN}"
    echo "  log: ${STEP_LOG[$key]:-N/A}"
  done
} > "$INDEX_MD"

printf 'CSim summary: %s\n' "$SUMMARY_MD"
printf 'Log index: %s\n' "$INDEX_MD"
printf 'Latest symlink: %s -> %s\n' "$LATEST_LINK" "$LOG_ROOT"

exit "$overall_rc"
