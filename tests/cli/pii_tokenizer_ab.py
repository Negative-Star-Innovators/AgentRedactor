from __future__ import annotations

import argparse
import asyncio
import hashlib
import importlib.util
import json
import os
import platform
import random
import re
import sys
import threading
import time
import urllib.request
from collections import Counter, defaultdict
from pathlib import Path

import psutil

_TESTS_ROOT = Path(__file__).resolve().parent.parent
_GUI_DIR = _TESTS_ROOT / "gui"
for _path in (str(_TESTS_ROOT), str(_GUI_DIR), str(Path(__file__).resolve().parent)):
    if _path not in sys.path:
        sys.path.insert(0, _path)

from config_factory import create_settings
from gui_process import _find_free_port
from mock_llm import MockLLM

_spec = importlib.util.spec_from_file_location("tokenizer_ab_cli_conftest", Path(__file__).with_name("conftest.py"))
_cli_conftest = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_cli_conftest)
CliEngine = _cli_conftest.CliEngine
ENGINE_EXE = _cli_conftest.ENGINE_EXE


class _MockLLMRunner:
    def __init__(self):
        self.llm = None
        self._started = threading.Event()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        async def serve():
            async with MockLLM() as llm:
                self.llm = llm
                self._started.set()
                while not self._stop.is_set():
                    await asyncio.sleep(0.1)

        asyncio.run(serve())

    def start(self):
        self._thread.start()
        assert self._started.wait(10), "mock LLM did not start"

    def stop(self):
        self._stop.set()
        self._thread.join(5)

ENTITY_RE = re.compile(
    r"\[DetectPII\] PII detected: type=(\S+) start=(\d+) end=(\d+) "
    r"text=\[(.*?)\] confidence=([0-9.eE+-]+)"
)
TOKENIZE_RE = re.compile(r"\[DetectPII\] TOKENIZE chars=(\d+) tokens=(\d+) elapsed_us=(\d+)")
INFERENCE_RE = re.compile(r"\[DetectPII\] INFERENCE chunks=(\d+) elapsed_us=(\d+)")
TOKEN_RE = re.compile(r"\[(\d+)\] id=(\d+) byteStart=(\d+) byteEnd=(\d+) decoded=\[(.*?)\]")
STAGES_RE = re.compile(
    r"\[DetectPII\] STAGES tokenize_us=(\d+) onnx_run_us=(\d+) "
    r"viterbi_us=(\d+) entity_assembly_us=(\d+)"
)
REPLACE_RE = re.compile(r"\[RedactWithPIIModel\] REPLACE entities=(\d+) elapsed_us=(\d+)")
JSON_SERIALIZE_RE = re.compile(r"\[RedactionPipeline\] JSON_SERIALIZE elapsed_us=(\d+)")
UPSTREAM_RE = re.compile(r"\[Upstream\] ROUNDTRIP elapsed_us=(\d+)")


class ProcessMonitor:
    def __init__(self, process_getter):
        self.process_getter = process_getter
        self.samples = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join(5)

    def _run(self):
        while not self._stop.is_set():
            process = self.process_getter()
            if process is not None:
                try:
                    info = psutil.Process(process.pid).memory_info()
                    self.samples.append((time.monotonic(), info.rss, getattr(info, "private", 0)))
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass
            self._stop.wait(0.02)

    def summary(self):
        if not self.samples:
            return {"samples": 0}
        return {
            "samples": len(self.samples),
            "first_rss_mib": self.samples[0][1] / 2**20,
            "last_rss_mib": self.samples[-1][1] / 2**20,
            "peak_rss_mib": max(sample[1] for sample in self.samples) / 2**20,
            "peak_private_mib": max(sample[2] for sample in self.samples) / 2**20,
        }


def parse_entities(log: str):
    return [
        {
            "type": match.group(1),
            "start": int(match.group(2)),
            "end": int(match.group(3)),
            "text": match.group(4),
            "confidence": float(match.group(5)),
        }
        for match in ENTITY_RE.finditer(log)
    ]


def parse_token_probe(log):
    return [
        {
            "id": int(match.group(2)),
            "start": int(match.group(3)),
            "end": int(match.group(4)),
        }
        for match in TOKEN_RE.finditer(log)
    ]


