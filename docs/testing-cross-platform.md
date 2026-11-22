# Testing Agent Redactor on Linux and macOS

How to validate a change on Linux and macOS before merge — written for agents
and humans working on machines other than the primary Windows dev box.

Windows work follows [windows/AGENTS.md](../windows/AGENTS.md); Linux and
macOS builds are documented in [linux/README.md](../linux/README.md) and
[macos/README.md](../macos/README.md). This page is the cross-platform
*testing* checklist that ties them together.

## What to validate (per platform)

1. **Build** the engine/CLI (Linux: CMake per linux/README.md; macOS:
   `macos/fetch-deps.sh` + CMake per macos/README.md).
2. **Engine starts and the detector initializes.** With a fresh config dir,
   `agent_redactor.log` in the config dir must contain the startup lines:

   ```
   [PIIDetector] Intra-op threads=<N> physical_cores=<M> total_ram=... env_override=unset|set
   [PIIDetector] MEM host_total=... host_available=... suggested_arena_cap=... chunk_window=8192 chunk_context=512 chunk_margin=512
   [PIIDetector] Model loaded with provider: CPU|CoreML|...
   ```

   `<N>` is the adaptive thread budget: ~75% of physical cores, clamped to
   [2, 12], reduced to ≤ 4 when total RAM < 10 GiB;
   `AGENTREDACTOR_ORT_THREADS` overrides it when set.
3. **Test suites**:

   ```bash
   cd tests
   AGENTREDACTOR_ENGINE_BIN=<engine binary> python3 -m pytest cli -q
   ```

   Engine binary locations: Linux `linux/build/agentredactor` (or
   `linux/build/engine/agentredactor` depending on the CMake target run);
   macOS the dev binary staged by the CMake build. The CLI suite must pass
   with **no skips caused by a missing engine**.

   The chunking regression test (`tests/cli/test_pii_chunking.py`) sends a
   32,768-token PII-dense request through the real proxy and asserts:

   - every model Run stays within the 8,192-token window (`>>RUN seqlen=`
     log lines) — the guard against the unbounded-memory regression;
   - detection accuracy against the *injected ground truth* in the fixture
     (recall/precision floors). If this test fails after an unrelated
     change, detection accuracy changed — review before merging.

4. **Smoke a giant request manually** (optional, mirrors the test):
   point a profile at any HTTP endpoint, POST a large
   `/v1/chat/completions` body through the proxy, and check the log —
   `>>RUN seqlen=` must never exceed 8,192, and there must be one Run per
   ~7,168 owned tokens.

## Useful environment variables

| variable | meaning |
|---|---|
| `AGENTREDACTOR_CONFIG_DIR` | per-run isolated config dir (tests set this) |
| `AGENTREDACTOR_ENGINE_BIN` | engine binary the test suite drives |
| `AGENTREDACTOR_ORT_THREADS` | override the adaptive intra-op thread count |
| `AGENTREDACTOR_ORT_LOG_LEVEL` | ONNX Runtime log verbosity (verbose/info/warning) |
| `AGENTREDACTOR_HELLO_SUPPRESS_PROMPT` | test-only: Hello consent behaves as cancelled |

## Notes

- The model's ~1.6 GB `model_quantized.onnx_data` is required for the
  PII/accuracy tests; without it the engine degrades (regex/keywords only)
  and `test_pii_chunking.py` skips itself.
- Inference thread counts and chunk windows are startup-decided and logged;
  if a machine behaves oddly, the MEM/Intra-op log lines are the first thing
  to attach to a report.
