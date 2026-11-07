#!/usr/bin/env bash
# Full CoreML-vs-CPU bench protocol. Each (provider, launch) pair runs in its
# OWN fresh process: cold session init (CoreML graph conversion included for
# the coreml provider), 2 discarded warm-up detections, 5 timed detections,
# and peak-RSS (getrusage ru_maxrss) after init and at process end.
# Launches run strictly sequentially to avoid self-interference; ambient
# machine load is snapshotted around each launch for the noise discussion.
set -uo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
MODEL_DIR="$DIR/../../../macos/build/engine/models"
BIN="$DIR/build/bench"
OUT="$DIR/results"
mkdir -p "$OUT"

run_one() {
    local provider="$1" launch="$2"
    local tag="${provider}_launch${launch}"
    echo "=== $tag $(date '+%H:%M:%S') ==="
    {
        echo "--- pre-load ---"
        sysctl -n vm.loadavg
        uptime
    } > "$OUT/${tag}_load.txt"
    AGENTREDACTOR_ORT_LOG_LEVEL=warning \
        "$BIN" "$provider" "$MODEL_DIR" "$DIR/input.txt" --warmups 2 --timed 5 \
        > "$OUT/${tag}.stdout" 2> "$OUT/${tag}.stderr"
    echo "exit=$? $(date '+%H:%M:%S')"
    {
        echo "--- post-load ---"
        sysctl -n vm.loadavg
        uptime
    } >> "$OUT/${tag}_load.txt"
}

# A token gesture toward quieting the machine: nothing else is started by this
# script; ambient load (Firefox, WindowServer, etc.) is reported, not hidden.
run_one cpu 1
run_one cpu 2
run_one coreml 1
run_one coreml 2

echo "ALL DONE"
