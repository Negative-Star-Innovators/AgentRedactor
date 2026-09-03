"""Validation: block-windowed model vs the shipped dense model.

Builds PII-dense documents (names, emails, phones, dates, addresses) at a
range of token lengths, with entities deliberately placed across band edges
(+-128 from any token) and block boundaries (multiples of 256), runs both
models, and compares predicted labels (must match exactly) and logits.

Usage: python validate_sparse.py [--max-tokens 16000]
"""

import argparse
import json

import numpy as np
import onnxruntime as ort
from tokenizers import Tokenizer

REF = r"_sparse_export/dense_original/model_quantized.onnx"
NEW = r"windows/models/onnx/model_quantized.onnx"
TOK = r"windows/models/tokenizer.json"

PEOPLE = ["Alice Johnson", "Bob Smith", "Carla Diaz", "David Lee", "Emma Brown",
          "Frank Miller", "Grace Hopper", "Henry Ford"]
EMAILS = ["alice.johnson@example.com", "bob.smith@corp.net", "carla@mail.org",
          "dlee@company.io", "emma.brown@site.co.uk"]
PHONES = ["+1-555-123-4567", "020 7946 0958", "+49 30 901820", "555-867-5309"]
DATES = ["January 5th 1979", "12/03/1985", "March 22, 1990", "07-11-2001"]
ADDRS = ["742 Evergreen Terrace, Springfield", "221B Baker Street, London",
         "10 Downing Street, London", "1600 Pennsylvania Avenue, Washington"]
FILLER = ("The quarterly report discusses logistics, revenue, procurement and "
          "scheduling for the coming fiscal period with no personal data. ")

with open(r"windows/models/config.json") as f:
    ID2LABEL = {int(k): v for k, v in json.load(f)["id2label"].items()}


def build_doc(tokenizer, target_tokens, seed, boundary_entities=False):
    """Compose a document of about target_tokens tokens. If
    boundary_entities, place entities at multiples of 64/128/256 so they
    straddle band edges and block boundaries."""
    rng = np.random.default_rng(seed)
    parts = []
    n = 0
    # rough filler to reach the vicinity of the target
    while n < target_tokens - 400:
        parts.append(FILLER * 8)
        n = len(tokenizer.encode("".join(parts), add_special_tokens=False).ids)
    # entity-dense tail (or boundary placement)
    ents = PEOPLE + EMAILS + PHONES + DATES + ADDRS
    cats = [PEOPLE, EMAILS, PHONES, DATES, ADDRS]
    pick = lambda i: cats[i][rng.integers(0, len(cats[i]))]  # noqa: E731
    if boundary_entities:
        # drop short filler segments so an entity's first token lands right
        # around multiples of 64 (covers 128 band edges and 256 block edges)
        for k in range(64, target_tokens - 64, 64):
            cur = len(tokenizer.encode("".join(parts), add_special_tokens=False).ids)
            need = k - cur
            if need > 0:
                seg = FILLER
                while len(tokenizer.encode(seg, add_special_tokens=False).ids) < need:
                    seg += FILLER
                # trim by words until the token count fits before k
                words = seg.split()
                while words and len(tokenizer.encode(" ".join(words), add_special_tokens=False).ids) > need:
                    words.pop()
                parts.append(" ".join(words) + " ")
            parts.append(ents[rng.integers(0, len(ents))] + ", ")
    else:
        while n < target_tokens:
            parts.append(f"Contact {pick(0)} at {pick(1)} "
                         f"or {pick(2)}, born {pick(3)}, "
                         f"lives at {pick(4)}. ")
            n = len(tokenizer.encode("".join(parts), add_special_tokens=False).ids)
    text = "".join(parts)
    enc = tokenizer.encode(text, add_special_tokens=False)
    ids = np.array([enc.ids[: max(target_tokens, 64)]], dtype=np.int64)
    return text, ids


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--max-tokens", type=int, default=16000)
    args = ap.parse_args()

    tokenizer = Tokenizer.from_file(TOK)
    opts = ort.SessionOptions()
    opts.log_severity_level = 3
    ref = ort.InferenceSession(REF, opts, providers=["CPUExecutionProvider"])
    new = ort.InferenceSession(NEW, opts, providers=["CPUExecutionProvider"])

    cases = []
    for seed, tgt in ((1, 500), (2, 2000), (3, 4000), (4, 8000),
                      (5, 12000), (6, args.max_tokens)):
        if tgt > args.max_tokens:
            continue
        cases.append((f"pii_dense_{tgt}", *build_doc(tokenizer, tgt, seed)))
    cases.append(("boundary_entities", *build_doc(tokenizer, min(6000, args.max_tokens), 7,
                                                  boundary_entities=True)))
    # padded tail: exercise the key-padding mask
    text, ids = build_doc(tokenizer, 3000, 8)
    cases.append(("padded_tail", text, ids))

    results = []
    for name, text, ids in cases:
        n = ids.shape[1]
        mask = np.ones_like(ids)
        pad = 0
        if name == "padded_tail":
            pad = 200
            mask[0, -pad:] = 0
        a = ref.run(None, {"input_ids": ids, "attention_mask": mask})[0]
        b = new.run(None, {"input_ids": ids, "attention_mask": mask})[0]
        la, lb = a.argmax(-1)[0], b.argmax(-1)[0]
        n_mismatch = int((la != lb).sum())
        max_abs = float(np.abs(a - b).max())
        # non-O label agreement (the labels that drive redaction)
        ent_a, ent_b = la != 0, lb != 0
        ent_disagree = int((ent_a != ent_b).sum())
        results.append(dict(name=name, tokens=n, pad=pad,
                            label_mismatches=n_mismatch,
                            entity_disagreements=ent_disagree,
                            max_abs_logit_diff=round(max_abs, 6),
                            non_o_ref=int(ent_a.sum())))
        print(f"{name:20s} tokens={n:6d} pad={pad} mismatches={n_mismatch} "
              f"entity_disagree={ent_disagree} max_abs_logit_diff={max_abs:.3e}")

    ok = all(r["label_mismatches"] == 0 and r["entity_disagreements"] == 0
             for r in results)
    with open(r"scripts/sparse_attention/validation_report.json", "w") as f:
        json.dump({"reference": REF, "candidate": NEW, "cases": results,
                   "labels_exact_all_cases": ok}, f, indent=2)
    print("LABELS EXACT ON ALL CASES" if ok else "FAIL: label mismatches found")


if __name__ == "__main__":
    main()
