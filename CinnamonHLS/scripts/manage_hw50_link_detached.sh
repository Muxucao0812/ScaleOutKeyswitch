#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STATE_DIR="${STATE_DIR:-$ROOT_DIR/build/detached/hw50_link}"
RUNS_DIR="${RUNS_DIR:-$STATE_DIR/runs}"
SOURCE_BUILD_DIR="${SOURCE_BUILD_DIR:-$ROOT_DIR/build/hw}"
STATE_FILE="$STATE_DIR/current.env"
PID_FILE="$STATE_DIR/current.pid"
BUILD_SCRIPT="$ROOT_DIR/scripts/build_xclbin_hw_50mhz.sh"

KERNEL_NAMES=(
  cinnamon_memory
  cinnamon_arithmetic
  cinnamon_montgomery
  cinnamon_ntt
  cinnamon_base_conv
  cinnamon_automorphism
  cinnamon_transpose
  cinnamon_dispatch
)

usage() {
  cat <<'EOF'
Usage:
  manage_hw50_link_detached.sh start
  manage_hw50_link_detached.sh status
  manage_hw50_link_detached.sh log

Environment overrides:
  SOURCE_BUILD_DIR  Directory containing prebuilt .xo files
  STATE_DIR         Directory for pid/state tracking
  RUNS_DIR          Directory for detached run outputs
EOF
}

require_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Missing required command: $cmd" >&2
    exit 1
  fi
}

