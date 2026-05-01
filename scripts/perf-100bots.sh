#!/usr/bin/env bash
# Perf harness: server + 100 bots + 1 real client (bench mode), then teardown.
#
# Usage:
#   bash scripts/perf-100bots.sh [BOTS] [SECONDS] [BUILD_DIR]
#     BOTS       — number of bot clients (default 100)
#     SECONDS    — bench duration in seconds (default 15; first 5s warm-up
#                  before percentiles stabilize, then 10s of measurement)
#     BUILD_DIR  — preset build dir (default build/release)
#
# Reads: build/<preset>/{server,clientbot,group2} binaries.
# Writes: bench-<timestamp>.log next to the script.
# Exits non-zero if any binary is missing or the client doesn't print
# the [bench] summary line.

set -uo pipefail

BOTS="${1:-100}"
SECONDS_TO_RUN="${2:-15}"
BUILD_DIR="${3:-build/release}"

cd "$(dirname "$0")/.."

if [[ ! -x "$BUILD_DIR/server" || ! -x "$BUILD_DIR/clientbot" || ! -x "$BUILD_DIR/group2" ]]; then
    echo "perf-100bots: missing binaries in $BUILD_DIR — build first" >&2
    exit 1
fi

LOG_DIR="$(pwd)/scripts"
TS="$(date +%Y%m%d-%H%M%S)"
LOG="$LOG_DIR/bench-$TS.log"

echo "[perf] bots=$BOTS seconds=$SECONDS_TO_RUN log=$LOG"

# Ensure no leftover instances from a previous aborted run.
pkill -f "$BUILD_DIR/server"   >/dev/null 2>&1 || true
pkill -f "$BUILD_DIR/clientbot" >/dev/null 2>&1 || true
pkill -f "$BUILD_DIR/group2"   >/dev/null 2>&1 || true
# TCP TIME_WAIT can block the port for ~60s; force-clear by killing any
# process still bound to 9999.  fuser exits non-zero if nothing was bound.
fuser -k 9999/tcp >/dev/null 2>&1 || true
# Wait for sockets to actually close (TIME_WAIT cycles through within a few
# seconds once the binding process is gone, since the bench server uses
# SO_REUSEADDR if available).
for _ in {1..20}; do
    if ! ss -tnl 2>/dev/null | grep -q ":9999 "; then break; fi
    sleep 0.2
done

cleanup() {
    echo "[perf] cleanup"
    [[ -n "${CLIENT_PID:-}" ]] && kill "$CLIENT_PID" 2>/dev/null || true
    [[ -n "${BOT_PID:-}"    ]] && kill "$BOT_PID"    2>/dev/null || true
    [[ -n "${SERVER_PID:-}" ]] && kill "$SERVER_PID" 2>/dev/null || true
    sleep 0.3
    pkill -f "$BUILD_DIR/server"   >/dev/null 2>&1 || true
    pkill -f "$BUILD_DIR/clientbot" >/dev/null 2>&1 || true
    pkill -f "$BUILD_DIR/group2"   >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

# 1. Server (background) — wait until it logs "listening"
"$BUILD_DIR/server" >>"$LOG" 2>&1 &
SERVER_PID=$!
for _ in {1..50}; do
    if grep -q "Server: listening" "$LOG" 2>/dev/null; then break; fi
    sleep 0.1
done
if ! grep -q "Server: listening" "$LOG"; then
    echo "[perf] FAILED — server never logged 'listening'" >&2
    exit 3
fi

# 2. Bots (background) — single process spawning N threads
"$BUILD_DIR/clientbot" "$BOTS" 127.0.0.1:9999 >>"$LOG" 2>&1 &
BOT_PID=$!
# Wait until all bots are reported connected, or fail.
for _ in {1..100}; do
    if grep -q "$BOTS/$BOTS bots connected" "$LOG" 2>/dev/null; then break; fi
    sleep 0.1
done

# 3. Real client in bench mode (foreground, but with a timeout safety net)
echo "[perf] launching real client (BENCH_SECONDS=$SECONDS_TO_RUN)"
BENCH_SECONDS="$SECONDS_TO_RUN" timeout "$((SECONDS_TO_RUN + 30))" \
    "$BUILD_DIR/group2" 2>>"$LOG" >>"$LOG" &
CLIENT_PID=$!
wait "$CLIENT_PID"
CLIENT_RC=$?

# 4. Extract the [bench] line and surface it.
SUMMARY=$(grep -E '^\[bench\] elapsed=' "$LOG" | tail -1 || true)
if [[ -z "$SUMMARY" ]]; then
    echo "[perf] FAILED — no [bench] summary in $LOG (client exit=$CLIENT_RC)" >&2
    tail -40 "$LOG" >&2
    exit 2
fi

echo "[perf] $SUMMARY"
echo "[perf] full log: $LOG"
