"""Chunked PII-model inference: window bound + detection-accuracy regression.

The engine decodes long documents in windows ("prepend" chunking): each model
Run sees at most PII_CHUNK_WINDOW = 8,192 tokens — 7,168 owned tokens plus 512
tokens of left context and 512 of right margin, whose labels are discarded.
Context margins fix the seam-accuracy loss of plain slicing (every emitted
token has its full receptive field, ≈ ±1,024 tokens for this model), and the
fixed window bounds the model's memory footprint regardless of request size.

These tests drive the REAL engine through the REAL proxy with a 32,768-token
PII-dense request whose injected entities are known (ground truth in
fixtures/pii_dense_corpus.json) and assert:

1. **Window bound** — every model Run stays within the window (read from the
   engine's `>>RUN seqlen=` log lines). Deterministic guard against the
   chunk-size regression that caused unbounded memory growth (the arena
   ratcheted to the largest Run ever seen).
2. **Detection accuracy vs ground truth** — recall (fraction of injected
   entities redacted) and precision (fraction of redactions that are real
   entities) floors. Tripwire: any change that shifts detection accuracy
   fails here and gets reviewed. The recall floor doubles as the leakage
   guard: an uncaught injected entity is by definition a raw leak.
"""

from __future__ import annotations

import asyncio
import json
import re
import threading
import urllib.request
from pathlib import Path

import pytest

from conftest import CliEngine, ENGINE_EXE
from config_factory import create_settings
from gui_process import _find_free_port, _wait_for_port
from mock_llm import MockLLM

FIXTURE = Path(__file__).parent / "fixtures" / "pii_dense_corpus.json"
WINDOW = 8192
RECALL_FLOOR = 85.0
PRECISION_FLOOR = 86.0
MAX_RAW_LEAKS = 5
CHUNK_COUNT_BAND = (4, 6)  # expected 5 for the 32,768-token slice


@pytest.fixture(scope="module")
def pii_fixture() -> dict:
    with FIXTURE.open(encoding="utf-8") as f:
        return json.load(f)


class _MockLLMRunner:
    """Runs MockLLM on a background event loop so sync tests can use it."""

    def __init__(self) -> None:
        self.llm: MockLLM | None = None
        self._started = threading.Event()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _run(self) -> None:
        async def serve() -> None:
            async with MockLLM() as llm:
                self.llm = llm
                self._started.set()
                while not self._stop.is_set():
                    await asyncio.sleep(0.1)

        asyncio.run(serve())

    def start(self) -> None:
        self._thread.start()
        assert self._started.wait(10), "mock LLM did not start"

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(5)


@pytest.fixture(scope="module")
def mock_upstream() -> MockLLM:
    runner = _MockLLMRunner()
    runner.start()
    yield runner.llm
    runner.stop()


@pytest.fixture(scope="module")
def chunk_engine(tmp_path_factory: pytest.TempPathFactory, mock_upstream: MockLLM) -> CliEngine:
    if not ENGINE_EXE.exists():
        pytest.skip(f"engine exe not built: {ENGINE_EXE}")
    config_dir = tmp_path_factory.mktemp("pii-chunking")
    proxy_port = _find_free_port()
    create_settings(
        data_dir=config_dir,
        upstream_url=mock_upstream.base_url,
        api_key="sk-chunk-test-key",
        proxy_port=proxy_port,
        logging_enabled=True,
        keywords=[],
        regex_patterns=[],
    )
    eng = CliEngine(config_dir, proxy_port)
    eng.start()
    yield eng
    eng.stop()


def test_chunked_inference_window_and_accuracy(
    chunk_engine: CliEngine, pii_fixture: dict, mock_upstream: MockLLM
) -> None:
    char_end = pii_fixture["slice_char_end"]
    content = pii_fixture["text"][:char_end]
    gt = [g for g in pii_fixture["gt"] if g["end"] <= char_end]

    body = json.dumps({
        "model": "chunk-test",
        "max_tokens": 16,
        "messages": [{"role": "user", "content": content}],
    }).encode("utf-8")
    req = urllib.request.Request(
        f"http://127.0.0.1:{chunk_engine.proxy_port}/v1/chat/completions",
        data=body,
        headers={"Content-Type": "application/json",
                 "Authorization": "Bearer sk-chunk-test-key"},
    )
    with urllib.request.urlopen(req, timeout=300) as resp:
        assert resp.status == 200
        response = resp.read().decode("utf-8", "replace")

    # --- 1. window bound (deterministic chunking regression guard) ---
    log_path = chunk_engine.config_dir / "agent_redactor.log"
    log = log_path.read_text(encoding="utf-8", errors="replace")
    if "Failed to load model" in log:
        pytest.skip("model weights not available on this runner")
    seqlens = [int(m) for m in re.findall(r">>RUN seqlen=(\d+)", log)]
    assert seqlens, "no model Runs logged — the PII model never executed"
    assert max(seqlens) <= WINDOW, (
        f"a model Run exceeded the {WINDOW}-token window: {max(seqlens)}")
    assert CHUNK_COUNT_BAND[0] <= len(seqlens) <= CHUNK_COUNT_BAND[1], (
        f"unexpected chunk count {len(seqlens)}: {seqlens}")

    # --- 2. detection accuracy vs injected ground truth ---
    forwarded = mock_upstream.last_request["messages"][-1]["content"]
    labels = forwarded.count("<<REDACTED_")
    assert labels >= 100, "expected redaction labels in the forwarded body"

    def leaked(g: dict) -> bool:
        original = pii_fixture["text"][g["start"]:g["end"]]
        return original in forwarded

    caught = sum(1 for g in gt if not leaked(g))
    recall = 100.0 * caught / len(gt)
    precision = 100.0 * caught / labels  # every label replaced one span
    assert recall >= RECALL_FLOOR, (
        f"recall {recall:.2f}% below floor ({caught}/{len(gt)} injected entities caught)")
    assert precision >= PRECISION_FLOOR, (
        f"precision {precision:.2f}% below floor ({labels} redactions, {caught} matched GT)")
