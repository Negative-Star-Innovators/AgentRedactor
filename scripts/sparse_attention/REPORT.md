# Sparse-attention re-export — validation report

**Task**: re-export `openai/privacy-filter` so the ±128-token banded attention
is computed structurally (linear memory) instead of as a dense N×N
MatMul+Softmax with a mask (quadratic memory, ~220 bytes/token² at fp32,
OOM on CPU for large requests).

## Path chosen

Step-0 feasibility check on ORT's `SparseAttention` contrib op
(`com.microsoft`, schema in `onnxruntime/core/graph/contrib_ops/bert_defs.cc`,
v1.24.4 — verified the CPU float/fp16 kernels are compiled into the shippable
CPU build):

> "Only supports unidirectional attention with cache of past key and value in
> linear buffers."

privacy-filter attention is **bidirectional** (`Abs(i−j) ≤ 128` band, from the
shipped graph's `Range/Sub/Abs/LessOrEqual` mask) and appends a **per-head
attention-sink logit** before softmax — neither is expressible with
`SparseAttention`. Verdict: **op semantics insufficient even though the CPU
kernel exists** → the documented fallback, exact windowed-block decomposition
with pure standard ONNX ops, was implemented (`make_sparse_model.py`).

Construction: S=256 queries per block; K/V padded by 128 on both sides and
gathered per block into a fixed W=512 window covering exactly the union of
the block's ±128 bands; in-window predicate `s ≤ w ≤ s+256` ≡ `|i−j| ≤ 128`;
the runtime `attention_mask` is zero-padded identically and gathered with the
same indices; per-head sinks appended pre-softmax and sliced off post-softmax;
GQA folded into score rows (14 q-heads = 2 kv-heads × 7, same head order as
the original `repeat_kv`). Weights, quant tensors and the external-data file
are **untouched** — the graph references the original
`model_quantized.onnx_data` byte-for-byte.

## Validation vs the official `model_quantized.onnx`

ORT 1.24.4, CPUExecutionProvider, fp32 activations, real tokenizer
(`windows/models/tokenizer.json`), PII-dense synthetic documents.

| case | tokens | pad | label mismatches | entity (non-`O`) disagreements | max abs logit diff |
|---|---|---|---|---|---|
| pii_dense | 500 | 0 | 0 | 0 | 1.3e-05 |
| pii_dense | 2000 | 0 | 0 | 0 | 1.9e-05 |
| pii_dense | 4000 | 0 | 0 | 0 | 1.9e-05 |
| pii_dense | 8000 | 0 | 0 | 0 | 2.3e-05 |
| pii_dense | 12000 | 0 | 0 | 0 | 2.7e-05 |
| pii_dense | 16000 | 0 | 0 | 0 | 2.3e-05 |
| boundary_entities | 6000 | 0 | 0 | 0 | 2.1e-05 |
| padded_tail | 3000 | 200 | 0 | 0 | 2.2e-05 |

Plus a random-token sweep at N = 1…2000 including block-boundary lengths
(255/256/257, 511/512/513) — labels exact at every N.

- **Predicted labels match exactly in every case** (the redaction-deciding
  `O` vs non-`O` boundary is bit-identical too).
- Logit deltas ≤ ~2.7e-5 — fp32 matmul tiling noise, far inside quantization
  tolerance (the shipped weights are 8-bit block-32).
- Boundary case: entities were placed so they straddle ±128 band edges and
  256-token block edges at every multiple of 64 tokens.
- The padded-tail case exercises the key-padding mask path (the engine always
  sends an all-ones mask, but the graph semantics are preserved regardless).

## Memory scaling (CPU, RSS of a fresh inference process)

Measured on a 94 GB RAM Windows/x64 box, ORT 1.24.4, one inference per
process, peak RSS sampled at 10 Hz (`measure_memory.py`, raw data in
`memory_report.json`):

| tokens | dense (shipped) | sparse (new) | dense time | sparse time |
|---|---|---|---|---|
| 1,024 | 1.75 GiB | 1.61 GiB | 2.8 s | 2.5 s |
| 4,096 | 4.80 GiB | 2.11 GiB | 8.4 s | 4.4 s |
| 8,192 | 14.46 GiB | — | 23.2 s | — |
| 12,288 | 28.70 GiB | — | 48.4 s | — |
| 16,384 | 50.14 GiB | 3.55 GiB | 75.8 s | 10.1 s |
| 65,536 | infeasible (≈200 GiB extrapolated) | 11.26 GiB | — | 38.0 s |

Marginal peak-RSS per additional token between successive sizes:

- **dense: 1.06 → 2.53 → 3.73 → 5.62 MB/token** — grows linearly with N,
  i.e. memory is quadratic in N (≈210 bytes/token², matching the ~220
  bytes/token² from the original investigation; 26K tokens ≈ 140 GB).
- **sparse: 0.18 → 0.13 → 0.17 MB/token** — flat, i.e. memory is linear in N
  (~150 KB/token all-in transient; 64K tokens ≈ 11 GiB, 128K ≈ 20 GiB
  extrapolated — inside a 32 GB machine's budget).

## Integration notes

- **Delivered in this branch**: `windows/models/onnx/model_quantized.onnx`
  has been replaced with the validated sparse graph (same filename, same
  external weights file, same I/O signature). The dense original is
  preserved (git-ignored) at `_sparse_export/dense_original/` for
  re-validation.

- **Drop-in**: keep the filename `onnx/model_quantized.onnx` and place the
  **existing** `model_quantized.onnx_data` next to it. Engine loads it via
  `core/src/pii_detector.cpp` (`MODEL_FILENAME`) unchanged.
- **`kWeightsExpectedBytes` unchanged**: the weights file is byte-identical
  (1,618,042,064 B) — `core/include/model_downloader.h` stays as-is, and
  **no R2 re-upload is needed** (only `.onnx_data` is downloaded from R2;
  the `.onnx` graph is a bundled companion file, so it ships with the
  installer/AppImage).
- **ORT version**: no bump required. The rewrite adds only standard ONNX ops
  (opset ≤ 21: Pad, Gather, Reshape, Transpose, MatMul, Add, Concat, Softmax,
  Slice, Where, Range, Mod, Expand, Equal, Unsqueeze) on top of the contrib
  ops the model already used (MatMulNBits, QMoE, RotaryEmbedding,
  GatherBlockQuantized, SkipSimplifiedLayerNormalization). Validated on ORT
  1.24.4; Linux CI already pins 1.29.0 and Windows pulls via vcpkg — both
  cover these.
- **Upgrade delivery** (`core/src/model_downloader.cpp` + `core/src/engine_app.cpp`):
  companion files (graph, tokenizer, config, calibration) were previously
  copied to the fallback model dir only when missing, and only on the
  first-run download path — so Velopack/AppImage upgrades would have kept
  the old dense graph forever (MSIX was unaffected: it bundles the weights
  and loads the graph from the install dir). The companion copy is now
  refresh-if-different (byte compare, temp+rename) and `EnsureModelFiles`
  is called at startup whenever the model loads from the user-writable
  fallback dir, so every upgrade picks up the new graph. R2 is untouched:
  it only ever served `model_quantized.onnx_data`, never the graph.
- **Chunking**: `MAX_TOKENS_PER_CHUNK`/`TOKEN_OVERLAP` in
  `core/include/constants.h` (128000/128) can stay — a 128K-token single
  inference now needs ~tens of GB of transient activations instead of
  terabytes. Whether to keep or shrink chunking is an app-side decision,
  explicitly out of scope here.
- Activations are fp32 throughout, matching the current model (fp16 was not
  needed to hit linear memory).

## Caveats

- Per-token compute grows vs dense for very short inputs (each query block
  attends a fixed 512-wide window): negligible in absolute terms at the
  engine's batch-1 latency, and massively cheaper than dense beyond ~1K
  tokens.
- Validated on Windows/x64 CPU. The graph is plain ONNX — Linux behaves
  identically modulo ORT version, but a Linux-run sanity pass is
  recommended before release.
