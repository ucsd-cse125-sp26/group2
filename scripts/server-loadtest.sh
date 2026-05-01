#!/usr/bin/env bash
# scripts/server-loadtest.sh
#
# Reproducible load-test harness for PR-1 (server-perf-design).
#
# Spawns the server (with GROUP2_SERVER_PROFILE=1 + CSV output) and the
# clientbot fleet (GROUP2_BOT_FLEET_RTT=1 + CSV output), captures both
# log streams, and produces a self-contained timestamped folder under
# `./loadtest-runs/` that can be diffed across PRs.
#
# Usage:
#   scripts/server-loadtest.sh [<numBots>] [<durationSec>] [<preset>]
#
# Defaults: 50 bots, 90 seconds, relwithdebinfo preset.
#
# Notes:
#   - Both server + bots run on this host. Pings are RTT through the
#     loopback interface; values < 1 ms are normal at low N.
#   - We do NOT inject simulated latency (per spec §1).
#   - Cleanup: always kills the server + bots on exit, even on Ctrl+C.
#   - Idempotent: each run gets a fresh timestamped folder, no overwrite.

set -u
set -o pipefail

NUM_BOTS="${1:-50}"
DURATION_S="${2:-90}"
PRESET="${3:-relwithdebinfo}"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/${PRESET}"

if [[ ! -x "${BUILD_DIR}/server" ]] || [[ ! -x "${BUILD_DIR}/clientbot" ]]; then
    echo "[loadtest] ERROR: missing binaries in ${BUILD_DIR}/" >&2
    echo "[loadtest]        run \`cmake --build --preset ${PRESET} --target server clientbot\` first" >&2
    exit 1
fi

TS="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="${REPO_ROOT}/loadtest-runs/${TS}-N${NUM_BOTS}-${PRESET}"
mkdir -p "${RUN_DIR}"

SERVER_LOG="${RUN_DIR}/server.log"
SERVER_CSV="${RUN_DIR}/server-perf.csv"
BOT_LOG="${RUN_DIR}/bots.log"
BOT_CSV="${RUN_DIR}/bots-fleet-rtt.csv"
SUMMARY="${RUN_DIR}/summary.txt"

echo "[loadtest] run dir: ${RUN_DIR}"
echo "[loadtest] N=${NUM_BOTS} duration=${DURATION_S}s preset=${PRESET}"

# `stopProcess pid label`: SIGINT for graceful shutdown; SIGKILL after a
# 5 s grace period if the process didn't exit. Returns immediately if
# `pid` is empty or already exited.
stopProcess() {
    local pid="$1"
    local label="$2"
    if [[ -z "${pid}" ]] || ! kill -0 "${pid}" 2>/dev/null; then
        return 0
    fi
    kill -INT "${pid}" 2>/dev/null || true
    for i in 1 2 3 4 5; do
        if ! kill -0 "${pid}" 2>/dev/null; then
            return 0
        fi
        sleep 1
    done
    echo "[loadtest] WARNING: ${label} (pid=${pid}) didn't exit on SIGINT, sending SIGKILL"
    kill -KILL "${pid}" 2>/dev/null || true
    wait "${pid}" 2>/dev/null || true
}

cleanup() {
    local code=$?
    echo "[loadtest] cleanup (exit=${code})..."
    stopProcess "${BOTS_PID:-}"   bots
    stopProcess "${SERVER_PID:-}" server
    return ${code}
}
trap cleanup EXIT INT TERM

# ── Server ─────────────────────────────────────────────────────────────
(
    cd "${BUILD_DIR}" && \
    GROUP2_SERVER_PROFILE=1 \
    GROUP2_SERVER_PROFILE_CSV="${SERVER_CSV}" \
    ./server > "${SERVER_LOG}" 2>&1
) &
SERVER_PID=$!

# Wait for the server to bind. The server loads + V-HACD-decomposes map
# assets *before* binding the listen socket, which can take several
# seconds on a fresh build dir. Poll the port instead of guessing.
SERVER_PORT="${SERVER_PORT:-9999}"
for attempt in $(seq 1 60); do
    # `bash` 4.0+ exposes /dev/tcp/host/port for a synchronous probe.
    # `(... 2>/dev/null) </dev/null` swallows the connection failure
    # message; the redirect form returns 0 only on successful connect.
    if (echo > "/dev/tcp/127.0.0.1/${SERVER_PORT}") 2>/dev/null; then
        echo "[loadtest] server is up after ${attempt}s"
        break
    fi
    if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
        echo "[loadtest] ERROR: server died during boot — see ${SERVER_LOG}" >&2
        exit 2
    fi
    sleep 1
done

# ── Bots ───────────────────────────────────────────────────────────────
(
    cd "${BUILD_DIR}" && \
    GROUP2_BOT_FLEET_RTT=1 \
    GROUP2_BOT_FLEET_RTT_CSV="${BOT_CSV}" \
    ./clientbot "${NUM_BOTS}" > "${BOT_LOG}" 2>&1
) &
BOTS_PID=$!

# ── Wait, then tear down. ──────────────────────────────────────────────
echo "[loadtest] running for ${DURATION_S}s..."
sleep "${DURATION_S}"

# Stop bots first (let them exit cleanly so server sees disconnects).
echo "[loadtest] stopping bots..."
stopProcess "${BOTS_PID}" bots
BOTS_PID=""

# Then server.
echo "[loadtest] stopping server..."
stopProcess "${SERVER_PID}" server
SERVER_PID=""

# ── Summary ────────────────────────────────────────────────────────────
{
    echo "loadtest summary"
    echo "================"
    echo "timestamp: ${TS}"
    echo "preset:    ${PRESET}"
    echo "N bots:    ${NUM_BOTS}"
    echo "duration:  ${DURATION_S}s"
    echo
    echo "Last 20 server perf lines"
    echo "-------------------------"
    grep -E '^\[perf ' "${SERVER_LOG}" | tail -20 || true
    echo
    echo "Last 10 fleet RTT lines"
    echo "-----------------------"
    grep -E '^\[fleet rtt' "${BOT_LOG}" | tail -10 || true
} | tee "${SUMMARY}"

echo "[loadtest] done — see ${RUN_DIR}/"
trap - EXIT INT TERM
