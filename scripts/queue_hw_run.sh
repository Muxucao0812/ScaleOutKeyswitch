#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/queue_hw_run.sh [options] -- <command> [args...]

Options:
  --wait-pid <pid>      Wait for this PID to exit before first run.
  --poll-seconds <sec>  Poll interval in seconds (default: 30).
  --max-retries <n>     Max busy retries; 0 means unlimited (default: 0).

Behavior:
  1) Optionally wait until --wait-pid exits.
  2) Run the command.
  3) If output contains "Device or resource busy" / "Xclbin on card is in use"
     / "failed to load xclbin", sleep and retry automatically.
EOF
}

WAIT_PID=""
POLL_SECONDS=30
MAX_RETRIES=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --wait-pid)
      WAIT_PID="${2:-}"
      shift 2
      ;;
    --poll-seconds)
      POLL_SECONDS="${2:-}"
      shift 2
      ;;
    --max-retries)
      MAX_RETRIES="${2:-}"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ $# -eq 0 ]]; then
  echo "Missing command after --" >&2
  usage
  exit 2
fi

if [[ -n "$WAIT_PID" ]]; then
  if ! [[ "$WAIT_PID" =~ ^[0-9]+$ ]]; then
    echo "--wait-pid must be an integer, got: $WAIT_PID" >&2
    exit 2
  fi
  while kill -0 "$WAIT_PID" 2>/dev/null; do
    echo "[$(date '+%F %T')] pid $WAIT_PID still running; wait ${POLL_SECONDS}s..."
    sleep "$POLL_SECONDS"
  done
  echo "[$(date '+%F %T')] pid $WAIT_PID exited; starting queued command."
fi

ATTEMPT=0
while true; do
  ATTEMPT=$((ATTEMPT + 1))
  LOG_FILE="$(mktemp -t queue-hw-run.XXXXXX.log)"
  echo "[$(date '+%F %T')] attempt #$ATTEMPT: $*"
  echo "log: $LOG_FILE"

  set +e
  "$@" > >(tee -a "$LOG_FILE") 2> >(tee -a "$LOG_FILE" >&2)
  RC=$?
  set -e

  if [[ $RC -eq 0 ]]; then
    echo "[$(date '+%F %T')] command finished successfully."
    exit 0
  fi

  if grep -Eiq "Device or resource busy|Xclbin on card is in use|failed to load xclbin" "$LOG_FILE"; then
    if [[ "$MAX_RETRIES" -gt 0 && "$ATTEMPT" -ge "$MAX_RETRIES" ]]; then
      echo "[$(date '+%F %T')] busy persists; reached max retries ($MAX_RETRIES)." >&2
      exit "$RC"
    fi
    echo "[$(date '+%F %T')] device busy; retry in ${POLL_SECONDS}s..."
    sleep "$POLL_SECONDS"
    continue
  fi

  echo "[$(date '+%F %T')] command failed with non-busy error; stop retry." >&2
  exit "$RC"
done
