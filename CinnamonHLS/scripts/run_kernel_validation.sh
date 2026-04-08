#!/usr/bin/env bash
set -u
set -o pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPU_BUILD_DIR="${CINNAMON_CPU_BUILD_DIR:-$ROOT_DIR/build/cpu}"
TIMESTAMP="${CINNAMON_VALIDATION_TS:-$(date +%Y%m%d_%H%M%S)}"
LOG_ROOT="$ROOT_DIR/build/logs/kernel_validation/$TIMESTAMP"
REPORT_DIR="$ROOT_DIR/build/reports"
PART_NAME="${CINNAMON_HLS_PART:-xcu55c-fsvh2892-2L-e}"
CLOCK_PERIOD_NS="${CINNAMON_HLS_CLOCK_NS:-5.0}"
RUN_OPTIONAL_COSIM="${CINNAMON_RUN_OPTIONAL_COSIM:-0}"

mkdir -p "$LOG_ROOT" "$REPORT_DIR"

SUMMARY_MD="$LOG_ROOT/validation_matrix.md"
INDEX_MD="$LOG_ROOT/log_index.md"
LATEST_LINK="$ROOT_DIR/build/logs/kernel_validation/latest"

if [[ -L "$LATEST_LINK" || -e "$LATEST_LINK" ]]; then
  rm -rf "$LATEST_LINK"
fi
ln -s "$LOG_ROOT" "$LATEST_LINK"

declare -A STEP_STATUS
declare -A STEP_LOG

overall_rc=0

run_step() {
  local key="$1"
  local cmd="$2"
  local log_path="$LOG_ROOT/${key}.log"

  echo "[RUN] $key"
  echo "[$(date '+%F %T')] $cmd" > "$log_path"

  if bash -lc "$cmd" >> "$log_path" 2>&1; then
    STEP_STATUS["$key"]="PASS"
  else
    local rc=$?
    STEP_STATUS["$key"]="FAIL($rc)"
    overall_rc=1
  fi
  STEP_LOG["$key"]="$log_path"
}

run_hls_kernel() {
  local kernel="$1"
  local top="$2"
  local src="$3"
  local tb="$4"
  local mode="$5"  # csim | csim_cosim

  local work_dir="$LOG_ROOT/hls_${kernel}"
  local prj_dir="$ROOT_DIR/.kernel_validation_prj_${kernel}_${TIMESTAMP}"
  mkdir -p "$work_dir"

  local tcl="$work_dir/run_${kernel}.tcl"
  cat > "$tcl" <<EOT
open_project -reset $prj_dir
set_top $top
add_files $src -cflags "-std=c++17 -I$ROOT_DIR/include -I$ROOT_DIR/kernels"
add_files -tb $tb -cflags "-std=c++17 -I$ROOT_DIR/include -I$ROOT_DIR/kernels"
open_solution -reset sol1 -flow_target vivado
set_part {$PART_NAME}
create_clock -period $CLOCK_PERIOD_NS
csim_design
csynth_design
EOT

  if [[ "$mode" == "csim_cosim" ]]; then
    cat >> "$tcl" <<'EOT'
cosim_design -rtl verilog -tool xsim
EOT
  elif [[ "$RUN_OPTIONAL_COSIM" == "1" ]]; then
    cat >> "$tcl" <<'EOT'
cosim_design -rtl verilog -tool xsim
EOT
  fi

  cat >> "$tcl" <<'EOT'
exit
EOT

  run_step "hls_${kernel}" "cd $ROOT_DIR && vitis_hls -f $tcl"
}

# Gate A/B/C: C++ + descriptor + python regression
run_step "cmake_configure" "cmake -S $ROOT_DIR -B $CPU_BUILD_DIR"
run_step "cmake_build" "cmake --build $CPU_BUILD_DIR -j\${CINNAMON_BUILD_JOBS:-8}"
run_step "ctest_gate_a" "ctest --test-dir $CPU_BUILD_DIR -R 'cinnamon_hls_(arithmetic|memory|ntt|base_conv|automorphism|montgomery|transpose)$' --output-on-failure"
run_step "ctest_gate_b" "ctest --test-dir $CPU_BUILD_DIR -R 'cinnamon_hls_kernel_descriptor$' --output-on-failure"
run_step "ctest_all" "ctest --test-dir $CPU_BUILD_DIR --output-on-failure"
run_step "pytest" "cd $ROOT_DIR && PYTHONPATH=$ROOT_DIR/python:\${PYTHONPATH:-} python3.10 -m pytest -q python/tests"

