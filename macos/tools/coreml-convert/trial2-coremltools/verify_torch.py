import numpy as np
import torch
import sys
sys.path.insert(0, "/tmp/coreml-exp")
from pf_model import Net

torch.set_grad_enabled(False)
W = dict(np.load("/tmp/coreml-exp/weights.npz"))
net = Net({k: torch.from_numpy(v) for k, v in W.items()}).eval()

import onnxruntime as ort
sess = ort.InferenceSession("/Users/ursusprizlius/Documents/Projects/AgentRedactor/macos/build/engine/models/onnx/model_quantized.onnx",
                            providers=["CPUExecutionProvider"])
rng = np.random.default_rng(7)
for (B, L) in [(2, 192), (1, 128), (1, 300), (2, 512)]:
    ids = rng.integers(0, 30000, size=(B, L)).astype(np.int64)
    am = np.ones((B, L), np.int64)
    if B > 1:
        am[1, int(L * 0.75):] = 0
    ref = sess.run(None, {"input_ids": ids, "attention_mask": am})[0]
    with torch.no_grad():
        out = net(torch.from_numpy(ids), torch.from_numpy(am), B, L).numpy()
    d = np.abs(ref - out)
    agree = (ref.argmax(-1) == out.argmax(-1))
    print(f"B={B} L={L}: maxabs={d.max():.4e} meanabs={d.mean():.3e} argmax_agree={agree.mean()*100:.2f}% exact={agree.all(-1).mean()*100:.0f}%", flush=True)
