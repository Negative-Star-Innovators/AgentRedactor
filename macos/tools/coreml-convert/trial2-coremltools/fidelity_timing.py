import numpy as np
import onnxruntime as ort
import coremltools as ct
import time, json

rng = np.random.default_rng(42)
ONNX = "/Users/ursusprizlius/Documents/Projects/AgentRedactor/macos/build/engine/models/onnx/model_quantized.onnx"
sess = ort.InferenceSession(ONNX, providers=["CPUExecutionProvider"])

MODELS = {
    128: "/tmp/coreml-exp/pf_L128_fp16.mlpackage",
    2048: "/tmp/coreml-exp/pf_L2048_fp16.mlpackage",
}

def make_input(L):
    ids = rng.integers(0, 30000, size=(1, L)).astype(np.int64)
    am = np.ones((1, L), dtype=np.int64)
    return ids, am

def ort_run(ids, am):
    return sess.run(None, {"input_ids": ids, "attention_mask": am})[0]

print("=== FIDELITY (K=30 random inputs per length, CoreML fp16 vs ORT CPU) ===", flush=True)
K = 30
fidelity = {}
for L in (128, 2048):
    ml = ct.models.MLModel(MODELS[L], compute_units=ct.ComputeUnit.ALL)
    out_name = ml.get_spec().description.output[0].name
    maxabs_all, meanabs_all, frame_agree, exact, corr = [], [], [], [], []
    for i in range(K):
        ids, am = make_input(L)
        ref = ort_run(ids, am)[0]
        got = ml.predict({"input_ids": ids.astype(np.int32), "attention_mask": am.astype(np.int32)})[out_name][0]
        d = np.abs(ref - got)
        maxabs_all.append(float(d.max())); meanabs_all.append(float(d.mean()))
        ag = (ref.argmax(-1) == got.argmax(-1))
        frame_agree.append(float(ag.mean())); exact.append(bool(ag.all()))
        corr.append(float(np.corrcoef(ref.ravel(), got.ravel())[0, 1]))
    fidelity[L] = dict(
        max_abs=float(np.max(maxabs_all)), mean_abs_over_samples=float(np.mean(maxabs_all)),
        mean_abs=float(np.mean(meanabs_all)),
        frame_agree_pct=float(np.mean(frame_agree)) * 100, exact_match_pct=float(np.mean(exact)) * 100,
        min_corr=float(np.min(corr)),
    )
    print(f"L={L}: {fidelity[L]}", flush=True)

print("=== TIMING (3 warmup + 20 timed, interleaved ort-cpu / coreml-ALL) ===", flush=True)
ml_all = {L: ct.models.MLModel(MODELS[L], compute_units=ct.ComputeUnit.ALL) for L in MODELS}
timing = {}
for L in (128, 2048):
    ids, am = make_input(L)
    ids32, am32 = ids.astype(np.int32), am.astype(np.int32)
    ml = ml_all[L]
    for _ in range(3):
        ort_run(ids, am); ml.predict({"input_ids": ids32, "attention_mask": am32})
    t_cpu, t_coreml = [], []
    for rep in range(20):
        t0 = time.perf_counter(); ort_run(ids, am); t_cpu.append((time.perf_counter() - t0) * 1000)
        t0 = time.perf_counter(); ml.predict({"input_ids": ids32, "attention_mask": am32}); t_coreml.append((time.perf_counter() - t0) * 1000)
    timing[L] = dict(
        cpu_med=float(np.median(t_cpu)), cpu_min=float(np.min(t_cpu)), cpu_max=float(np.max(t_cpu)),
        coreml_med=float(np.median(t_coreml)), coreml_min=float(np.min(t_coreml)), coreml_max=float(np.max(t_coreml)),
    )
    print(f"L={L}: cpu med/min/max = {timing[L]['cpu_med']:.1f}/{timing[L]['cpu_min']:.1f}/{timing[L]['cpu_max']:.1f} ms | "
          f"coreml(ALL) med/min/max = {timing[L]['coreml_med']:.1f}/{timing[L]['coreml_min']:.1f}/{timing[L]['coreml_max']:.1f} ms", flush=True)

json.dump({"fidelity": {str(k): v for k, v in fidelity.items()}, "timing": timing},
          open("/tmp/coreml-exp/results.json", "w"), indent=2)
print("saved /tmp/coreml-exp/results.json")
