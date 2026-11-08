# Trial 2 brief (restart instructions)

If Trial 2 was interrupted (session terminated / agent lost), re-run from
here. Working copies may still exist in `/tmp/coreml-exp/` — check there
first (survives session kills; wiped on reboot).

## Setup

```bash
python3.12 -m venv /tmp/coreml-exp-venv
/tmp/coreml-exp-venv/bin/pip install coremltools onnxruntime onnx numpy
```

## Model

`/Users/ursusprizlius/Documents/Projects/AgentRedactor/macos/build/engine/models/onnx/model_quantized.onnx`
(int8 token-classification transformer, 33 NER labels; config:
`windows/models/config.json`.) Verify weights size 1618042064 if re-fetching.

## Protocol

1. Inspect ONNX inputs/outputs (names, shapes, dynamic axes).
2. Convert with coremltools:
   a. `ct.convert(..., minimum_deployment_target=ct.target.macOS14)`
   b. plus `compute_precision=ct.precision.FLOAT16` variant (ANE prefers fp16)
   c. flexible sequence length via `ct.RangeDim(1, 4096)` if dynamic axes fail
   d. fallback routes if direct fails: onnxsim then convert; legacy
      `coremltools.converters.onnx`.
   Keep every attempt's errors verbatim.
3. Fidelity vs ORT CPU: 30 random token-id sequences (vocab clamp per
   config.json), lengths {128, 512, 2048}; feed identical inputs to both;
   report max/mean abs logit diff, argmax agreement % (frame + exact).
   Headline failure if argmax agreement < 90%.
4. Timing: lengths {128, 512, 2048, 8192}, 3 warmups + 20 timed, interleaved
   ort-cpu vs coreml (`compute_units=ALL`, and CPU_AND_GPU if ALL fails);
   median/min/max.
5. Write verdict here in PROGRESS.md (converted+faithful+faster /
   converted-but-unfaithful / failed-why) and move artifacts from /tmp into
   `trial2-coremltools/` (scripts, logs, report; NOT the venv).