# Gate D: HLS simulation (mandatory cosim: ntt/base_conv/automorphism)
run_hls_kernel "memory" "cinnamon_memory" "kernels/cinnamon_memory.cpp" "hls/tb/tb_cinnamon_memory.cpp" "csim"
run_hls_kernel "arithmetic" "cinnamon_arithmetic" "kernels/cinnamon_arithmetic.cpp" "hls/tb/tb_cinnamon_arithmetic.cpp" "csim"
run_hls_kernel "montgomery" "cinnamon_montgomery" "kernels/cinnamon_montgomery.cpp" "hls/tb/tb_cinnamon_montgomery.cpp" "csim"
run_hls_kernel "ntt" "cinnamon_ntt" "kernels/cinnamon_ntt.cpp" "hls/tb/tb_cinnamon_ntt.cpp" "csim_cosim"
run_hls_kernel "base_conv" "cinnamon_base_conv" "kernels/cinnamon_base_conv.cpp" "hls/tb/tb_cinnamon_base_conv.cpp" "csim_cosim"
run_hls_kernel "automorphism" "cinnamon_automorphism" "kernels/cinnamon_automorphism.cpp" "hls/tb/tb_cinnamon_automorphism.cpp" "csim_cosim"
run_hls_kernel "transpose" "cinnamon_transpose" "kernels/cinnamon_transpose.cpp" "hls/tb/tb_cinnamon_transpose.cpp" "csim_cosim"

# Build summary files.
{
  echo "# Validation Matrix"
  echo
  echo "Generated on: $(date '+%Y-%m-%d %H:%M:%S %Z')"
  echo
  echo "| Gate | Scope | Status | Evidence |"
  echo "|---|---|---|---|"

  gate_a_status="${STEP_STATUS[ctest_gate_a]:-NOT_RUN}"
  gate_b_status="${STEP_STATUS[ctest_gate_b]:-NOT_RUN}"
  gate_c_status="${STEP_STATUS[ctest_all]:-NOT_RUN} / ${STEP_STATUS[pytest]:-NOT_RUN}"

  hls_req=(hls_ntt hls_base_conv hls_automorphism hls_transpose)
  hls_opt=(hls_memory hls_arithmetic hls_montgomery)

  gate_d_req="PASS"
  for k in "${hls_req[@]}"; do
    if [[ "${STEP_STATUS[$k]:-NOT_RUN}" != "PASS" ]]; then
      gate_d_req="FAIL"
      break
    fi
  done

  gate_d_opt="PASS"
  for k in "${hls_opt[@]}"; do
    if [[ "${STEP_STATUS[$k]:-NOT_RUN}" != "PASS" ]]; then
      gate_d_opt="FAIL"
      break
    fi
  done

  echo "| Gate A | 模块级 C++ 向量测试 | $gate_a_status | ${STEP_LOG[ctest_gate_a]:-N/A} |"
  echo "| Gate B | Descriptor 回放测试 | $gate_b_status | ${STEP_LOG[ctest_gate_b]:-N/A} |"
  echo "| Gate C | 软件回归 (ctest+pytest) | $gate_c_status | ${STEP_LOG[ctest_all]:-N/A}; ${STEP_LOG[pytest]:-N/A} |"
  echo "| Gate D.1 | HLS csim/cosim (ntt/base_conv/auto/transpose) | $gate_d_req | ${STEP_LOG[hls_ntt]:-N/A}; ${STEP_LOG[hls_base_conv]:-N/A}; ${STEP_LOG[hls_automorphism]:-N/A}; ${STEP_LOG[hls_transpose]:-N/A} |"
  echo "| Gate D.2 | HLS csim (arithmetic/montgomery/memory) | $gate_d_opt | ${STEP_LOG[hls_arithmetic]:-N/A}; ${STEP_LOG[hls_montgomery]:-N/A}; ${STEP_LOG[hls_memory]:-N/A} |"
  echo
  echo "## Per-Step Status"
  echo
  for key in cmake_configure cmake_build ctest_gate_a ctest_gate_b ctest_all pytest hls_memory hls_arithmetic hls_montgomery hls_ntt hls_base_conv hls_automorphism hls_transpose; do
    echo "- $key: ${STEP_STATUS[$key]:-NOT_RUN}"
  done
} > "$SUMMARY_MD"

{
  echo "# Kernel Validation Log Index"
  echo
  echo "Timestamp: $TIMESTAMP"
  echo
  for key in cmake_configure cmake_build ctest_gate_a ctest_gate_b ctest_all pytest hls_memory hls_arithmetic hls_montgomery hls_ntt hls_base_conv hls_automorphism hls_transpose; do
    echo "- $key"
    echo "  - status: ${STEP_STATUS[$key]:-NOT_RUN}"
    echo "  - log: ${STEP_LOG[$key]:-N/A}"
  done
} > "$INDEX_MD"

cp "$SUMMARY_MD" "$REPORT_DIR/validation_matrix.md"

echo
printf 'Validation summary: %s\n' "$SUMMARY_MD"
printf 'Log index: %s\n' "$INDEX_MD"
printf 'Latest symlink: %s -> %s\n' "$LATEST_LINK" "$LOG_ROOT"

exit $overall_rc
