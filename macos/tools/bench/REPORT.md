# CoreML vs CPU execution provider — PII NER benchmark (macOS)

Date: 2026-10-08. Author: empirical trial via `macos/tools/bench/`.
Question: should the macOS engine enable ONNX Runtime's CoreML execution
provider (Neural Engine/GPU) instead of CPU for `model_quantized.onnx`?

## TL;DR

**Do not ship CoreML — keep CPU.** CoreML EP *is* present in the official
ORT 1.29.0 osx-arm64 build, and session creation succeeds, but with this
model every detection run fails inside a CoreML subgraph at inference time;
`PIIDetector::DetectPII` swallows the exception per chunk and returns
**zero entities**. The app would silently stop detecting PII. The more modern
Core ML 5 MLProgram variant fails even earlier (session creation). Latency
and memory are not better either. Revisit only if ORT's CoreML EP gains
`RotaryEmbedding`/bf16 support or the model is re-exported for CoreML.

## Environment

- Host: Mac14,3 (MacBook Pro 14" 2023), Apple M2, 8 cores (4P+4E), 16 GB RAM,
  macOS 26.6.2 (25G83).
- ONNX Runtime 1.29.0, official osx-arm64 tarball (pinned by
  `macos/fetch-deps.sh`, sha256 `d0706fc3…cf92`).
- Model: `macos/build/engine/models/onnx/model_quantized.onnx` +
  `model_quantized.onnx_data` (1,618,042,064 bytes — verified).
  OpenAI-privacy-filter token classifier, hidden 640, bf16 weights,
  `default_n_ctx` 128000; graph has 353 nodes incl. 80 `RotaryEmbedding`.
- Input: `input.txt`, 155,657 bytes of business prose, all 8 PII types seeded
  (`gen_input.py`, fixed seed) → one ~128k-token-budget chunk per detection
  (≈35–40k tokens), 2,570 entities found by the CPU path on every run.
- **Noise disclaimer:** ambient load average 3.1–5.5 on 8 cores throughout
  (Firefox, WindowServer, Spotlight, the kimi CLI itself, a dev
  AgentRedactor.app running). Runs were sequential; absolute numbers wobble
  ~±10% run to run, but the CPU/CoreML gap is qualitative, not noise.

## 1. Numbers

Steady state = 2 warm-up detections (discarded) + 5 timed detections of the
full 155 KB blob, per fresh process. Latency in ms; RSS = peak `ru_maxrss`.

| metric | CPU L1 | CPU L2 | CoreML L1 | CoreML L2 |
|---|---|---|---|---|
| init (cold) | 433 | 437 | 2,759 | 2,754 |
| active provider | CPU | CPU | CoreML | CoreML |
| RSS after init (MB) | 182 | 181 | 241 | 242 |
| timed latency median | 62,181 | 64,968 | 53,076 | 57,456 |
| timed latency min | 59,671 | 61,055 | 20,588 | 25,572 |
| timed latency max | 67,042 | 70,890 | 72,376 | 85,027 |
| **entities / run** | **2,570** | **2,570** | **0** | **0** |
| RSS end (MB) | 8,980 | 10,442 | 8,847 | 8,885 |

CoreML + `ModelCacheDirectory` (cross-process conversion cache):

| metric | cached L1 (writes cache) | cached L2 (cache hit) |
|---|---|---|
| init (cold) | 2,337 | **1,849** |
| latency median | 18,376 | 26,323 |
| latency min–max | 7,043–61,057 | 13,511–73,127 |
| entities / run | 0 | 0 |
| RSS end (MB) | 7,260 | 7,778 |

- Default CoreML (no cache dir): conversion output goes to a per-session
  temp dir deleted at close → **no cross-process reuse** (2,759 → 2,754 ms;
  measured, not assumed). With an explicit cache dir the second process's
  init drops ~21% (2,337 → 1,849 ms) and the cache is 58 MB of
  `*_dynamic_nn/compiled_model.mlmodelc` keyed by model hash.
- The attractive-looking CoreML "medians" are an artifact of the failure
  mode: runs are bimodal (7–28 s vs 53–114 s) depending on where in the
  graph the CoreML subgraph dies; CPU is a consistent 60–71 s. CoreML never
  completes a detection.
- CPU RSS peak grew 8.98 → 10.44 GB between identical launches — ORT's CPU
  arena growth is not deterministic; both figures exceed the ~4.7 GB
  suggested-arena-cap currently logged (observability-only) by
  `PIIDetector::Initialize`. Worth attention independent of this trial.

## 2. Is CoreML EP available in official ORT 1.29.0 osx-arm64?

**Yes.** `nm -gU` shows `OrtSessionOptionsAppendExecutionProvider_CoreML`
exported, and `Ort::GetAvailableProviders()` returns
`CoreMLExecutionProvider, WebGpuExecutionProvider, CPUExecutionProvider`.
No custom build needed; the Python `onnxruntime-coreml` alternative is moot
(not installed here; unnecessary since the C++ build already ships the EP).
`#include <coreml_provider_factory.h>` ships in the tarball's include dir.

## 3. Fallback / unsupported-op observations (stderr captures)

Every CoreML launch logged at WARNING:

```
[W:onnxruntime:, coreml_execution_provider.cc:137 GetCapability]
CoreMLExecutionProvider::GetCapability, number of partitions supported by
CoreML: 48  number of nodes in the graph: 353  number of nodes supported by
CoreML: 107
```

- Only 107/353 nodes are CoreML-compatible; the graph shatters into 48
  partitions around 246 CPU-resident nodes. The model's 80
  `RotaryEmbedding` nodes (attention position encoding) have no CoreML EP
  kernel; the remaining ~166 are shape/dynamic-dimension glue
  (`Range`, `Where`, `Sub/Div`, `Expand`, `Slice`, …) plus partition
  boundaries. Even if inference worked, 48 host↔CoreML handoffs around
  CPU-resident rotary attention would dominate.
- Every one of the 7 detections per launch then hit, once:

```
[E:onnxruntime:, sequential_executor.cc:671 ExecuteKernel] Non-zero status
code returned while running …_CoreML_…_2/…_8/…_26/…_32 node. Status Message:
Error executing model: Unable to compute the prediction using a neural
network model. … (error code: -1).
```

  `PIIDetector::DetectPII` catches `Ort::Exception` per chunk and skips it →
  the whole chunk contributes zero labels → **0 entities returned** (4/4
  fresh processes, plus 2 cached). At the app's default log level the ORT
  error does reach stderr, but nothing in the engine treats "detection
  failed" differently from "no PII found" — redaction would silently pass
  text through. This is the decisive finding: CoreML isn't just slower or
  heavier, it is a silent-correctness failure.
- Root cause is inside the converted CoreML models (bf16 activations/weights
  and/or dynamic sequence shapes — the cached artifacts are literally named
  `*_dynamic_nn`). ORT 1.29's converter accepts the nodes into partitions
  but the resulting `.mlmodel` cannot execute.
- `COREML_FLAG_CREATE_MLPROGRAM` (Core ML 5 program format, tested via the
  `AGENTREDACTOR_COREML_FLAGS` knob): `GetCapability` improves to 154/353
  nodes / 49 partitions, but session creation fails outright:
  `Failed to create MLModel … model.mil … error code: -14` — Initialize()
  returns false. Another dead end at default options.
- CPU launches: stderr contains only 10 benign
  `CleanUnusedInitializersAndNodeArgs` warnings — clean baseline.

## 4. Recommendation

**Keep CPU. Do not ship a CoreML setting (not as default, not as opt-in).**

Reasoning against the tempting options:

- *Default?* No — correctness blocker (silent zero-detection), no latency or
  memory win, and a 6× slower cold init (2.8 s vs 0.43 s; 1.85 s best-case
  with a warm on-disk cache that must then be managed and invalidated).
- *Opt-in setting?* Still no. An opt-in would hand users a footgun whose
  failure mode (fail-open: text passes through unredacted) is invisible in
  the UI. There is today no detection-health signal (entity-count sanity,
  error propagation out of `DetectPII`) that could gate it safely. Building
  that machinery for a provider that is also *not faster* is not justified.
- *Init cost is not the pain point anyway.* CPU cold init is 433 ms — fine
  for first-load UX, and this is a long-running daemon so it amortizes to
  zero. The cache-dir experiment was measured for completeness; its ~0.9 s
  saving is irrelevant now.

What actually matters for this app's reality (localhost proxy, per-request
latency, 16 GB budget):