is_pid_running() {
  local pid="${1:-}"
  [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null
}

get_state() {
  local key="$1"
  if [[ -f "$STATE_FILE" ]]; then
    sed -n "s/^${key}=//p" "$STATE_FILE" | tail -n 1
  fi
}

linger_enabled() {
  loginctl show-user "$(id -un)" -p Linger 2>/dev/null | grep -q '^Linger=yes$'
}

ensure_source_xos() {
  local kernel
  for kernel in "${KERNEL_NAMES[@]}"; do
    if [[ ! -f "$SOURCE_BUILD_DIR/${kernel}.xo" ]]; then
      echo "Missing source xo: $SOURCE_BUILD_DIR/${kernel}.xo" >&2
      exit 1
    fi
  done
}

write_state() {
  local run_id="$1"
  local run_dir="$2"
  local log_file="$3"
  local pid="$4"
  local launcher="$5"
  local unit_name="$6"

  mkdir -p "$STATE_DIR"
  cat > "$STATE_FILE" <<EOF
run_id=$run_id
run_dir=$run_dir
log_file=$log_file
xclbin=$run_dir/cinnamon_fpga.hw.xclbin
pid=$pid
launcher=$launcher
unit_name=$unit_name
started_at=$(date --iso-8601=seconds)
source_build_dir=$SOURCE_BUILD_DIR
EOF
  printf '%s\n' "$pid" > "$PID_FILE"
}

print_status() {
  local pid run_id run_dir log_file xclbin launcher unit_name status
  pid="$(cat "$PID_FILE" 2>/dev/null || true)"
  run_id="$(get_state run_id)"
  run_dir="$(get_state run_dir)"
  log_file="$(get_state log_file)"
  xclbin="$(get_state xclbin)"
  launcher="$(get_state launcher)"
  unit_name="$(get_state unit_name)"

  if [[ -z "$run_id" ]]; then
    echo "No detached run metadata found."
    exit 1
  fi

  if [[ "$launcher" == "systemd-run" && -n "$unit_name" ]]; then
    if systemctl --user is-active --quiet "$unit_name"; then
      status="running"
      pid="$(systemctl --user show "$unit_name" -p MainPID --value)"
    else
      status="stopped"
    fi
  elif is_pid_running "$pid"; then
    status="running"
  else
    status="stopped"
  fi
  echo "status=$status"
  echo "pid=${pid:-unknown}"
  echo "run_id=$run_id"
  echo "run_dir=$run_dir"
  echo "log_file=$log_file"
  echo "xclbin=$xclbin"
  echo "launcher=${launcher:-unknown}"
  if [[ -n "$unit_name" ]]; then
    echo "unit_name=$unit_name"
  fi
  if [[ -f "$xclbin" ]]; then
    echo "xclbin_present=yes"
  else
    echo "xclbin_present=no"
  fi
}

active_run_exists() {
  local launcher unit_name pid
  launcher="$(get_state launcher)"
  unit_name="$(get_state unit_name)"
  pid="$(cat "$PID_FILE" 2>/dev/null || true)"

  if [[ "$launcher" == "systemd-run" && -n "$unit_name" ]]; then
    systemctl --user is-active --quiet "$unit_name"
    return
  fi

  is_pid_running "$pid"
}

build_run_script() {
  local run_dir="$1"
  local log_file="$2"
  local run_script="$3"
  local var

  {
    echo "#!/usr/bin/env bash"
    echo "set -euo pipefail"
    printf 'export PATH=%q\n' "$PATH"
    if [[ -n "${LD_LIBRARY_PATH:-}" ]]; then
      printf 'export LD_LIBRARY_PATH=%q\n' "$LD_LIBRARY_PATH"
    fi
    for var in XILINX_VIVADO XILINX_HLS XILINX_VITIS; do
      if [[ -n "${!var:-}" ]]; then
        printf 'export %s=%q\n' "$var" "${!var}"
      fi
    done
    printf 'cd %q\n' "$ROOT_DIR"
    printf 'exec env BUILD_DIR=%q BUILD_STAGE=link %q >>%q 2>&1\n' \
      "$run_dir" "$BUILD_SCRIPT" "$log_file"
  } > "$run_script"

  chmod +x "$run_script"
}

start_with_systemd() {
  local run_script="$1"
  local unit_name="$2"
  local pid

  require_cmd systemd-run
  systemd-run --user --unit "$unit_name" --collect "$run_script" >/dev/null
  pid="$(systemctl --user show "$unit_name" -p MainPID --value)"
  printf '%s\n' "$pid"
}

start_with_nohup() {
  local run_script="$1"
  local pid

  require_cmd nohup
  require_cmd setsid
  nohup setsid "$run_script" >/dev/null 2>&1 < /dev/null &
  pid=$!
  printf '%s\n' "$pid"
}

start_run() {
  local pid run_id run_dir log_file run_script unit_name launcher kernel

  require_cmd cp

  mkdir -p "$STATE_DIR" "$RUNS_DIR"
  if active_run_exists; then
    echo "A detached run is already active."
    print_status
    exit 0
  fi

  ensure_source_xos

  run_id="$(date +%Y%m%d_%H%M%S)"
  run_dir="$RUNS_DIR/$run_id"
  log_file="$run_dir/hw50_link.log"
  run_script="$run_dir/run.sh"
  unit_name="hw50-link-${run_id}"
  mkdir -p "$run_dir"

  for kernel in "${KERNEL_NAMES[@]}"; do
    cp -f "$SOURCE_BUILD_DIR/${kernel}.xo" "$run_dir/${kernel}.xo"
  done

  build_run_script "$run_dir" "$log_file" "$run_script"

  if linger_enabled; then
    launcher="systemd-run"
    pid="$(start_with_systemd "$run_script" "$unit_name")"
  else
    launcher="nohup-setsid"
    unit_name=""
    pid="$(start_with_nohup "$run_script")"
  fi

  write_state "$run_id" "$run_dir" "$log_file" "$pid" "$launcher" "$unit_name"

  echo "Started detached 50MHz link run."
  print_status
}

show_log() {
  local log_file
  log_file="$(get_state log_file)"
  if [[ -z "$log_file" || ! -f "$log_file" ]]; then
    echo "No detached run log found." >&2
    exit 1
  fi
  exec tail -f "$log_file"
}

main() {
  local cmd="${1:-start}"
  case "$cmd" in
    start)
      start_run
      ;;
    status)
      print_status
      ;;
    log)
      show_log
      ;;
    *)
      usage >&2
      exit 1
      ;;
  esac
}

main "$@"
