"""Placeholder label counters must survive an engine restart.

The same keyword redacted before and after a restart must get
<<REDACTED_KEYWORD_1>> the second time — never a reissued
<<REDACTED_KEYWORD_0>> — otherwise a label still referenced by provider-side
conversation state could be silently re-mapped to different PII.
"""
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from pathlib import Path

import pytest

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux engine test")

_tests = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_tests))
sys.path.insert(0, str(_tests / "gui"))
from config_factory import create_settings
from gui_process import _find_free_port, _wait_for_port

ENGINE = os.environ.get("AGENTREDACTOR_ENGINE_BIN",
    str(_tests.parent / "linux/build/engine/agentredactor"))

KEYWORD = "Counter Persistence Canary Seven"

captured = []


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(length))
        captured.append(body)
        # Echo the last user message back: contains placeholder labels, which
        # the engine must restore to the original for the client.
        content = body["messages"][-1]["content"]
        payload = json.dumps({"choices": [{"message": {"role": "assistant", "content": content}}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *a):
        pass


def test_label_counter_persists_across_engine_restart():
    if not Path(ENGINE).exists():
        pytest.skip(f"engine exe not built: {ENGINE}")

    upstream = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=upstream.serve_forever, daemon=True).start()

    config_dir = Path(tempfile.mkdtemp(prefix="ar-counter-check-"))
    proxy_port = _find_free_port()
    create_settings(
        data_dir=config_dir,
        upstream_url=f"http://127.0.0.1:{upstream.server_address[1]}",
        api_key="sk-counter-check",
        proxy_port=proxy_port,
        logging_enabled=False,
        keywords=[{"text": KEYWORD, "case_sensitive": False, "enabled": True}],
        regex_patterns=[],
    )
    env = dict(os.environ)
    env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)

    def post(text: str) -> str:
        body = json.dumps({"model": "t", "messages": [{"role": "user", "content": text}]}).encode()
        req = urllib.request.Request(
            f"http://127.0.0.1:{proxy_port}/v1/chat/completions",
            data=body, headers={"Content-Type": "application/json"})
        return urllib.request.urlopen(req, timeout=120).read().decode()

    proc = None
    try:
        proc = subprocess.Popen([ENGINE], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        assert _wait_for_port(proxy_port, timeout=300), "engine never started"
        time.sleep(2)

        captured.clear()
        resp = post(KEYWORD)
        upstream_body = json.dumps(captured[0])
        assert "<<REDACTED_KEYWORD_0>>" in upstream_body
        assert KEYWORD not in upstream_body
        # Echoed label is restored for the client.
        assert KEYWORD in resp and "<<REDACTED" not in resp

        # Restart the engine: in-memory maps are gone, the keyword config and
        # the persisted counter file are not.
        proc.terminate()
        proc.wait(timeout=30)
        proc = subprocess.Popen([ENGINE], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        assert _wait_for_port(proxy_port, timeout=300), "engine never restarted"
        time.sleep(2)

        captured.clear()
        resp = post(KEYWORD)
        upstream_body = json.dumps(captured[0])
        assert "<<REDACTED_KEYWORD_1>>" in upstream_body
        assert "<<REDACTED_KEYWORD_0>>" not in upstream_body
        assert KEYWORD not in upstream_body
        assert KEYWORD in resp and "<<REDACTED" not in resp

        state_file = config_dir / "redaction_state.json"
        assert state_file.exists(), "counter state file was not persisted"
        state = json.loads(state_file.read_text(encoding="utf-8"))
        assert state["version"] == 1
        counters = next(iter(state["profiles"].values()))
        assert counters["keyword"] == 2
    finally:
        if proc and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
        upstream.shutdown()