1. **Per-request latency is CPU-bound in attention, not in provider choice.**
   62–65 s for a 155 KB blob (~35–40k tokens, single chunk, 4 ORT threads)
   would be the proxy's worst-case stall. The productive levers are chunk
   size / `TOKEN_OVERLAP` policy, intra-op thread count, and possibly
   small-input fast paths — not the execution provider.
2. **Memory is the tighter constraint, and it doesn't improve with CoreML**
   (8.8 vs 8.9–10.4 GB peak). Implementing the already-logged bounded CPU
   arena cap (`GetSuggestedArenaBytes`, ~60% of available RAM, capped 8 GiB —
   ~4.7 GB on this host) matters more than provider choice; measured peaks
   exceed even that.
3. **If GPU/NPU inference is ever revisited:** prerequisites are (a) ORT
   CoreML EP support for `RotaryEmbedding` and bf16 (or a float16 re-export
   of the model without rotary fusion), (b) a detection-health/error
   contract in `PIIDetector` so EP failures fail closed, and (c) re-running
   this bench harness — everything needed is in `macos/tools/bench/`.

## 5. Methodology / artifacts

- Core change (additive, `#ifdef __APPLE__` only, CPU default
  byte-identical): `core/src/pii_detector.cpp` — explicit `SetProvider(
  L"coreml")` wires `OrtSessionOptionsAppendExecutionProvider_CoreML` with
  CPU fallback + log; diagnostics knobs `AGENTREDACTOR_ORT_LOG_LEVEL`
  (verbose|info|warning), `AGENTREDACTOR_COREML_CACHE_DIR`,
  `AGENTREDACTOR_COREML_FLAGS`. `preferredProvider_ == L"auto"` still takes
  the exact former CPU path.
- Harness: `macos/tools/bench/{bench.cpp,build.sh,gen_input.py,
  run_bench.sh,run_cache_bench.sh,parse_results.sh,providers.cpp}`.
  Bench links `macos/build/core/libagentredactor-core.a` + the engine's
  `engine_loc.cpp` shim and drives the unmodified production
  `DetectPII` path (tokenizer → session → viterbi → merge).
- Raw logs: `macos/tools/bench/results/*.stdout|stderr|_load.txt`.
- Protocol per (provider, launch): fresh process → time `Initialize()` →
  RSS → 2 warm-up detections → 5 timed detections (median/min/max) → RSS.
  `AGENTREDACTOR_ORT_LOG_LEVEL=warning` on all timed runs.