def reference_token_probe(text):
    from tokenizers import Tokenizer

    tokenizer_path = _TESTS_ROOT.parent / "windows" / "models" / "tokenizer.json"
    encoding = Tokenizer.from_file(str(tokenizer_path)).encode(text)
    byte_offsets = [len(text[:index].encode("utf-8")) for index in range(len(text) + 1)]
    return [
        {"id": token_id, "start": byte_offsets[start], "end": byte_offsets[end]}
        for token_id, (start, end) in zip(encoding.ids, encoding.offsets)
    ]


def metric_counts(ground_truth, predictions):
    gt_keys = Counter((entity["type"], entity["start"], entity["end"]) for entity in ground_truth)
    pred_keys = Counter((entity["type"], entity["start"], entity["end"]) for entity in predictions)
    exact_tp = sum(min(count, pred_keys[key]) for key, count in gt_keys.items())
    exact = {
        "tp": exact_tp,
        "fp": len(predictions) - exact_tp,
        "fn": len(ground_truth) - exact_tp,
    }

    unmatched_gt = set(range(len(ground_truth)))
    unmatched_pred = set(range(len(predictions)))
    for gt_index, gt in enumerate(ground_truth):
        for pred_index, pred in enumerate(predictions):
            if pred_index not in unmatched_pred:
                continue
            if (gt["type"], gt["start"], gt["end"]) == (pred["type"], pred["start"], pred["end"]):
                unmatched_gt.discard(gt_index)
                unmatched_pred.discard(pred_index)
                break
    exact_matches = len(ground_truth) - len(unmatched_gt)
    partial = 0
    for gt_index in sorted(unmatched_gt):
        gt = ground_truth[gt_index]
        best = None
        for pred_index in unmatched_pred:
            pred = predictions[pred_index]
            if pred["type"] != gt["type"]:
                continue
            overlap = max(0, min(gt["end"], pred["end"]) - max(gt["start"], pred["start"]))
            if overlap and (best is None or overlap > best[1]):
                best = (pred_index, overlap)
        if best is not None:
            unmatched_pred.remove(best[0])
            partial += 1
    overlap = {
        "tp": exact_matches + partial,
        "partial": partial,
        "fp": len(unmatched_pred),
        "fn": len(ground_truth) - exact_matches - partial,
    }
    return {"exact": exact, "overlap": overlap}


def with_rates(counts):
    tp, fp, fn = counts["tp"], counts["fp"], counts["fn"]
    precision = 100 * tp / (tp + fp) if tp + fp else 100.0
    recall = 100 * tp / (tp + fn) if tp + fn else 100.0
    return {
        **counts,
        "precision_percent": precision,
        "recall_percent": recall,
        "f1_percent": 2 * precision * recall / (precision + recall) if precision + recall else 0.0,
    }


def accuracy_report(ground_truth, predictions, text, forwarded):
    counts = metric_counts(ground_truth, predictions)
    by_type = {}
    for entity_type in sorted({entity["type"] for entity in ground_truth}):
        gt = [entity for entity in ground_truth if entity["type"] == entity_type]
        pred = [entity for entity in predictions if entity["type"] == entity_type]
        by_type[entity_type] = {
            key: with_rates(value) for key, value in metric_counts(gt, pred).items()
        }
    leaks = []
    for entity in ground_truth:
        original = text[entity["start"]:entity["end"]]
        if original in forwarded:
            leaks.append(entity)
    return {
        "ground_truth_count": len(ground_truth),
        "prediction_count": len(predictions),
        "exact": with_rates(counts["exact"]),
        "overlap": with_rates(counts["overlap"]),
        "by_type_overlap": by_type,
        "raw_leak_count": len(leaks),
        "raw_leaks": leaks,
        "predictions": predictions,
    }


