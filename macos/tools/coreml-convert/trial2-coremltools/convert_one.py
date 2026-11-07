import numpy as np
import torch
import coremltools as ct
import sys, time, traceback

L = int(sys.argv[1])
tag = sys.argv[2]
precision = ct.precision.FLOAT16 if tag == "fp16" else ct.precision.FLOAT32

sys.path.insert(0, "/tmp/coreml-exp")
from pf_model import Net

print("coremltools", ct.__version__, "torch", torch.__version__, flush=True)
W = dict(np.load("/tmp/coreml-exp/weights.npz"))
# fp16 source weights: keeps peak memory ~3x lower (fp32 source gets OOM-killed on 17GB RAM)
weights = {k: torch.from_numpy(v).half() for k, v in W.items()}
del W
import gc; gc.collect()
net = Net(weights).eval()

ids = torch.randint(0, 30000, (1, L))
am = torch.ones(1, L, dtype=torch.int64)

class NetFixed(torch.nn.Module):
    def __init__(self, base, B, L):
        super().__init__()
        self.base = base
        self.B = B
        self.L = L
    def forward(self, input_ids, attention_mask):
        return self.base(input_ids, attention_mask, self.B, self.L)

fixed = NetFixed(net, 1, L).eval()
with torch.inference_mode():
    traced = torch.jit.trace(fixed, (ids, am))
    out = traced(ids, am)
print("traced OK", out.shape, out.dtype, flush=True)
del weights, net, fixed, out
gc.collect()
print("freed source weights; entering ct.convert", flush=True)

t0 = time.time()
from coremltools.converters.mil.mil.passes.pass_pipeline import PassPipeline
pipeline = PassPipeline.DEFAULT
# coremltools 9.0 bug: fuse_linear_bias validates fused bias against fp16 weight but
# frontend has cast fp16 const params to fp32 -> ValueError. Disable the fusion.
pipeline.remove_passes(["common::fuse_linear_bias"])
ml = ct.convert(
    traced,
    inputs=[
        ct.TensorType(name="input_ids", shape=(1, L), dtype=np.int64),
        ct.TensorType(name="attention_mask", shape=(1, L), dtype=np.int64),
    ],
    minimum_deployment_target=ct.target.macOS14,
    compute_precision=precision,
    pass_pipeline=pipeline,
)
dt = time.time() - t0
path = f"/tmp/coreml-exp/pf_L{L}_{tag}.mlpackage"
ml.save(path)
print(f"CONVERT_OK L={L} {tag} in {dt:.1f}s -> {path}", flush=True)
