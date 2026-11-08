#!/usr/bin/env bash
# Secondary experiment: does ORT 1.29's CoreML ModelCacheDirectory make the
# second fresh-process init cheaper? First launch populates the cache, second
# should hit it if the cache works.
set -uo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
MODEL_DIR="$DIR/../../../macos/build/engine/models"
BIN="$DIR/build/bench"
OUT="$DIR/results"
CACHE="$OUT/coreml_cache"
mkdir -p "$OUT" "$CACHE"

for launch in 1 2; do
    tag="coreml_cached_launch${launch}"
    echo "=== $tag $(date '+%H:%M:%S') ==="
    AGENTREDACTOR_ORT_LOG_LEVEL=warning \
    AGENTREDACTOR_COREML_CACHE_DIR="$CACHE" \
        "$BIN" coreml "$MODEL_DIR" "$DIR/input.txt" --warmups 2 --timed 5 \
        > "$OUT/${tag}.stdout" 2> "$OUT/${tag}.stderr"
    echo "exit=$? $(date '+%H:%M:%S')"
done
echo "cache dir contents:"
find "$CACHE" -type f | head -20
du -sh "$CACHE"
echo "CACHED DONE"