def timing_from_log(log):
    result = {}
    tokenize = list(TOKENIZE_RE.finditer(log))
    inference = list(INFERENCE_RE.finditer(log))
    if tokenize:
        result["tokenization_calls"] = len(tokenize)
        result["tokenize_chars"] = sum(int(match.group(1)) for match in tokenize)
        result["token_count"] = sum(int(match.group(2)) for match in tokenize)
        result["tokenize_seconds"] = sum(int(match.group(3)) for match in tokenize) / 1e6
    if inference:
        result["inference_chunks"] = sum(int(match.group(1)) for match in inference)
        result["inference_seconds"] = sum(int(match.group(2)) for match in inference) / 1e6
    pipeline = re.findall(r"\[RedactionPipeline\] TIMING total=(\d+)us", log)
    if pipeline:
        result["pipeline_seconds"] = sum(int(value) for value in pipeline) / 1e6
    raw_entities = re.findall(r"Raw entities: (\d+)", log)
    result["raw_entity_count"] = sum(int(value) for value in raw_entities)
    stages = list(STAGES_RE.finditer(log))
    result["onnx_run_seconds"] = sum(int(match.group(2)) for match in stages) / 1e6
    result["viterbi_seconds"] = sum(int(match.group(3)) for match in stages) / 1e6
    result["entity_assembly_seconds"] = sum(int(match.group(4)) for match in stages) / 1e6
    result["replacement_seconds"] = sum(int(match.group(2)) for match in REPLACE_RE.finditer(log)) / 1e6
    result["json_serialize_seconds"] = sum(int(match.group(1)) for match in JSON_SERIALIZE_RE.finditer(log)) / 1e6
    result["upstream_seconds"] = sum(int(match.group(1)) for match in UPSTREAM_RE.finditer(log)) / 1e6
    return result


