"""Repro: watch engine RSS while sending proxied requests with PII text."""
import http.server
import json
import os
import resource
import socket
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

PII_TEXT = (
    "Hello, my name is Jonathan Smith and my email is jonathan.smith@example.com. "
    "I live at 742 Evergreen Terrace, Springfield. Call me at +1-555-867-5309. "
    "My account number is 123456789012 and my password is hunter2. "
    "Please also contact Maria Garcia at maria.garcia@example.org or visit https://example.com/private. "
) * 8  # ~2.5 KB of PII-dense text per request


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body_in = self.rfile.read(length)
        stream = b'"stream":true' in body_in.replace(b" ", b"")
        if stream:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            # ~5MB SSE stream: exercises long-lived streaming responses.
            payload = "x" * 4000
            for i in range(1250):
                chunk = json.dumps({"choices": [{"delta": {"content": payload}}]})
                self.wfile.write(f"data: {chunk}\n\n".encode())
                if i % 50 == 0:
                    self.wfile.flush()
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()
            return
        body = json.dumps({"id": "x", "choices": [{"message": {"role": "assistant", "content": "ok"}}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


def mem_available_mb():
    with open("/proc/meminfo") as f:
        for line in f:
            if line.startswith("MemAvailable"):
                return int(line.split()[1]) / 1024
    return -1


def anon_mb(pid):
    """Private anonymous heap — the memory that actually pressures the OOM killer
    (VmRSS also counts mmap'ed model-file pages, which are reclaimable)."""
    try:
        with open(f"/proc/{pid}/smaps_rollup") as f:
            for line in f:
                if line.startswith("Anonymous:"):
                    return int(line.split()[1]) / 1024
    except FileNotFoundError:
        pass
    return -1


def rss_mb(pid):
    with open(f"/proc/{pid}/status") as f:
        for line in f:
            if line.startswith("VmRSS"):
                return int(line.split()[1]) / 1024
    return -1


def main():
    upstream = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    upstream_port = upstream.server_address[1]
    threading.Thread(target=upstream.serve_forever, daemon=True).start()

    config_dir = Path(tempfile.mkdtemp(prefix="ar-memtest-"))
    proxy_port = _find_free_port()
    create_settings(
        data_dir=config_dir,
        upstream_url=f"http://127.0.0.1:{upstream_port}",
        api_key="sk-memtest",
        proxy_port=proxy_port,
        logging_enabled=True,  # match the user's real config
        keywords=[],
        regex_patterns=[],
    )
    env = dict(os.environ)
    env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
    # UNCAPPED on purpose this time: we want to watch the runaway allocation
    # climb. The watchdog below kills the engine at 1.8GB anon so the machine
    # stays safe.
    proc = subprocess.Popen([ENGINE], env=env, stdout=subprocess.DEVNULL,
        stderr=open("/tmp/engine_stderr.log", "w"))
    try:
        if not _wait_for_port(proxy_port, timeout=300):
            print("engine never started listening"); return
        time.sleep(3)  # let model load settle
        print(f"engine pid={proc.pid} baseline RSS={rss_mb(proc.pid):.0f} MB")

        # Measure the memory scaling curve: ~650 / 2,600 / 3,500 tokens.
        cases = [("2.5KB", PII_TEXT)] + [
            (f"{k}KB", PII_TEXT * (k * 1024 // len(PII_TEXT)))
            for k in (10, 13)
        ]
        for label, text in cases:
            body = json.dumps({
                "model": "test",
                "messages": [{"role": "user", "content": text}],
            }).encode()
            t0 = time.time()
            req = urllib.request.Request(
                f"http://127.0.0.1:{proxy_port}/v1/chat/completions",
                data=body, headers={"Content-Type": "application/json"})
            result = {}

            def do_request():
                try:
                    result["resp"] = urllib.request.urlopen(req, timeout=600).read()
                except Exception as e:
                    result["err"] = e

            t = threading.Thread(target=do_request, daemon=True)
            t.start()
            peak = 0
            while t.is_alive():
                if proc.poll() is not None:
                    print(f"{label}: ENGINE DIED mid-request")
                    break
                r = rss_mb(proc.pid)
                a = anon_mb(proc.pid)
                peak = max(peak, r)
                if a > 3200:
                    print(f"{label}: RUNAWAY at anon={a:.0f} MB — killing engine")
                    proc.kill()
                    break
                print(f"  t={time.time()-t0:6.1f}s RSS={r:7.0f} MB anon={a:6.0f} MB sysAvail={mem_available_mb():6.0f} MB")
                time.sleep(0.05)
            t.join(timeout=5)
            if "err" in result:
                print(f"{label}: FAILED after {time.time()-t0:.1f}s: {result['err']}")
                break
            if "resp" in result:
                print(f"{label}: done in {time.time()-t0:.1f}s peakRSS={peak:.0f} anon={anon_mb(proc.pid):.0f} MB")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        upstream.shutdown()
        # The engine log records which catch block swallowed the bad_alloc.
        log = config_dir / "agent_redactor.log"
        if log.exists():
            print(f"--- engine log ({log}):")
            print(log.read_text(errors="replace")[-4000:])


if __name__ == "__main__":
    main()
