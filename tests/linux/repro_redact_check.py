"""One-off: verify redaction correctness with 512-token chunking.

Sends PII-dense bodies of several sizes through the proxy; the mock upstream
captures what it receives; assert no PII survives and redaction labels exist.
Entities land on chunk boundaries by construction (text repeats uniformly).
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

_tests = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_tests))
sys.path.insert(0, str(_tests / "gui"))
from config_factory import create_settings
from gui_process import _find_free_port, _wait_for_port

ENGINE = os.environ.get("AGENTREDACTOR_ENGINE_BIN",
    str(_tests.parent / "linux/build-release/engine/agentredactor"))

PII_STRINGS = [
    "jonathan.smith@example.com", "Jonathan Smith", "742 Evergreen Terrace",
    "+1-555-867-5309", "123456789012", "maria.garcia@example.org",
    "https://example.com/private",
]
PII_TEXT = (
    "Hello, my name is Jonathan Smith and my email is jonathan.smith@example.com. "
    "I live at 742 Evergreen Terrace, Springfield. Call me at +1-555-867-5309. "
    "My account number is 123456789012 and my password is hunter2. "
    "Please also contact Maria Garcia at maria.garcia@example.org or visit https://example.com/private. "
)

captured = []


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        captured.append(self.rfile.read(length))
        body = json.dumps({"choices": [{"message": {"role": "assistant", "content": "ok"}}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


def main():
    upstream = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    upstream_port = upstream.server_address[1]
    threading.Thread(target=upstream.serve_forever, daemon=True).start()

    config_dir = Path(tempfile.mkdtemp(prefix="ar-redact-check-"))
    proxy_port = _find_free_port()
    create_settings(
        data_dir=config_dir,
        upstream_url=f"http://127.0.0.1:{upstream_port}",
        api_key="sk-redact-check",
        proxy_port=proxy_port,
        logging_enabled=False,
        keywords=[],
        regex_patterns=[],
    )
    env = dict(os.environ)
    env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
    proc = subprocess.Popen([ENGINE], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    failed = False
    try:
        if not _wait_for_port(proxy_port, timeout=300):
            print("engine never started"); sys.exit(1)
        time.sleep(2)
        for kb in (2, 5, 10, 20, 40):
            text = PII_TEXT * (kb * 1024 // len(PII_TEXT))
            captured.clear()
            body = json.dumps({
                "model": "t", "messages": [{"role": "user", "content": text}],
            }).encode()
            req = urllib.request.Request(
                f"http://127.0.0.1:{proxy_port}/v1/chat/completions",
                data=body, headers={"Content-Type": "application/json"})
            urllib.request.urlopen(req, timeout=600).read()
            assert captured, "upstream saw no request"
            upstream_body = captured[0].decode()
            leaks = [p for p in PII_STRINGS if p in upstream_body]
            labels = upstream_body.count("<<REDACTED_PII_")
            status = "OK" if not leaks and labels > 0 else "FAIL"
            if leaks or labels == 0:
                failed = True
            print(f"{kb}KB body: labels={labels} leaks={leaks or 'none'} -> {status}")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        upstream.shutdown()
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
