#!/usr/bin/env bash
# Summarizes results/*.stdout into a compact table for the report.
set -uo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
R="$DIR/results"

for f in "$R"/*.stdout; do
    [ -f "$f" ] || continue
    tag="$(basename "$f" .stdout)"
    echo "== $tag =="
    grep -E "^(INIT_MS|ACTIVE_PROVIDER|RSS_AFTER_INIT_MB|RSS_END_MB|LAT_MEDIAN_MS|LAT_MIN_MS|LAT_MAX_MS|INPUT_BYTES|ENABLED_TYPES)=" "$f"
    grep -cE "^TIMED" "$f" | sed 's/^/timed_runs=/'
    grep -E "^(WARMUP|TIMED)" "$f"
    # entity counts should be identical across runs; surface any drift
    ents=$(grep -oE "ENTITIES=[0-9]+" "$f" | sort -u | tr '\n' ' ')
    echo "unique_entity_counts: $ents"
    stderr="$R/${tag}.stderr"
    if [ -s "$stderr" ]; then
        echo "stderr_lines=$(wc -l < "$stderr" | tr -d ' ')"
    else
        echo "stderr_lines=0"
    fi
done
