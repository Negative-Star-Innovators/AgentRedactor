# CoreML conversion experiments — progress log

## 2026-10-09 — trial 2 (post-reboot rebuild)
- Machine rebooted; /tmp wiped. Rebuilding from recorded findings.
- Prior findings being reproduced: direct ONNX->CoreML conversion impossible on all coremltools
  routes (7.2/8.2/9.0: ONNX frontend removed); working route = PyTorch rebuild -> torch.jit.trace
  -> ct.convert (fuse_linear_bias pass must be disabled for fp16 source).
- Stage 1 done: venv rebuilt (coremltools 9.0, onnxruntime 1.30.0, onnx 1.23.2, torch 2.14.1, numpy 2.5.3);
  scripts persisted (extract_weights.py, pf_model.py, convert_one.py, verify_torch.py, fidelity_timing.py).
- Stage 2 done: weights.npz re-extracted (5.63 GB fp32 dequantized). PyTorch rebuild matches ORT
  bit-for-bit (maxabs 2e-5..5.5e-5, 100% argmax agreement at L=128/192/300/512, incl. padded batches).
  L128+L2048 fp16 conversions launched.
- Stage 3 done: CONVERT_OK L128 fp16 (28.8s) + L2048 fp16 (27.3s). Package sizes in package-sizes.txt.
- Stage 4 done — FINAL NUMBERS (CoreML fp16 mlpackage vs original ONNX via ORT CPU, Apple M2):
  Fidelity (K=30 random inputs, frame-level argmax agreement over 33 labels):
    L=128 : max_abs=8.97 mean_abs=0.120 argmax_agree=100.00% exact=100% corr>=0.9897
    L=2048: max_abs=11.77 mean_abs=0.113 argmax_agree=99.995% exact=90% corr>=0.9958
  Timing (median of 20, interleaved, compute_units=ALL):
    L=128 : ORT-CPU 243.8 ms | CoreML 322.3 ms  (CoreML 1.32x SLOWER)
    L=2048: ORT-CPU 1055.9 ms | CoreML 5037.2 ms (CoreML 4.77x SLOWER)
  Package sizes: 2.6 GB fp16 mlpackage (either length) vs 1.6 GB int8 onnx_data.
