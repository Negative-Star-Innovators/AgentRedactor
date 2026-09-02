# Sparse-attention re-export of openai/privacy-filter

Tooling and report for the banded-attention rewrite of the shipped
`onnx/model_quantized.onnx`. See `REPORT.md` for results; the canonical
artifact produced here is a drop-in replacement `.onnx` graph that computes
the ±128-token banded attention structurally (linear memory) instead of
densely (quadratic memory).

## Layout

- `make_sparse_model.py` — rewrites the shipped graph in place-structure:
  per layer the N×N `MatMul → mask add → sink concat → Softmax → A·V`
  core is replaced by an exact windowed-block decomposition (S=256 query
  tokens per block, gathered K/V window of W=S+256, additive band +
  key-padding masks, per-head sink logit preserved). Pure standard ONNX
  ops (opset ≤ 21) plus the contrib ops the model already used.
  Weights and the external-data layout are **untouched** — the existing
  `model_quantized.onnx_data` stays byte-identical and is referenced as-is.
- `validate_sparse.py` — parity vs the dense original on PII-dense documents
  (500–16000 tokens), boundary entities, padded tail. Writes
  `validation_report.json`.
- `smoke_parity.py` — fast random-token parity sweep at N = 1…2000.
- `measure_memory.py` — per-process RSS sweep (dense to 16K, sparse to 64K).
  Writes `memory_report.json`.

Scripts run from the repo root. They compare the vendored
`windows/models/onnx/model_quantized.onnx` (now the sparse build) against
the preserved dense original at `_sparse_export/dense_original/`
(scratch, git-ignored).

## Why not ORT's SparseAttention op

The op's CPU kernel exists in shippable ORT (verified in 1.24.4:
`onnxruntime::contrib::SparseAttention<float>` is compiled into the CPU
build), but the schema
(`onnxruntime/core/graph/contrib_ops/bert_defs.cc`, v1.24.4) states
"Only supports unidirectional attention with cache of past key and value in
linear buffers", and the op has no attention-sink input. privacy-filter
needs a *bidirectional* ±128 band and per-head sinks, so the documented
fallback — exact in-graph block decomposition — is the path taken.

## Correctness sketch

For query block b covering tokens [bS, bS+S), the gathered K/V window is
padded-coords [bS, bS+W), i.e. tokens [bS−128, bS+S+128): exactly the
∪ of the ±128 bands of every query in the block. Within the window, query
row with local index s attends window column w iff s ≤ w ≤ s+256, which is
|i−j| ≤ 128 with i = bS+s, j = bS−128+w — the same predicate as the dense
`Abs(i−j) <= 128` mask. The engine's `attention_mask` is zero-padded with
the same halo and gathered with the same indices, so padding and masked
keys behave exactly as in the dense graph (additive −3.4e38 before softmax,
sink column appended, sliced off after). GQA is folded into the score rows
(14 q-heads = 2 kv-heads × 7), matching the original `repeat_kv` head order
h → kv-head h//7. Padded query rows are sliced off before o_proj.
