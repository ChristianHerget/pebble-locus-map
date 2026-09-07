#!/bin/bash
# The checks run in their own session; this supervisor never invokes them in an
# `if`/`||` context, which would disable Bash errexit inside shell functions.
check_stage() {
  if [[ -n "${CHECK_PASS:-}" && "$1" == @(emery|gabbro)-* ]]; then
    printf '\036stage\t%s-pass-%s\n' "$1" "$CHECK_PASS"
    return
  fi
  printf '\036stage\t%s\n' "$1"
}

check_result() {
  printf '\036result\t%s\n' "$1"
}

check_plan() {
  local stage
  for stage in "$@"; do
    printf '\036plan\t%s\n' "$stage"
  done
}

check_diagnostic_candidates() {
  local directory
  : > "$CHECK_LOG_DIR/failure-files.txt" || return 74
  for directory in "$CHECK_LOG_DIR/reports" "$CHECK_LOG_DIR/diagnostics"; do
    [[ -d "$directory" ]] || continue
    find "$directory" -type f -name '*.xml' >> "$CHECK_LOG_DIR/failure-files.txt" || return 74
  done
  if [[ -d "$CHECK_LOG_DIR/diagnostics" ]]; then
    find "$CHECK_LOG_DIR/diagnostics" -type f \( -name '*.log' -o -name '*.txt' \) \
      >> "$CHECK_LOG_DIR/failure-files.txt" || return 74
  fi
}

check_report() {
  set -euo pipefail
  local_root=$1
  shift
  mkdir -p "$local_root/build/check-logs"
  CHECK_LOG_DIR=$(mktemp -d "$local_root/build/check-logs/$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")
  export CHECK_LOG_DIR
  printf '%s\n' "$CHECK_LOG_DIR" > "$local_root/build/check-logs/latest"
  : > "$CHECK_LOG_DIR/combined.log"
  : > "$CHECK_LOG_DIR/manifest.tsv"
  : > "$CHECK_LOG_DIR/runs.txt"
  : > "$CHECK_LOG_DIR/started"
  awk_options=()
  # mawk otherwise waits for a full input buffer before reporting stage starts.
  if awk -W version 2>&1 | grep -q mawk; then
    awk_options=(-W interactive)
  fi
  printf 'Logs: %s\n' "$CHECK_LOG_DIR"
  capture_pid='' heartbeat_pid='' check_pid=''
  transport=$(mktemp -d "${TMPDIR:-/tmp}/trackglance-report.XXXXXX")
  stop_reporting() {
    local status=$?
    trap - EXIT INT TERM HUP
    if [[ -n "$check_pid" ]]; then
      kill -TERM -- "-$check_pid" 2>/dev/null || true
      wait "$check_pid" 2>/dev/null || true
    fi
    if [[ -n "$heartbeat_pid" ]]; then
      kill -TERM -- "-$heartbeat_pid" 2>/dev/null || true
      wait "$heartbeat_pid" 2>/dev/null || true
    fi
    if [[ -n "$capture_pid" ]]; then
      wait "$capture_pid" || { (( status != 0 )) || status=74; }
    fi
    rm -f "$transport/input"
    rmdir "$transport"
    printf '%s\n' "$status" > "$CHECK_LOG_DIR/status" || { (( status != 0 )) || status=74; }
    # Nested checks can replace this pointer, including with a container-only path.
    # Publish the finishing invocation's directory for host-side diagnostic replay.
    printf '%s\n' "$CHECK_LOG_DIR" > "$local_root/build/check-logs/latest" \
      || { (( status != 0 )) || status=74; }
    exit "$status"
  }
  trap stop_reporting EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  trap 'exit 129' HUP
  mkfifo "$transport/input"
  (
    set -o pipefail
    tee "$CHECK_LOG_DIR/combined.log" < "$transport/input" |
      LC_ALL=C awk "${awk_options[@]}" -v directory="$CHECK_LOG_DIR" -v verbose="${CHECK_VERBOSE:-false}" \
        -f "$local_root/tools/podman/check-report.awk"
  ) &
  capture_pid=$!
  setsid bash -c '
    while sleep "${CHECK_HEARTBEAT_SECONDS:-60}"; do
      stage=$(cat "$CHECK_LOG_DIR/active" 2>/dev/null) || stage=starting
      printf "[active] %s\n" "$stage"
    done
  ' &
  heartbeat_pid=$!
  setsid env CHECK_REPORT_ACTIVE=1 "$@" > "$transport/input" 2>&1 &
  check_pid=$!
  status=0
  wait "$check_pid" || status=$?
  check_pid=
  wait "$capture_pid" || { (( status != 0 )) || status=74; }
  capture_pid=
  sync -f "$CHECK_LOG_DIR/combined.log" || { (( status != 0 )) || status=74; }
  printf '%s\n' "$status" > "$CHECK_LOG_DIR/status" || { (( status != 0 )) || status=74; }
  exit "$status"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  check_report "$@"
fi
