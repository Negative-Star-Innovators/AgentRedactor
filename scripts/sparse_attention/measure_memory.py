"""Memory scaling measurement for dense vs block-windowed attention.

Runs each (model, token-count) inference in a fresh subprocess, sampling the
child's RSS (including its threads' allocations) while inference runs.
Reports baseline RSS (session loaded) and peak RSS, and the implied
bytes/token slope across sizes.

Usage:
  python measure_memory.py                # full sweep
  python measure_memory.py child MODEL N  # internal: one run
"""

import json
import subprocess
import sys
import threading
import time

import numpy as np
import psutil

REF = r"_sparse_export/dense_original/model_quantized.onnx"
NEW = r"windows/models/onnx/model_quantized.onnx"

# (model, [sizes]); dense stops where RAM allows, sparse covers the card sizes
SWEEP = [
    (REF, [1024, 4096, 8192, 12288, 16384]),
    (NEW, [1024, 4096, 16384, 65536]),
]


def _child(model, n):
    import onnxruntime as ort

    opts = ort.SessionOptions()
    opts.log_severity_level = 3
    sess = ort.InferenceSession(model, opts, providers=["CPUExecutionProvider"])
    ids = np.random.default_rng(0).integers(0, 200000, size=(1, n), dtype=np.int64)
    mask = np.ones((1, n), dtype=np.int64)
    sess.run(None, {"input_ids": ids, "attention_mask": mask})
    time.sleep(0.5)  # let the sampler catch the tail


def _sample(pid, out):
    try:
        proc = psutil.Process(pid)
    except psutil.Error:
        return
    while True:
        try:
            rss = proc.memory_info().rss
        except psutil.Error:
            break
        out.append(rss)
        time.sleep(0.1)


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "child":
        _child(sys.argv[2], int(sys.argv[3]))
        return

    rows = []
    for model, sizes in SWEEP:
        for n in sizes:
            samples = []
            proc = subprocess.Popen([sys.executable, __file__, "child", model, str(n)])
            t = threading.Thread(target=_sample, args=(proc.pid, samples))
            t.start()
            t0 = time.time()
            rc = proc.wait()
            t.join()
            peak = max(samples) if samples else 0
            rows.append(dict(model="dense" if model == REF else "sparse",
                             tokens=n, ok=(rc == 0),
                             peak_rss=peak, seconds=round(time.time() - t0, 1)))
            print(f"{rows[-1]['model']:6s} N={n:6d} ok={rc == 0} "
                  f"peak_rss={peak / 2**30:8.2f} GiB  ({rows[-1]['seconds']}s)")
            if rc != 0:
                break  # don't try larger sizes for a model that just failed

    # slope between successive sizes of the same model (bytes/token beyond
    # the linear term shows up as growth in this number)
    print("\nper-token peak-RSS deltas between successive sizes:")
    by_model = {}
    for r in rows:
        by_model.setdefault(r["model"], []).append(r)
    slopes = {}
    for name, rs in by_model.items():
        rs = [r for r in rs if r["ok"]]
        ss = []
        for a, b in zip(rs, rs[1:]):
            d = (b["peak_rss"] - a["peak_rss"]) / (b["tokens"] - a["tokens"])
            ss.append(round(d, 1))
        slopes[name] = ss
        print(f"  {name}: {ss} bytes/token")

    with open(r"scripts/sparse_attention/memory_report.json", "w") as f:
        json.dump({"runs": rows, "marginal_bytes_per_token": slopes}, f, indent=2)


if __name__ == "__main__":
    main()