def run_request(port, path, body, engine):
    log_path = engine.config_dir / "agent_redactor.log"
    log_offset = log_path.stat().st_size if log_path.exists() else 0
    monitor = ProcessMonitor(lambda: engine.process)
    monitor.start()
    started = time.monotonic()
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}",
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json", "Authorization": "Bearer sk-tokenizer-ab"},
    )
    with urllib.request.urlopen(request, timeout=1200) as response:
        response.read()
    wall_seconds = time.monotonic() - started
    monitor.stop()
    with log_path.open("rb") as log_file:
        log_file.seek(log_offset)
        log = log_file.read().decode("utf-8", "replace")
    return {
        "wall_seconds": wall_seconds,
        "memory": monitor.summary(),
        **timing_from_log(log),
    }, log


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("accuracy", "signature"), required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--signature-chars")
    parser.add_argument("--signature-source", type=Path)
    args = parser.parse_args()

    result = {
        "label": args.label,
        "mode": args.mode,
        "engine": str(ENGINE_EXE),
        "engine_bytes": ENGINE_EXE.stat().st_size,
        "platform": platform.platform(),
        "machine": platform.machine(),
        "requests": [],
    }
    runner = _MockLLMRunner()
    runner.start()
    engine = None
    try:
        work_dir = args.output.parent / f"{args.label}-{args.mode}-engine"
        proxy_port = _find_free_port()
        create_settings(
            data_dir=work_dir,
            upstream_url=runner.llm.base_url,
            api_key="sk-tokenizer-ab",
            proxy_port=proxy_port,
            logging_enabled=True,
            keywords=[],
            regex_patterns=[],
        )
        settings_path = work_dir / "settings.json"
        settings = json.loads(settings_path.read_text(encoding="utf-8"))
        settings["showSensitive"] = args.mode == "accuracy"
        settings_path.write_text(json.dumps(settings), encoding="utf-8")

        engine = CliEngine(work_dir, proxy_port)
        startup_monitor = ProcessMonitor(lambda: engine.process)
        startup_monitor.start()
        started = time.monotonic()
        engine.start()
        result["startup_seconds"] = time.monotonic() - started
        startup_monitor.stop()
        result["startup_memory"] = startup_monitor.summary()
        if args.mode == "accuracy":
            sensitive = engine.run_cli("set", "show-sensitive", "true")
            if sensitive.returncode != 0:
                raise RuntimeError(f"failed to enable sensitive diagnostics: {sensitive.stdout} {sensitive.stderr}")

        if args.mode == "accuracy":
            fixture_path = _TESTS_ROOT / "cli" / "fixtures" / "pii_dense_corpus.json"
            fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
            text = fixture["text"][:fixture["slice_char_end"]]
            ground_truth = [entity for entity in fixture["gt"] if entity["end"] <= len(text)]
            body = {
                "model": "tokenizer-ab",
                "max_tokens": 16,
                "messages": [{"role": "user", "content": text}],
            }
            timing, log = run_request(proxy_port, "/v1/chat/completions", body, engine)
            forwarded = runner.llm.last_request["messages"][-1]["content"]
            predictions = parse_entities(log)
            result["accuracy"] = accuracy_report(ground_truth, predictions, text, forwarded)
            result["requests"].append({"name": "pii_dense", **timing})

            clean_text = (
                "The engineering team reviewed the deployment pipeline and updated the documentation. "
                "A URL such as https://example.com is not automatically personal information. "
                "Long identifiers and Base64-like values must not cause indiscriminate false positives: "
                "QWxhZGRpbjpvcGVuIHNlc2FtZQ==. Ordinary prose continues with no intended PII."
            )
            clean_body = {
                "model": "tokenizer-ab-clean",
                "max_tokens": 16,
                "messages": [{"role": "user", "content": clean_text}],
            }
            timing, log = run_request(proxy_port, "/v1/chat/completions", clean_body, engine)
            clean_predictions = parse_entities(log)
            result["clean_false_positives"] = clean_predictions
            result["requests"].append({"name": "clean", **timing})

            probe_text = "é A😀B JohnSmith1234 a+++++b"
            probe_body = {
                "model": "tokenizer-ab-probe",
                "max_tokens": 16,
                "messages": [{"role": "user", "content": probe_text}],
            }
            timing, log = run_request(proxy_port, "/v1/chat/completions", probe_body, engine)
            expected_probe = reference_token_probe(probe_text)
            actual_probe = parse_token_probe(log)
            result["token_probe"] = {
                "text": probe_text,
                "ids_match": [token["id"] for token in actual_probe] == [token["id"] for token in expected_probe],
                "offsets_match": actual_probe == expected_probe,
                "expected": expected_probe,
                "actual": actual_probe,
            }
            result["requests"].append({"name": "token_probe", **timing})
        else:
            signature_cases = []
            if args.signature_source:
                log_text = args.signature_source.read_text(encoding="utf-8", errors="replace")
                values = list(dict.fromkeys(re.findall(r'"signature"\s*:\s*"([^"]*)"', log_text)))
                allowed = ({int(value) for value in args.signature_chars.split(",")}
                           if args.signature_chars else None)
                values = [value for value in values if allowed is None or len(value) in allowed]
                values.sort(key=len)
                for value in values:
                    signature_cases.append((len(value), value))
                    signature_cases.append((len(value), value))
                result["signature_source"] = str(args.signature_source)
            else:
                rng = random.Random(20261123)
                alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
                lengths = ([int(value) for value in args.signature_chars.split(",")]
                           if args.signature_chars else [10000, 10000, 60000, 100000])
                for length in lengths:
                    signature_cases.append((length, "".join(rng.choice(alphabet) for _ in range(length))))
            for case_index, (length, signature) in enumerate(signature_cases, 1):
                body = {
                    "model": "claude-haiku-4-5",
                    "max_tokens": 16,
                    "messages": [{
                        "role": "user",
                        "content": [
                            {"type": "thinking", "thinking": "short reasoning", "signature": signature},
                            {"type": "text", "text": "Summarize the deployment status."},
                        ],
                    }],
                }
                timing, _ = run_request(proxy_port, "/v1/messages", body, engine)
                forwarded = runner.llm.last_request["messages"][-1]["content"][0]["signature"]
                result["requests"].append({
                    "name": f"signature_{length}",
                    "case_index": case_index,
                    "signature_chars": length,
                    "signature_preserved": forwarded == signature,
                    "original_sha256": hashlib.sha256(signature.encode("ascii")).hexdigest(),
                    "forwarded_sha256": hashlib.sha256(forwarded.encode("utf-8")).hexdigest(),
                    "forwarded_chars": len(forwarded),
                    "redaction_label_count": forwarded.count("<<REDACTED_"),
                    **timing,
                })
    finally:
        if engine is not None:
            engine.stop()
        runner.stop()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
