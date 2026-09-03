"""Quick parity smoke test: dense vs block-windowed model at small N."""

import numpy as np
import onnxruntime as ort

REF = r"_sparse_export/dense_original/model_quantized.onnx"
NEW = r"windows/models/onnx/model_quantized.onnx"

opts = ort.SessionOptions()
opts.log_severity_level = 3
ref = ort.InferenceSession(REF, opts, providers=["CPUExecutionProvider"])
new = ort.InferenceSession(NEW, opts, providers=["CPUExecutionProvider"])

rng = np.random.default_rng(0)
for n in (1, 7, 37, 128, 129, 255, 256, 257, 300, 511, 512, 513, 1000, 2000):
    ids = rng.integers(0, 200000, size=(1, n), dtype=np.int64)
    mask = np.ones((1, n), dtype=np.int64)
    # exercise the padding-mask path on some lengths
    if n in (300, 1000):
        mask[0, n - 50:] = 0
    a = ref.run(None, {"input_ids": ids, "attention_mask": mask})[0]
    b = new.run(None, {"input_ids": ids, "attention_mask": mask})[0]
    la, lb = a.argmax(-1), b.argmax(-1)
    label_match = (la == lb).all()
    max_abs = np.abs(a - b).max()
    denom = np.maximum(np.abs(a), 1.0)
    max_rel = (np.abs(a - b) / denom).max()
    print(f"N={n:5d} labels_exact={bool(label_match)} max_abs={max_abs:.4e} max_rel={max_rel:.4e}")
    if not label_match:
        bad = np.argwhere(la != lb)[:5]
        print("   first mismatches:", bad.tolist())
